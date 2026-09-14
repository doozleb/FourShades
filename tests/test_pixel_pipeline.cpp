#include <doctest/doctest.h>

#include "core/Ppu.h"

using namespace fourshades;

namespace {
// Fills tile 0 with a row pattern and points the whole map at it.
void setUpTile(Ppu& ppu, u8 low, u8 high) {
    ppu.write(0xFF40, 0x11); // LCD off so writes land
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), low);
        ppu.vramWrite(static_cast<u16>(0x8001 + row), high);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    ppu.write(0xFF47, 0xE4); // BGP: colour 0->0, 1->1, 2->2, 3->3
    ppu.write(0xFF40, 0x91); // LCD on, BG on, tile data at 0x8000, map 0x9800
}

// Runs one whole line and returns the dot mode 3 ended on.
int runLine(Ppu& ppu) {
    int dots = 0;
    while (ppu.mode() != 3) { ppu.tick(); dots += 4; }
    int drawing = 0;
    while (ppu.mode() == 3) { ppu.tick(); drawing += 4; }
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
    ppu.write(0xFF43, 0x04); // SCX = 4
    CHECK(runLine(ppu) == 176);
    ppu.write(0xFF43, 0x05); // SCX = 5
    CHECK(runLine(ppu) == 180); // 172 + 5 = 177 dots, reported as 180 when counted in whole M-cycles
}

TEST_CASE("SCX and SCY move the viewport") {
    Ppu ppu;
    ppu.write(0xFF40, 0x11);
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
    ppu.write(0xFF47, 0xE4);
    ppu.write(0xFF40, 0x91);
    runLine(ppu);
    CHECK(ppu.frame()[7] == 0);
    CHECK(ppu.frame()[8] == 3); // tile 1 starts at x = 8

    ppu.write(0xFF43, 0x08); // SCX = 8: that tile moves to x = 0
    runLine(ppu); // this draws line 1, not line 0, so read row 1 below
    CHECK(ppu.frame()[Ppu::kWidth + 0] == 3);
    CHECK(ppu.frame()[Ppu::kWidth + 8] == 0);
}

TEST_CASE("clearing LCDC bit 0 blanks the background") {
    Ppu ppu;
    setUpTile(ppu, 0xFF, 0xFF); // every pixel colour 3
    runLine(ppu);
    CHECK(ppu.frame()[0] == 3);
    ppu.write(0xFF40, 0x90); // background off
    runLine(ppu); // this draws line 1, not line 0, so read row 1 below
    CHECK(ppu.frame()[Ppu::kWidth] == 0);
}

TEST_CASE("LCDC bit 4 clear selects signed tile addressing from 0x9000") {
    Ppu ppu;
    ppu.write(0xFF40, 0x11); // LCD off so writes land
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
    ppu.write(0xFF47, 0xE4);
    ppu.write(0xFF40, 0x81); // LCD on, BG on, LCDC bit 4 CLEAR: signed addressing, map at 0x9800
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
    ppu.write(0xFF40, 0x11); // LCD off so writes land
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
    ppu.write(0xFF47, 0xE4);
    ppu.write(0xFF40, 0x91); // LCDC bit 3 CLEAR: map at 0x9800
    runLine(ppu);
    CHECK(ppu.frame()[0] == 1);

    ppu.write(0xFF40, 0x99); // LCDC bit 3 SET (0x91 | 0x08): map at 0x9C00
    runLine(ppu); // this draws line 1, not line 0, so read row 1 below
    CHECK(ppu.frame()[Ppu::kWidth + 0] == 2);
}

TEST_CASE("SCY's low bits pick the tile row drawn on a line (fine scroll)") {
    Ppu ppu;
    ppu.write(0xFF40, 0x11); // LCD off so writes land
    // Tile 0: row 0 is solid colour 3; row 1 is colour 0 (empty).
    ppu.vramWrite(0x8000, 0xFF);
    ppu.vramWrite(0x8001, 0xFF); // row 0 -> colour 3
    ppu.vramWrite(0x8002, 0x00);
    ppu.vramWrite(0x8003, 0x00); // row 1 -> colour 0
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    ppu.write(0xFF47, 0xE4);
    ppu.write(0xFF42, 0x01); // SCY = 1
    ppu.write(0xFF40, 0x91);
    // Line 0 reads y = line(0) + scy(1) = 1, so row 1 (colour 0) is drawn, not
    // row 0 (colour 3).
    runLine(ppu);
    CHECK(ppu.frame()[0] == 0);
    CHECK(ppu.frame()[7] == 0);
}

TEST_CASE("SCY's high bits pick the tile-map row a line is fetched from (map scroll)") {
    Ppu ppu;
    ppu.write(0xFF40, 0x11); // LCD off so writes land
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
    ppu.write(0xFF47, 0xE4);
    ppu.write(0xFF42, 0x08); // SCY = 8: y / 8 == 1 on line 0, so map row 1 is used
    ppu.write(0xFF40, 0x91);
    runLine(ppu);
    CHECK(ppu.frame()[0] == 2); // tile 1's colour, proving map row 1 was fetched, not row 0
}

TEST_CASE("a whole frame is drawn line by line") {
    Ppu ppu;
    ppu.write(0xFF40, 0x11);
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0xFF);
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x00);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    ppu.write(0xFF47, 0xE4);
    ppu.write(0xFF40, 0x91);
    while (ppu.frameCount() == 0) {
        ppu.tick();
    }
    for (int y = 0; y < Ppu::kHeight; ++y) {
        CHECK(ppu.frame()[static_cast<std::size_t>(y) * Ppu::kWidth] == 1);
    }
}
