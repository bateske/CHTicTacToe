// Sound and the status LED.
//
// CHBlackjack's piezo sequencer: effects are short step lists (6 bytes a
// step) kept inside the piezo's 1-4 kHz sweet spot. From CHChess: the soft
// effects, played on a narrow pulse so they sit under everything else.
// This game has no music, so CHBlackjack's score player is left out.
#pragma once
#include <stdint.h>

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Chip, Coin, Whoosh, Win, BigWin, Lose, Broke,
    Place, Poof, Tic, Tac, Meow, Title, Boom,
    Tick, Tock,                      // soft (quieter than the rest): keep them last
    COUNT
};

namespace audio {

bool begin(uint8_t mode);           // 0 off, else on
void setMode(uint8_t mode);
void sfx(Sfx s);
// A one-step effect at the lowest priority (typewriter, ticks):
// refused while anything above priority 1 sounds. soft = the narrow pulse.
void blip(uint16_t hz, uint16_t ms, bool soft = false);
void update();                      // once per logic tick: LED patterns

// Status LED (PB9): short patterns for wins.
enum Led : uint8_t { LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY };
void led(Led pattern);

#ifdef CHSIM
extern uint8_t simLast;             // the last effect played (scripts and tests)
#endif

}  // namespace audio
