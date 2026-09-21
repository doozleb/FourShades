#include <doctest/doctest.h>

#include "core/Ppu.h"

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
} // namespace

TEST_CASE("a drawn line is mode 2, then 3, then 0, and lasts 456 dots") {
    Ppu ppu;
    CHECK(ppu.mode() == 2);
    run(ppu, 76);
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
    const u8 seen = run(ppu, 144 * Ppu::kDotsPerLine);
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
    run(ppu, 153 * Ppu::kDotsPerLine);
    CHECK(ppu.ly() == 153);
    run(ppu, 4);
    CHECK(ppu.ly() == 0); // still line 153 internally, but LY reads 0
    CHECK(ppu.mode() == 1);
}

TEST_CASE("the STAT interrupt fires on a rising edge, not while the line stays high") {
    Ppu ppu;
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
    static_cast<void>(ppu.write(0xFF45, 0x02)); // LYC = 2
    static_cast<void>(ppu.write(0xFF41, 0x40)); // LYC source
    const u8 seen = run(ppu, 2 * Ppu::kDotsPerLine + 4);
    CHECK((ppu.read(0xFF41) & 0x04) != 0);
    CHECK((seen & 0x02) != 0);
}

TEST_CASE("writing STAT on DMG requests its spurious interrupt in the writing cycle") {
    Ppu ppu;       // power-on: mode 2, no sources selected
    run(ppu, 256); // now in mode 0 of line 0
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
    Ppu ppu; // mode 2: OAM blocked
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
