#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in Draw/Mask and CHGfx)
#include <CHGfx.h>
#include <string.h>
#include "Stage.h"
#include "ChipArt.h"
#include "../game/Text.h"
#include "../gfx/Draw.h"
#include "../gfx/Fmt.h"
#include "../gfx/Palette.h"
#include "../gfx/Remap.h"
#include "../fx/Fx.h"
#include "../audio/Audio.h"
#include "../assets/Assets.h"

namespace stage {

static const int BOARD_Y0 = 12, BOARD_Y1 = 116;         // between the two bars
static const int HOME_X = 116 << 4, HOME_Y = 56 << 4;   // where the dealer's glove waits

static int16_t gx, gy, tx, ty;          // the glove's fingertip, Q4
static bool gloveOn, gloveCpu, moving, reaching, dirty;
static uint8_t alertT, busyT, strikeT, tossT, tossWho, quipT, lastPhase;
static uint16_t catT;
static uint32_t lastSig;
static const char *quip;
static char note[24];

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
struct Geo { int16_t x0, y0; uint8_t cw, r; };

static Geo geo(const Board &b) {
    Geo g;
    g.cw = b.n == 9 ? 28 : (b.w == 5 ? 18 : (b.w == 7 ? 14 : 11));
    g.r = b.n == 9 ? 9 : (b.w == 5 ? 6 : (b.w == 7 ? 5 : 3));
    int gap = (b.flags & F_ULTIMATE) ? 2 : 0;
    g.x0 = (int16_t)((128 - b.w * g.cw - gap) / 2 + 1);
    g.y0 = (int16_t)(BOARD_Y0 + 1 + (104 - b.h * g.cw - gap) / 2);
    return g;
}

static void cellPos(const Board &b, uint8_t cell, int &cx, int &cy) {
    int x = cell % b.w, y = cell / b.w % b.h;
    if (b.d > 1) {                                       // TOWER: four slanted floors
        int z = cell / 16;
        cx = 34 + x * 16 + (3 - y) * 4;
        cy = 18 + (3 - z) * 25 + y * 6;
        return;
    }
    Geo g = geo(b);
    bool u = (b.flags & F_ULTIMATE) != 0;
    cx = g.x0 + x * g.cw + (u ? x / 3 : 0) + (g.cw - 1) / 2;
    cy = g.y0 + y * g.cw + (u ? y / 3 : 0) + (g.cw - 1) / 2;
}

static uint8_t radius(const Board &b) { return b.d > 1 ? 2 : geo(b).r; }

// ---------------------------------------------------------------------------
// Marks
// ---------------------------------------------------------------------------
static void drawX(int cx, int cy, int r, uint8_t c) {
    int t = r < 3 ? 1 : r / 4, w = r < 3 ? 3 : (r < 5 ? 2 : 2 * t + 1);   // small marks: 2 px strokes; TOWER: 3
    for (int d = -r; d <= r; d++) {
        gfx_hline(cx + d - t, cy + d, w, c);
        gfx_hline(cx - d - t, cy + d, w, c);
    }
}

void mark(int cx, int cy, int r, uint8_t sym, uint8_t colour) {
    if (sym == 1) {
        if (r >= 6) drawX(cx + 1, cy + 1, r, INK);
        drawX(cx, cy, r, colour ? colour : RED);
        return;
    }
    uint8_t c = colour ? colour : CYAN;
    if (r < 3) {                                         // TOWER: flat on its floor
        gfx_fillEllipse(cx, cy, 4, 2, c);
        gfx_hline(cx - 2, cy, 5, FELT_DK);
        return;
    }
    if (r < 5) {
        gfx_circle(cx, cy, r, c);
        gfx_circle(cx, cy, r - 1, c);
        return;
    }
    gfx_fillCircle(cx + 1, cy + 1, r, INK);
    gfx_fillCircle(cx, cy, r, colour ? colour : BLUE);
    gfx_circle(cx, cy, r - 1, c);
    gfx_fillCircle(cx, cy, r - 2 - r / 4, FELT);
    gfx_circle(cx, cy, r - 2 - r / 4, INK);
}

// GOBBLE: a chip seen from above, by size.
static void piece(int cx, int cy, uint8_t lvl, uint8_t owner, bool hot) {
    int r = 4 + lvl * 3;
    gfx_fillCircle(cx, cy, r, INK);
    gfx_fillCircle(cx, cy, r - 1, owner == 1 ? RED : BLUE);
    if (lvl) gfx_circle(cx, cy, r - 3, hot ? FX_B : WHITE);
    gfx_fillRect(cx - 1, cy - 1, 2, 2, hot ? FX_B : WHITE);
}

// ---------------------------------------------------------------------------
// The felt
// ---------------------------------------------------------------------------
static void drawTower() {
    for (int z = 0; z < 4; z++) {
        int ly = 15 + (3 - z) * 25;
        for (int yy = 0; yy <= 24; yy++) gfx_hline(40 - yy * 2 / 3, ly + yy, 65, FELT_DK);
        for (int r = 0; r <= 4; r++) gfx_hline(40 - 4 * r, ly + r * 6, 65, FELT_LT);
        for (int c = 0; c <= 4; c++) gfx_line(40 + c * 16, ly, 24 + c * 16, ly + 24, FELT_LT);
        char f[3] = {(char)('1' + z), 'F', 0};
        text35(8, ly + 10, f, GOLD);
    }
}

static void drawGrid(const Board &b) {
    Geo g = geo(b);
    bool u = (b.flags & F_ULTIMATE) != 0, big = b.n == 9;
    int W = b.w * g.cw + (u ? 2 : 0) - 1, H = b.h * g.cw + (u ? 2 : 0) - 1;
    if (!big) {
        gfx_fillRect(g.x0 - 1, g.y0 - 1, W + 2, H + 2, FELT);
        gfx_rect(g.x0 - 2, g.y0 - 2, W + 4, H + 4, GOLD);
    }
    for (int i = 1; i < b.w; i++) {
        int x = g.x0 + i * g.cw + (u ? i / 3 : 0) - 1;
        bool heavy = big || (u && i % 3 == 0);
        gfx_vline(x, g.y0, H, heavy ? GOLD : FELT_DK);
        if (heavy) gfx_vline(x - 1, g.y0, H, GOLD);
    }
    for (int i = 1; i < b.h; i++) {
        int y = g.y0 + i * g.cw + (u ? i / 3 : 0) - 1;
        bool heavy = big || (u && i % 3 == 0);
        gfx_hline(g.x0, y, W, heavy ? GOLD : FELT_DK);
        if (heavy) gfx_hline(g.x0, y - 1, W, GOLD);
    }
}

static void drawMarks(const Match &m) {
    const Board &b = m.b;
    uint8_t r = radius(b);
    // VANISH: the mark that goes next, for the side about to move.
    uint8_t fading = NONE;
    if ((b.flags & F_VANISH) && !b.result && b.qn[b.turn] == 3) fading = b.q[b.turn][0];
    for (uint8_t i = 0; i < b.n; i++) {
        uint8_t c = b.cell[i];
        if (!c) continue;
        if ((b.flags & F_DARK) && topOf(c) == 2 && !(b.seen >> i & 1)) continue;   // not found yet
        int cx, cy;
        cellPos(b, i, cx, cy);
        if (topOf(c) == 3) {                             // MINES: a crater
            gfx_fillCircle(cx, cy, 6, INK);
            gfx_circle(cx, cy, 6, WINE);
            drawX(cx, cy, 2, WOOD);
            continue;
        }
        bool hot = i == fading;
        if (!(b.flags & F_ULTIMATE)) for (uint8_t j = 0; j < 5; j++) if (b.win[j] == i) hot = true;
        if (b.flags & F_GOBBLE) piece(cx, cy, levelOf(c), topOf(c), hot || i == b.last);
        else mark(cx, cy, r, topOf(c), hot ? FX_A : 0);
    }
    if (b.flags & F_ULTIMATE) {
        Geo g = geo(b);
        for (uint8_t s = 0; s < 9; s++) {
            int x = g.x0 + s % 3 * 34, y = g.y0 + s / 3 * 34;
            if (b.small[s]) {
                dither(x, y, 32, 32, FELT_DK, 0);
                if (b.small[s] < 3) mark(x + 16, y + 16, 12, b.small[s]);
            } else if (s == b.must && m.phase == Phase::Human) gfx_rect(x - 1, y - 1, 34, 34, FX_B);
        }
    }
}

// The winning line, drawn out from one end.
static void drawStrike(const Board &b) {
    if (!strikeT || b.win[0] == NONE || (b.flags & F_WRAP)) return;
    uint8_t last = 4;
    while (b.win[last] == NONE) last--;
    int ax, ay, bx, by;
    cellPos(b, b.win[0], ax, ay);
    cellPos(b, b.win[last], bx, by);
    int r = radius(b);
    if (b.flags & F_ULTIMATE) r = 12;
    int dx = bx - ax, dy = by - ay;
    int ex = dx ? (dx > 0 ? r : -r) : 0, ey = dy ? (dy > 0 ? r : -r) : 0;
    ax -= ex; ay -= ey; bx += ex; by += ey;
    int p = strikeT > 16 ? 16 : strikeT;
    bx = ax + (bx - ax) * p / 16;
    by = ay + (by - ay) * p / 16;
    static const int8_t OFF[5][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}, {0, 0}};
    for (int i = 0; i < 5; i++)
        gfx_line(ax + OFF[i][0], ay + OFF[i][1], bx + OFF[i][0], by + OFF[i][1], i == 4 ? WHITE : FX_A);
}

// ---------------------------------------------------------------------------
// The side panels of the 3x3 tables
// ---------------------------------------------------------------------------
static void chipPile(int cx, uint8_t n, uint8_t lifted) {
    for (uint8_t i = 0; i < n; i++) {
        bool up = i >= n - lifted;
        art::chip(cx, 100 - i * 3 - (up ? 5 : 0), up ? 1 : 2, i == n - 1 || i == n - lifted - 1);
    }
}

static void drawSides(const Match &m) {
    const Board &b = m.b;
    for (uint8_t s = 0; s < 2; s++) {
        int cx = s ? 117 : 10;
        bool turn = !b.result && b.turn == s && m.phase >= Phase::Human && m.phase <= Phase::Settle;
        const char *who = m.two ? (s ? "P2" : "P1") : (s ? "HIM" : "YOU");
        text35(cx - 5, 17, who, turn ? FX_B : SILVER);
        if (!(b.flags & (F_WILD | F_GOBBLE))) mark(cx, 30, 4, (uint8_t)((b.flags & F_SAME) ? 1 : s + 1));
        if (b.flags & F_GOBBLE) {
            for (uint8_t l = 0; l < 3; l++) {
                int y = 34 + l * 22;
                if (!b.stock[s][l]) continue;
                if (s == b.turn && l == m.size && m.phase == Phase::Human) gfx_rect(s ? 107 : 0, y - 11, 21, 23, FX_B);
                piece(cx, y, l, (uint8_t)(s + 1), false);
                if (b.stock[s][l] > 1) text35(cx + 5, y + 6, "2", WHITE);
            }
        }
        if (b.flags & F_AUCTION) {
            bool bidding = m.phase == Phase::Bid || m.phase == Phase::BidShow;
            uint8_t lift = !bidding ? 0 : (s ? (m.phase == Phase::BidShow ? m.bidC : 0) : m.bidP);
            // During the show the chips have already changed hands: draw them as bid.
            uint8_t n = b.chips[s];
            if (m.phase == Phase::BidShow) {
                uint8_t paid = b.turn ? m.bidC : m.bidP;
                n = (uint8_t)(b.turn == s ? n + paid : n - paid);
            }
            chipPile(cx, n, lift);
            char buf[4];
            *fmtInt(buf, n) = 0;
            text35(cx - text35Width(buf) / 2, 108, buf, WHITE);
        }
    }
    if (b.flags & F_BLITZ) {
        char buf[4];
        *fmtInt(buf, m.wins) = 0;
        text35(111, 50, "WON", SILVER);
        text35x2(117 - text35Width(buf), 60, buf, GOLD);
        text35(3, 50, "TIME", SILVER);
        *fmtInt(buf, (m.clock + 59) / 60) = 0;
        text35x2(10 - text35Width(buf), 60, buf, m.clock < 600 ? RED : WHITE);
        if (m.phase == Phase::Human) {                   // the shot clock, draining
            gfx_rect(7, 78, 6, 28, SILVER);
            int h = 26 * m.shot / SHOT_TICKS;
            gfx_fillRect(8, 79 + 26 - h, 4, h, m.shot < 60 ? RED : FX_B);
        }
    }
}

// ---------------------------------------------------------------------------
// The bars
// ---------------------------------------------------------------------------
static const char *status(const Match &m) {
    const Board &b = m.b;
    switch (m.phase) {
        case Phase::Over: return quipT ? quip : "A: AGAIN    B: TABLES";
        case Phase::Toss: return "THE COIN DECIDES";
        case Phase::Bid: return "UP/DOWN: BID   A: LOCK IN";
        case Phase::BidShow: return note;
        case Phase::Human:
            if (quipT) return quip;
            if (b.flags & F_GOBBLE) return "A: PLACE    B: SIZE";
            if (b.flags & F_WILD) return "A: X     B: O";
            if (b.flags & F_MISERE) return "DON'T MAKE THREE";
            if (b.flags & F_DARK) return "FEEL YOUR WAY";
            if (m.two) return b.turn ? "PLAYER 2" : "PLAYER 1";
            return "YOUR MOVE";
        case Phase::Think: case Phase::Reach: return quipT ? quip : "THE DEALER THINKS";
        default: return quipT ? quip : "";
    }
}

static void drawBars(const Match &m, const Casino &c) {
    gfx_fillRect(0, 0, 128, 11, INK);
    gfx_hline(0, 11, 128, GOLD);
    text35(3, 3, MODE_NAME[m.mode], GOLD);
    char buf[12];
    if (m.two) {                                         // the score, not the money
        char *p = fmtInt(fmtStr(buf, "P1 "), m.score[0]);
        *fmtStr(fmtInt(fmtStr(p, " - "), m.score[1]), " P2") = 0;
    } else {
        *fmtMoney(buf, c.purse) = 0;
        text35(125 - text35Width(buf), 3, buf, WHITE);
        *fmtMoney(fmtStr(buf, "BET "), ANTES[c.ante]) = 0;
    }
    text35(m.two ? 125 - text35Width(buf) : 64 - text35Width(buf) / 2 + 6, 3, buf, SILVER);
    gfx_fillRect(0, 117, 128, 11, INK);
    gfx_hline(0, 116, 128, GOLD);
    const char *s = status(m);
    text35(64 - text35Width(s) / 2, 120, s, quipT && s == quip ? WHITE : FELT_LT);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
static void say(const char *s) { quip = s; quipT = 110; }

static void tip(const Board &b, uint8_t cell, int16_t &x, int16_t &y) {
    int cx, cy;
    cellPos(b, cell, cx, cy);
    x = (int16_t)(cx << 4);
    y = (int16_t)((cy - radius(b) / 2) << 4);
}

void reset(const Match &m) {
    (void)m;
    gloveOn = moving = reaching = false;
    alertT = busyT = strikeT = tossT = quipT = 0;
    catT = 0;
    lastPhase = 0xFF;
    lastSig = 0;
    dirty = true;
    fx::clear();
}

void invalidate() { dirty = true; }

bool busy() { return busyT || tossT || (reaching && moving); }

void paid(int32_t net) {
    char buf[10], *p = buf;
    if (net > 0) *p++ = '+';
    *fmtMoney(p, net) = 0;
    if (net) fx::floatText(buf, 64, 24, net > 0 ? GOLD : RED);
}

void onEvents(const Match &m) {
    const Board &b = m.b;
    for (uint8_t i = 0; i < m.nEv; i++) {
        const Event &e = m.ev[i];
        int cx = 64, cy = 64;
        dirty = true;
        switch (e.type) {
            case EV_START:
                strikeT = 0;
                break;
            case EV_TOSS:
                tossT = 1; tossWho = e.a;
                audio::sfx(Sfx::Coin);
                break;
            case EV_BID: {
                char *p = fmtInt(fmtStr(note, "YOU "), m.bidP);
                *fmtInt(fmtStr(p, "   HIM "), m.bidC) = 0;
                fx::floatText(e.a ? "HIS GO" : "YOURS!", 64, 60, e.a ? RED : GOLD);
                audio::sfx(e.a ? Sfx::Deny : Sfx::Chip);
                busyT = 50;
                break;
            }
            case EV_MOVE: audio::sfx(Sfx::Cursor); break;
            case EV_ARG: audio::sfx(Sfx::Tick); break;
            case EV_DENY: audio::sfx(Sfx::Deny); alertT = 10; break;
            case EV_TIMEOUT: fx::floatText("TOO SLOW", 64, 60, RED); break;
            case EV_BOOM:
                cellPos(b, e.a, cx, cy);
                fx::floatText("BOOM!", cx, cy - 10, GOLD);
                fx::burst(fx::SPARK, cx, cy, 16, 50, GOLD);
                fx::shake(8, 2);
                pal::flash(FELT, 0xFFF, 3);
                audio::sfx(Sfx::Boom);
                busyT = 20;
                break;
            case EV_BUMP:
                cellPos(b, e.a, cx, cy);
                fx::floatText("BUMP!", cx, cy - 12, WHITE);
                fx::burst(fx::STAR, cx, cy, 8, 30, CYAN);
                audio::sfx(Sfx::Deny);
                break;
            case EV_PLACE: {
                busyT = 6;
                if ((b.flags & F_DARK) && e.b && !b.result) {       // nothing to see
                    say(QUIPS[QUIP_DARK]);
                    audio::sfx(Sfx::Tock);
                    break;
                }
                cellPos(b, e.a, cx, cy);
                fx::burst(fx::DUST, cx, cy, 6, 24, e.b ? CYAN : SKIN);
                uint8_t need = (uint8_t)(b.k - b.run);
                if (b.result && b.win[0] != NONE) fx::floatText("TOE!", cx, cy - 8, FX_A);
                else if (need == 1) { fx::floatText("TAC", cx, cy - 8, WHITE); audio::sfx(Sfx::Tac); }
                else if (need == 2 && b.n == 9) { fx::floatText("TIC", cx, cy - 8, WHITE); audio::sfx(Sfx::Tic); }
                else audio::sfx(Sfx::Place);
                if (!e.b && !b.result && !m.two) {       // the dealer has opinions
                    uint32_t r = fx::rnd();
                    if (b.n == 9 && b.left == 8 && !(b.flags & F_BLITZ))
                        say(QUIPS[e.a == 4 ? QUIP_CENTRE : ((e.a & 1) ? QUIP_EDGE : QUIP_CORNER)]);
                    else if (r % 7 == 0 && !(b.flags & F_BLITZ)) say(QUIPS[QUIP_ANY + (r >> 8) % QUIP_ANY_N]);
                }
                busyT = 6;
                break;
            }
            case EV_GONE:
                cellPos(b, e.a, cx, cy);
                fx::burst(fx::SPARK, cx, cy, 10, 30, FX_A);
                audio::sfx(Sfx::Poof);
                break;
            case EV_SMALL:
                cellPos(b, smallCentre(e.a), cx, cy);
                fx::burst(fx::STAR, cx, cy, 12, 40, FX_B);
                audio::sfx(b.small[e.a] == 1 ? Sfx::Win : Sfx::Lose);
                break;
            case EV_BOARD:
                strikeT = 1;
                if (e.a == R_P0) { fx::floatText("+1", 64, 60, GOLD); audio::sfx(Sfx::Win); fx::fountain(fx::COIN, 64, 110, 6); }
                else if (e.a == R_P1) { fx::floatText("-5 SEC", 64, 60, RED); audio::sfx(Sfx::Lose); }
                else { fx::floatText("CAT!", 64, 60, CYAN); audio::sfx(Sfx::Meow); }
                break;
            case EV_OVER: {
                bool mis = (b.flags & F_MISERE) != 0;
                if (!(b.flags & F_BLITZ)) strikeT = 1;
                if (b.flags & F_BLITZ) {
                    *fmtStr(fmtInt(note, m.wins), m.wins == 1 ? " BOARD" : " BOARDS") = 0;
                    fx::banner(note, e.a == R_P0 ? fx::B_RAINBOW : fx::B_RED, 64, 110);
                } else if (m.two && e.a != R_DRAW) fx::banner(e.a == R_P0 ? "P1 WINS!" : "P2 WINS!", fx::B_RAINBOW, 64, 110);
                else if (e.a == R_P0) fx::banner(mis ? "HE MADE 3!" : "YOU WIN!", fx::B_RAINBOW, 64, 110);
                else if (e.a == R_P1) fx::banner(mis ? "OOPS! THREE" : "HOUSE WINS", fx::B_RED, 64, 110);
                else { fx::banner("CAT'S GAME", fx::B_CYAN, 56, 110); catT = 1; }
                if (e.a == R_P0 || (m.two && e.a == R_P1)) {
                    audio::sfx(Sfx::BigWin);
                    audio::led(audio::LED_PARTY);
                    fx::shake(10, 2);
                    fx::fountain(fx::COIN, 40, 112, 8);
                    fx::fountain(fx::CONFETTI, 88, 112, 14);
                } else audio::sfx(e.a == R_P1 ? Sfx::Lose : Sfx::Meow);
                if (!m.two) say(QUIPS[QUIP_OVER + (e.a - 1) * 2 + (fx::rnd() & 1)]);
                busyT = 40;
                break;
            }
        }
    }
}

void update(const Match &m) {
    const Board &b = m.b;
    fx::update();
    if (busyT) busyT--;
    if (alertT) alertT--;
    if (quipT && !--quipT) dirty = true;
    if (strikeT && strikeT < 17) strikeT++;
    if (tossT && ++tossT > 44) {
        tossT = 0;
        fx::floatText(tossWho ? (m.two ? "P2!" : "HIS GO") : (m.two ? "P1!" : "YOURS!"), 64, 46, tossWho ? CYAN : SKIN);
    }
    if (catT && ++catT > 150) catT = 0;

    // The glove: yours rides the cursor; the dealer's comes from his side.
    Phase p = m.phase;
    bool was = gloveOn && !gloveCpu;
    reaching = p == Phase::Reach;
    if (p == Phase::Human) {
        bool second = b.turn != 0;                       // two players: the red cuff
        if (gloveCpu != second) was = false;
        gloveOn = true; gloveCpu = second;
        tip(b, m.cur, tx, ty);
        if (!was) { gx = tx; gy = ty; }
    } else if (p == Phase::Think) {
        if (!gloveOn || !gloveCpu) { gx = HOME_X; gy = HOME_Y; }
        gloveOn = gloveCpu = true;
        tx = HOME_X; ty = HOME_Y;
    } else if (p == Phase::Reach && !(b.flags & F_DARK)) {   // in the dark his hand isn't seen
        gloveOn = gloveCpu = true;
        tip(b, m.pend.cell, tx, ty);
    } else if (p != Phase::Settle) gloveOn = false;
    int dx = tx - gx, dy = ty - gy;
    moving = dx > 12 || dx < -12 || dy > 12 || dy < -12;
    if (moving) { gx = (int16_t)(gx + dx / 3); gy = (int16_t)(gy + dy / 3); }
    else { gx = tx; gy = ty; }

    uint32_t sig = (uint32_t)p | ((uint32_t)m.cur << 4) | ((uint32_t)m.size << 12) | ((uint32_t)m.bidP << 16) |
                   ((uint32_t)gloveOn << 24);
    if (sig != lastSig) { lastSig = sig; dirty = true; }
}

// ---------------------------------------------------------------------------
bool render(const Match &m, const Casino &c, uint32_t frame) {
    const Board &b = m.b;
    int lo, hi;
    bool live = dirty || busyT || tossT || catT || alertT || moving || (strikeT && strikeT < 17) ||
                fx::activeRows(lo, hi) || fx::bannerActive() ||
                ((b.flags & F_BLITZ) && m.phase != Phase::Over);
    if (!live) return false;
    dirty = moving;                                      // one more frame once it lands

    gfx_fillRect(0, BOARD_Y0, 128, BOARD_Y1 - BOARD_Y0, FELT);
    if (b.d > 1) drawTower(); else drawGrid(b);
    drawMarks(m);
    if (b.n == 9) drawSides(m);
    drawStrike(b);

    if (!b.result && b.last != NONE && b.turn == 0 && !m.two && b.n > 9 && !(b.flags & F_DARK)) {
        int cx, cy;                                      // where the dealer just went
        cellPos(b, b.last, cx, cy);
        int h = b.d > 1 ? 3 : (geo(b).cw - 1) / 2 - 1;
        gfx_rect(cx - (b.d > 1 ? 7 : h), cy - h, 2 * (b.d > 1 ? 7 : h) + 1, 2 * h + 1, SILVER);
    }
    if (m.phase == Phase::Human) {                       // the cursor's cell
        int cx, cy;
        uint8_t at = m.cur;
        if ((b.flags & F_GRAVITY) && rules::drop(b, at) != NONE) at = rules::drop(b, at);   // where it will land
        cellPos(b, at, cx, cy);
        if (b.d > 1) gfx_rect(cx - 8, cy - 3, 17, 7, FX_B);
        else {
            int h = (geo(b).cw - 1) / 2;
            gfx_rect(cx - h, cy - h, 2 * h + 1, 2 * h + 1, FX_B);
            if (h > 6) gfx_rect(cx - h + 1, cy - h + 1, 2 * h - 1, 2 * h - 1, FX_B);
        }
    }
    if (tossT) {                                         // the coin, turning over as it flies
        int cy = 70 - ((fx::isin(tossT * 128 / 44) * 34) >> 8);
        int ry = tossT > 36 ? 9 : 1 + (((fx::isin(tossT * 26) < 0 ? -fx::isin(tossT * 26) : fx::isin(tossT * 26)) * 8) >> 8);
        gfx_fillEllipse(64, cy, 9, ry, GOLD);
        gfx_ellipse(64, cy, 9, ry, WOOD);
        if (tossT > 36) mark(64, cy, 4, (uint8_t)(tossWho + 1), tossWho ? BLUE : RED);
    }
    if (catT) sprite4((catT & 8) ? CAT1 : CAT2, (int)catT - 18, 104, RM_ID);
    fx::drawParticles(2);
    if (gloveOn) {
        const uint8_t *rm = alertT ? RM_ALERT : (gloveCpu ? RM_CPU : RM_ID);
        sprite4(HAND, (gx >> 4) - HAND_TIP, (gy >> 4) - 15, rm);
    }
    fx::drawFloats();
    fx::drawBanner();
    fx::applyShake(BOARD_Y0, BOARD_Y1 - 1);
    drawBars(m, c);
    (void)frame;
    return true;
}

}  // namespace stage
