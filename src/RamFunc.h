// RAMFUNC(name): run a function from SRAM instead of flash.
//
// Flash runs with 3 wait states and no cache, so a hot per-pixel loop costs
// ~3 us a pixel from there. Flash programming must run from SRAM too.
//
// The section name looks odd on purpose (CHGfx 1.3 uses the same scheme).
// The core's link script puts *(.gnu.linkonce.r.*) first in .data, which
// startup copies from flash to SRAM. The old ".srodata.*" name landed after
// .sdata instead, inside the 4 KB the global pointer reaches, and pushed
// small variables out of it: every access to one of those took an extra
// instruction (260 B of flash in the release build). linkonce only merges
// sections of the same name, so each function needs its own name, which
// also lets the linker drop the unused ones one by one.
//
// The simulator keeps noinline but not the section: Mach-O (macOS) rejects
// ELF section names.
#pragma once

#ifdef CHSIM
#define RAMFUNC(name) __attribute__((noinline))
#else
#define RAMFUNC(name) __attribute__((section(".gnu.linkonce.r.chtt." #name), noinline))
#endif
