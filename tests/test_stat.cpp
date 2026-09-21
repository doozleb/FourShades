// The dot-level LCD timing Mooneye's hardware-verified acceptance/ppu tests
// measure from inside the machine. Everything asserted here is a number one
// of those ROMs checks; where a ROM gives a table, the table is copied from
// its source verbatim so that a regression names the exact cycle that moved.
#include <doctest/doctest.h>

#include "core/GameBoy.h"
#include "core/Ppu.h"

#include <cstdint>
#include <memory>
#include <vector>

using namespace fourshades;

namespace {

std::unique_ptr<GameBoy> machineRunning(std::vector<u8> program) {
    std::vector<u8> rom(0x8000, 0x00);
    rom[0x0100] = 0xC3; // JP 0x0150, clear of the header at 0x0134-0x014D
    rom[0x0101] = 0x50;
    rom[0x0102] = 0x01;
    for (std::size_t i = 0; i < program.size(); ++i) {
        rom[0x0150 + i] = program[i];
    }
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    rom[0x014D] = sum;
    auto cart = Cartridge::load(std::move(rom), nullptr);
    REQUIRE(cart.has_value());
    auto gb = std::make_unique<GameBoy>(std::move(*cart));
    for (int i = 0; i < 4000; ++i) {
        gb->step();
    }
    return gb;
}

// lcdon_timing-GS's measurement, to the letter: with the LCD already off,
//     ldh (LCDC), a   <- $81, the PPU starts here
//     nops <count>
//     ld a, (de)      <- the read the table is about
// The byte read is left in 0xC000. `lyc` is loaded before the PPU starts.
u8 readAfterLcdOn(int nops, u16 address, u8 lyc) {
    std::vector<u8> p{0x3E, 0x00, 0xE0, 0x40,                                   // LCD off
                      0x3E, lyc,  0xE0, 0x45,                                   // LYC
                      0x11, static_cast<u8>(address & 0xFF), static_cast<u8>(address >> 8),
                      0x3E, 0x81, 0xE0, 0x40};                                  // LCD on
    p.insert(p.end(), static_cast<std::size_t>(nops), 0x00);
    p.push_back(0x1A);                                 // ld a,(de)
    p.insert(p.end(), {0xEA, 0x00, 0xC0, 0x18, 0xFE}); // ld (C000),a ; jr -2
    return machineRunning(std::move(p))->peek(0xC000);
}

// lcdon_write_timing-GS's measurement: the same shape, but the instruction
// under test is `ld (de), a` with A = $81. The LCD is switched off again
// before the byte is read back, so only the write itself can be blocked.
// Both VRAM and OAM start as zeroes, so $81 means the write landed.
u8 writeAfterLcdOn(int nops, u16 address) {
    std::vector<u8> p{0x3E, 0x00, 0xE0, 0x40,
                      0x11, static_cast<u8>(address & 0xFF), static_cast<u8>(address >> 8),
                      0x3E, 0x81, 0xE0, 0x40};
    p.insert(p.end(), static_cast<std::size_t>(nops), 0x00);
    p.push_back(0x12);                                 // ld (de),a
    p.insert(p.end(), {0xAF, 0xE0, 0x40});             // xor a ; ldh (LCDC),a: LCD off
    p.push_back(0x1A);                                 // ld a,(de)
    p.insert(p.end(), {0xEA, 0x00, 0xC0, 0x18, 0xFE});
    return machineRunning(std::move(p))->peek(0xC000);
}

// The cycle counts lcdon_timing-GS and lcdon_write_timing-GS read at, in the
// order their expectation tables use.
constexpr int kReadCycles[24] = {0,  17,  60,  110, 130, 174, 224, 244,
                                 1,  18,  61,  111, 131, 175, 225, 245,
                                 2,  19,  62,  112, 132, 176, 226, 246};
constexpr int kWriteCycles[19] = {0,   17,  18,  60,  61,  110, 111, 112, 130, 131,
                                  132, 174, 175, 224, 225, 226, 244, 245, 246};

void checkReadTable(const char* what, u16 address, u8 lyc, const u8 (&expected)[24]) {
    for (int i = 0; i < 24; ++i) {
        CAPTURE(what);
        CAPTURE(kReadCycles[i]);
        CHECK(readAfterLcdOn(kReadCycles[i], address, lyc) == expected[i]);
    }
}

} // namespace

TEST_CASE("LY, STAT and read access after the LCD is switched on match lcdon_timing-GS") {
    // Copied from acceptance/ppu/lcdon_timing-GS.s (verified on DMG, MGB,
    // SGB, SGB2). Line 0 starts in mode 0 and goes straight to mode 3; it is
    // 452 dots long, so LY turns 1 four dots earlier than a full line would
    // give. From line 1 on, LY and the OAM lock lead STAT's mode 2 by one
    // M-cycle, and the VRAM lock leads STAT's mode 3 by one M-cycle.
    static constexpr u8 kLy[24] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02,
                                   0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02,
                                   0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02};
    static constexpr u8 kStatLyc0[24] = {0x84, 0x84, 0x87, 0x84, 0x82, 0x83, 0x80, 0x82,
                                         0x84, 0x87, 0x84, 0x80, 0x82, 0x80, 0x80, 0x82,
                                         0x84, 0x87, 0x84, 0x82, 0x83, 0x80, 0x82, 0x83};
    static constexpr u8 kStatLyc1[24] = {0x80, 0x80, 0x83, 0x80, 0x86, 0x87, 0x84, 0x82,
                                         0x80, 0x83, 0x80, 0x80, 0x86, 0x84, 0x80, 0x82,
                                         0x80, 0x83, 0x80, 0x86, 0x87, 0x84, 0x82, 0x83};
    static constexpr u8 kOam[24] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0xFF,
                                    0x00, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0xFF,
                                    0x00, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0xFF};
    static constexpr u8 kVram[24] = {0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00,
                                     0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF,
                                     0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF};
    checkReadTable("LY", 0xFF44, 0x00, kLy);
    checkReadTable("STAT, LYC=0", 0xFF41, 0x00, kStatLyc0);
    checkReadTable("STAT, LYC=1", 0xFF41, 0x01, kStatLyc1);
    checkReadTable("OAM read", 0xFE00, 0x00, kOam);
    checkReadTable("VRAM read", 0x8000, 0x00, kVram);
}

TEST_CASE("write access after the LCD is switched on matches lcdon_write_timing-GS") {
    // Copied from acceptance/ppu/lcdon_write_timing-GS.s. $81 means the write
    // landed. Writes are not refused over the same dots as reads: an OAM
    // write gets through on the M-cycle LY changes and again on the one where
    // the PPU has left mode 2 for mode 3 while STAT still reports mode 2, and
    // a VRAM write is refused only while STAT reports mode 3.
    static constexpr u8 kOam[19] = {0x81, 0x81, 0x00, 0x00, 0x81, 0x81, 0x81, 0x00, 0x00, 0x81,
                                    0x00, 0x00, 0x81, 0x81, 0x81, 0x00, 0x00, 0x81, 0x00};
    static constexpr u8 kVram[19] = {0x81, 0x81, 0x00, 0x00, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
                                     0x00, 0x00, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x00};
    for (int i = 0; i < 19; ++i) {
        CAPTURE(kWriteCycles[i]);
        CHECK(writeAfterLcdOn(kWriteCycles[i], 0xFE00) == kOam[i]);
        CHECK(writeAfterLcdOn(kWriteCycles[i], 0x8000) == kVram[i]);
    }
}

TEST_CASE("the mode 2 STAT source is pulsed at line 144, on VBlank's own dot") {
    // acceptance/ppu/vblank_stat_intr-GS measures the VBlank interrupt and a
    // mode-2-selected STAT interrupt at line 144 as simultaneous: it resets
    // DIV at a fixed point on line 143 and finds the same DIV value at both.
    Ppu ppu;
    ppu.write(0xFF41, 0x20); // mode 2 source only
    int vblankTick = -1;
    int statTick = -1;
    for (int tick = 0; tick < 145 * Ppu::kDotsPerLine / 4; ++tick) {
        const u8 requested = ppu.tick();
        if (ppu.lineNumber() != 144) {
            continue; // the mode 2 source fires on every drawn line as well
        }
        if ((requested & 0x01) != 0 && vblankTick < 0) {
            vblankTick = tick;
        }
        if ((requested & 0x02) != 0 && statTick < 0) {
            statTick = tick;
        }
    }
    CHECK(vblankTick > 0);
    CHECK(statTick == vblankTick);
}

TEST_CASE("the LY=LYC comparison stops with the PPU and keeps its last result") {
    // acceptance/ppu/stat_lyc_onoff, round by round (verified on every model).
    SUBCASE("round 1: the flag survives the PPU being switched off") {
        Ppu ppu;
        ppu.write(0xFF41, 0x40); // LYC source
        ppu.write(0xFF45, 0x90); // LYC = 144
        while (ppu.ly() != 144) {
            ppu.tick();
        }
        ppu.tick();
        CHECK((ppu.read(0xFF41) & 0x04) != 0); // LY = LYC = 144
        ppu.write(0xFF40, 0x11);               // LCD off: STAT now reports mode 0
        CHECK(ppu.read(0xFF41) == 0xC4);
        ppu.write(0xFF45, 0x01); // the comparison clock is not running
        CHECK(ppu.read(0xFF41) == 0xC4);
        // Switching the PPU on restarts it: LY = 0 against LYC = 1 is no
        // match, and a falling flag asks for no interrupt.
        CHECK((ppu.write(0xFF40, 0x80) & 0x02) == 0);
        CHECK(ppu.read(0xFF41) == 0xC0);
    }
    SUBCASE("round 2: an unchanged result produces no new interrupt") {
        Ppu ppu;
        ppu.write(0xFF41, 0x40);
        ppu.write(0xFF45, 0x90);
        while (ppu.ly() != 144) {
            ppu.tick();
        }
        ppu.tick();
        ppu.write(0xFF40, 0x11);
        ppu.write(0xFF45, 0x00); // no effect while the PPU is off
        CHECK(ppu.read(0xFF41) == 0xC4);
        // LY = 144 vs LYC = $90 becomes LY = 0 vs LYC = 0: still a match, so
        // the level line never falls and never rises again.
        CHECK((ppu.write(0xFF40, 0x80) & 0x02) == 0);
        CHECK(ppu.read(0xFF41) == 0xC4);
        CHECK((ppu.tick() & 0x02) == 0);
    }
    SUBCASE("round 3: a false result survives too") {
        Ppu ppu;
        ppu.write(0xFF41, 0x40);
        ppu.write(0xFF45, 0x00);
        while (ppu.ly() != 144) {
            ppu.tick();
        }
        ppu.tick();
        ppu.write(0xFF40, 0x11);
        CHECK(ppu.read(0xFF41) == 0xC0);
        ppu.write(0xFF45, 0x01);
        CHECK(ppu.read(0xFF41) == 0xC0);
        CHECK((ppu.write(0xFF40, 0x80) & 0x02) == 0); // LY = 0 vs LYC = 1
        CHECK(ppu.read(0xFF41) == 0xC0);
    }
    SUBCASE("round 4: switching the PPU on can raise the line inside that cycle") {
        Ppu ppu;
        ppu.write(0xFF41, 0x40);
        ppu.write(0xFF45, 0x00);
        while (ppu.ly() != 144) {
            ppu.tick();
        }
        ppu.tick();
        ppu.write(0xFF40, 0x11);
        CHECK(ppu.read(0xFF41) == 0xC0);
        // LY = 0 against LYC = 0 is a match the moment the comparison
        // restarts. The ROM has a `di` as the very next instruction, so an
        // interrupt one M-cycle later would never be taken: the request has
        // to come out of the write itself.
        CHECK((ppu.write(0xFF40, 0x80) & 0x02) != 0);
        CHECK(ppu.read(0xFF41) == 0xC4);
    }
}

TEST_CASE("LY leads the mode 0 STAT interrupt by 50 M-cycles, fewer as SCX grows") {
    // acceptance/ppu/hblank_ly_scx_timing-GS: with (SCX mod 8) = 0 the read
    // 50 M-cycles after the mode 0 interrupt is the first to see the new LY;
    // 1-4 make it 49 and 5-7 make it 48, because mode 3 is lengthened by
    // exactly SCX mod 8 dots and the CPU can only look on M-cycle boundaries.
    for (int scx = 0; scx <= 8; ++scx) {
        CAPTURE(scx);
        Ppu ppu;
        ppu.write(0xFF43, static_cast<u8>(scx));
        ppu.write(0xFF41, 0x08); // mode 0 source
        while (ppu.lineNumber() != 2) {
            ppu.tick();
        }
        int interruptAt = -1;
        int lyChangedAt = -1;
        for (int tick = 0; tick < Ppu::kDotsPerLine / 4 + 4; ++tick) {
            const u8 requested = ppu.tick();
            if ((requested & 0x02) != 0 && interruptAt < 0) {
                interruptAt = tick;
            }
            if (ppu.ly() == 3 && lyChangedAt < 0) {
                lyChangedAt = tick;
            }
        }
        REQUIRE(interruptAt >= 0);
        REQUIRE(lyChangedAt > interruptAt);
        const int low = scx % 8;
        const int expected = low == 0 ? 50 : (low <= 4 ? 49 : 48);
        CHECK(lyChangedAt - interruptAt == expected);
    }
}

TEST_CASE("mode 3 lengthens by the object penalty intr_2_mode0_timing_sprites measures") {
    // A slice of acceptance/ppu/intr_2_mode0_timing_sprites' 104 cases, one
    // from each of its groups. Its "extra cycles" figure is the number of
    // M-cycles by which the mode 0 STAT interrupt is pushed back, which is
    // how many whole M-cycles longer the CPU sees mode 3 last.
    struct Case {
        int extraCycles;
        std::vector<u8> x;
    };
    const std::vector<Case> cases = {
        {2, {0}},                                        // one object, off the left edge
        {2, {3}},   {1, {4}},   {2, {8}},   {1, {164}},  // one object, X mod 8 matters
        {4, {0, 0}},                                     // a second object in the same tile
        {5, {0, 8}},                                     // ... or in the next tile: two terms
        {16, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},            // ten in one tile
        {15, {2, 2, 2, 2, 2, 2, 2, 2, 2, 2}},
        {17, {0, 0, 0, 0, 0, 160, 160, 160, 160, 160}},  // five and five, two tiles
        {27, {0, 8, 16, 24, 32, 40, 48, 56, 64, 72}},    // ten tiles
        {15, {5, 13, 21, 29, 37, 45, 53, 61, 69, 77}},   // ten tiles, no tile term at all
        {0, {168, 168, 168, 168, 168, 168, 168, 168, 168, 168}}, // all off the right edge
    };
    for (const Case& c : cases) {
        CAPTURE(c.extraCycles);
        CAPTURE(c.x.size());
        CAPTURE(static_cast<int>(c.x.front()));
        Ppu ppu;
        ppu.write(0xFF40, 0x11); // LCD off
        for (std::size_t i = 0; i < c.x.size(); ++i) {
            ppu.dmaWriteOam(static_cast<int>(i) * 4 + 0, 0x52); // every object on line 0x42
            ppu.dmaWriteOam(static_cast<int>(i) * 4 + 1, c.x[i]);
            ppu.dmaWriteOam(static_cast<int>(i) * 4 + 2, static_cast<u8>(0x30 + i));
            ppu.dmaWriteOam(static_cast<int>(i) * 4 + 3, 0x00);
        }
        ppu.write(0xFF40, 0x93); // LCD on, background and objects enabled
        while (ppu.lineNumber() != 0x42) {
            ppu.tick();
        }
        while (ppu.mode() != 3) {
            ppu.tick();
        }
        int drawing = 0;
        while (ppu.mode() == 3) {
            ppu.tick();
            drawing += 4;
        }
        CHECK(drawing / 4 - Ppu::kMinDrawDots / 4 == c.extraCycles);
    }
}
