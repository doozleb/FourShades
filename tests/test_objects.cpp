#include <doctest/doctest.h>

#include "core/Ppu.h"

using namespace fourshades;

namespace {
// Background tile 0 = colour 1; object tile 1 = colour 3 (left half) and
// colour 0 (right half), so transparency is visible.
void setUpObjects(Ppu& ppu) {
    ppu.write(0xFF40, 0x11); // LCD off
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8000 + row), 0xFF); // tile 0: colour 1
        ppu.vramWrite(static_cast<u16>(0x8001 + row), 0x00);
        ppu.vramWrite(static_cast<u16>(0x8010 + row), 0xF0); // tile 1: 3,3,3,3,0,0,0,0
        ppu.vramWrite(static_cast<u16>(0x8011 + row), 0xF0);
    }
    for (u16 i = 0; i < 0x400; ++i) {
        ppu.vramWrite(static_cast<u16>(0x9800 + i), 0x00);
    }
    for (u16 i = 0; i < 0xA0; ++i) {
        ppu.oamWrite(static_cast<u16>(0xFE00 + i), 0x00); // every object off-screen
    }
    ppu.write(0xFF47, 0xE4); // BGP straight through
    ppu.write(0xFF48, 0xE4); // OBP0 straight through
    ppu.write(0xFF49, 0x1B); // OBP1 reversed
    ppu.write(0xFF40, 0x93); // LCD on, BG on, objects on, 8x8
}

void putObject(Ppu& ppu, int index, u8 y, u8 x, u8 tile, u8 flags) {
    const u16 base = static_cast<u16>(0xFE00 + index * 4);
    // Brief bug: hardcoding the re-enable to 0x93 clobbers LCDC bits a test
    // set before calling putObject (e.g. bit 2 for 8x16 objects). Save and
    // restore whatever LCDC was in effect instead.
    const u8 previousLcdc = ppu.read(0xFF40);
    ppu.write(0xFF40, 0x11); // LCD off so OAM is writable
    ppu.oamWrite(base, y);
    ppu.oamWrite(static_cast<u16>(base + 1), x);
    ppu.oamWrite(static_cast<u16>(base + 2), tile);
    ppu.oamWrite(static_cast<u16>(base + 3), flags);
    ppu.write(0xFF40, previousLcdc);
}

int runLineObjects(Ppu& ppu) {
    while (ppu.mode() != 3) { ppu.tick(); }
    int drawing = 0;
    while (ppu.mode() == 3) { ppu.tick(); drawing += 4; }
    return drawing;
}
} // namespace

TEST_CASE("an object is drawn over the background, and colour 0 is transparent") {
    Ppu ppu;
    setUpObjects(ppu);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x00); // line 0, screen x = 16
    runLineObjects(ppu);
    CHECK(ppu.frame()[15] == 1); // background
    CHECK(ppu.frame()[16] == 3); // object's left half
    CHECK(ppu.frame()[19] == 3);
    CHECK(ppu.frame()[20] == 1); // transparent: background shows through
}

TEST_CASE("the priority flag puts the object behind background colours 1-3") {
    Ppu ppu;
    setUpObjects(ppu);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x80); // priority set
    runLineObjects(ppu);
    CHECK(ppu.frame()[16] == 1); // background wins
}

TEST_CASE("objects use OBP0 or OBP1 by their palette flag") {
    Ppu ppu;
    setUpObjects(ppu);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x10); // OBP1, which is reversed
    runLineObjects(ppu);
    CHECK(ppu.frame()[16] == 0); // colour 3 through 0x1B is shade 0
}

TEST_CASE("objects can be flipped in both directions") {
    Ppu ppu;
    setUpObjects(ppu);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x20); // X flip
    runLineObjects(ppu);
    CHECK(ppu.frame()[16] == 1); // the transparent half is now on the left
    CHECK(ppu.frame()[20] == 3);

    // Y flip with a tile whose rows differ: row 0 solid, row 7 empty.
    // Brief bug: it zeroed row 1 (0x8012/0x8013), but an 8-tall Y-flipped
    // object showing screen row 0 selects tile row 7 - 0 = 7
    // (0x801E/0x801F), so that is the row that must be made empty.
    ppu.write(0xFF40, 0x11);
    ppu.vramWrite(0x801E, 0x00);
    ppu.vramWrite(0x801F, 0x00);
    ppu.write(0xFF40, 0x93);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x40); // Y flip: row 0 shows row 7
    while (ppu.frameCount() == 0) { ppu.tick(); }
    CHECK(ppu.frame()[16] == 1); // row 7 of the tile is empty, so background
}

TEST_CASE("8x16 objects use two tiles and ignore the index's low bit") {
    Ppu ppu;
    setUpObjects(ppu);
    ppu.write(0xFF40, 0x97); // 8x16 objects
    putObject(ppu, 0, 16, 8 + 16, 0x01, 0x00); // index 1 -> top tile 0
    runLineObjects(ppu);
    CHECK(ppu.frame()[16] == 1); // tile 0 is colour 1, drawn through OBP0
}

TEST_CASE("only ten objects are drawn on a line, chosen in OAM order") {
    Ppu ppu;
    setUpObjects(ppu);
    for (int i = 0; i < 12; ++i) {
        putObject(ppu, i, 16, static_cast<u8>(8 + i * 8), 1, 0x00);
    }
    runLineObjects(ppu);
    CHECK(ppu.lineObjects().size() == 10);
    CHECK(ppu.frame()[72] == 3);  // object 9, the last one selected
    CHECK(ppu.frame()[80] == 1);  // object 10 was dropped
}

TEST_CASE("an object off the left edge still uses one of the ten slots") {
    Ppu ppu;
    setUpObjects(ppu);
    putObject(ppu, 0, 16, 0, 1, 0x00); // X = 0: invisible
    for (int i = 1; i < 11; ++i) {
        putObject(ppu, i, 16, static_cast<u8>(8 + i * 8), 1, 0x00);
    }
    runLineObjects(ppu);
    CHECK(ppu.lineObjects().size() == 10);
    CHECK(ppu.frame()[80] == 1); // the eleventh object never got a slot
}

TEST_CASE("each object lengthens mode 3") {
    Ppu ppu;
    setUpObjects(ppu);
    const int plain = runLineObjects(ppu);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x00);
    const int withOne = runLineObjects(ppu);
    CHECK(withOne > plain);
    CHECK(withOne - plain <= 12); // one object costs 6-11 dots
}

TEST_CASE("clearing LCDC bit 1 hides objects") {
    Ppu ppu;
    setUpObjects(ppu);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x00);
    ppu.write(0xFF40, 0x91); // objects off
    runLineObjects(ppu);
    CHECK(ppu.frame()[16] == 1);
}
