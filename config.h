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

// Device debug builds would drop things to fit (CHTT_LEAN); this game fits
// whole (45.5 KB with the protocol), so nothing is dropped.
#define CHTT_LEAN        0

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHTT_PROFILE
#define CHTT_PROFILE     0
#endif

// Logic runs at a fixed 60 Hz; drawing catches up as it can.
#define CHTT_FPS         60
