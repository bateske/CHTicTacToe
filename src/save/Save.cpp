#pragma GCC optimize("Os")   // cold code: size over speed
#include <Arduino.h>
#include <string.h>
#include <CHGfx.h>
#include "../../config.h"
#include "../RamFunc.h"
#include "../game/Match.h"
#include "Save.h"

namespace save {

#if CHTT_LEAN
// Device debug builds: no saving (it does not fit beside the protocol).
bool available() { return false; }
bool load(Casino &, bool &hasGame) { hasGame = false; return false; }
bool store(const Casino &, bool) { return false; }
void allowWrites(bool) {}
#else

static const uint32_t MAGIC = 0x54544843u;       // "CHTT"
static const uint8_t VERSION = 1;
static const uint32_t PAGE = 256;
static const uint32_t PAGE_A = 0xF500, PAGE_B = 0xF600;   // metadata page is 0xF700

struct Record {
    uint32_t magic;
    uint8_t  version, hasGame;
    uint16_t seq;
    int32_t  purse;
    uint8_t  mode, ante, streak, pad;
    Options  opt;
    Stats    stats;
    uint32_t crc;
};
static_assert(sizeof(Record) <= PAGE, "save record must fit one flash page");

static uint16_t lastSeq = 0;
static bool broken = false;

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static bool valid(const Record *r) {
    return r->magic == MAGIC && r->version == VERSION &&
           r->crc == crc32((const uint8_t *)r, (uint32_t)(sizeof(Record) - 4));
}

#ifndef CHSIM
extern "C" uint32_t _data_lma, _data_vma, _edata;

static uint32_t imageEnd() {
    return (uint32_t)&_data_lma + ((uint32_t)&_edata - (uint32_t)&_data_vma);
}

// Flash controller, mirrored from CH32SerialBoot/bootloader/src/flash.c.
// Must run from SRAM, with interrupts off (the vector table is in flash).
#define CR_STRT 0x00000040u
#define CR_FLOCK 0x00008000u
#define CR_PAGE_PG 0x00010000u
#define CR_PAGE_ER 0x00020000u
#define CR_BUF_LOAD 0x00040000u
#define CR_BUF_RST 0x00080000u
#define SR_BSY 0x00000001u
#define PROG(a) ((a) + 0x08000000u)

RAMFUNC(save) static void pageWrite(uint32_t addr, const uint32_t *w) {
    uint32_t irq;
    __asm volatile("csrr %0, 0x800" : "=r"(irq));
    __asm volatile("csrw 0x800, %0" : : "r"(irq & ~0x88u));
    FLASH->KEYR = 0x45670123u; FLASH->KEYR = 0xCDEF89ABu;
    FLASH->MODEKEYR = 0x45670123u; FLASH->MODEKEYR = 0xCDEF89ABu;
    FLASH->CTLR |= CR_PAGE_ER;
    FLASH->ADDR = PROG(addr);
    FLASH->CTLR |= CR_STRT;
    while (FLASH->STATR & SR_BSY) {}
    FLASH->CTLR &= ~CR_PAGE_ER;
    FLASH->CTLR |= CR_PAGE_PG;
    FLASH->CTLR |= CR_BUF_RST;
    while (FLASH->STATR & SR_BSY) {}
    FLASH->CTLR &= ~CR_PAGE_PG;
    for (uint32_t i = 0; i < PAGE / 4; i++) {
        FLASH->CTLR |= CR_PAGE_PG;
        *(volatile uint32_t *)(PROG(addr) + i * 4) = w[i];
        FLASH->CTLR |= CR_BUF_LOAD;
        while (FLASH->STATR & SR_BSY) {}
        FLASH->CTLR &= ~CR_PAGE_PG;
    }
    FLASH->CTLR |= CR_PAGE_PG;
    FLASH->ADDR = PROG(addr);
    FLASH->CTLR |= CR_STRT;
    while (FLASH->STATR & SR_BSY) {}
    FLASH->CTLR &= ~CR_PAGE_PG;
    FLASH->CTLR |= CR_FLOCK;
    __asm volatile("csrw 0x800, %0" : : "r"(irq));
}

// Two pages when the image leaves room for them, else just the last one.
static bool twoPages() { return imageEnd() <= PAGE_A; }
bool available() { return !broken && imageEnd() <= PAGE_B; }

static const Record *page(uint32_t a) { return (const Record *)a; }

static bool writePage(uint32_t addr, const uint8_t *buf) {
    pageWrite(addr, (const uint32_t *)buf);
    return memcmp((const void *)addr, buf, PAGE) == 0;
}
#else
// Simulator: in-memory "flash" so save/continue flows can be scripted.
static uint8_t simFlash[2][PAGE];
bool available() { return !broken; }
static bool twoPages() { return true; }
static const Record *page(uint32_t a) { return (const Record *)simFlash[a == PAGE_B]; }
static bool writePage(uint32_t addr, const uint8_t *buf) {
    memcpy(simFlash[addr == PAGE_B], buf, PAGE);
    return true;
}
#endif

static const Record *best() {
    if (!available()) return nullptr;
    const Record *a = page(PAGE_A), *b = page(PAGE_B);
    bool va = twoPages() && valid(a), vb = valid(b);
    if (va && vb) return (int16_t)(a->seq - b->seq) > 0 ? a : b;
    return va ? a : (vb ? b : nullptr);
}

bool load(Casino &c, bool &hasGame) {
    hasGame = false;
    const Record *rec = best();
    if (!rec) return false;
    lastSeq = rec->seq;
    c.opt = rec->opt;
    c.stats = rec->stats;
    if (rec->hasGame && rec->purse > 0) {
        hasGame = true;
        c.purse = rec->purse;
        c.mode = rec->mode < MODE_COUNT ? rec->mode : 0;
        c.ante = rec->ante;
        c.streak = rec->streak;
    }
    return true;
}

#if CHTT_DEBUG && !defined(CHSIM)
// Debug builds on the board write only when a script asks (the E hook): the
// pages are shared with whatever else the board runs, and with the release.
static bool writes = false;
void allowWrites(bool on) { writes = on; }
#else
void allowWrites(bool) {}
#endif

bool store(const Casino &c, bool withGame) {
    if (!available()) return false;
#if CHTT_DEBUG && !defined(CHSIM)
    if (!writes) return false;
#endif
    uint8_t *buf = gfx_chunkScratch();          // idle between gfx_wait() and the next flush
    memset(buf, 0xFF, PAGE);
    Record &rec = *(Record *)buf;
    memset(&rec, 0, sizeof rec);
    rec.magic = MAGIC;
    rec.version = VERSION;
    rec.seq = (uint16_t)(lastSeq + 1);
    rec.opt = c.opt;
    rec.stats = c.stats;
    rec.hasGame = withGame ? 1 : 0;
    rec.purse = c.purse;
    rec.mode = c.mode; rec.ante = c.ante; rec.streak = c.streak;
    rec.crc = crc32(buf, (uint32_t)(sizeof rec - 4));
    uint32_t addr = ((rec.seq & 1) || !twoPages()) ? PAGE_B : PAGE_A;   // alternate pages
    if (!writePage(addr, buf)) { broken = true; return false; }
    lastSeq = rec.seq;
    return true;
}

#endif  // CHTT_LEAN

}  // namespace save
