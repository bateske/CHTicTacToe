// Sprite colour remaps (art colour -> screen colour) for sprite4, which
// always takes one. One sprite gives several looks.
#pragma once
#include <stdint.h>

extern const uint8_t RM_ID[16];      // as drawn
extern const uint8_t RM_CPU[16];     // the croupier's glove: red cuff (CHChess's CPU glove)
extern const uint8_t RM_ALERT[16];   // the glove flashing red: refused
