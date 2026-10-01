// Easing curves, integer sine and presentation-only randomness. Pure (no
// graphics), so host tests can link it. Integer
// maths only: soft-float trig once cost a demo on this chip 8.5 KB of flash
// and half its frame rate.
#pragma once
#include <stdint.h>

namespace fx {

enum Ease : uint8_t { LINEAR, OUT_CUBIC, OUT_BACK, IN_OUT, OUT_BOUNCE };
// t in 0..n -> 0..256 (OUT_BACK/OUT_BOUNCE may overshoot).
int ease(Ease e, int t, int n);
int isin(int a);                    // a in 1/256 turns -> -256..256

// Presentation-only randomness (never touches game outcomes).
uint32_t rnd();
int rndRange(int lo, int hi);
void reseed();                      // debug: restart the sequence

}  // namespace fx
