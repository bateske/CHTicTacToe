// CHTicTacToe - TIC TAC TOE: ROYALE for the CHGame handheld (CH32X035,
// 128x128 ST7735, piezo), in the casino style of CHBlackjack and CHChess:
// noughts and crosses for money, at twelve tables with twelve sets of rules,
// against CHBlackjack's croupier.
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
#include "config.h"
#include <CHGfx.h>
#include "src/CHGame.h"
#include "src/gfx/Palette.h"
#include "src/states/Screens.h"
#include "src/save/Save.h"
#include "src/debug/Debug.h"

#if CHTT_DEBUG
#ifdef CHSIM
#include <string.h>
#include "src/gfx/Fmt.h"
uint64_t sim_hostNanos();
// Q: calibration for chdrive's cal - host ns for the primitives the CHGfx
// benchmark measured on the board (benchmark-results.txt), so the
// simulator's render times can be read as device milliseconds.
static void calibrate() {
    static uint8_t spr[8 * 16];
    memset(spr, 0x3F, sizeof spr);
    uint64_t t0, r[5];
    t0 = sim_hostNanos(); for (int i = 0; i < 200; i++) gfx_clear((uint8_t)i); r[0] = (sim_hostNanos() - t0) / 200;
    t0 = sim_hostNanos(); for (int i = 0; i < 20000; i++) gfx_hline(0, i & 127, 128, (uint8_t)i); r[1] = (sim_hostNanos() - t0) / 20000;
    t0 = sim_hostNanos(); for (int i = 0; i < 2000; i++) gfx_blit(spr, i & 63, i & 63, 16, 16, 15); r[2] = (sim_hostNanos() - t0) / 2000;
    t0 = sim_hostNanos(); for (int i = 0; i < 1000; i++) gfx_text(0, i & 63, "ABCDEFGHIJKLMNOPQRSTUVWX", 1); r[3] = (sim_hostNanos() - t0) / 1000;
    t0 = sim_hostNanos(); for (int i = 0; i < 1000; i++) gfx_fillCircle(64, 64, 30, (uint8_t)i); r[4] = (sim_hostNanos() - t0) / 1000;
    char buf[96], *p = fmtStr(buf, "CAL");
    for (int k = 0; k < 5; k++) { *p++ = ' '; p = fmtInt(p, (int32_t)r[k]); }
    fmtStr(p, "\n");
    dbg::print(buf);
}
#endif

// Game commands for the debug protocol (tools/chsim/chdrive.py 'say').
//   R <seed>          reseed the match's generator
//   J <T|G|P|W|L|O|S> [table]  jump to a screen (G the tables room, P play)
//   C <cell> [arg]    play that cell for the player (arg: size or symbol)
//   H <cell>          the dealer's next move
//   M <amount>        set the purse
//   D <0..2>          the dealer: tipsy, sharp, shark
//   V <ticks>         BLITZ: set the clock
//   E <1|0>           let a debug build on the board write its save pages
static bool debugHook(char cmd, const char *args) {
    switch (cmd) {
        case 'R': screens::debugSeed(dbg::parseNum(args, 10)); return true;
        case 'J': {
            const char *p = args + 1;
            screens::debugJump(args[0], (uint8_t)dbg::parseNum(p, 10));
            return true;
        }
        case 'C': {
            const char *p = args;
            uint8_t cell = (uint8_t)dbg::parseNum(p, 10);
            screens::debugCell(cell, (uint8_t)dbg::parseNum(p, 10));     // not legal: nothing happens
            return true;
        }
        case 'H': screens::debugDealer((uint8_t)dbg::parseNum(args, 10)); return true;
        case 'M': screens::debugPurse((int32_t)dbg::parseNum(args, 10)); return true;
        case 'D': screens::debugLevel((uint8_t)dbg::parseNum(args, 10)); return true;
        case 'V': screens::debugClock((uint16_t)dbg::parseNum(args, 10)); return true;
        case 'E': save::allowWrites(args[0] == '1'); return true;
#ifdef CHSIM
        case 'Q': calibrate(); return true;
#endif
    }
    return false;
}
#endif

void setup() {
    arduboy.boot();
    dbg::paintStack();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init();
    screens::begin();
    arduboy.setFrameRate(CHTT_FPS);
#if CHTT_DEBUG
    dbg::hook = debugHook;
#endif
}

void loop() {
    dbg::poll();
    if (!arduboy.nextFrame()) return;
    dbg::markUpdateStart();
    // Logic runs at a fixed 60 Hz. If a heavy frame made drawing fall
    // behind, catch up (up to three ticks) before drawing again, so the clock
    // and the gloves never slow down.
    uint8_t ticks = 0;
    do {
        arduboy.pollButtons();
        pal::tick();
        screens::update();
    } while (++ticks < 3 && arduboy.nextFrame());
    pal::commit();                  // staged by CHGfx: lands with the next flush
    gfx_wait();
    dbg::markRenderStart();
    screens::render(arduboy.frameCount);
    dbg::markRenderEnd();
    gfx_flushAsync();
}
