// The one 16-colour palette, and the tricks it allows.
//
// CHGfx applies the palette while it converts the framebuffer for the panel,
// so recolouring an index recolours every pixel that uses it for free.
// All edits land in a staging copy and pal::commit() works out the final
// colours (flash, desaturate, fade) once per frame. CHGfx stages
// gfx_setPalette() itself (it takes effect when the next flush starts), so
// commit() may run while a frame is still going out.
#pragma once
#include <stdint.h>

enum : uint8_t {
    INK = 0, WHITE, FELT_DK, FELT, FELT_LT, SILVER, RED, WINE,
    GOLD, WOOD, BLUE, NAVY, SKIN, CYAN, FX_A, FX_B,
};

namespace pal {

void init();                                // defaults + immediate commit
void setFade(uint8_t level);                // 0 = black .. 16 = full colour
uint8_t fade();
void setDesaturate(uint8_t amount);         // 0 = colour .. 16 = grey
void flash(uint8_t index, uint16_t rgb444, uint8_t frames);  // override briefly
void setFx(uint8_t index, uint16_t rgb444); // FX_A/FX_B manual control
void setCycling(bool on);                   // rainbow FX_A + pulse FX_B
void tick();                                // once per logic tick, before commit
void resetClock();                          // debug: restart the FX_A/FX_B cycle
void commit();                              // once per frame, before the flush
uint16_t rgb444(uint8_t index);             // current staged colour

}  // namespace pal
