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
    run(ppu, 4); // 80 dots of OAM scan
    CHECK(ppu.mode() == 3);
    run(ppu, 168);
    CHECK(ppu.mode() == 3);
    run(ppu, 4); // 172 dots of drawing
    CHECK(ppu.mode() == 0);
    CHECK(ppu.ly() == 0);
    run(ppu, 204); // the rest of the line
    CHECK(ppu.ly() == 1);
    CHECK(ppu.mode() == 2);
}

TEST_CASE("VBlank starts at line 144 and asks for its interrupt once") {
    Ppu ppu;
    const u8 seen = run(ppu, 144 * Ppu::kDotsPerLine);
    CHECK(ppu.ly() == 144);
    CHECK(ppu.mode() == 1);
    CHECK((seen & 0x01) != 0);
    const u8 rest = run(ppu, 9 * Ppu::kDotsPerLine);
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
    ppu.write(0xFF41, 0x20); // mode 2 source only
    ppu.tick();              // the DMG write quirk's own interrupt (tested below)
    u8 seen = 0;
    for (int i = 0; i < 15; ++i) {
        seen = static_cast<u8>(seen | ppu.tick()); // still inside mode 2
    }
    CHECK((seen & 0x02) == 0); // the line stayed high: no second interrupt
    run(ppu, Ppu::kDotsPerLine - 80);            // through mode 3 and 0
    const u8 next = run(ppu, 80);                // back into mode 2
    CHECK((next & 0x02) != 0);
}

TEST_CASE("LY=LYC sets the flag and can request an interrupt") {
    Ppu ppu;
    ppu.write(0xFF45, 0x02); // LYC = 2
    ppu.write(0xFF41, 0x40); // LYC source
    const u8 seen = run(ppu, 2 * Ppu::kDotsPerLine + 4);
    CHECK((ppu.read(0xFF41) & 0x04) != 0);
    CHECK((seen & 0x02) != 0);
}

TEST_CASE("writing STAT on DMG can request a spurious interrupt") {
    Ppu ppu;               // power-on: mode 2, no sources selected
    run(ppu, 200);         // now in mode 0 of line 0
    ppu.write(0xFF41, 0x00); // selects nothing, but acts as 0xFF for one M-cycle
    CHECK((ppu.tick() & 0x02) != 0);
}

TEST_CASE("turning the LCD off blanks the screen and holds LY at 0") {
    Ppu ppu;
    run(ppu, 3 * Ppu::kDotsPerLine);
    ppu.write(0xFF40, 0x11); // LCD off
    CHECK(ppu.ly() == 0);
    CHECK(ppu.mode() == 0);
    CHECK((ppu.read(0xFF41) & 0x03) == 0);
    CHECK(run(ppu, 10 * Ppu::kDotsPerLine) == 0); // no interrupts while off
    CHECK(ppu.ly() == 0);
    ppu.write(0xFF40, 0x91); // back on: drawing starts again
    run(ppu, 4);
    CHECK(ppu.mode() == 2);
}

TEST_CASE("VRAM and OAM keep their own storage, and LY is read-only") {
    Ppu ppu;
    ppu.write(0xFF40, 0x11); // LCD off, so nothing is blocked
    ppu.vramWrite(0x8000, 0x3C);
    ppu.oamWrite(0xFE00, 0x42);
    CHECK(ppu.peekVram(0x8000) == 0x3C);
    CHECK(ppu.peekOam(0xFE00) == 0x42);
    ppu.dmaWriteOam(1, 0x77);
    CHECK(ppu.peekOam(0xFE01) == 0x77);
    ppu.write(0xFF44, 0x55);
    CHECK(ppu.read(0xFF44) == 0x00);
}

TEST_CASE("the frame starts blank and every pixel is a shade 0-3") {
    Ppu ppu;
    run(ppu, 154 * Ppu::kDotsPerLine);
    CHECK(ppu.frame().size() == static_cast<std::size_t>(Ppu::kWidth * Ppu::kHeight));
    for (const u8 pixel : ppu.frame()) {
        CHECK(pixel <= 3);
    }
}
