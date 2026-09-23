#include <doctest/doctest.h>

#include "core/Ppu.h"

#include <algorithm>
#include <cstdint>

using namespace fourshades;

namespace {
// Runs `dots` dots (4 per tick) and returns every IF bit seen.
u8 run(Ppu& ppu, int dots) {
    u8 seen = 0;
    for (int i = 0; i < dots / 4; ++i) {
        seen = static_cast<u8>(seen | ppu.tick());
    }
    return seen;
}

// Advances to the top of a frame, wherever the PPU powered on: the first
// M-cycle boundary of line 0 at which STAT reports mode 2. STAT trails the
// PPU's own mode by one M-cycle, so that boundary is dot 4, not dot 0 - line
// 0 still reports mode 1 for its first M-cycle. Cases below that count dots
// from here therefore start four dots into the line.
void toTopOfFrame(Ppu& ppu) {
    while (!(ppu.ly() == 0 && ppu.mode() == 2)) {
        ppu.tick();
    }
}
} // namespace

TEST_CASE("the PPU starts where the boot ROM left it") {
    Ppu ppu;
    CHECK(ppu.mode() == 1);       // VBlank
    CHECK(ppu.read(0xFF44) == 0); // LY reads 0 on line 153
    CHECK(ppu.read(0xFF41) == 0x85);
}

// The dot within line 153 the boot ROM leaves the PPU on is not given by Pan
// Docs. It was solved from the two reads a power-on register walk makes: see
// docs/known-divergences.md, "The PPU's power-on phase within line 153".
TEST_CASE("the PPU's power-on phase is the measured one, and M-cycle aligned") {
    Ppu ppu;
    CHECK(ppu.lineDot() == 356);
    CHECK(ppu.lineDot() % 4 == 0); // an odd phase desynchronises dot from M-cycle
}

// The same two constraints the register walk imposes, pinned here by the
// M-cycle counts it reads at rather than by its name: at 1139 M-cycles from
// power-on STAT must still report mode 0, and at 1190 LY must read 0x0A.
TEST_CASE("the power-on phase puts mode 0 at 1139 M-cycles and LY 0x0A at 1190") {
    Ppu ppu;
    for (int i = 0; i < 1139; ++i) {
        ppu.tick();
    }
    CHECK(ppu.ly() == 9);
    CHECK(ppu.mode() == 0);
    CHECK(ppu.read(0xFF41) == 0x80); // mode 0, LYC 0 unmatched
    for (int i = 1139; i < 1190; ++i) {
        ppu.tick();
    }
    CHECK(ppu.ly() == 0x0A);
    CHECK(ppu.read(0xFF44) == 0x0A);
}

TEST_CASE("a drawn line is mode 2, then 3, then 0, and lasts 456 dots") {
    Ppu ppu;
    toTopOfFrame(ppu); // line 0, dot 4: the first M-cycle STAT calls mode 2
    CHECK(ppu.mode() == 2);
    run(ppu, 72); // dot 76
    CHECK(ppu.mode() == 2);
    run(ppu, 4);            // 80 dots: the OAM scan is over ...
    CHECK(ppu.mode() == 2); // ... but STAT reports mode 2 for one more M-cycle
    CHECK(ppu.vramBlocked()); // the fetcher already has the bus, though
    run(ppu, 4);
    CHECK(ppu.mode() == 3);
    run(ppu, 168);
    CHECK(ppu.mode() == 3);
    run(ppu, 4); // 172 dots of drawing, reported from dot 84 to dot 256
    CHECK(ppu.mode() == 0);
    CHECK(ppu.ly() == 0);
    run(ppu, 200); // the rest of the line
    CHECK(ppu.ly() == 1);
    CHECK(ppu.mode() == 0);  // LY leads STAT's mode 2 by one M-cycle ...
    CHECK(ppu.oamBlocked()); // ... but the scan has already claimed OAM
    run(ppu, 4);
    CHECK(ppu.mode() == 2);
}

TEST_CASE("VBlank starts at line 144 and asks for its interrupt once") {
    Ppu ppu;
    toTopOfFrame(ppu); // line 0, dot 4
    const u8 seen = run(ppu, 144 * Ppu::kDotsPerLine - 4);
    CHECK(ppu.ly() == 144);
    CHECK(ppu.mode() == 0); // STAT catches up with mode 1 an M-cycle later
    CHECK((seen & 0x01) != 0);
    run(ppu, 4);
    CHECK(ppu.mode() == 1);
    const u8 rest = run(ppu, 9 * Ppu::kDotsPerLine - 4);
    CHECK((rest & 0x01) == 0); // only once per frame
    CHECK(ppu.frameCount() == 1);
    run(ppu, Ppu::kDotsPerLine);
    CHECK(ppu.ly() == 0); // 154 lines, then back to the top
}

TEST_CASE("line 153 reports as line 0 after its first few dots") {
    Ppu ppu;
    toTopOfFrame(ppu); // line 0, dot 4
    run(ppu, 153 * Ppu::kDotsPerLine - 4);
    CHECK(ppu.ly() == 153);
    run(ppu, 4);
    CHECK(ppu.ly() == 0); // still line 153 internally, but LY reads 0
    CHECK(ppu.mode() == 1);
}

TEST_CASE("the STAT interrupt fires on a rising edge, not while the line stays high") {
    Ppu ppu;
    toTopOfFrame(ppu); // line 0, dot 4: inside mode 2
    // mode 2 source only; the DMG write quirk's own interrupt (tested below)
    // comes straight back out of the write.
    CHECK((ppu.write(0xFF41, 0x20) & 0x02) != 0);
    u8 seen = 0;
    for (int i = 0; i < 16; ++i) {
        seen = static_cast<u8>(seen | ppu.tick()); // still inside mode 2
    }
    CHECK((seen & 0x02) == 0); // the line stayed high: no second interrupt
    run(ppu, Ppu::kDotsPerLine - 80);            // through mode 3 and 0
    const u8 next = run(ppu, 80);                // back into mode 2
    CHECK((next & 0x02) != 0);
}

TEST_CASE("LY=LYC sets the flag and can request an interrupt") {
    Ppu ppu;
    toTopOfFrame(ppu); // line 0, dot 4
    static_cast<void>(ppu.write(0xFF45, 0x02)); // LYC = 2
    static_cast<void>(ppu.write(0xFF41, 0x40)); // LYC source
    const u8 seen = run(ppu, 2 * Ppu::kDotsPerLine); // line 2, dot 4
    CHECK((ppu.read(0xFF41) & 0x04) != 0);
    CHECK((seen & 0x02) != 0);
}

TEST_CASE("writing STAT on DMG requests its spurious interrupt in the writing cycle") {
    Ppu ppu;           // no sources selected
    toTopOfFrame(ppu); // line 0, dot 4
    run(ppu, 252);     // now in mode 0 of line 0 (dot 256)
    CHECK(ppu.mode() == 0);
    // The write selects nothing, but acts as if 0xFF had been written for one
    // cycle, and mode 0 is among the conditions that raises the level line.
    // Hardware does that inside the writing M-cycle, not the one after it:
    // stat_lyc_onoff's round 4 only passes because switching the LCD on
    // raises the line in time for the very next instruction boundary.
    CHECK((ppu.write(0xFF41, 0x00) & 0x02) != 0);
    CHECK((ppu.tick() & 0x02) == 0); // and only once
}

TEST_CASE("turning the LCD off blanks the screen and holds LY at 0") {
    Ppu ppu;
    run(ppu, 3 * Ppu::kDotsPerLine);
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off
    CHECK(ppu.ly() == 0);
    CHECK(ppu.mode() == 0);
    CHECK((ppu.read(0xFF41) & 0x03) == 0);
    CHECK(run(ppu, 10 * Ppu::kDotsPerLine) == 0); // no interrupts while off
    CHECK(ppu.ly() == 0);
    static_cast<void>(ppu.write(0xFF40, 0x91)); // back on: drawing starts again
    // The line the LCD comes on for has no mode 2 at all: it reports mode 0
    // and goes straight to mode 3 eighty dots in.
    run(ppu, 4);
    CHECK(ppu.mode() == 0);
    CHECK_FALSE(ppu.oamBlocked());
    run(ppu, 76);
    CHECK(ppu.mode() == 3);
}

TEST_CASE("VRAM and OAM keep their own storage, and LY is read-only") {
    Ppu ppu;
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off, so nothing is blocked
    ppu.vramWrite(0x8000, 0x3C);
    ppu.oamWrite(0xFE00, 0x42);
    CHECK(ppu.peekVram(0x8000) == 0x3C);
    CHECK(ppu.peekOam(0xFE00) == 0x42);
    ppu.dmaWriteOam(1, 0x77);
    CHECK(ppu.peekOam(0xFE01) == 0x77);
    static_cast<void>(ppu.write(0xFF44, 0x55));
    CHECK(ppu.read(0xFF44) == 0x00);
}

TEST_CASE("VRAM is blocked in mode 3, OAM in modes 2 and 3") {
    Ppu ppu;
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off: nothing blocked
    ppu.vramWrite(0x8000, 0x11);
    ppu.oamWrite(0xFE00, 0x22);
    CHECK(ppu.vramRead(0x8000) == 0x11);
    CHECK(ppu.oamRead(0xFE00) == 0x22);

    static_cast<void>(ppu.write(0xFF40, 0x91)); // on, but the first line has no mode 2 ...
    while (ppu.lineNumber() != Ppu::kLines - 1) { ppu.tick(); }
    while (ppu.lineNumber() == Ppu::kLines - 1) { ppu.tick(); } // ... so use the next line 0
    CHECK(ppu.lineNumber() == 0);
    CHECK(ppu.mode() == 1);  // STAT still reports VBlank for one more M-cycle
    CHECK(ppu.oamBlocked()); // even though line 0's scan already owns OAM
    CHECK_FALSE(ppu.vramBlocked());

    run(ppu, 4);
    CHECK(ppu.mode() == 2);
    CHECK_FALSE(ppu.vramBlocked());
    CHECK(ppu.oamBlocked());
    CHECK(ppu.vramRead(0x8000) == 0x11);
    CHECK(ppu.oamRead(0xFE00) == 0xFF);
    ppu.oamWrite(0xFE00, 0x33); // dropped
    CHECK(ppu.peekOam(0xFE00) == 0x22);

    run(ppu, Ppu::kOamScanDots - 4); // dot 80: the fetcher has taken VRAM ...
    CHECK(ppu.mode() == 2);          // ... while STAT still reports mode 2
    CHECK(ppu.vramBlocked());
    CHECK(ppu.oamBlocked());
    // Writes are not refused over the same dots as reads: this one M-cycle,
    // where the PPU has left mode 2 but STAT has not caught up, lets an OAM
    // write through (Mooneye acceptance/ppu/lcdon_write_timing-GS).
    CHECK_FALSE(ppu.oamWriteBlocked());
    CHECK_FALSE(ppu.vramWriteBlocked());

    run(ppu, 4);
    CHECK(ppu.mode() == 3);
    CHECK(ppu.vramBlocked());
    CHECK(ppu.oamBlocked());
    CHECK(ppu.vramWriteBlocked());
    CHECK(ppu.oamWriteBlocked());
    CHECK(ppu.vramRead(0x8000) == 0xFF);
    ppu.vramWrite(0x8000, 0x44); // dropped
    CHECK(ppu.peekVram(0x8000) == 0x11);

    run(ppu, Ppu::kMinDrawDots); // into mode 0
    CHECK(ppu.mode() == 0);
    CHECK_FALSE(ppu.vramBlocked());
    CHECK_FALSE(ppu.oamBlocked());
    CHECK(ppu.vramRead(0x8000) == 0x11);
    CHECK(ppu.oamRead(0xFE00) == 0x22);
}

TEST_CASE("DMA and peek ignore blocking") {
    Ppu ppu;
    toTopOfFrame(ppu); // mode 2: OAM blocked
    ppu.dmaWriteOam(0, 0x5A);
    CHECK(ppu.peekOam(0xFE00) == 0x5A);
    CHECK(ppu.oamRead(0xFE00) == 0xFF);
}

TEST_CASE("the frame starts blank and every pixel is a shade 0-3") {
    Ppu ppu;
    run(ppu, 154 * Ppu::kDotsPerLine);
    CHECK(ppu.frame().size() == static_cast<std::size_t>(Ppu::kWidth * Ppu::kHeight));
    for (const u8 pixel : ppu.frame()) {
        CHECK(pixel <= 3);
    }
}

namespace {
// True when every pixel of the frame is shade 0 - what a DMG panel reads when
// nothing is driving it (Pan Docs, LCDC: "When the display is disabled the
// screen is blank, which on DMG is displayed as a white 'whiter' than color
// #0").
bool blank(const Ppu& ppu) {
    const auto& frame = ppu.frame();
    return std::all_of(frame.begin(), frame.end(), [](u8 pixel) { return pixel == 0; });
}
} // namespace

// Pan Docs, Reducing Power Consumption: STOP "is intended to switch the Game
// Boy into VERY low power standby mode", and on CGB "leaving the LCD enabled
// when invoking STOP will result in a black screen". Nothing clocks the PPU
// while the machine is in that state, so it neither advances nor drives the
// panel, and a DMG panel with no drive reads blank. See
// docs/known-divergences.md, "STOP stops the PPU and blanks the LCD".
TEST_CASE("a stopped clock blanks the LCD and freezes the PPU where it stood") {
    Ppu ppu;
    static_cast<void>(ppu.write(0xFF47, 0x0F)); // BGP: colour 0 becomes shade 3
    run(ppu, Ppu::kLines * Ppu::kDotsPerLine);  // a whole frame of empty tiles
    REQUIRE_FALSE(blank(ppu));

    ppu.setClockStopped(true);
    CHECK(ppu.clockStopped());
    CHECK(blank(ppu));
    const int line = ppu.lineNumber();
    const int dot = ppu.lineDot();
    const std::uint64_t frames = ppu.frameCount();
    // Two frames' worth of nothing, and 37 M-cycles more: not a whole number
    // of frames, so a PPU still running would land on another line and dot.
    run(ppu, 2 * Ppu::kLines * Ppu::kDotsPerLine + 37 * 4);
    CHECK(ppu.lineNumber() == line);
    CHECK(ppu.lineDot() == dot);
    CHECK(ppu.frameCount() == frames);
    CHECK(blank(ppu));

    // Starting the clock again does not blank anything: the PPU picks the
    // frame up where it left it and draws over it.
    ppu.setClockStopped(false);
    CHECK_FALSE(ppu.clockStopped());
    run(ppu, Ppu::kLines * Ppu::kDotsPerLine);
    CHECK(ppu.frameCount() > frames);
    CHECK_FALSE(blank(ppu));
}
