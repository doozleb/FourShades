#include <doctest/doctest.h>

#include "core/Ppu.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace fourshades;

namespace {
// Switching the LCD on starts a line that has no mode 2 at all: it reports
// mode 0, goes straight to mode 3, selects no objects and is 452 dots long
// (Mooneye acceptance/ppu/lcdon_timing-GS, pinned in tests/test_stat.cpp).
// Every case below wants an ordinary line, so run the odd one out plus the
// rest of the frame and come back to a line 0 that behaves like any other.
void enableLcd(Ppu& ppu, u8 lcdc) {
    static_cast<void>(ppu.write(0xFF40, lcdc));
    while (ppu.lineNumber() != Ppu::kLines - 1) { ppu.tick(); }
    while (ppu.lineNumber() == Ppu::kLines - 1) { ppu.tick(); }
}

// Fills tile 0 with a row pattern and points the whole map at it.
void setUpTile(Ppu& ppu, u8 low, u8 high) {
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), low);
        ppu.vramWrite(static_cast<u16>(0x8001 + row), high);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4)); // BGP: colour 0->0, 1->1, 2->2, 3->3
    enableLcd(ppu, 0x91); // LCD on, BG on, tile data at 0x8000, map 0x9800
}

// Runs forward to `dot` of `line`. Line 0 draws four dots ahead of every
// other line (see "line 0 starts drawing four dots earlier" below), so a case
// about where a mid-line write lands says which line it means.
void runTo(Ppu& ppu, int line, int dot) {
    while (ppu.lineNumber() != line || ppu.lineDot() != dot) { ppu.tick(); }
}

// Runs one whole line and returns the dot mode 3 ended on.
int runLine(Ppu& ppu) {
    int dots = 0;
    while (ppu.mode() != 3) { ppu.tick(); dots += 4; }
    int drawing = 0;
    while (ppu.mode() == 3) { ppu.tick(); drawing += 4; }
    // A line's last pixels reach the frame after mode 3 ends - rendering
    // trails the mode-3 window at both ends (docs/known-divergences.md,
    // "Rendering runs seven dots behind the mode-3 window") - so let the
    // pipeline finish before frame() is read. Running to the end of the line
    // rather than a fixed number of M-cycles keeps this correct whatever the
    // lag becomes: the line is always drawn by the time the next one starts.
    const int drawnLine = ppu.lineNumber();
    while (ppu.lineNumber() == drawnLine) { ppu.tick(); }
    return drawing;
}
} // namespace

TEST_CASE("shadeFor picks the two bits a palette assigns to a colour") {
    CHECK(shadeFor(0xE4, 0) == 0);
    CHECK(shadeFor(0xE4, 1) == 1);
    CHECK(shadeFor(0xE4, 2) == 2);
    CHECK(shadeFor(0xE4, 3) == 3);
    CHECK(shadeFor(0x1B, 0) == 3); // reversed palette
    CHECK(shadeFor(0xFF, 1) == 3);
}

TEST_CASE("a tiled background is drawn with the palette applied") {
    Ppu ppu;
    setUpTile(ppu, 0b10101010, 0b11001100); // colours 3,2,1,0 repeating
    runLine(ppu);
    const auto& frame = ppu.frame();
    CHECK(frame[0] == 3);
    CHECK(frame[1] == 2);
    CHECK(frame[2] == 1);
    CHECK(frame[3] == 0);
    CHECK(frame[4] == 3);
    CHECK(frame[8] == 3); // the next tile repeats
    CHECK(frame[159] == 0);
}

TEST_CASE("a plain line draws in 172 dots and SCX's low bits lengthen it") {
    Ppu ppu;
    setUpTile(ppu, 0xFF, 0x00); // every pixel colour 1
    CHECK(runLine(ppu) == 172);
    // SCX = 4 gives a raw mode-3 length of 172 + 4 = 176 dots, which is itself
    // a multiple of 4. runLine only samples mode() between ticks of 4 dots, so
    // this is the one value in this test that is observed exactly: an
    // off-by-one bug here would produce 177 raw dots and still report 180,
    // the same number the (untouched) SCX = 5 case below expects.
    static_cast<void>(ppu.write(0xFF43, 0x04)); // SCX = 4
    CHECK(runLine(ppu) == 176);
    static_cast<void>(ppu.write(0xFF43, 0x05)); // SCX = 5
    CHECK(runLine(ppu) == 180); // 172 + 5 = 177 dots, reported as 180 when counted in whole M-cycles
}

TEST_CASE("rendering starts seven dots after mode 3 does") {
    // Mealybug Tearoom's m3_bgp_change writes BGP during mode 3 and
    // photographs the result on real DMG hardware; the reference image pins
    // which pixel each write first colours. Its second write lands on dot 100
    // of the line and first shows on pixel 1, so pixel 0 is emitted on dot
    // 100 - twenty dots into a mode 3 that began on dot 80, not thirteen.
    // Mooneye's intr_2_mode3_timing and intr_2_mode0_timing keep the mode-3
    // window itself where it is, so the gap is real: rendering trails it.
    Ppu ppu;
    setUpTile(ppu, 0x00, 0x00); // every pixel colour 0
    runTo(ppu, 1, 100); // twenty dots into line 1's mode 3
    static_cast<void>(ppu.write(0xFF47, 0xE7)); // BGP: colour 0 now shades to 3
    while (ppu.lineDot() < 300) { ppu.tick(); }
    const auto* row = &ppu.frame()[Ppu::kWidth];
    CHECK(row[0] == 0); // drawn on dot 100, before the write landed
    CHECK(row[1] == 3); // drawn on dot 101, the first dot that sees it
    CHECK(row[2] == 3);
}

TEST_CASE("line 0 starts drawing four dots earlier than the lines below it") {
    // Every Mealybug Tearoom ppu test runs its handler off the mode-2 STAT
    // interrupt and spends four extra cycles on every line except line 0
    // (inc/utils.asm's line_0_fix, "line 0 timing is different by 4 cycles"),
    // and in every DMG reference image line 0 then comes out identical to
    // line 1. Mooneye's intr_1_2_timing-GS pins the interrupt itself, so it
    // is the drawing that moves: line 0's OAM scan is 76 dots, not 80, and a
    // write landing on the same dot of the line reaches a pixel four further
    // to the right.
    Ppu ppu;
    setUpTile(ppu, 0x00, 0x00); // every pixel colour 0
    const auto firstPixelChanged = [&ppu](int line) {
        static_cast<void>(ppu.write(0xFF47, 0xE4)); // colour 0 -> shade 0
        runTo(ppu, line, 100);
        static_cast<void>(ppu.write(0xFF47, 0xE7)); // colour 0 -> shade 3
        while (ppu.lineDot() < 300) { ppu.tick(); }
        const std::size_t row = static_cast<std::size_t>(line) * Ppu::kWidth;
        for (int x = 0; x < Ppu::kWidth; ++x) {
            if (ppu.frame()[row + static_cast<std::size_t>(x)] != 0) { return x; }
        }
        return -1;
    };
    CHECK(firstPixelChanged(0) == 5);
    CHECK(firstPixelChanged(1) == 1);
    CHECK(firstPixelChanged(2) == 1);
}

TEST_CASE("a palette written during mode 3 reads as the old value OR the new one for one dot") {
    // Mealybug Tearoom's m3_bgp_change, on real DMG hardware, shows one
    // pixel of a third colour at each edge of every band it paints. On the
    // line where the palette goes 0x46 -> 0x45, the edge pixels are shade 3,
    // which is neither palette's colour 0 (2 and 1) but is 0x46 | 0x45 =
    // 0x47's. The reference image shows the same one-pixel seam at every one
    // of the six palette writes on every line, so it is the write itself, not
    // the values: for the dot the write lands on, the palette the pixel is
    // shaded with is the bitwise OR of the old and new values.
    Ppu ppu;
    setUpTile(ppu, 0x00, 0x00); // every pixel colour 0
    static_cast<void>(ppu.write(0xFF47, 0x01)); // colour 0 -> shade 1
    runTo(ppu, 1, 100);
    static_cast<void>(ppu.write(0xFF47, 0x02)); // colour 0 -> shade 2
    while (ppu.lineDot() < 300) { ppu.tick(); }
    const auto* row = &ppu.frame()[Ppu::kWidth];
    CHECK(row[0] == 1); // the old palette
    CHECK(row[1] == 3); // 0x01 | 0x02 = 0x03, for this one dot only
    CHECK(row[2] == 2); // the new palette
    CHECK(row[3] == 2);
}

TEST_CASE("LCDC's colour-selection bits are read a dot before the palette shades the pixel") {
    // The case above pins the palette to the pixel's own dot: a write landing
    // after the M-cycle that ends on dot 100 shades the pixel drawn on dot 101.
    // Mealybug Tearoom's three references for LCDC bits 0 and 1 put their seams
    // one pixel further right than that, on real DMG hardware, so the colour a
    // pixel carries is chosen a dot before the palette shades it. See
    // PixelPipeline::kLcdcSelectLag.
    Ppu ppu;
    setUpTile(ppu, 0xFF, 0xFF); // every pixel colour 3, shade 3 under BGP 0xE4
    runTo(ppu, 1, 100);
    static_cast<void>(ppu.write(0xFF40, 0x90)); // LCDC bit 0 clear: blank the background
    while (ppu.lineDot() < 300) { ppu.tick(); }
    const auto* row = &ppu.frame()[Ppu::kWidth];
    CHECK(row[0] == 3); // drawn on dot 100, before the write landed
    CHECK(row[1] == 3); // drawn on dot 101 and chosen on dot 100: still the old bit
    CHECK(row[2] == 0); // chosen on dot 101, the first dot that sees it
    CHECK(row[3] == 0);
}

TEST_CASE("the object-enable bit is read a dot early too, on the same dot as the blanking bit") {
    // Bit 1 at emission decides whether an object already merged into the queue
    // covers the background, and it comes from the same dot bit 0 does. An
    // object at screen x = 0 stalls the line eleven dots, so pixel 0 is drawn on
    // dot 111 and pixel n on dot 111 + n; a write landing after the M-cycle that
    // ends on dot 112 is therefore chosen from first by pixel 3.
    Ppu ppu;
    setUpTile(ppu, 0x00, 0x00); // background all colour 0
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so OAM and VRAM land
    for (u16 row = 0; row < 16; ++row) {
        ppu.vramWrite(static_cast<u16>(0x8020 + row), 0xFF); // tile 2: every pixel colour 3
    }
    static_cast<void>(ppu.write(0xFF48, 0xE4)); // OBP0: colour 3 -> shade 3
    ppu.oamWrite(0xFE00, 0x10); // Y = 16: on every line drawn here
    ppu.oamWrite(0xFE01, 0x08); // X = 8: screen x = 0, so it covers pixels 0-7
    ppu.oamWrite(0xFE02, 0x02);
    ppu.oamWrite(0xFE03, 0x00);
    enableLcd(ppu, 0x93); // LCD on, background on, objects on
    runTo(ppu, 1, 112);
    static_cast<void>(ppu.write(0xFF40, 0x91)); // LCDC bit 1 clear: objects off
    while (ppu.lineDot() < 400) { ppu.tick(); }
    const auto* row = &ppu.frame()[Ppu::kWidth];
    CHECK(row[0] == 3); // dot 111
    CHECK(row[1] == 3); // dot 112, before the write landed
    CHECK(row[2] == 3); // dot 113, chosen on dot 112: still the old bit
    CHECK(row[3] == 0); // dot 114, chosen on dot 113: the object is gone
}

TEST_CASE("the line's first pixel chooses its colour on the dot it is shaded") {
    // The dot of lag is the gap between one pixel's colour being chosen and the
    // previous one being shaded, so the line's first pixel has nothing to lag
    // behind. Two of the three Mealybug Tearoom references measure it: each puts
    // one object off the left edge of every line at an OAM X that walks the
    // stall's length band by band, and on the band where the stall ends exactly
    // on the dot a write lands, both photograph the line's first pixel with the
    // new bit rather than the old one.
    //
    // An object at OAM X = 2 is that band. Pan Docs' penalty makes it nine dots
    // - six for the fetch and three for the five background pixels to the right
    // of background x = -6, less two - so pixel 0 is drawn on dot 109, the first
    // dot a write landing after the M-cycle ending on dot 108 is seen on.
    const auto firstPixelOf = [](u8 objectX) {
        Ppu ppu;
        setUpTile(ppu, 0xFF, 0xFF); // every pixel colour 3
        static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so OAM lands
        ppu.oamWrite(0xFE00, 0x10); // Y = 16: on every line drawn here
        ppu.oamWrite(0xFE01, objectX);
        ppu.oamWrite(0xFE02, 0x02); // tile 2, all zero: transparent, so only the stall shows
        ppu.oamWrite(0xFE03, 0x00);
        enableLcd(ppu, 0x93);
        runTo(ppu, 1, 108);
        static_cast<void>(ppu.write(0xFF40, 0x92)); // LCDC bit 0 clear, objects still on
        while (ppu.lineDot() < 400) { ppu.tick(); }
        return std::array<u8, 3>{ppu.frame()[Ppu::kWidth], ppu.frame()[Ppu::kWidth + 1],
                                ppu.frame()[Ppu::kWidth + 2]};
    };
    // Nine dots of stall: pixel 0 is drawn on dot 109 and blanked, although the
    // dot before it - the stall's last - still had the bit set.
    CHECK(firstPixelOf(0x02) == std::array<u8, 3>{0, 0, 0});
    // Eight dots: pixel 0 is drawn on dot 108, before the write lands, and the
    // lag then carries the old bit one pixel further. Nothing about the first
    // pixel is special here, which is what makes the case above a measurement
    // rather than an assumption about where a stall leaves the stage.
    CHECK(firstPixelOf(0x03) == std::array<u8, 3>{3, 3, 0});
}

TEST_CASE("the last pixels of a line are drawn after mode 3 has ended") {
    // The other end of the same seven dots: mode 3 ends once the fetcher has
    // read everything the line needs, while the pixels still in the FIFO take
    // seven more dots to reach the LCD. A palette write in those dots still
    // colours them.
    Ppu ppu;
    setUpTile(ppu, 0x00, 0x00);
    runTo(ppu, 1, 256);
    REQUIRE(ppu.mode() == 0); // mode 3 is over: it ran dots 80 to 251
    static_cast<void>(ppu.write(0xFF47, 0xE7));
    while (ppu.lineDot() < 300) { ppu.tick(); }
    const auto* row = &ppu.frame()[Ppu::kWidth];
    CHECK(row[156] == 0); // drawn on dot 256
    CHECK(row[157] == 3); // drawn on dot 257, after mode 0 began
    CHECK(row[159] == 3);
}

TEST_CASE("SCX and SCY move the viewport") {
    Ppu ppu;
    static_cast<void>(ppu.write(0xFF40, 0x11));
    // Tile 0 all colour 0; tile 1 all colour 3.
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8010 + row), 0xFF);
        ppu.vramWrite(static_cast<u16>(0x8011 + row), 0xFF);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    ppu.vramWrite(0x9801, 0x01); // the second tile of the first row
    static_cast<void>(ppu.write(0xFF47, 0xE4));
    enableLcd(ppu, 0x91);
    runLine(ppu);
    CHECK(ppu.frame()[7] == 0);
    CHECK(ppu.frame()[8] == 3); // tile 1 starts at x = 8

    static_cast<void>(ppu.write(0xFF43, 0x08)); // SCX = 8: that tile moves to x = 0
    runLine(ppu); // this draws line 1, not line 0, so read row 1 below
    CHECK(ppu.frame()[Ppu::kWidth + 0] == 3);
    CHECK(ppu.frame()[Ppu::kWidth + 8] == 0);
}

TEST_CASE("clearing LCDC bit 0 blanks the background") {
    Ppu ppu;
    setUpTile(ppu, 0xFF, 0xFF); // every pixel colour 3
    runLine(ppu);
    CHECK(ppu.frame()[0] == 3);
    static_cast<void>(ppu.write(0xFF40, 0x90)); // background off
    runLine(ppu); // this draws line 1, not line 0, so read row 1 below
    CHECK(ppu.frame()[Ppu::kWidth] == 0);
}

TEST_CASE("LCDC bit 4 clear selects signed tile addressing from 0x9000") {
    Ppu ppu;
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    // Tile index 0x00's row 0 lives at 0x9000 + 0*16 = 0x9000: colour 1 throughout.
    ppu.vramWrite(0x9000, 0xFF);
    ppu.vramWrite(0x9001, 0x00);
    // Tile index 0xFF's row 0 lives at 0x9000 + (signed)0xFF*16 = 0x9000 - 16 =
    // 0x8FF0: colour 2 throughout. A broken (unsigned) cast would instead read
    // 0x9000 + 0xFF*16 = 0xA5F0, outside the tile-data area used here, so this
    // tile would come back as whatever colour 0 shades to instead of 2.
    ppu.vramWrite(0x8FF0, 0x00);
    ppu.vramWrite(0x8FF1, 0xFF);
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00); // every map entry -> tile 0x00
    }
    ppu.vramWrite(0x9801, 0xFF); // the second map entry -> tile 0xFF
    static_cast<void>(ppu.write(0xFF47, 0xE4));
    enableLcd(ppu, 0x81); // LCD on, BG on, LCDC bit 4 CLEAR: signed addressing, map at 0x9800
    runLine(ppu);
    const auto& frame = ppu.frame();
    for (int x = 0; x < 8; ++x) {
        CHECK(frame[static_cast<std::size_t>(x)] == 1); // tile 0x00, read from 0x9000
    }
    for (int x = 8; x < 16; ++x) {
        CHECK(frame[static_cast<std::size_t>(x)] == 2); // tile 0xFF, read from 0x8FF0
    }
}

TEST_CASE("LCDC bit 3 selects the background map at 0x9C00 instead of 0x9800") {
    Ppu ppu;
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    // Tile index 1: colour 1 throughout. Tile index 2: colour 2 throughout.
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + 16 + row), 0xFF);
        ppu.vramWrite(static_cast<u16>(0x8001 + 16 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8000 + 32 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8001 + 32 + row), 0xFF);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x01); // whole 0x9800 map -> tile 1
        ppu.vramWrite(static_cast<u16>(0x9C00 + i), 0x02); // whole 0x9C00 map -> tile 2
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4));
    enableLcd(ppu, 0x91); // LCDC bit 3 CLEAR: map at 0x9800
    runLine(ppu);
    CHECK(ppu.frame()[0] == 1);

    static_cast<void>(ppu.write(0xFF40, 0x99)); // LCDC bit 3 SET (0x91 | 0x08): map at 0x9C00
    runLine(ppu); // this draws line 1, not line 0, so read row 1 below
    CHECK(ppu.frame()[Ppu::kWidth + 0] == 2);
}

TEST_CASE("SCY's low bits pick the tile row drawn on a line (fine scroll)") {
    Ppu ppu;
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    // Tile 0: row 0 is solid colour 3; row 1 is colour 0 (empty).
    ppu.vramWrite(0x8000, 0xFF);
    ppu.vramWrite(0x8001, 0xFF); // row 0 -> colour 3
    ppu.vramWrite(0x8002, 0x00);
    ppu.vramWrite(0x8003, 0x00); // row 1 -> colour 0
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4));
    static_cast<void>(ppu.write(0xFF42, 0x01)); // SCY = 1
    enableLcd(ppu, 0x91);
    // Line 0 reads y = line(0) + scy(1) = 1, so row 1 (colour 0) is drawn, not
    // row 0 (colour 3).
    runLine(ppu);
    CHECK(ppu.frame()[0] == 0);
    CHECK(ppu.frame()[7] == 0);
}

TEST_CASE("SCY's high bits pick the tile-map row a line is fetched from (map scroll)") {
    Ppu ppu;
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    // Tile 0: colour 1 throughout. Tile 1: colour 2 throughout.
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0xFF);
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8010 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8011 + row), 0xFF);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00); // map row 0: tile 0 throughout
    }
    for (u16 i = 0; i < 0x20; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9820 + i), 0x01); // map row 1: tile 1 throughout
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4));
    static_cast<void>(ppu.write(0xFF42, 0x08)); // SCY = 8: y / 8 == 1 on line 0, so map row 1 is used
    enableLcd(ppu, 0x91);
    runLine(ppu);
    CHECK(ppu.frame()[0] == 2); // tile 1's colour, proving map row 1 was fetched, not row 0
}

namespace {
// A ruler for the dot each background fetch reads a bitplane on. Tile 0's row 1
// is colour 0 and its row 2 is colour 1, the whole map is tile 0, and a line is
// drawn with SCY = 0 (so every fetch reads row 1) until SCY = 1 is written into
// the middle of it (so every fetch from then on reads row 2). Tile by tile, the
// picture then says which bitplane read happened before the write and which
// after, and SCY is read at the same three steps the reference hardware tests
// change it on (Mealybug's notes: the tile-index step and both bitplane steps).
//
// Only the low bitplane differs between the two rows, because only the low
// bitplane's read dot is separable: the writes a test can place land at the end
// of an M-cycle, four dots apart, and the low read is the one step whose dot
// crosses one of those boundaries when the fetcher is five steps over eight dots
// instead of four over six.
void setUpScyRowRuler(Ppu& ppu) {
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x00);
    }
    ppu.vramWrite(0x8004, 0xFF); // row 2's low bitplane: colour 1 across the tile
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4));
    static_cast<void>(ppu.write(0xFF42, 0x00)); // SCY = 0: line 1 reads row 1
    enableLcd(ppu, 0x91);
}

// Runs line `line` with `value` written to `reg` at `dot`, and hands back the
// row that was drawn. The write lands at the end of the M-cycle that ends on
// `dot`, so the first dot that can see it is `dot` + 1.
const u8* lineWithWriteAt(Ppu& ppu, int line, int dot, u16 reg, u8 value) {
    runTo(ppu, line, dot);
    static_cast<void>(ppu.write(reg, value));
    while (ppu.lineNumber() == line) { ppu.tick(); }
    return &ppu.frame()[static_cast<std::size_t>(line) * Ppu::kWidth];
}
} // namespace

TEST_CASE("a background fetch reads its low bitplane four dots before the tile's first pixel") {
    // Pan Docs, "Pixel FIFO": the fetcher has five steps - Get tile, Get tile
    // data low, Get tile data high, Sleep, Push - the first four of two dots
    // each, and Get Tile Data High "also pushes a row of background/window
    // pixels to the FIFO". That extra push is the one that carries an
    // undisturbed line: the FIFO empties exactly as the step completes, so the
    // row goes in on the dot the high bitplane is read and its first pixel is
    // drawn on that same dot. The low bitplane is therefore read two dots
    // earlier, and the tile index two before that.
    //
    // The tile drawn at x = 8-15 has its first pixel on line dot 108, so its
    // low bitplane is read on dot 106. A four-step, six-dot fetcher reads it on
    // dot 104 instead - two dots earlier, and on the other side of the M-cycle
    // that ends on dot 104. Line 1, not line 0: line 0 draws four dots early.
    Ppu ppu;
    setUpScyRowRuler(ppu);
    const u8* row = lineWithWriteAt(ppu, 1, 104, 0xFF42, 0x01); // SCY = 1 from dot 105
    CHECK(row[0] == 0);  // read on dot 98, before the write
    CHECK(row[7] == 0);
    CHECK(row[8] == 1);  // read on dot 106, after it
    CHECK(row[15] == 1);
    CHECK(row[16] == 1); // read on dot 114
}

TEST_CASE("consecutive background fetches read their low bitplanes eight dots apart") {
    // The same ruler one M-cycle later. The tile at x = 16-23 reads its low
    // bitplane on dot 114, eight dots after the tile at x = 8-15 read its own:
    // one complete fetch is eight dots, not six. A six-dot fetcher reads it on
    // dot 112, before this write.
    Ppu ppu;
    setUpScyRowRuler(ppu);
    const u8* row = lineWithWriteAt(ppu, 1, 112, 0xFF42, 0x01); // SCY = 1 from dot 113
    CHECK(row[8] == 0);  // read on dot 106, before the write
    CHECK(row[15] == 0);
    CHECK(row[16] == 1); // read on dot 114, after it
    CHECK(row[23] == 1);
    CHECK(row[24] == 1); // read on dot 122
}

// ---------------------------------------------------------------------------
// Which dot of a two-dot fetch stage samples the registers it needs
//
// A fetch stage is two dots. The registers that build its VRAM address are
// sampled on the stage's first dot; the byte arrives at the end of the second.
// Mealybug Tearoom's PPU notes name the stages a register is read at - SCY at
// the tile-index stage `B` and both bitplane stages `0` and `1`, TILE_SEL at
// `0` and `1` - but not which of a stage's two dots, and the cases above
// cannot separate them: a write lands at the end of an M-cycle, and on a plain
// line every stage's two dots sit on the same side of every M-cycle boundary,
// so both readings give the same picture.
//
// A transparent object breaks that alignment. Its fetch stalls the background
// fetcher by a number of dots the OBJ penalty algorithm decides, and an odd
// number of them moves the rest of the line's stages across the M-cycle grid,
// so a write can then land between a stage's two dots. The object draws
// nothing - its tile is all colour 0 - so only the stall shows.
namespace {
// Parks a fully transparent object at OAM X `x` on every line, and enables
// objects. Its fetch costs 6 dots plus Pan Docs' tile term minus the
// first-object rebate: 7 dots at OAM X = 9 and 5 at OAM X = 11 (see
// PixelPipeline::objectPenalty), both odd, and both taken after the line's
// first pixels rather than before its warm-up.
void addStallingObject(Ppu& ppu, u8 x) {
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so OAM and VRAM land
    for (u16 row = 0; row < 16; ++row) {
        ppu.vramWrite(static_cast<u16>(0x8020 + row), 0x00); // tile 2: all colour 0
    }
    ppu.oamWrite(0xFE00, 0x10); // Y = 16: on every line drawn here
    ppu.oamWrite(0xFE01, x);
    ppu.oamWrite(0xFE02, 0x02); // tile 2, fully transparent
    ppu.oamWrite(0xFE03, 0x00);
    enableLcd(ppu, 0x93); // as 0x91, plus objects enabled
}

// The ruler of setUpScyRowRuler, with the difference between the two rows moved
// to whichever bitplane a case is about. Row 1 of tile 0 is colour 0; row 2 is
// colour 1 if the difference is in the low bitplane and colour 2 if it is in
// the high one. So a tile that took one bitplane from each row shows colour 3,
// which neither row can produce on its own.
void setUpScyPlaneRuler(Ppu& ppu, bool highPlane) {
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x00);
    }
    ppu.vramWrite(static_cast<u16>(highPlane ? 0x8005 : 0x8004), 0xFF); // row 2
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4));
    static_cast<void>(ppu.write(0xFF42, 0x00)); // SCY = 0: line 1 reads row 1
}
} // namespace

TEST_CASE("SCY reaches the low bitplane stage on the stage's first dot") {
    // Mealybug Tearoom's PPU notes: "the SCY register is read during the
    // background tile fetch B, 0 and 1 stages". This is stage 0, to the dot.
    //
    // The object at OAM X = 9 is fetched once screen pixel 1 is due, on line
    // dot 101, and stalls the fetcher for seven dots. The tile at x = 8-15
    // therefore has its first pixel on line dot 115 and its stages on dots
    // 110-111 (index), 112-113 (low) and 114-115 (high). A SCY written on dot
    // 112 is visible from dot 113, so the low bitplane is read before it and
    // the high bitplane after it: the tile keeps row 1's low bitplane, which
    // is colour 0. Sampling on a stage's last dot reads the low bitplane on
    // dot 113 instead and draws colour 1. Line 1, not line 0: line 0 draws
    // four dots early.
    Ppu ppu;
    setUpScyPlaneRuler(ppu, /*highPlane=*/false);
    addStallingObject(ppu, 9);
    const u8* row = lineWithWriteAt(ppu, 1, 112, 0xFF42, 0x01);
    CHECK(row[8] == 0);
    CHECK(row[15] == 0);
    CHECK(row[16] == 1); // the next tile reads both bitplanes after the write
}

TEST_CASE("SCY reaches the high bitplane stage on the stage's first dot") {
    // Stage 1 of the same sentence. The object at OAM X = 11 costs five dots
    // and is taken once screen pixel 3 is due, on line dot 103, so the tile at
    // x = 8-15 has its first pixel on dot 113 and its stages on dots 108-109,
    // 110-111 and 112-113. A SCY written on dot 112 is visible from dot 113:
    // every stage of this tile is over before it, so the tile is row 1
    // throughout, colour 0. Sampling on a stage's last dot reads the high
    // bitplane on dot 113 and mixes row 2's high bitplane into it - colour 2.
    Ppu ppu;
    setUpScyPlaneRuler(ppu, /*highPlane=*/true);
    addStallingObject(ppu, 11);
    const u8* row = lineWithWriteAt(ppu, 1, 112, 0xFF42, 0x01);
    CHECK(row[8] == 0);
    CHECK(row[15] == 0);
    CHECK(row[16] == 2); // the next tile reads both bitplanes after the write
}

TEST_CASE("SCY reaches the tile-index stage on the stage's first dot") {
    // Stage B of the same sentence, which is the one that picks the tile-map
    // row. Map row 0 is tile 0 and map row 1 is tile 1, and tile 1's row 1 is
    // colour 3, so SCY = 8 swaps the tile the whole line draws without moving
    // the row within it: only stage B can see the difference.
    //
    // The object at OAM X = 11 again, so the tile at x = 8-15 reads its index
    // on dot 108. A SCY written on dot 108 is visible from dot 109, so this
    // tile keeps map row 0 - tile 0, colour 0 - and the tile after it takes
    // tile 1. Sampling on a stage's last dot reads the index on dot 109 and
    // draws tile 1 here already.
    Ppu ppu;
    setUpScyPlaneRuler(ppu, /*highPlane=*/false);
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8010 + row), 0xFF); // tile 1: colour 3
        ppu.vramWrite(static_cast<u16>(0x8011 + row), 0xFF);
    }
    for (u16 i = 0; i < 32; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9820 + i), 0x01); // map row 1: tile 1
    }
    addStallingObject(ppu, 11);
    const u8* row = lineWithWriteAt(ppu, 1, 108, 0xFF42, 0x08);
    CHECK(row[8] == 0);
    CHECK(row[15] == 0);
    CHECK(row[16] == 3); // the next tile reads its index on dot 116
}

TEST_CASE("LCDC bit 4 written between a fetch's two bitplane stages mixes two tile patterns") {
    // Mealybug Tearoom's PPU notes: "TILE_SEL is read during the 0 and 1
    // stages of background tile data fetching. Changing its value during
    // background tile data fetch allows for mixing tile bitplane data from two
    // different tile patterns." This is that mixing, and it only happens if
    // each bitplane stage reads the bit on its own first dot.
    //
    // Tile 0 at 0x8000 - the area LCDC bit 4 set selects - has a low bitplane
    // of 0xFF and no high one; tile 0 at 0x9000, which a clear bit 4 selects,
    // has the high bitplane and no low one. Neither can draw colour 3 alone.
    //
    // The object at OAM X = 9 puts this tile's stages on dots 110-111,
    // 112-113 and 114-115. Clearing bit 4 on dot 112 is visible from dot 113,
    // so the low bitplane comes from 0x8000 and the high one from 0x9000:
    // colour 3. Sampling on a stage's last dot reads the low bitplane on dot
    // 113 as well, takes both from 0x9000, and draws colour 2.
    Ppu ppu;
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0xFF); // bit 4 set: low only
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x9000 + row), 0x00); // bit 4 clear: high only
        ppu.vramWrite(static_cast<u16>(0x9001 + row), 0xFF);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4));
    static_cast<void>(ppu.write(0xFF42, 0x00));
    addStallingObject(ppu, 9);
    const u8* row = lineWithWriteAt(ppu, 1, 112, 0xFF40, 0x83); // LCDC bit 4 clear
    CHECK(row[8] == 3);  // low from 0x8000, high from 0x9000
    CHECK(row[15] == 3);
    CHECK(row[16] == 2); // the next tile reads both bitplanes from 0x9000
}

namespace {
// Background tile 0 = colour 1 everywhere; window tile 1 = colour 3 everywhere;
// background map at 0x9800 (all tile 0), window map at 0x9C00 (all tile 1).
void setUpWindow(Ppu& ppu) {
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0xFF); // tile 0: colour 1
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8010 + row), 0xFF); // tile 1: colour 3
        ppu.vramWrite(static_cast<u16>(0x8011 + row), 0xFF);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
        ppu.vramWrite(static_cast<u16>(0x9C00 + i), 0x01);
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4));
    static_cast<void>(ppu.write(0xFF4A, 0x00)); // WY = 0
    static_cast<void>(ppu.write(0xFF4B, 0x27)); // WX = 39, so the window starts at x = 32
    enableLcd(ppu, 0xF1); // LCD on, BG on, window on, window map 0x9C00
}
} // namespace

TEST_CASE("a WX below 7 puts the window's first pixels off the left edge") {
    // WX = 7 lines the window's first pixel up with screen x = 0, so WX = 4
    // puts the first three off the left edge and screen x = 0 shows the
    // window's fourth pixel. Mealybug Tearoom's m3_wx_4_change sets WX = 4
    // before mode 3 and photographs the line: its DMG reference is the same
    // picture as WX = 7 would give, moved three pixels left, with three more
    // window pixels visible at the right-hand edge.
    Ppu ppu;
    setUpWindow(ppu);
    // Window tile (index 1) row 0: colour 1 in its leftmost pixel only.
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    ppu.vramWrite(0x8010, 0x80);
    ppu.vramWrite(0x8011, 0x00);
    static_cast<void>(ppu.write(0xFF4B, 0x04)); // WX = 4
    enableLcd(ppu, 0xF1);
    runLine(ppu);
    const auto& frame = ppu.frame();
    for (int x = 0; x < 5; ++x) {
        CHECK(frame[static_cast<std::size_t>(x)] == 0); // window pixels 3-7
    }
    CHECK(frame[5] == 1);  // the next window tile's first pixel
    CHECK(frame[6] == 0);
    CHECK(frame[13] == 1); // and the one after that
}

TEST_CASE("the window covers the background from WX-7 onwards") {
    Ppu ppu;
    setUpWindow(ppu);
    runLine(ppu);
    CHECK(ppu.frame()[31] == 1); // background
    CHECK(ppu.frame()[32] == 3); // window starts
    CHECK(ppu.frame()[159] == 3);
}

TEST_CASE("starting the window lengthens mode 3 by six dots") {
    Ppu ppu;
    setUpWindow(ppu);
    // 0xF1 with bit 5 (window enable) cleared: 0xF1 keeps bits 6 (window map),
    // 4 (tile data) and 0 (BG/window enable) set, so only bit 5 changes.
    static_cast<void>(ppu.write(0xFF40, 0xD1)); // window off for now

    const int plain = runLine(ppu);
    static_cast<void>(ppu.write(0xFF40, 0xF1)); // window on
    const int withWindow = runLine(ppu);
    CHECK(withWindow == plain + 8); // 6 dots, rounded up to whole M-cycles
}

TEST_CASE("a window trigger on the line's last pixel still lengthens mode 3 by six dots") {
    // WX = 166 puts the trigger point (WX - 7) at pixelX_ = 159, the very
    // last pixel of the line: stepDot's restart (queue cleared, fetcher back
    // to its Tile step) has to run to completion - Tile+DataLow+DataHigh, 2
    // dots each, then Push - before that pixel can be emitted, so it costs
    // the same six raw dots as the mid-line trigger above, not the one dot a
    // pixel-count-only estimate would assume is left. Mode 3's STAT-visible
    // length is derived from that hardware rule, not read off the code: 172
    // (plain) + 6 raw dots = 178, which the whole-M-cycle sampling below
    // rounds up to 180, exactly the "+8" the mid-line trigger above measures
    // - the six-dot cost is the same wherever the trigger lands.
    Ppu ppu;
    setUpWindow(ppu);
    static_cast<void>(ppu.write(0xFF40, 0xD1)); // window off for now
    const int plain = runLine(ppu);
    static_cast<void>(ppu.write(0xFF4B, 0xA6)); // WX = 166
    static_cast<void>(ppu.write(0xFF40, 0xF1)); // window on
    const int withWindow = runLine(ppu);
    CHECK(withWindow == plain + 8);
}

TEST_CASE("a window trigger inside the line's last dots still lengthens mode 3 by six dots") {
    // WX = 160 puts the trigger point (WX - 7) at pixelX_ = 153, which is
    // where a line with no window to come has exactly PixelPipeline::
    // kRenderLag dots left and mode 0 is decided. The restart happens one
    // dot later, so the six dots it costs have to keep being counted while
    // the restart is actually running: the queue is empty and the fetcher is
    // back at its first step, so no pixel moves for five more dots. A count
    // that charges the restart only while it is still to come reads five
    // dots short here and ends mode 3 five dots early - four, once the
    // M-cycle sampling below rounds it. The figures are picked so both
    // answers land on whole M-cycles: plain + 8 (correct) against plain + 4.
    Ppu ppu;
    setUpWindow(ppu);
    static_cast<void>(ppu.write(0xFF40, 0xD1)); // window off for now
    const int plain = runLine(ppu);
    static_cast<void>(ppu.write(0xFF4B, 0xA0)); // WX = 160
    static_cast<void>(ppu.write(0xFF40, 0xF1)); // window on
    const int withWindow = runLine(ppu);
    CHECK(withWindow == plain + 8);
}

TEST_CASE("the window does not draw above WY") {
    Ppu ppu;
    setUpWindow(ppu);
    // setUpWindow leaves WY = 0 and the PPU at the top of a line, and the Y
    // condition is latched at the beginning of each scanline (Pan Docs,
    // "Window rendering criteria"), so WY has to be changed with no line in
    // progress for the new value to govern the frame below. Landing the
    // write during a line's OAM scan instead would leave the condition
    // already latched from WY = 0, which is the behaviour, not the test.
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off
    static_cast<void>(ppu.write(0xFF4A, 0x02)); // WY = 2
    enableLcd(ppu, 0xF1);
    for (const std::uint64_t frame = ppu.frameCount(); ppu.frameCount() == frame;) {
        ppu.tick(); // to the end of the frame that is being drawn now
    }
    CHECK(ppu.frame()[static_cast<std::size_t>(1) * Ppu::kWidth + 40] == 1); // line 1: background
    CHECK(ppu.frame()[static_cast<std::size_t>(2) * Ppu::kWidth + 40] == 3); // line 2: window
}

TEST_CASE("consecutive lines draw consecutive window rows when the window is enabled all frame") {
    // Pins the row mapping (windowLine_ -> tile row), not the "only advances
    // on lines that drew it" rule: WY = 0 and LCDC bit 5 stay set for the
    // whole frame here, so the window is drawn on every line and this test
    // would still pass even if the counter advanced unconditionally. See
    // "the window's counter does not advance on lines LCDC disables it..."
    // below for a test that actually exercises that rule.
    Ppu ppu;
    setUpWindow(ppu);
    static_cast<void>(ppu.write(0xFF4A, 0x00));
    // Window tile row 1 is colour 0 so we can tell which window row was drawn.
    static_cast<void>(ppu.write(0xFF40, 0x11));
    ppu.vramWrite(0x8012, 0x00);
    ppu.vramWrite(0x8013, 0x00);
    enableLcd(ppu, 0xF1);
    for (const std::uint64_t frame = ppu.frameCount(); ppu.frameCount() == frame;) {
        ppu.tick(); // to the end of the frame that is being drawn now
    }
    CHECK(ppu.frame()[static_cast<std::size_t>(0) * Ppu::kWidth + 40] == 3); // window row 0
    CHECK(ppu.frame()[static_cast<std::size_t>(1) * Ppu::kWidth + 40] == 0); // window row 1
}

TEST_CASE("the window's counter does not advance on lines LCDC disables it, so a mid-frame enable starts at row 0") {
    // WY = 0 latches windowReached() from line 0 onward regardless of LCDC
    // bit 5 (that's the divergence recorded in docs/known-divergences.md),
    // but LCDC bit 5 itself stays clear for lines 0 and 1, so the window
    // must not actually draw, and its line counter must not advance, on
    // either of them. If advanceWindowLine() were called unconditionally
    // instead of only on lines the window actually drew, line 2 would show
    // window row 2, not row 0.
    Ppu ppu;
    setUpWindow(ppu);
    // Window tile (index 1) row 0 is colour 3 (set by setUpWindow's fill);
    // give every other row colour 2 so row 0 and row 2 are visibly
    // different: a tile row is two bytes, the first the low bit of each
    // pixel and the second the high bit, so colour = (high << 1) | low.
    // low = 0x00, high = 0xFF makes every pixel in the row colour 2.
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    for (u16 row = 2; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8010 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8011 + row), 0xFF);
    }
    // 0xD1 is 0xF1 with LCDC bit 5 (window enable) cleared: window disabled
    // for lines 0 and 1.
    enableLcd(ppu, 0xD1);
    runLine(ppu); // line 0: window disabled, background only
    runLine(ppu); // line 1: window disabled, background only
    static_cast<void>(ppu.write(0xFF40, 0xF1)); // window enabled from line 2 onward
    runLine(ppu); // line 2: window enabled, should draw its row 0

    CHECK(ppu.frame()[static_cast<std::size_t>(0) * Ppu::kWidth + 40] == 1); // line 0: background
    CHECK(ppu.frame()[static_cast<std::size_t>(1) * Ppu::kWidth + 40] == 1); // line 1: background
    // BGP = 0xE4 is the identity mapping (bits 1:0 shade colour 0, 3:2 shade
    // colour 1, and so on, here set up so shade == colour), so window row 0
    // (colour 3) reads back as shade 3, and the wrongly-advanced row 2
    // (colour 2) would read back as shade 2.
    CHECK(ppu.frame()[static_cast<std::size_t>(2) * Ppu::kWidth + 40] == 3); // line 2: window row 0
}

TEST_CASE("a whole frame is drawn line by line") {
    Ppu ppu;
    static_cast<void>(ppu.write(0xFF40, 0x11));
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0xFF);
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x00);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4));
    enableLcd(ppu, 0x91);
    for (const std::uint64_t frame = ppu.frameCount(); ppu.frameCount() == frame;) {
        ppu.tick(); // to the end of the frame that is being drawn now
    }
    for (int y = 0; y < Ppu::kHeight; ++y) {
        CHECK(ppu.frame()[static_cast<std::size_t>(y) * Ppu::kWidth] == 1);
    }
}


// ---------------------------------------------------------------------------
// Characterisation: the window's X counter
//
// These cases are a net, not a target. They record what the pipeline produces
// today for a spread of WX values and for the ways the window can be reached
// (or missed) on a line, so that introducing the hardware's scanline X counter
// - Pan Docs, "Window": a counter initialised to 0, incremented once per
// pixel rendered and seven extra times before the first pixel is rendered,
// compared for equality against WX - can be proved to change no picture and no
// mode-3 length. The expected strings below were captured from the code that
// preceded the counter; a difference in any of them is a behaviour change, not
// a better answer, and belongs in the task that means to make it.
//
// Each row is one line of the LCD as 160 shade digits, plus the dots STAT
// reported mode 3 lasting, sampled in whole M-cycles the way runLine does.
namespace {
// Background tile 0 is a flat colour 1. Window tile 1 carries 3,2,1,0,0,1,2,3
// across its even rows and 1,0,1,0,0,1,0,1 across its odd ones, so a digit
// says three things at once: that the window is drawing at that pixel, which
// pixel of its tile landed there, and whether the window row is even or odd.
// Both patterns have period 8 and neither is its own rotation by four, so a
// window that starts four pixels early or late is visible in the picture and
// not only in the dot count.
void setUpWindowRuler(Ppu& ppu, u8 lcdc, u8 scx, u8 wx, u8 wy) {
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0xFF); // tile 0: colour 1
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8010 + row), 0xA5);
        ppu.vramWrite(static_cast<u16>(0x8011 + row), (row % 4) == 0 ? 0xC3 : 0x00);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
        ppu.vramWrite(static_cast<u16>(0x9C00 + i), 0x01);
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4)); // BGP: shade == colour
    static_cast<void>(ppu.write(0xFF43, scx));
    static_cast<void>(ppu.write(0xFF4A, wy));
    static_cast<void>(ppu.write(0xFF4B, wx));
    enableLcd(ppu, lcdc);
}

std::string lineDigits(const Ppu& ppu, int line) {
    std::string out;
    for (int x = 0; x < Ppu::kWidth; ++x) {
        const std::size_t i = static_cast<std::size_t>(line) * Ppu::kWidth + static_cast<std::size_t>(x);
        out.push_back(static_cast<char>('0' + ppu.frame()[i]));
    }
    return out;
}

struct CharLine {
    int dots = 0;
    std::string pixels;
};

// Runs one whole line and returns both its picture and the dots mode 3 lasted.
// A non-negative `writeDot` lands `value` in `address` on the first tick at or
// past that line dot, which is how the cases below reach the window part-way
// through mode 3.
CharLine characterise(Ppu& ppu, int writeDot = -1, u16 address = 0, u8 value = 0) {
    while (ppu.mode() != 3) { ppu.tick(); }
    const int line = ppu.lineNumber();
    int drawing = 0;
    while (ppu.mode() == 3) {
        if (writeDot >= 0 && ppu.lineDot() >= writeDot) {
            static_cast<void>(ppu.write(address, value));
            writeDot = -1;
        }
        ppu.tick();
        drawing += 4;
    }
    while (ppu.lineNumber() == line) { ppu.tick(); } // let the last pixels land
    return CharLine{drawing, lineDigits(ppu, line)};
}
} // namespace

TEST_CASE("characterisation: the window's picture and mode-3 length for a spread of WX and SCX") {
    // WX = 7 is the value that lines the window's first pixel up with screen
    // x = 0; 0 to 6 push that many pixels off the left edge, 8 and 9 move it
    // right, 39 is an ordinary mid-line start, 166 is the last WX that reaches
    // the line at all and 167 and 255 never do. SCX 1, 5 and 7 are here
    // because the trigger waits for the SCX discard to drain before it can
    // fire, so the discard and the counter interact.
    struct Row { u8 scx; u8 wx; int dots; const char* pixels; };
    static const Row rows[] = {
        {0, 0, 180,
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"},
        {0, 1, 180,
         "23321001233210012332100123321001233210012332100123321001233210012332100123321001"
         "23321001233210012332100123321001233210012332100123321001233210012332100123321001"},
        {0, 2, 180,
         "12332100123321001233210012332100123321001233210012332100123321001233210012332100"
         "12332100123321001233210012332100123321001233210012332100123321001233210012332100"},
        {0, 3, 180,
         "01233210012332100123321001233210012332100123321001233210012332100123321001233210"
         "01233210012332100123321001233210012332100123321001233210012332100123321001233210"},
        {0, 4, 180,
         "00123321001233210012332100123321001233210012332100123321001233210012332100123321"
         "00123321001233210012332100123321001233210012332100123321001233210012332100123321"},
        {0, 5, 180,
         "10012332100123321001233210012332100123321001233210012332100123321001233210012332"
         "10012332100123321001233210012332100123321001233210012332100123321001233210012332"},
        {0, 6, 180,
         "21001233210012332100123321001233210012332100123321001233210012332100123321001233"
         "21001233210012332100123321001233210012332100123321001233210012332100123321001233"},
        {0, 7, 180,
         "32100123321001233210012332100123321001233210012332100123321001233210012332100123"
         "32100123321001233210012332100123321001233210012332100123321001233210012332100123"},
        {0, 8, 180,
         "13210012332100123321001233210012332100123321001233210012332100123321001233210012"
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"},
        {0, 9, 180,
         "11321001233210012332100123321001233210012332100123321001233210012332100123321001"
         "23321001233210012332100123321001233210012332100123321001233210012332100123321001"},
        {0, 39, 180,
         "11111111111111111111111111111111321001233210012332100123321001233210012332100123"
         "32100123321001233210012332100123321001233210012332100123321001233210012332100123"},
        {0, 160, 180,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111113210012"},
        {0, 165, 180,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111132"},
        {0, 166, 180,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111113"},
        {0, 167, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {0, 255, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {1, 0, 180,
         "32100123321001233210012332100123321001233210012332100123321001233210012332100123"
         "32100123321001233210012332100123321001233210012332100123321001233210012332100123"},
        {1, 6, 180,
         "10012332100123321001233210012332100123321001233210012332100123321001233210012332"
         "10012332100123321001233210012332100123321001233210012332100123321001233210012332"},
        {1, 7, 180,
         "21001233210012332100123321001233210012332100123321001233210012332100123321001233"
         "21001233210012332100123321001233210012332100123321001233210012332100123321001233"},
        {1, 8, 180,
         "13210012332100123321001233210012332100123321001233210012332100123321001233210012"
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"},
        {1, 166, 180,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111113"},
        {5, 0, 184,
         "01233210012332100123321001233210012332100123321001233210012332100123321001233210"
         "01233210012332100123321001233210012332100123321001233210012332100123321001233210"},
        {5, 6, 184,
         "23321001233210012332100123321001233210012332100123321001233210012332100123321001"
         "23321001233210012332100123321001233210012332100123321001233210012332100123321001"},
        {5, 7, 184,
         "12332100123321001233210012332100123321001233210012332100123321001233210012332100"
         "12332100123321001233210012332100123321001233210012332100123321001233210012332100"},
        {5, 8, 184,
         "13210012332100123321001233210012332100123321001233210012332100123321001233210012"
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"},
        {5, 166, 184,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111113"},
        {7, 0, 188,
         "23321001233210012332100123321001233210012332100123321001233210012332100123321001"
         "23321001233210012332100123321001233210012332100123321001233210012332100123321001"},
        {7, 6, 188,
         "32100123321001233210012332100123321001233210012332100123321001233210012332100123"
         "32100123321001233210012332100123321001233210012332100123321001233210012332100123"},
        {7, 7, 188,
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"},
        {7, 8, 188,
         "13210012332100123321001233210012332100123321001233210012332100123321001233210012"
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"},
        {7, 166, 188,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111113"},
    };
    for (const Row& row : rows) {
        Ppu ppu;
        setUpWindowRuler(ppu, 0xF1, row.scx, row.wx, 0x00);
        const CharLine got = characterise(ppu);
        CHECK_MESSAGE(got.dots == row.dots, "SCX ", row.scx, " WX ", row.wx);
        CHECK_MESSAGE(got.pixels == row.pixels, "SCX ", row.scx, " WX ", row.wx);
    }
}

TEST_CASE("characterisation: a line the window never reaches") {
    struct Row { u8 lcdc; u8 wy; int dots; const char* pixels; };
    static const Row rows[] = {
        // LCDC bit 5 clear all line, and a line above WY: both plain
        // background, both a 172-dot mode 3.
        {0xD1, 0x00, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {0xF1, 0x01, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
    };
    for (const Row& row : rows) {
        Ppu ppu;
        setUpWindowRuler(ppu, row.lcdc, 0x00, 0x27, row.wy);
        const CharLine got = characterise(ppu);
        CHECK(got.dots == row.dots);
        CHECK(got.pixels == row.pixels);
    }
}

TEST_CASE("characterisation: LCDC bit 5 set part-way through mode 3") {
    // The comparison against WX is an equality, so enabling the window matters
    // only if the counter still has that match left to make. Dot 120 is about
    // twenty pixels in (the counter reads about 27) and dot 200 about a hundred
    // (about 107): a WX below that is never matched and the line stays plain
    // background at its plain 172 dots, while a WX still ahead of the counter
    // is matched where it says. WX = 39 is the pair that shows both sides of
    // it, and WX = 120 the value neither write dot has passed.
    //
    // The rows that now draw nothing used to fire the trigger late, on the dot
    // LCDC was written, because the per-dot comparison was a greater-or-equal.
    // That was the stand-in for re-activation and is what this task replaced;
    // see docs/known-divergences.md, "The window can start more than once on a
    // scanline, and its row advances at each start".
    struct Row { u8 wx; int writeDot; int dots; const char* pixels; };
    static const Row rows[] = {
        {0, 120, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {0, 200, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {4, 120, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {4, 200, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {7, 120, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {7, 200, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {39, 120, 180,
         "11111111111111111111111111111111321001233210012332100123321001233210012332100123"
         "32100123321001233210012332100123321001233210012332100123321001233210012332100123"},
        {39, 200, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {120, 120, 180,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111132100123321001233210012332100123321001233210012"},
        {120, 200, 180,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111132100123321001233210012332100123321001233210012"},
    };
    for (const Row& row : rows) {
        Ppu ppu;
        setUpWindowRuler(ppu, 0xD1, 0x00, row.wx, 0x00);
        const CharLine got = characterise(ppu, row.writeDot, 0xFF40, 0xF1);
        CHECK_MESSAGE(got.dots == row.dots, "WX ", row.wx, " dot ", row.writeDot);
        CHECK_MESSAGE(got.pixels == row.pixels, "WX ", row.wx, " dot ", row.writeDot);
    }
}

TEST_CASE("characterisation: WX lowered part-way through mode 3") {
    // WX starts at 200, which never reaches the line, and is lowered at dot
    // 200, where the counter reads about 107. A new WX behind the counter is
    // never matched - the comparison is an equality - so 0, 8 and even 100
    // leave the line plain background; 150 is still ahead of it and is matched
    // where it says, at screen x = 143.
    struct Row { u8 wx; int dots; const char* pixels; };
    static const Row rows[] = {
        {0, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {8, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {100, 172,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"},
        {150, 180,
         "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
         "11111111111111111111111111111111111111111111111111111111111111132100123321001233"},
    };
    for (const Row& row : rows) {
        Ppu ppu;
        setUpWindowRuler(ppu, 0xF1, 0x00, 200, 0x00);
        const CharLine got = characterise(ppu, 200, 0xFF4B, row.wx);
        CHECK_MESSAGE(got.dots == row.dots, "WX ", row.wx);
        CHECK_MESSAGE(got.pixels == row.pixels, "WX ", row.wx);
    }
}

TEST_CASE("characterisation: four consecutive lines of window, and the row each draws") {
    // The window's own line counter advances once per line that drew it, so
    // these four lines must alternate between the window tile's even and odd
    // rows. m2_win_en_toggle is the ROM that pins this rule; the odd-row
    // pattern in setUpWindowRuler is what makes a wrongly-advanced counter
    // visible here.
    struct Row { u8 wx; int line; int dots; const char* pixels; };
    static const Row rows[] = {
        {0, 0, 180,
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"},
        {0, 1, 180,
         "11010010110100101101001011010010110100101101001011010010110100101101001011010010"
         "11010010110100101101001011010010110100101101001011010010110100101101001011010010"},
        {0, 2, 180,
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"
         "33210012332100123321001233210012332100123321001233210012332100123321001233210012"},
        {0, 3, 180,
         "11010010110100101101001011010010110100101101001011010010110100101101001011010010"
         "11010010110100101101001011010010110100101101001011010010110100101101001011010010"},
        {4, 0, 180,
         "00123321001233210012332100123321001233210012332100123321001233210012332100123321"
         "00123321001233210012332100123321001233210012332100123321001233210012332100123321"},
        {4, 1, 180,
         "00101101001011010010110100101101001011010010110100101101001011010010110100101101"
         "00101101001011010010110100101101001011010010110100101101001011010010110100101101"},
        {4, 2, 180,
         "00123321001233210012332100123321001233210012332100123321001233210012332100123321"
         "00123321001233210012332100123321001233210012332100123321001233210012332100123321"},
        {4, 3, 180,
         "00101101001011010010110100101101001011010010110100101101001011010010110100101101"
         "00101101001011010010110100101101001011010010110100101101001011010010110100101101"},
        {39, 0, 180,
         "11111111111111111111111111111111321001233210012332100123321001233210012332100123"
         "32100123321001233210012332100123321001233210012332100123321001233210012332100123"},
        {39, 1, 180,
         "11111111111111111111111111111111101001011010010110100101101001011010010110100101"
         "10100101101001011010010110100101101001011010010110100101101001011010010110100101"},
        {39, 2, 180,
         "11111111111111111111111111111111321001233210012332100123321001233210012332100123"
         "32100123321001233210012332100123321001233210012332100123321001233210012332100123"},
        {39, 3, 180,
         "11111111111111111111111111111111101001011010010110100101101001011010010110100101"
         "10100101101001011010010110100101101001011010010110100101101001011010010110100101"},
    };
    u8 currentWx = 0xFF;
    Ppu ppu;
    for (const Row& row : rows) {
        if (row.wx != currentWx) {
            currentWx = row.wx;
            ppu = Ppu{};
            setUpWindowRuler(ppu, 0xF1, 0x00, row.wx, 0x00);
        }
        const CharLine got = characterise(ppu);
        CHECK_MESSAGE(got.dots == row.dots, "WX ", row.wx, " line ", row.line);
        CHECK_MESSAGE(got.pixels == row.pixels, "WX ", row.wx, " line ", row.line);
    }
}

// ---------------------------------------------------------------------------
// Clearing LCDC bit 5 part-way along a line
//
// Mealybug Tearoom's own PPU notes, quoted in full in docs/known-divergences.md
// ("The window's scanline X counter, and the evidence for it, quoted"), under
// LCDC bit 5:
//
//   "WIN_EN can be disabled during mode 3. The disabling will take effect at
//   the end of the current window tile being drawn. When the current window
//   tile has finished being drawn, the PPU will start drawing background tiles
//   again."
//   "When the background resumes drawing it is on a tile boundary. The low 3
//   bits of SCX have no effect."
//
// The cases below need to tell background pixels from window pixels and to see
// where the resumed background's tile boundaries fall, so they use a ruler of
// their own rather than setUpWindowRuler's: the window tile is a flat colour 3
// and the background tile carries colour 2 in its leftmost pixel and colour 1
// in the other seven, so every background tile boundary is a visible 2 and
// every window pixel is a 3.
namespace {
void setUpWindowOffRuler(Ppu& ppu, u8 scx, u8 wx) {
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0x7F); // tile 0: 2,1,1,1,1,1,1,1
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x80);
        ppu.vramWrite(static_cast<u16>(0x8010 + row), 0xFF); // tile 1: flat colour 3
        ppu.vramWrite(static_cast<u16>(0x8011 + row), 0xFF);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
        ppu.vramWrite(static_cast<u16>(0x9C00 + i), 0x01);
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4)); // BGP: shade == colour
    static_cast<void>(ppu.write(0xFF43, scx));
    static_cast<void>(ppu.write(0xFF4A, 0x00)); // WY = 0: every line is below it
    static_cast<void>(ppu.write(0xFF4B, wx));
    enableLcd(ppu, 0xF1); // LCD on, BG on, window on, window map 0x9C00
}

// The screen x at which the line'"'"'s run of window 3s gives way to background
// again, or -1 if the window never drew or never stopped.
int backgroundResumesAt(const std::string& pixels) {
    const std::size_t first = pixels.find('3');
    if (first == std::string::npos) { return -1; }
    const std::size_t after = pixels.find_first_not_of('3', first);
    return after == std::string::npos ? -1 : static_cast<int>(after);
}
} // namespace

TEST_CASE("clearing LCDC bit 5 part-way along a line stops the window at the end of its tile") {
    // WX = 39 puts the window's first pixel on screen x = 32, a tile boundary
    // of its own, so every window tile spans x = 32 + 8k .. 32 + 8k + 7 and the
    // dot the write lands on picks which of them is the last one drawn.
    Ppu ppu;
    setUpWindowOffRuler(ppu, 0x00, 0x27);
    // 0xD1 is 0xF1 with bit 5 (window enable) cleared and nothing else.
    const CharLine got = characterise(ppu, 200, 0xFF40, 0xD1);
    CHECK(got.pixels.substr(0, 32) == "21111111211111112111111121111111");
    const int resume = backgroundResumesAt(got.pixels);
    REQUIRE(resume > 32); // the window drew at least one tile, then stopped
    CHECK((resume - 32) % 8 == 0); // ...at the end of one of its tiles
    // From there on it is background again, and the resumed background starts
    // a tile of its own at that pixel.
    std::string expected;
    for (int x = resume; x < Ppu::kWidth; ++x) {
        expected.push_back(((x - resume) % 8) == 0 ? '2' : '1');
    }
    CHECK(got.pixels.substr(static_cast<std::size_t>(resume)) == expected);
}

TEST_CASE("the background that resumes when LCDC bit 5 is cleared ignores SCX's low 3 bits") {
    // Mealybug: "When the background resumes drawing it is on a tile boundary.
    // The low 3 bits of SCX have no effect." SCX = 5 shifts the background
    // drawn before the window by five pixels - its tile boundaries land on
    // x = 3, 11, 19 ... - and the background that resumes after the window
    // must ignore that shift and start a tile where the window stopped.
    Ppu ppu;
    setUpWindowOffRuler(ppu, 0x05, 0x27);
    const CharLine got = characterise(ppu, 200, 0xFF40, 0xD1);
    CHECK(got.pixels.substr(0, 32) == "11121111111211111112111111121111");
    const int resume = backgroundResumesAt(got.pixels);
    REQUIRE(resume > 32);
    CHECK((resume - 32) % 8 == 0);
    CHECK(got.pixels[static_cast<std::size_t>(resume)] == '2');
    CHECK(got.pixels[static_cast<std::size_t>(resume) + 1] == '1');
}

TEST_CASE("setting LCDC bit 5 again after a mid-line stop does not bring the window back") {
    // Mealybug: "Setting WIN_EN again during mode 3 on the same scanline will
    // have no effect unless WX has been updated to set the window to activate
    // on a pixel that hasn't been drawn yet." WX is left where it is here, so
    // the second write must change nothing and the background must run to the
    // end of the line. (Re-activation with a moved WX, and the window row
    // advance that comes with it, is a later task; until then the window
    // activates once per line at most.)
    Ppu ppu;
    setUpWindowOffRuler(ppu, 0x00, 0x27);
    // Clear bit 5 early in the window, set it again twenty-odd pixels later.
    while (ppu.mode() != 3) { ppu.tick(); }
    const int line = ppu.lineNumber();
    bool cleared = false;
    bool set = false;
    while (ppu.mode() == 3) {
        if (!cleared && ppu.lineDot() >= 150) {
            static_cast<void>(ppu.write(0xFF40, 0xD1)); // window off
            cleared = true;
        } else if (cleared && !set && ppu.lineDot() >= 200) {
            static_cast<void>(ppu.write(0xFF40, 0xF1)); // window on again
            set = true;
        }
        ppu.tick();
    }
    while (ppu.lineNumber() == line) { ppu.tick(); }
    const std::string pixels = lineDigits(ppu, line);
    REQUIRE(set);
    const int resume = backgroundResumesAt(pixels);
    REQUIRE(resume > 32);
    CHECK(pixels.find('3', static_cast<std::size_t>(resume)) == std::string::npos);
}

TEST_CASE("a window stopped before it pushes a tile does not clip the background instead") {
    // WX = 4 is matched during the counter's free increments, so the window's
    // three leftmost pixels are owed to the clip at its first push (see
    // PixelPipeline::startWindow and "A WX below 7 pushes the window's leftmost
    // pixels off the screen" in docs/known-divergences.md). Clearing LCDC bit 5
    // anywhere in the dots before that push must leave the clip unspent: those
    // three pixels are the window's, and taking them out of the background tile
    // pushed in its place would shift the whole rest of the line left by three.
    //
    // WX = 4 puts the window's first visible pixel on screen x = 0 and leaves
    // five pixels of its first tile on screen, so a window that drew n tiles
    // hands the line back at x = 5 + 8(n - 1), and one that drew none hands it
    // back at x = 0. Either way the background that follows starts a tile of
    // its own right there. The sweep covers the dots either side of the first
    // push without depending on which of them it is.
    for (int writeDot = 88; writeDot <= 112; writeDot += 4) {
        Ppu ppu;
        setUpWindowOffRuler(ppu, 0x00, 0x04);
        const CharLine got = characterise(ppu, writeDot, 0xFF40, 0xD1);
        const std::size_t first = got.pixels.find_first_not_of('3');
        REQUIRE_MESSAGE(first != std::string::npos, "dot ", writeDot);
        const int resume = static_cast<int>(first);
        const bool onATileBoundary = resume == 0 || (resume - 5) % 8 == 0;
        CHECK_MESSAGE(onATileBoundary, "dot ", writeDot, " resume ", resume);
        std::string expected;
        for (int x = resume; x < Ppu::kWidth; ++x) {
            expected.push_back(((x - resume) % 8) == 0 ? '2' : '1');
        }
        CHECK_MESSAGE(got.pixels.substr(first) == expected, "dot ", writeDot);
    }
}

// The ruler above with a window tile whose rows differ, so that a fetch which
// read its low bitplane while the window was on and its high one after bit 5
// went low - taking one from the window's row and one from the background's -
// draws a colour neither a whole window tile nor a background tile can. The
// window's row counter is at 1 on line 1, having advanced once on line 0, and
// SCY = 2 puts the background's row at 3; tile 1 is colour 3 on every row but
// row 3, where it has a low bitplane only and draws colour 1.
namespace {
void setUpWindowRowRuler(Ppu& ppu, u8 wx) {
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0x7F); // tile 0: 2,1,1,1,1,1,1,1
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x80);
        ppu.vramWrite(static_cast<u16>(0x8010 + row), 0xFF); // tile 1: colour 3 ...
        ppu.vramWrite(static_cast<u16>(0x8011 + row), 0xFF);
    }
    ppu.vramWrite(0x8017, 0x00); // ... except on row 3, where it is colour 1
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
        ppu.vramWrite(static_cast<u16>(0x9C00 + i), 0x01);
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4)); // BGP: shade == colour
    static_cast<void>(ppu.write(0xFF43, 0x00)); // SCX = 0
    static_cast<void>(ppu.write(0xFF42, 0x02)); // SCY = 2: line 1 reads row 3
    static_cast<void>(ppu.write(0xFF4A, 0x00)); // WY = 0: every line is below it
    static_cast<void>(ppu.write(0xFF4B, wx));
    enableLcd(ppu, 0xF1); // LCD on, BG on, window on, window map 0x9C00
}
} // namespace

// WX = 6 is matched by the counter on line dot 99 (93 + WX), the activation
// spends that dot resetting the fetcher, and the window's fetches follow: the
// first has its stages on dots 100-101, 102-103 and 104-105 and pushes as the
// last of them completes, on dot 105. From there each fetch is eight dots, so
// the window's fetches push on dots 105, 113, 121 ... and, with the one pixel a
// WX of 6 owes the discard, they draw screen x = 0-6, 7-14, 15-22 and 23-30.
// A write lands at the end of an M-cycle, so the dots it can first be seen on
// are 101, 105, 109, 113 ...: those are what the cases below aim at.

TEST_CASE("a cleared LCDC bit 5 does not split the fetch it lands in") {
    // Mealybug: "The disabling will take effect at the end of the current
    // window tile being drawn." The write lands on dot 116, so it is visible
    // from 117 - after the third window fetch has read its tile index on dot 116
    // and before it reads either bitplane, on dots 118 and 120. Reading bit 5 at
    // each stage instead takes the tile index from the window's map and both
    // bitplanes from the background's row, which draws colour 1 across
    // x = 15-22; the tile has to come out whole, colour 3, with the background
    // resuming at x = 23.
    //
    // This is the shape `m3_lcdc_win_en_change_multiple` photographs: its writes
    // land inside a window fetch, between two of its stages.
    Ppu ppu;
    setUpWindowRowRuler(ppu, /*wx=*/0x06);
    const u8* row = lineWithWriteAt(ppu, 1, 116, 0xFF40, 0xD1); // bit 5 clear from 117
    CHECK(row[14] == 3); // the second window tile, read before the write
    CHECK(row[15] == 3); // the third: begun before the write and finished after it
    CHECK(row[22] == 3);
    CHECK(row[23] == 2); // background again, on a tile boundary
    CHECK(row[24] == 1);
}

TEST_CASE("the fetcher samples LCDC bit 5 on a fetch's last dot") {
    // The write lands on dot 112, so it is visible from 113 - the dot the second
    // window fetch completes on. That fetch keeps its own window tile; the
    // sample on that dot is what makes the third fetch a background one, so the
    // window hands the line back at x = 15 rather than at 23.
    Ppu ppu;
    setUpWindowRowRuler(ppu, /*wx=*/0x06);
    const u8* row = lineWithWriteAt(ppu, 1, 112, 0xFF40, 0xD1); // bit 5 clear from 113
    CHECK(row[7] == 3); // the second window tile, whole
    CHECK(row[14] == 3);
    CHECK(row[15] == 2); // background from here, starting a tile of its own
    CHECK(row[16] == 1);
    CHECK(row[23] == 2);
}

TEST_CASE("a window fetch that has only read its tile index still draws a window tile") {
    // The other end of the same rule, and the case `…_multiple_wx` photographs:
    // a WX of 7 is matched on dot 100 and the window's first fetch reads its
    // tile index on dot 101, the very dot this write is first visible on. The
    // fetch is already under way, so it finishes as a window fetch and its eight
    // pixels are the window's - exactly one window tile, because the sample on
    // that fetch's last dot, 106, already sees bit 5 clear and sends the next
    // fetch to the background map. A fetcher that read bit 5 at its tile-index
    // stage instead would draw no window pixel at all here.
    Ppu ppu;
    setUpWindowRowRuler(ppu, /*wx=*/0x07);
    const u8* row = lineWithWriteAt(ppu, 1, 100, 0xFF40, 0xD1); // bit 5 clear from 101
    CHECK(row[0] == 3); // the window's first and only tile, drawn whole
    CHECK(row[7] == 3);
    CHECK(row[8] == 2); // background again, on a tile boundary
    CHECK(row[9] == 1);
    CHECK(row[16] == 2);
}

// ---------------------------------------------------------------------------
// Re-activation, and the window row counter
//
// Pan Docs, "Window behavior", quoted in full in docs/known-divergences.md
// ("The window's scanline X counter, and the evidence for it, quoted"):
//
//   "When this counter is equal to WX, if the Y condition is true and the
//   Window enable bit is set in LCDC, background rendering is reset, beginning
//   anew from the active row of the Window's tilemap. The coordinate of the
//   active Window row is then incremented."
//   "This process can happen more than once per scanline, making the Window's
//   "tilemap Y coordinate" increase more than once in the scanline. ... However,
//   this requires "disabling" the Window by briefly clearing its enable bit
//   from LCDC first."
//
// and Mealybug Tearoom's PPU notes, from the same section of that file:
//
//   "Setting WIN_EN again during mode 3 on the same scanline will have no
//   effect unless WX has been updated to set the window to activate on a pixel
//   that hasn't been drawn yet."
//   "If WX has been updated correctly and WIN_EN is set again then the PPU
//   stops drawing the background, and will activate the window again, but it
//   will start drawing the next row of the window, on the same scanline."
//
// The two sentences are one rule: the counter is compared for *equality*
// against WX on every dot, and every match that finds the window not already
// drawing activates it and advances the window's row. A bare re-enable does
// nothing because the counter is monotonic and has already gone past an
// unchanged WX; a re-enable with WX moved ahead of the counter is matched
// again, and the row it draws is the next one.
namespace {
struct TimedWrite { int dot; u16 address; u8 value; };

// Runs one whole line, landing each write on the first tick at or past its
// line dot, and returns the line's picture and the dots mode 3 lasted. The
// dots must be in increasing order and at least an M-cycle apart, as they
// would be for a handler writing the registers one instruction at a time.
CharLine characteriseWrites(Ppu& ppu, const std::vector<TimedWrite>& writes) {
    while (ppu.mode() != 3) { ppu.tick(); }
    const int line = ppu.lineNumber();
    std::size_t next = 0;
    int drawing = 0;
    while (ppu.mode() == 3) {
        while (next < writes.size() && ppu.lineDot() >= writes[next].dot) {
            static_cast<void>(ppu.write(writes[next].address, writes[next].value));
            ++next;
        }
        ppu.tick();
        drawing += 4;
    }
    while (ppu.lineNumber() == line) { ppu.tick(); }
    return CharLine{drawing, lineDigits(ppu, line)};
}

// setUpWindowRuler's window tile draws 3,2,1,0,0,1,2,3 across its even rows
// and 1,0,1,0,0,1,0,1 across its odd ones, so the eight pixels a window band
// opens with say which row of the window the band drew.
constexpr const char* kEvenWindowRow = "32100123";
constexpr const char* kOddWindowRow = "10100101";
} // namespace

TEST_CASE("the window activates twice on one line when WX is moved ahead of the counter, and the second band draws the next row") {
    // WX = 39 starts the window at screen x = 32 with its row 0 (even). LCDC
    // bit 5 is then cleared, WX moved to 120 - a pixel the counter has not
    // reached - and bit 5 set again, which is exactly the sequence Mealybug's
    // notes describe: the window activates a second time at screen x = 113 and
    // draws its *next* row (row 1, odd).
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0x27, 0x00);
    const CharLine got = characteriseWrites(ppu, {{160, 0xFF40, 0xD1},
                                                  {164, 0xFF4B, 0x78},
                                                  {168, 0xFF40, 0xF1}});
    CHECK(got.pixels.substr(0, 32) == std::string(32, '1'));
    CHECK(got.pixels.substr(32, 8) == kEvenWindowRow);
    CHECK(got.pixels.substr(113, 8) == kOddWindowRow);
    // Between the two bands the background has the line back. The background
    // tile is a flat colour 1 here and the window's row is not, so the run of
    // 1s that ends where the second band begins is the gap; it has to be
    // non-empty, and it has to start where one of the first band's tiles ended.
    int resume = 113;
    while (resume > 0 && got.pixels[static_cast<std::size_t>(resume) - 1] == '1') { --resume; }
    CHECK(resume > 32);
    CHECK(resume < 113);
    CHECK((resume - 32) % 8 == 0);
    // Two activations, two fetcher restarts: 172 + 6 + 6 = 184 dots, against
    // the 180 a single activation is sampled as.
    CHECK(got.dots == 184);
}

TEST_CASE("two activations on a line advance the window's row twice, so the next line starts two rows on") {
    // The row counter advances per activation, not per line: after a line with
    // two activations the next line's window must draw an *even* row again
    // (row 2), not the odd row 1 a once-per-line advance would leave it on.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0x27, 0x00);
    const CharLine first = characteriseWrites(ppu, {{160, 0xFF40, 0xD1},
                                                    {164, 0xFF4B, 0x78},
                                                    {168, 0xFF40, 0xF1}});
    REQUIRE(first.pixels.substr(32, 8) == kEvenWindowRow); // row 0
    REQUIRE(first.pixels.substr(113, 8) == kOddWindowRow); // row 1
    // Put WX back where it was and draw an ordinary line.
    static_cast<void>(ppu.write(0xFF4B, 0x27));
    const CharLine second = characteriseWrites(ppu, {});
    CHECK(second.pixels.substr(32, 8) == kEvenWindowRow); // row 2, not row 1
    CHECK(second.dots == 180);
}

TEST_CASE("a line the window is enabled on but never reaches does not advance its row") {
    // LCDC bit 5 is set and the Y condition holds, but WX = 200 is past every
    // value the counter takes, so there is no activation and no row advance:
    // the line after it draws window row 0, not row 1.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0xC8, 0x00);
    const CharLine missed = characteriseWrites(ppu, {});
    CHECK(missed.pixels == std::string(160, '1'));
    CHECK(missed.dots == 172);
    static_cast<void>(ppu.write(0xFF4B, 0x27));
    const CharLine drawn = characteriseWrites(ppu, {});
    CHECK(drawn.pixels.substr(32, 8) == kEvenWindowRow); // row 0
}

TEST_CASE("LCDC bit 5 set after the counter has gone past WX does not start the window") {
    // The comparison is an equality, so a window enabled once the counter is
    // already past WX is simply never matched on that line - and, having never
    // activated, it does not advance its row either. Mealybug's notes put it
    // as "no effect unless WX has been updated to set the window to activate
    // on a pixel that hasn't been drawn yet"; here WX is not updated at all.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xD1, 0x00, 0x27, 0x00); // window off to start with
    const CharLine late = characteriseWrites(ppu, {{240, 0xFF40, 0xF1}});
    CHECK(late.pixels == std::string(160, '1'));
    CHECK(late.dots == 172);
    const CharLine next = characteriseWrites(ppu, {});
    CHECK(next.pixels.substr(32, 8) == kEvenWindowRow); // still row 0
}

// ---------------------------------------------------------------------------
// WX moved ahead of the counter while the window is already drawing
//
// Pan Docs, "Pixel FIFO":
//
//   "When the value of WX changes after the window has started rendering and
//   the new value of WX is reached again, a pixel with color value of 0 and the
//   lowest priority is pushed onto the background FIFO."
//
// Two things that sentence leaves to be measured, and that the DMG references
// for the three WX-change ROMs settle (see docs/known-divergences.md, "A WX
// changed while the window is drawing pushes one colour-0 pixel, and only onto
// an empty FIFO"):
//
//   - it is a *push*, so the pixel is an extra one: the rest of the line moves
//     one pixel right and its last pixel falls off the edge. It costs no dots,
//     because the dot it is emitted on is the dot the fetcher's own push was
//     going to use.
//   - "pushed onto the background FIFO" means the FIFO's one push port, which
//     takes a push only when the FIFO is empty - exactly as the fetcher's push
//     does. A match that lands part-way through a tile is swallowed.
//
// The window row does *not* advance: this is not an activation.
TEST_CASE("a WX moved ahead of the counter while the window draws pushes one colour-0 pixel") {
    // WX = 39 starts the window at screen x = 32, so its tiles begin at
    // x = 32, 40, 48 ... and the background FIFO is empty at the top of each of
    // those dots. WX is then moved to 87, which the counter reaches at
    // x = 87 - 7 = 80 - a tile boundary - so the push lands there.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0x27, 0x00);
    const CharLine got = characteriseWrites(ppu, {{160, 0xFF4B, 0x57}});
    CHECK(got.pixels.substr(0, 32) == std::string(32, '1'));      // background
    CHECK(got.pixels.substr(32, 8) == kEvenWindowRow);            // window row 0
    CHECK(got.pixels.substr(72, 8) == kEvenWindowRow);            // still in step at x = 72
    CHECK(got.pixels[80] == '0');                                 // the colour-0 pixel
    CHECK(got.pixels.substr(81, 8) == kEvenWindowRow);            // and the line shifts right
    CHECK(got.pixels.substr(153, 7) == std::string(kEvenWindowRow).substr(0, 7));
    // One activation, one fetcher restart: 172 + 6 = 178 dots, sampled as 180.
    // The pushed pixel costs nothing.
    CHECK(got.dots == 180);
}

TEST_CASE("the pushed pixel is a background colour 0, so BGP decides its shade") {
    // Mealybug Tearoom's m3_wx_4_change_sprites runs the same sequence under a
    // reversed BGP, where background colour 0 shades to 3, and its DMG
    // reference shows the pushed pixel as shade 3 on an otherwise flat band.
    // So the pixel goes through the background palette like any other; it is
    // not a shade written straight into the line.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0x27, 0x00);
    static_cast<void>(ppu.write(0xFF47, 0x1B)); // BGP: colour 0 -> 3, 1 -> 2, 2 -> 1, 3 -> 0
    const CharLine got = characteriseWrites(ppu, {{160, 0xFF4B, 0x57}});
    CHECK(got.pixels.substr(0, 32) == std::string(32, '2'));  // background colour 1
    CHECK(got.pixels.substr(72, 8) == "01233210");            // the window row, repalettised
    CHECK(got.pixels[80] == '3');                             // colour 0 under this palette
    CHECK(got.pixels.substr(81, 8) == "01233210");
}

namespace {
// The picture an ordinary WX = 39 line draws: background to x = 31, then the
// given window row tiled to the right-hand edge. Spelled out rather than taken
// from a neighbouring line, because consecutive lines draw alternating window
// rows and a "nothing happened" case has to be compared against its own row.
std::string plainWindowLine(const char* windowRow) {
    std::string out(32, '1');
    while (out.size() < 160) { out += windowRow; }
    return out;
}
} // namespace

TEST_CASE("a WX match part-way through a window tile pushes nothing") {
    // WX = 84 is reached at x = 77, five pixels into the tile that started at
    // x = 72: the FIFO still holds three of that tile's pixels, so the push
    // finds no room and the line is the one an unchanged WX would have drawn.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0x27, 0x00);
    const CharLine got = characteriseWrites(ppu, {{160, 0xFF4B, 0x54}});
    CHECK(got.pixels == plainWindowLine(kEvenWindowRow));
    CHECK(got.dots == 180);
}

TEST_CASE("a WX lowered behind the counter while the window draws pushes nothing") {
    // The comparison is an equality against a counter that only counts up, so
    // a WX moved to a pixel already drawn is never reached again.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0x27, 0x00);
    const CharLine got = characteriseWrites(ppu, {{160, 0xFF4B, 0x28}}); // WX = 40, reached at x = 33
    CHECK(got.pixels == plainWindowLine(kEvenWindowRow));
    CHECK(got.dots == 180);
}

TEST_CASE("the pushed pixel is not an activation, so the window's row does not advance") {
    // Pan Docs advances "the coordinate of the active Window row" when a match
    // *resets background rendering*. A match that only pushes a pixel is not
    // that, so the line after one with a push must draw the window's next row
    // once, not twice: the odd row, not the even one two activations would give.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0x27, 0x00);
    const CharLine first = characteriseWrites(ppu, {{160, 0xFF4B, 0x57}});
    REQUIRE(first.pixels.substr(32, 8) == kEvenWindowRow); // row 0
    REQUIRE(first.pixels[80] == '0');                      // the push happened
    static_cast<void>(ppu.write(0xFF4B, 0x27));            // WX back where it was
    const CharLine second = characteriseWrites(ppu, {});
    CHECK(second.pixels.substr(32, 8) == kOddWindowRow);   // row 1, not row 2
    CHECK(second.dots == 180);
}

TEST_CASE("an activation does not push a colour-0 pixel of its own on the dots that follow it") {
    // The match that starts the window and the match that pushes a pixel are
    // the same comparison, and the counter does not move while the restarted
    // fetcher spends its six dots - so a match has to be acted on once, not on
    // every dot the counter sits on it. WX = 7 is the sharpest case: it is
    // matched by the last of the counter's free increments, before any pixel is
    // rendered, and the counter then stays at 7 until pixel 0 is emitted.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0x07, 0x00); // WX = 7: window from x = 0
    const CharLine got = characteriseWrites(ppu, {});
    CHECK(got.pixels.substr(0, 8) == kEvenWindowRow);
    CHECK(got.pixels.substr(152, 8) == kEvenWindowRow);
}

// ---------------------------------------------------------------------------
// The counter's free increments, one per dot
//
// Pan Docs gives the counter seven free increments before the first pixel but
// not when they fall, and taking them all in one dot is what "A WX below 7
// pushes the window's leftmost pixels off the screen" in
// docs/known-divergences.md was working around. Two Mealybug Tearoom
// references measure where they fall, both of them by cutting a band out of a
// line with BGP and reading the width of the band off the photograph:
//
//   - m3_window_timing writes WX = LY every line. Its DMG reference puts the
//     line's first pixel six dots later than a plain line's for every WX from
//     0 to 10 alike, and one dot less late for each of WX = 11 to 15: the same
//     six-dot restart wherever the match lands, measured from the pixel the
//     match pre-empts.
//   - m3_wx_6_change writes WX = 6 during mode 2 and WX = LY four dots before
//     the first pixel. Its reference shows no window at all on lines 4 and 5,
//     the window from line 6 with its left edge at LY - 7, and no window from
//     line 102 - which pins the comparison against WX two dots behind the
//     counter.
//
// See docs/known-divergences.md, "The window's X counter is compared once per
// dot, against a WX two dots old", for the full derivation.
namespace {
// The picture a window whose first `skip` pixels are off the left edge draws
// from screen x = 0: its row, tiled, rotated left by that many pixels.
std::string clippedWindowLine(const char* windowRow, int skip) {
    std::string out;
    while (out.size() < 160 + 16) { out += windowRow; }
    return out.substr(static_cast<std::size_t>(skip), 160);
}
} // namespace

TEST_CASE("the free increments are spread one per dot, so every WX below 8 costs the same six dots") {
    // Spread one per dot, the counter reaches WX = 0 nine dots before it
    // reaches WX = 7 - and the window's first tile therefore arrives nine dots
    // earlier too, with nine more of its pixels to throw off the left edge.
    // The two cancel: m3_window_timing's reference puts the first pixel of all
    // eight lines in the same place, six dots after a plain line's.
    for (u8 wx = 0; wx <= 7; ++wx) {
        Ppu ppu;
        setUpWindowRuler(ppu, 0xF1, 0x00, wx, 0x00);
        const CharLine got = characterise(ppu);
        // 172 + 6 = 178 raw dots, sampled in whole M-cycles as 180 - exactly
        // what a mid-line activation costs ("starting the window lengthens
        // mode 3 by six dots" above).
        CHECK_MESSAGE(got.dots == 180, "WX ", wx);
        CHECK_MESSAGE(got.pixels == clippedWindowLine(kEvenWindowRow, 7 - wx), "WX ", wx);
    }
}

TEST_CASE("WX = 0 is matched before the fine scroll, so the window is shifted left by SCX % 8") {
    // Pan Docs, "Window behavior": "If WX is equal to 0, the Window is
    // switched to before the initial 'fine scroll' adjustment, causing it to be
    // shifted left by SCX % 8 pixels." Spread one per dot, the counter reaches
    // 0 long before the line's first push, so the SCX discard is still owed
    // when the window's own tile arrives and is spent on it - which is that
    // sentence, and which taking the free increments after the discard had
    // drained could not produce.
    for (u8 scx = 0; scx <= 7; ++scx) {
        Ppu ppu;
        setUpWindowRuler(ppu, 0xF1, scx, 0x00, 0x00);
        const CharLine got = characterise(ppu);
        CHECK_MESSAGE(got.pixels == clippedWindowLine(kEvenWindowRow, 7 + scx), "SCX ", scx);
    }
}

TEST_CASE("the counter is compared against a WX two dots old") {
    // m3_wx_6_change's shape: WX = 6 before mode 3, rewritten four dots before
    // the line's first pixel. Two dots behind the counter, the comparison that
    // could have matched 6 reads the new value and the one that could have
    // matched the new value has already gone past it, so WX = 5 leaves the line
    // plain background - while WX = 7, still ahead of the counter, is matched
    // by the last free increment and starts the window on screen x = 0.
    //
    // WY = 1 and a line run off first, because line 0 draws four dots early
    // (see "line 0 starts drawing four dots earlier" above) and these cases
    // name a dot of mode 3: on WY = 1 line 0 draws no window at all, so line 1
    // is an ordinary line drawing the window's row 0.
    {
        Ppu ppu;
        setUpWindowRuler(ppu, 0xF1, 0x00, 0x06, 0x01);
        runLine(ppu);
        const CharLine got = characteriseWrites(ppu, {{96, 0xFF4B, 0x05}});
        CHECK(got.pixels == std::string(160, '1')); // no window at all
        CHECK(got.dots == 172);
    }
    {
        Ppu ppu;
        setUpWindowRuler(ppu, 0xF1, 0x00, 0x06, 0x01);
        runLine(ppu);
        const CharLine got = characteriseWrites(ppu, {{96, 0xFF4B, 0x07}});
        CHECK(got.pixels == clippedWindowLine(kEvenWindowRow, 0));
        CHECK(got.dots == 180);
    }
}

TEST_CASE("a WX reached again before the window has pushed a tile pushes no colour-0 pixel") {
    // Pan Docs' pixel-FIFO sentence is about a WX changed "after the window has
    // started rendering". WX = 5 starts the window during the free increments
    // and WX = 6 is then reached one dot later, while the restarted fetcher is
    // still six dots from its first push and the FIFO is empty - so the empty
    // FIFO alone is not the condition. m3_wx_5_change runs exactly this on its
    // line 6 and its reference shows the plain WX = 5 picture: the window had
    // not started rendering, and nothing is pushed.
    //
    // WY = 1 and a line run off first, so that the write lands on the dot it is
    // meant to: line 0 draws four dots early.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0x05, 0x01);
    runLine(ppu);
    const CharLine got = characteriseWrites(ppu, {{96, 0xFF4B, 0x06}});
    CHECK(got.pixels == clippedWindowLine(kEvenWindowRow, 2));
    CHECK(got.dots == 180);
}

TEST_CASE("the fine-scroll discard reads SCX at the line's first tile fetch, not when mode 3 begins") {
    // The two are an M-cycle apart, and m3_window_timing_wx_0 lands an SCX
    // write between them on every line. Writing SCX at line dot 88 - after
    // rendering has started but before that fetch reads the map - has to take
    // effect on this line's discard: at SCX = 4 the window, matched by the
    // counter's first free increment, is shifted left by 4 + 7 pixels.
    //
    // WY = 1 and a line run off first: line 0 draws four dots early.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0x00, 0x01);
    runLine(ppu);
    const CharLine got = characteriseWrites(ppu, {{88, 0xFF43, 0x04}});
    CHECK(got.pixels == clippedWindowLine(kEvenWindowRow, 7 + 4));
}

TEST_CASE("an object fetch that stalls the line's warm-up does not hold the free increments back") {
    // The increments are dots, not pixels. An object at screen x = 0 is fetched
    // before the fetcher's first step and stalls eight dots there, so a counter
    // that only moved on the dots the fetcher ran would reach WX = 4 eight dots
    // late - by which time the WX written during mode 3 has overtaken it and the
    // window starts a whole tile-and-a-bit further right. Mealybug Tearoom's
    // m3_wx_4_change_sprites is the reference that measures this; LCDC bit 1
    // here is what puts the object in the way.
    //
    // WY = 1 and a line run off first, so the mode-3 write lands on the dot it
    // is meant to: line 0 draws four dots early.
    Ppu ppu;
    setUpWindowRuler(ppu, 0xF1, 0x00, 0x04, 0x01);
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so OAM and VRAM land
    for (u16 row = 0; row < 16; ++row) {
        ppu.vramWrite(static_cast<u16>(0x8020 + row), 0x00); // tile 2: all colour 0
    }
    ppu.oamWrite(0xFE00, 0x10); // Y = 16: on every line drawn here
    ppu.oamWrite(0xFE01, 0x08); // X = 8: screen x = 0
    ppu.oamWrite(0xFE02, 0x02); // tile 2, fully transparent: it costs its fetch
    ppu.oamWrite(0xFE03, 0x00); //   and draws nothing, so only the stall shows
    enableLcd(ppu, 0xF3);       // as 0xF1, plus objects enabled
    runLine(ppu);
    const CharLine got = characteriseWrites(ppu, {{96, 0xFF4B, 0x28}}); // WX = 40
    // The window still starts where WX = 4 says: three of its pixels off the
    // left edge, screen x = 0 showing its fourth. A held-back counter would
    // leave background as far as x = 33.
    CHECK(got.pixels == clippedWindowLine(kEvenWindowRow, 3));
}

// ---------------------------------------------------------------------------
// Mode 3's length, derived to the dot
//
// STAT's mode field is the only thing mode 3's length shows in, and every case
// above reads it in whole M-cycles: Ppu::tick is four dots and mode 3 begins on
// a dot that is a multiple of four, so a raw length of L dots is reported as
// the next multiple of four at or above L. One reading therefore pins L only to
// within three dots - every window figure above is a 180, which is any of 177,
// 178, 179 or 180 - and a length that is wrong by one, two or three dots is
// invisible in it. Mode 3's length is what STAT reports and nothing else, so
// such an error leaves every picture in this file perfect.
//
// SCX's low three bits are the finer ruler. They lengthen the line by one dot
// each - the fine-scroll discard - and they do that independently of the
// window, so sweeping SCX 0 to 7 over one scenario gives eight readings which
// step from one multiple of four to the next at the two values of SCX where
// L + SCX crosses one. Where those two steps fall says what L is: exactly one
// L fits all eight readings, and solveRawDots asserts that there is exactly
// one. The sweep is therefore part of every case here rather than a case of
// its own, and it is also what pins the SCX term itself at one dot per bit -
// a term of two dots per bit, or one that saturated, leaves no L fitting all
// eight readings at all.
namespace {
// The one raw mode-3 length consistent with eight whole-M-cycle readings taken
// at SCX 0 to 7. Fails if none fits, and fails if more than one does.
int solveRawDots(const std::array<int, 8>& sampled) {
    int found = -1;
    int fits = 0;
    for (int raw = 160; raw <= 320; ++raw) {
        bool ok = true;
        for (int scx = 0; scx < 8 && ok; ++scx) {
            ok = ((raw + scx + 3) / 4) * 4 == sampled[static_cast<std::size_t>(scx)];
        }
        if (ok) {
            found = raw;
            ++fits;
        }
    }
    CHECK_MESSAGE(fits == 1, "readings ", sampled[0], " ", sampled[1], " ", sampled[2], " ",
                  sampled[3], " ", sampled[4], " ", sampled[5], " ", sampled[6], " ",
                  sampled[7]);
    return found;
}

// The raw mode-3 length of a line set up by setUpWindowRuler, measured by the
// SCX sweep. `linesFirst` runs that many whole lines before the one measured,
// which is how a case says it wants an ordinary line rather than line 0.
int rawMode3Dots(u8 lcdc, u8 wx, u8 wy, int linesFirst = 0) {
    std::array<int, 8> sampled{};
    for (int scx = 0; scx < 8; ++scx) {
        Ppu ppu;
        setUpWindowRuler(ppu, lcdc, static_cast<u8>(scx), wx, wy);
        for (int i = 0; i < linesFirst; ++i) {
            runLine(ppu);
        }
        sampled[static_cast<std::size_t>(scx)] = characterise(ppu).dots;
    }
    return solveRawDots(sampled);
}
} // namespace

TEST_CASE("mode 3 is 172 dots plus SCX's low three bits on a line with no window") {
    // 172 and the SCX term are the two parts of this that hardware measures.
    // Mooneye's intr_2_mode0_timing ("verified: DMG, MGB, SGB, SGB2, CGB, AGB,
    // AGS") times the mode-2 interrupt to the mode-0 one on a bare line and
    // pins the 172; hblank_ly_scx_timing-GS reads LY through the HBlank
    // boundary for each of SCX 0 to 7 and pins the one dot per bit. Both are in
    // the ppu timing group, and between them they are the only hardware
    // measurements of mode 3's length the suite has.
    CHECK(rawMode3Dots(0xD1, 0x27, 0x00) == 172); // LCDC bit 5 clear all line
    CHECK(rawMode3Dots(0xF1, 0x27, 0x01) == 172); // the line is above WY
    CHECK(rawMode3Dots(0xF1, 0xA7, 0x00) == 172); // WX = 167: the counter never reaches it
    CHECK(rawMode3Dots(0xF1, 0xFF, 0x00) == 172); // WX = 255: the same
}

TEST_CASE("one window activation lengthens mode 3 by exactly six dots, wherever on the line it falls") {
    // Six, not five and not seven: the restart puts the fetcher back at its
    // Tile step with the queue cleared, and Tile+DataLow+DataHigh take two dots
    // each before Push can run again. The pixel the match pre-empts is already
    // in the one-dot-per-pixel count, so six is the whole of the extra.
    //
    // The same six for every WX from 0 to 166 is the substance: below 7 the
    // match comes early among the counter's free increments and the leftover
    // increments are charged as discarded pixels, at 166 it pre-empts the
    // line's very last pixel and the cost has to keep being counted while the
    // restart is actually running. Both ends are arithmetic that could be out
    // by a dot or two without any picture moving.
    CHECK(rawMode3Dots(0xF1, 0x00, 0x00) == 178); // WX = 0
    CHECK(rawMode3Dots(0xF1, 0x01, 0x00) == 178);
    CHECK(rawMode3Dots(0xF1, 0x04, 0x00) == 178); // WX below 7: pixels off the left edge
    CHECK(rawMode3Dots(0xF1, 0x06, 0x00) == 178);
    CHECK(rawMode3Dots(0xF1, 0x07, 0x00) == 178); // WX = 7: the window from pixel 0
    CHECK(rawMode3Dots(0xF1, 0x08, 0x00) == 178);
    CHECK(rawMode3Dots(0xF1, 0x27, 0x00) == 178); // WX = 39: an ordinary mid-line start
    CHECK(rawMode3Dots(0xF1, 0xA0, 0x00) == 178); // WX = 160: the trigger lands at pixel 153,
                                                  //   where a plain line has only its lag left
    CHECK(rawMode3Dots(0xF1, 0xA5, 0x00) == 178);
    CHECK(rawMode3Dots(0xF1, 0xA6, 0x00) == 178); // WX = 166: the trigger pre-empts pixel 159
}

TEST_CASE("the charge for an activation still to come is exactly six dots, and only an ordinary line measures it") {
    // Every other figure in this file is measured on line 0, and line 0 cannot
    // measure this one. dotsRemaining charges an activation it can see coming
    // kWindowRestartDots dots, and that charge is only ever the term that
    // decides the boundary if the boundary would otherwise be decided *before*
    // the activation fires - which needs 160 - pixelX_ + 6 to be down at the
    // line's render lag while the trigger is still ahead of the counter. On
    // line 0 the lag is three dots (it draws four dots early, see "line 0
    // starts drawing four dots earlier" above), so that never happens for any
    // WX at all: the activation has always fired first and the charge has
    // dropped out. On an ordinary line the lag is seven and the two cross at
    // WX = 165 and WX = 166, where the trigger pre-empts the line's last two
    // pixels.
    //
    // So those two values are the only place in the suite where the *size* of
    // the charge shows at all, and it shows there as one raw dot - which whole
    // M-cycles round away, and which no picture anywhere reflects. Without the
    // SCX ruler above, charging five dots or seven instead of six passes every
    // other case in this file and all 165 test ROMs.
    CHECK(rawMode3Dots(0xF1, 0xA5, 0x00, 1) == 178); // WX = 165
    CHECK(rawMode3Dots(0xF1, 0xA6, 0x00, 1) == 178); // WX = 166
    // And the lengths an ordinary line shares with line 0, so that the
    // four-dot cancellation itself is pinned rather than assumed.
    CHECK(rawMode3Dots(0xD1, 0x27, 0x00, 1) == 172);
    CHECK(rawMode3Dots(0xF1, 0x00, 0x00, 1) == 178);
    CHECK(rawMode3Dots(0xF1, 0x07, 0x00, 1) == 178);
    CHECK(rawMode3Dots(0xF1, 0x27, 0x00, 1) == 178);
    CHECK(rawMode3Dots(0xF1, 0xA0, 0x00, 1) == 178);
}

TEST_CASE("two window activations on one line lengthen mode 3 by exactly twelve dots") {
    // The row-advance side of this line is pinned by "the window activates
    // twice on one line ..." above; this is its dot count, to the dot. Each
    // activation is a full fetcher restart, and the stop in between costs
    // nothing at all - Mealybug's notes have the background resuming on a tile
    // boundary with the queue intact, so nothing is refetched.
    //
    // Nothing in either suite measures this length. It is derived from the
    // six-dot restart the case above pins, applied twice.
    std::array<int, 8> sampled{};
    for (int scx = 0; scx < 8; ++scx) {
        Ppu ppu;
        setUpWindowRuler(ppu, 0xF1, static_cast<u8>(scx), 0x27, 0x00);
        // WX = 39 starts the window at screen x = 32; LCDC bit 5 is then
        // cleared, WX moved to 120 - which the counter reaches around line dot
        // 219, after every write here whatever SCX is - and bit 5 set again.
        sampled[static_cast<std::size_t>(scx)] =
            characteriseWrites(ppu, {{160, 0xFF40, 0xD1},
                                     {164, 0xFF4B, 0x78},
                                     {168, 0xFF40, 0xF1}})
                .dots;
    }
    CHECK(solveRawDots(sampled) == 184); // 172 + 6 + 6
}

// ---------------------------------------------------------------------------
// What an object fetch costs, and who pays it
//
// An object fetch stalls the line. Two hardware sources measure that stall from
// two different sides, and they do not agree unless the fetcher and the pixels
// pay different amounts:
//
//   - Mealybug Tearoom's m3_bgp_change_sprites photographs the pixel stream
//     directly - it rewrites BGP at known dots and the seams say which pixel
//     was being drawn on each of them - and over eighteen different OAM X
//     values it puts the line's pixels exactly Pan Docs' full OBJ penalty
//     behind an object-free line;
//   - Mooneye's hardware-verified intr_2_mode0_timing_sprites measures mode 3's
//     length and puts it three dots short of that sum, once per line.
//
// Both hold if the line's first object fetch costs the background fetcher three
// dots less than it costs the pixels: the fetcher runs on through three of the
// stall's dots, the FIFO carries the three pixels it gains, and mode 3 ends
// with the fetcher rather than with the last pixel. See
// docs/known-divergences.md, "An object fetch costs the pixels three dots more
// than it costs the fetcher and mode 3".

TEST_CASE("an object fetch waits for the pixel it pre-empts rather than starting the line") {
    // Pan Docs, "Pixel FIFO", on the object fetch: "the fetcher is advanced one
    // step until it's at step 5 or until the background FIFO is not empty". An
    // object at screen x = 0 therefore waits for the line's first push - the
    // dot pixel 0 would have been drawn on - instead of stalling the line
    // before the fetcher has taken a step.
    //
    // The line's warm-up is then undisturbed: the first tile reads its low
    // bitplane on line dot 97 and the row goes in on dot 100, where the
    // object's eleven-dot fetch pre-empts it, so pixel 0 is drawn on dot 111.
    // A SCY written on dot 100 is visible from dot 101, which is after that
    // read: the first tile keeps row 1, colour 0. Fetching the object on the
    // line's first rendering dot instead pushes the whole warm-up eight dots
    // later, the first tile reads its low bitplane on dot 105, and the tile
    // draws row 2's colour 1. Line 1, not line 0: line 0 draws four dots early.
    Ppu ppu;
    setUpScyRowRuler(ppu);
    addStallingObject(ppu, 8); // OAM X = 8: screen x = 0
    const u8* row = lineWithWriteAt(ppu, 1, 100, 0xFF42, 0x01);
    CHECK(row[0] == 0);
    CHECK(row[7] == 0);
    CHECK(row[8] == 1); // the tile after it reads its low bitplane on dot 115
}

TEST_CASE("the pixels reaching the LCD pay an object fetch's full Pan Docs penalty") {
    // m3_bgp_change_sprites' measurement, as one dot. The object at OAM X = 8
    // costs Pan Docs' flat six dots plus a tile term of 7 - 0 - 2 = 5: eleven
    // dots, all of them ahead of pixel 0, which is therefore drawn on line dot
    // 111 and takes the BGP in force then. A BGP written on dot 108 is visible
    // from dot 109, so pixel 0 sees it. Charging the pixels three dots less -
    // the rebate the mode-3 length needs - draws pixel 0 on dot 108 instead,
    // before the write.
    Ppu ppu;
    setUpTile(ppu, 0xFF, 0x00); // every background pixel colour 1
    addStallingObject(ppu, 8);
    const u8* row = lineWithWriteAt(ppu, 1, 108, 0xFF47, 0x00); // BGP: colour 1 -> shade 0
    CHECK(row[0] == 0);
    CHECK(row[7] == 0);
}

TEST_CASE("an object fetch leaves the fetcher three dots ahead of the pixels") {
    // The other half of the same reconciliation, and the half that says the
    // three dots are the fetcher's rather than nobody's. The object at OAM X = 8
    // costs the pixels eleven dots but the fetcher only eight, so every fetch
    // from here reads its registers three dots earlier than the eight-dot
    // rhythm alone would put them. The tile at x = 8-15 has its first pixel on
    // dot 119, so its index would be read on dot 114 and is read on dot 111.
    //
    // SCY = 8 moves the whole line to the next tile-map row, where the tile is
    // colour 3 instead of colour 0, so only the tile-index stage can see it -
    // and that stage is the one a three-dot lead moves across the M-cycle
    // boundary at 112, which the two bitplane stages both stay on one side of.
    // Written on dot 112, visible from 113: the lead reads the index before it
    // and keeps map row 0, while a fetcher that lost all eleven dots reads on
    // dot 114 and draws map row 1's colour 3 here already.
    Ppu ppu;
    setUpScyPlaneRuler(ppu, /*highPlane=*/false);
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8010 + row), 0xFF); // tile 1: colour 3
        ppu.vramWrite(static_cast<u16>(0x8011 + row), 0xFF);
    }
    for (u16 i = 0; i < 32; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9820 + i), 0x01); // map row 1: tile 1
    }
    addStallingObject(ppu, 8);
    const u8* row = lineWithWriteAt(ppu, 1, 112, 0xFF42, 0x08);
    CHECK(row[8] == 0);
    CHECK(row[15] == 0);
    CHECK(row[16] == 3); // the tile after it reads its index on dot 119
}

TEST_CASE("an object fetch triggered on the dot the window activates does not wait for the window's row") {
    // The two things that can happen on the dot the line's first row reaches the
    // FIFO: the X counter reaches WX and the window resets the fetcher, and an
    // object at screen x = 0 pre-empts the pixel that row was about to feed.
    // Which of them the other sees decides whether the object's fetch runs now
    // or waits another six dots for the window's own first row - and a Mealybug
    // Tearoom reference says now: the pixel was due, so the fetch that pre-empts
    // it starts, whether or not the window then takes the fetcher away. See
    // docs/known-divergences.md, "The window's fetches, and the object fetch that
    // lands on the same dot as the activation".
    //
    // WX = 7 puts the activation on line dot 100 and the object at OAM X = 8
    // costs eleven dots, so pixel 0 is drawn on dot 114. Waiting for the
    // window's row instead puts the whole eleven-dot fetch six dots later and
    // pixel 0 on dot 117. BGP is rewritten on dot 116: the palette shorts the old
    // and new values together for the one dot after the write (dot 117), so the
    // three shades 1, 3 and 2 name the three sides of that boundary and say
    // exactly which dot pixel 0 was drawn on. Line 1, not line 0.
    Ppu ppu;
    // LCDC $B3: window on with its map at $9800, where every tile is tile 0 -
    // colour 1 across - so the whole line is one colour and the shades below are
    // BGP's alone.
    setUpWindowRuler(ppu, 0xB3, /*scx=*/0x00, /*wx=*/0x07, /*wy=*/0x00);
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so OAM and VRAM land
    for (u16 row = 0; row < 16; ++row) {
        ppu.vramWrite(static_cast<u16>(0x8020 + row), 0x00); // tile 2: transparent
    }
    ppu.oamWrite(0xFE00, 0x10); // Y = 16: on every line drawn here
    ppu.oamWrite(0xFE01, 0x08); // X = 8: screen x = 0, fetched at pixel 0
    ppu.oamWrite(0xFE02, 0x02);
    ppu.oamWrite(0xFE03, 0x00);
    enableLcd(ppu, 0xB3);
    // BGP $E4 shades colour 1 to 1, $08 shades it to 2, and the two shorted
    // together ($EC) to 3.
    const u8* row = lineWithWriteAt(ppu, 1, 116, 0xFF47, 0x08);
    CHECK(row[0] == 1); // drawn on dot 114, before the write
    CHECK(row[2] == 1); // dot 116
    CHECK(row[3] == 3); // dot 117: the palette short
    CHECK(row[4] == 2); // dot 118: the new palette
}

// ---------------------------------------------------------------------------
// The same question for a window fetch, and it needs no object at all
//
// A window fetch reads the same three stages' worth of registers as a
// background fetch, with LCDC bit 6 in place of bit 3 for the tilemap. Whether
// it samples them on the same dot of each stage is a separate question - the
// Mealybug notes say nothing about window fetches - and it can be asked without
// a stalling object, because the window's own restart puts its stages wherever
// WX says: the counter reaches WX on line dot 93 + WX, the activation spends
// that dot resetting the fetcher, and the stages follow. A WX chosen so that a
// stage's first dot is the last dot of an M-cycle separates the stage's two dots
// on an otherwise undisturbed line.
namespace {
// The window drawing tile 0 of its map everywhere, with map $9800's tile 0
// colour 1 and map $9C00's tile 1 colour 3, so which map a window fetch read is
// the colour it drew. LCDC bit 6 starts clear.
void setUpWindowMapRuler(Ppu& ppu, u8 wx) {
    setUpWindowRuler(ppu, 0xB3, /*scx=*/0x00, wx, /*wy=*/0x00);
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8010 + row), 0xFF); // tile 1: colour 3
        ppu.vramWrite(static_cast<u16>(0x8011 + row), 0xFF);
    }
}
} // namespace

TEST_CASE("a window fetch reads LCDC bit 6 on the tile-index stage's first dot") {
    // WX = 6 puts the activation on line dot 99, so the window's first fetch has
    // its tile-index stage on dots 100-101. LCDC bit 6 written on dot 100 is
    // visible from 101: the first dot reads the window's tilemap at $9800 - tile
    // 0, colour 1 - and the stage's last dot would read $9C00 and draw tile 1's
    // colour 3 here instead. The window's second fetch reads its index on dot
    // 108 either way and draws colour 3, which is what says the write landed.
    // Line 1, not line 0: line 0 draws four dots early.
    Ppu ppu;
    setUpWindowMapRuler(ppu, /*wx=*/0x06);
    const u8* row = lineWithWriteAt(ppu, 1, 100, 0xFF40, 0xF3); // bit 6 set from 101
    CHECK(row[0] == 1); // the window's first tile: map $9800
    CHECK(row[6] == 1);
    CHECK(row[7] == 3); // its second tile: map $9C00
}

// The bitplane-mixing ruler of the case above, as a helper: tile 0 at $8000 has
// a low bitplane only and tile 0 at $9000 a high one only, so a fetch that took
// one from each area draws colour 3 and neither area can do it alone. LCDC bit 4
// starts set, and the window's map is $9800 where every tile is tile 0.
namespace {
void setUpWindowPlaneRuler(Ppu& ppu, u8 wx) {
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off so writes land
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0xFF); // bit 4 set: low only
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x9000 + row), 0x00); // bit 4 clear: high only
        ppu.vramWrite(static_cast<u16>(0x9001 + row), 0xFF);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    static_cast<void>(ppu.write(0xFF47, 0xE4));
    static_cast<void>(ppu.write(0xFF4A, 0x00)); // WY = 0
    static_cast<void>(ppu.write(0xFF4B, wx));
    enableLcd(ppu, 0xB3); // window on, bit 4 set, both maps $9800
}
} // namespace

TEST_CASE("a window fetch reads LCDC bit 4 on the low bitplane stage's first dot") {
    // Mealybug's TILE_SEL sentence is about background fetches; this is the same
    // mixing for a window fetch, and the case that `m3_lcdc_tile_sel_win_change`
    // photographs. Tile 0 at $8000 - what a set bit 4 selects - has a low
    // bitplane only, and tile 0 at $9000, which a clear bit 4 selects, has a high
    // one only, so neither can draw colour 3 alone.
    //
    // WX = 4 puts the activation on dot 97 and the window's first fetch's
    // bitplane stages on dots 100-101 and 102-103. Clearing bit 4 on dot 100 is
    // visible from 101, so the low bitplane comes from $8000 and the high one
    // from $9000: colour 3. Sampling on each stage's last dot reads both from
    // $9000 and draws colour 2.
    Ppu ppu;
    setUpWindowPlaneRuler(ppu, /*wx=*/0x04);
    const u8* row = lineWithWriteAt(ppu, 1, 100, 0xFF40, 0xA3); // bit 4 clear from 101
    CHECK(row[0] == 3); // one bitplane from each area
    CHECK(row[3] == 3);
}

TEST_CASE("a window fetch reads LCDC bit 4 on the high bitplane stage's first dot") {
    // The other bitplane stage, and the other WX that lands a window stage on an
    // M-cycle boundary. WX = 6 puts the activation on dot 99 and the window's
    // first fetch's stages on dots 100-101, 102-103 and 104-105. Clearing bit 4
    // on dot 104 is visible from 105, so both bitplanes come from $8000 - a low
    // plane and no high one, colour 1 - while the stage's last dot would read the
    // high plane from $9000 and mix colour 3 out of the two areas.
    Ppu ppu;
    setUpWindowPlaneRuler(ppu, /*wx=*/0x06);
    const u8* row = lineWithWriteAt(ppu, 1, 104, 0xFF40, 0xA3); // bit 4 clear from 105
    CHECK(row[0] == 1); // both bitplanes from $8000
    CHECK(row[5] == 1);
}
