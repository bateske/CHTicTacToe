// Saving options, lifetime stats and a run in progress (the purse, the table
// and the stake last chosen, the streak).
//
// CHGame has no EEPROM, but its bootloader only erases the flash pages a new
// sketch occupies, so the last pages of the application region survive
// re-uploads. Two pages are used in turn, each record carrying a sequence
// number and a CRC, so a power cut mid-write can only lose the newest save.
// If the sketch ever grows into those pages, saving switches itself off
// rather than overwrite code. (From CHBlackjack and CHChess, with its own
// magic: the CHGame games share the pages, and each ignores the others'
// records.)
#pragma once
#include <stdint.h>

struct Casino;

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Casino &c, bool &hasGame);     // options and stats; the run too if hasGame
// Call after gfx_wait(): the page is built in CHGfx's chunk scratch.
bool store(const Casino &c, bool withGame);
void allowWrites(bool on);          // debug builds on the board: off until a script turns it on

}  // namespace save
