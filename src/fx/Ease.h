// The bounce curve, integer sine and presentation-only randomness. Pure (no
// graphics), so host tests can link it. Integer maths only: soft-float trig
// once cost a demo on this chip 8.5 KB of flash and half its frame rate.
#pragma once
#include <stdint.h>

namespace fx {

int bounce(int t, int n);           // t in 0..n -> 0..256: falls, bounces twice, settles
int isin(int a);                    // a in 1/256 turns -> -256..256

// Presentation-only randomness (never touches game outcomes).
uint32_t rnd();
int rndRange(int lo, int hi);
void reseed();                      // debug: restart the sequence

}  // namespace fx
