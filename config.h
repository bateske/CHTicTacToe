// CHTicTacToe build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the CHGame core 0.2.4+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~3.4 KB of flash); release builds also set USB
// "Upload only" (no Serial). tools/device.py has the exact settings.
#pragma once

#define CHTT_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds (it costs ~2 KB and needs USB Serial).
// tools/device.py turns it on with --build-property build.extra_flags.
#ifndef CHTT_DEBUG
#ifdef CHSIM
#define CHTT_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHTT_DEBUG       0
#endif
#endif

// Device debug builds carry the protocol (~1.7 KB) and so leave out what
// the tests never need: the end screens' PPOT lettering (plain lettering
// instead) and saving. -DCHTT_FULL keeps them (it does not fit). The
// simulator and release builds keep everything.
#if CHTT_DEBUG && !defined(CHSIM) && !defined(CHTT_FULL)
#define CHTT_LEAN        1
#else
#define CHTT_LEAN        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHTT_PROFILE
#define CHTT_PROFILE     0
#endif

// Logic runs at a fixed 60 Hz; drawing catches up as it can.
#define CHTT_FPS         60
