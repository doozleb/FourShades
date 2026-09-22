#include <doctest/doctest.h>

#include "core/Ppu.h"

#include <cstdint>

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

TEST_CASE("the window does not draw above WY") {
    Ppu ppu;
    setUpWindow(ppu);
    static_cast<void>(ppu.write(0xFF4A, 0x02)); // WY = 2
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
