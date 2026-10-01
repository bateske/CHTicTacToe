#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in Draw/Mask and CHGfx)
#include <CHGfx.h>
#include <string.h>
#include "Fx.h"
#include "../gfx/Draw.h"
#include "../gfx/Mask.h"
#include "../gfx/Palette.h"
#include "../RamFunc.h"

namespace fx {

// ---------------------------------------------------------------------------
// Particles
// ---------------------------------------------------------------------------
struct Particle { int16_t x, y; int8_t vx, vy; uint8_t life, colour, kind, age; };
static Particle parts[48];
const uint8_t RAIN[5] = {RED, GOLD, FELT_LT, CYAN, BLUE};

void spawn(Kind k, int x, int y, int vx, int vy, uint8_t life, uint8_t colour) {
    Particle *slot = nullptr;
    for (auto &p : parts) if (!p.life) { slot = &p; break; }
    if (!slot) slot = &parts[rnd() % 48];                 // steal one
    slot->x = (int16_t)(x << 4); slot->y = (int16_t)(y << 4);
    slot->vx = (int8_t)(vx < -127 ? -127 : vx > 127 ? 127 : vx);
    slot->vy = (int8_t)(vy < -127 ? -127 : vy > 127 ? 127 : vy);
    slot->life = life; slot->colour = colour; slot->kind = k; slot->age = 0;
}

void burst(Kind k, int x, int y, uint8_t n, int speed, uint8_t colour) {
    for (uint8_t i = 0; i < n; i++) {
        int a = (int)(i * 256 / n) + rndRange(0, 12);
        int sp = speed / 2 + rndRange(0, speed / 2 + 1);
        int vy = (isin(a) * sp) >> 8;
        if (k == DUST) vy /= 2;                           // puffs spread along the felt
        spawn(k, x, y, (isin(a + 64) * sp) >> 8, vy, (uint8_t)rndRange(20, 40), colour);
    }
}

void fountain(Kind k, int x, int y, uint8_t n) {
    static const uint8_t CONF[6] = {RED, GOLD, FELT_LT, CYAN, BLUE, WHITE};
    for (uint8_t i = 0; i < n; i++)
        spawn(k, x + rndRange(-4, 5), y, rndRange(-28, 29), rndRange(-60, -30), (uint8_t)rndRange(40, 70),
              k == COIN ? GOLD : CONF[rnd() % 6]);
}

bool particles() {
    for (auto &p : parts) if (p.life) return true;
    return false;
}

static void updateParticles() {
    for (auto &p : parts) {
        if (!p.life) continue;
        p.life--; p.age++;
        p.x += p.vx; p.y += p.vy;
        switch (p.kind) {
            case CONFETTI: if (p.age & 1) p.vy += 2; if (p.vy > 24) p.vy = 24;
                           p.vx = (int8_t)(p.vx * 15 / 16); break;
            case COIN:     p.vy += 3; if (p.y > (122 << 4)) { p.vy = (int8_t)(-p.vy / 2); p.y = 122 << 4; } break;
            case RAIN_DROP: break;
            case DUST:     p.vx = (int8_t)(p.vx * 7 / 8); p.vy = (int8_t)(p.vy * 7 / 8); break;
            default:       p.vy += (p.age & 3) == 0; break;
        }
    }
}

void drawParticles(uint8_t dust) {
    for (auto &p : parts) {
        if (!p.life) continue;
        int x = p.x >> 4, y = p.y >> 4;
        switch (p.kind) {
            case SPARK:
                gfx_pixel(x, y, p.colour);
                if (p.age < 8) { gfx_pixel(x - 1, y, p.colour); gfx_pixel(x + 1, y, p.colour);
                                 gfx_pixel(x, y - 1, p.colour); gfx_pixel(x, y + 1, p.colour); }
                break;
            case CONFETTI:
                if ((p.age >> 2) & 1) gfx_hline(x, y, 2, p.colour);
                else gfx_vline(x, y, 2, p.colour);
                break;
            case COIN: {
                int w = ((p.age >> 2) & 3) == 2 ? 1 : 3;
                gfx_fillRect(x - w / 2, y - 1, w, 3, GOLD);
                gfx_pixel(x, y, w == 3 ? WOOD : GOLD);
                break;
            }
            case RAIN_DROP: gfx_vline(x, y, 3, p.colour); break;
            case STAR:
                gfx_hline(x - 1, y, 3, p.colour); gfx_vline(x, y - 1, 3, p.colour);
                break;
            case DUST: {                                  // a puff, down to a speck, centred
                int s = p.life > 10 ? dust : (p.life > 4 || (p.life & 1)) ? (dust + 1) / 2 : 0;
                gfx_fillRect(x - s / 2, y - s / 2, s, s, p.colour);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Banner
// ---------------------------------------------------------------------------
static char bannerText[14];
static uint8_t bannerStyle, bannerT, bannerFrames;
static int bannerCy;
static bool bannerHeld;

void banner(const char *text, BannerStyle s, int cy, uint8_t frames) {
    strncpy(bannerText, text, sizeof bannerText - 1);
    bannerText[sizeof bannerText - 1] = 0;
    bannerStyle = s; bannerCy = cy; bannerT = 0; bannerFrames = frames;
    bannerHeld = false;
}

void holdBanner(bool on) { bannerHeld = on; }

bool bannerActive() { return bannerFrames != 0; }

void drawBanner() {
    if (!bannerFrames) return;
    int t = bannerT;
    uint8_t scale = t < 3 ? 2 : (t < 7 ? 4 : 3);
    int w = text35WidthScaled(bannerText, scale);
    while (w > 124 && scale > 2) w = text35WidthScaled(bannerText, --scale);
    int h = 6 * scale;
    int8_t dy[14];
    int n = (int)strlen(bannerText);
    for (int k = 0; k < n && k < 14; k++) dy[k] = (int8_t)((isin(t * 10 + k * 36) * 2) >> 8) + 2;
    Mask m = maskBegin(w + 1, h + 5);
    maskText35(m, 0, 0, bannerText, scale, dy);
    // Last few frames: blink out.
    if (bannerFrames < 10 && (bannerFrames & 2)) return;
    uint8_t ramp[32];
    for (int r = 0; r < h + 5 && r < 32; r++) {
        switch (bannerStyle) {
            case B_RAINBOW: ramp[r] = RAIN[((r / 2) + t / 3) % 5]; break;
            case B_RED:     ramp[r] = r < 3 ? WHITE : RED; break;
            default:        ramp[r] = r < 3 ? WHITE : CYAN; break;      // B_CYAN (the only other one used)
        }
    }
    uint8_t outline = bannerStyle == B_RAINBOW ? FX_A : INK;
    maskDraw(m, 64 - w / 2, bannerCy - h / 2 - 2, WHITE, outline, bannerStyle == B_RAINBOW ? INK : WINE, ramp);
}

// ---------------------------------------------------------------------------
// Floating text
// ---------------------------------------------------------------------------
struct Float { int16_t x, y; uint8_t t, colour; char text[8]; };
static Float floats[4];

void floatText(const char *text, int x, int y, uint8_t colour) {
    Float *f = &floats[0];
    for (auto &q : floats) if (!q.t) { f = &q; break; }
    f->x = (int16_t)x; f->y = (int16_t)y; f->t = 50; f->colour = colour;
    strncpy(f->text, text, 7); f->text[7] = 0;
}

void drawFloats() {
    for (auto &f : floats) {
        if (!f.t) continue;
        int y = f.y - (50 - f.t) / 2;
        int x = f.x - text35Width(f.text) / 2;
        if (f.t < 8 && (f.t & 1)) continue;
        text35(x + 1, y + 1, f.text, INK);
        text35(x, y, f.text, f.colour);
    }
}

// ---------------------------------------------------------------------------
// Shake
// ---------------------------------------------------------------------------
static uint8_t shakeT, shakeAmp;

void shake(uint8_t frames, uint8_t amp) { shakeT = frames; shakeAmp = amp; }

// Rows y0..y1 moved dy rows and one byte (2 px) sideways, in one pass of
// word copies from SRAM (newlib's memmove is a byte loop in flash: ~10 ms a
// shaken frame). Walks away from the direction of travel so every source row
// is read before it is overwritten; rows the move uncovers shift in place.
RAMFUNC(shake) static void shiftRows(int y0, int y1, int dy, bool right) {
    const int W = GFX_FB_STRIDE / 4;
    for (int k = 0; k <= y1 - y0; k++) {
        int y = dy > 0 ? y1 - k : y0 + k, sy = y - dy;
        if (sy < y0 || sy > y1) sy = y;
        uint32_t *d = (uint32_t *)(gfx_fb + y * GFX_FB_STRIDE);
        const uint32_t *s = (const uint32_t *)(gfx_fb + sy * GFX_FB_STRIDE);
        if (right) {        // d[i] = s[i - 1], the first byte kept
            for (int j = W - 1; j > 0; j--) d[j] = (s[j] << 8) | (s[j - 1] >> 24);
            d[0] = (s[0] << 8) | (s[0] & 0xFF);
        } else {            // d[i] = s[i + 1], the last byte kept
            for (int j = 0; j < W - 1; j++) d[j] = (s[j] >> 8) | (s[j + 1] << 24);
            d[W - 1] = (s[W - 1] >> 8) | (s[W - 1] & 0xFF000000u);
        }
    }
}

void applyShake(int y0, int y1) {
    if (!shakeT) return;
    int a = (shakeAmp * shakeT + 9) / 10;
    if (a < 1) a = 1;
    shiftRows(y0, y1, (shakeT & 1) ? a : -a, (shakeT & 2) != 0);    // 2 px sideways
}

bool activeRows(int &lo, int &hi) {
    lo = 999; hi = -1;
    if (shakeT) { lo = 0; hi = 127; return true; }
    for (auto &p : parts) if (p.life) { int y = p.y >> 4; if (y - 2 < lo) lo = y - 2; if (y + 3 > hi) hi = y + 3; }
    for (auto &f : floats) if (f.t) { int y = f.y - (50 - f.t) / 2; if (y - 1 < lo) lo = y - 1; if (y + 7 > hi) hi = y + 7; }
    if (bannerFrames) { if (bannerCy - 18 < lo) lo = bannerCy - 18; if (bannerCy + 18 > hi) hi = bannerCy + 18; }
    return hi >= lo;
}

void clear() {
    memset(parts, 0, sizeof parts);
    memset(floats, 0, sizeof floats);
    bannerFrames = 0;
    bannerHeld = false;
    shakeT = 0;
}

void update() {
    updateParticles();
    if (bannerFrames) {
        if (!bannerHeld || bannerFrames > 10) bannerFrames--;    // held: up, until let go to blink out
        if (!++bannerT) bannerT = 128;                           // (the same phase of the dance)
    }
    for (auto &f : floats) if (f.t) f.t--;
    if (shakeT) shakeT--;
}

}  // namespace fx
