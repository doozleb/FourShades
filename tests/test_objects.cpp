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

TEST_CASE("an 8x16 object reads tile + 1 once the screen row reaches 8") {
    Ppu ppu;
    setUpObjects(ppu);
    ppu.write(0xFF40, 0x97); // 8x16 objects
    // Y = 8 puts the object's own rows 0-7 off the top of the screen, so
    // line 0 (the only line runLineObjects draws) is the object's row
    // 8 - lineNumber(0) - (y(8) - 16) = 8 - the first row of its bottom
    // tile. Tile data is contiguous in VRAM, so "tile + 1" here is tile
    // 0's neighbour, tile 1, which setUpObjects gives a distinct pattern
    // (colour 3 / colour 0) from tile 0 (uniform colour 1).
    putObject(ppu, 0, 8, 8 + 16, 0x01, 0x00); // index 1 -> masked to tile 0, +1 = tile 1
    runLineObjects(ppu);
    CHECK(ppu.frame()[16] == 3); // tile 1's left half
    CHECK(ppu.frame()[20] == 1); // tile 1's right half is transparent: background shows
}

TEST_CASE("a Y-flipped 8x16 object flips across all sixteen rows, not each half") {
    Ppu ppu;
    setUpObjects(ppu);
    ppu.write(0xFF40, 0x97); // 8x16 objects
    // Y = 16 (screen y = 0) puts line 0 at the object's own row 0. Y-flipped,
    // Pan Docs' rule is row = height - 1 - row = 16 - 1 - 0 = 15, which (via
    // the same contiguous-VRAM arithmetic as the test above) reads tile 1's
    // last row, not tile 0's: a flip confined to each 8-row half would
    // instead reflect row 0 within the top half alone (row 7 of tile 0,
    // colour 1 -- indistinguishable from "no object" here), so seeing tile
    // 1's pattern (colour 3 / colour 0) is what proves the flip spans the
    // whole 16 rows.
    putObject(ppu, 0, 16, 8 + 16, 0x01, 0x40); // index 1 -> tile 0/1 pair, Y flip
    runLineObjects(ppu);
    CHECK(ppu.frame()[16] == 3); // tile 1's last row, left half
    CHECK(ppu.frame()[20] == 1); // tile 1's last row, right half is transparent
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

TEST_CASE("an object at the left edge stays aligned with pixelX_ when SCX discards pixels") {
    Ppu ppu;
    setUpObjects(ppu);
    ppu.write(0xFF43, 0x03); // SCX = 3: the line's first 3 background pixels are discarded
    putObject(ppu, 0, 16, 8, 1, 0x00); // OAM X = 8 -> screen X = 0
    runLineObjects(ppu);
    // The object's X is already in screen space (Pan Docs), so it belongs
    // at screen columns 0-3 (colour 3) / 4-7 (colour 0, transparent)
    // regardless of SCX: SCX only discards background pixels scrolled off
    // the left edge before pixelX_ starts counting, and never touches the
    // object's own coordinates. objects_ is a shift register whose
    // invariant is "objects_[k] holds the object pixel for screen pixel
    // pixelX_ + k"; if it shifts once per popped background pixel even
    // while SCX's discard is draining (pixelX_ not yet advancing), the
    // object's leftmost 3 columns are shifted out during the discard and
    // the whole object appears 3 columns to the left of where it belongs.
    CHECK(ppu.frame()[0] == 3);
    CHECK(ppu.frame()[1] == 3);
    CHECK(ppu.frame()[2] == 3);
    CHECK(ppu.frame()[3] == 3);
    CHECK(ppu.frame()[4] == 1); // transparent: background shows through
    CHECK(ppu.frame()[5] == 1);
    CHECK(ppu.frame()[6] == 1);
    CHECK(ppu.frame()[7] == 1);
}

TEST_CASE("each object lengthens mode 3") {
    Ppu ppu;
    setUpObjects(ppu);
    const int plain = runLineObjects(ppu);
    CHECK(plain == 172);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x00); // screen x = 16, SCX = 0
    const int withOne = runLineObjects(ppu);
    // Pan Docs' "OBJ penalty algorithm": a flat 6 dots plus a tile term the
    // first time an object's leftmost pixel falls in a new tile. Screen
    // x = 16 with SCX = 0 is tile (0 + 16) / 8 = 2, and the object's pixel
    // sits 7 - (16 & 7) = 7 pixels short of that tile's right edge; since
    // 7 > 2, the tile term is 7 - 2 = 5. So the penalty is the flat 6 plus
    // 5 = 11 raw dots on top of the plain line's 172 raw dots: 183 raw,
    // which runLineObjects (counting whole 4-dot M-cycles) reports as 184.
    CHECK(withOne == 184);
}

TEST_CASE("an object at OAM X = 0 always costs eleven dots") {
    Ppu ppu;
    setUpObjects(ppu);
    const int plain = runLineObjects(ppu);
    CHECK(plain == 172);
    putObject(ppu, 0, 16, 0, 1, 0x00); // OAM X = 0
    const int withOne = runLineObjects(ppu);
    // X = 0 is the special case that always costs the flat 6 plus the full
    // 5-dot tile term -- 11 dots flat, no tile lookup at all -- so unlike
    // the general case above it can't vary with pixelX_ or SCX. 172 + 11 =
    // 183 raw dots, reported as 184: the same number as the x = 16 case
    // above, but for a different reason (a hardcoded 11, not a computed
    // one), which is exactly why this needs its own assertion rather than
    // reusing that test's tolerance.
    CHECK(withOne == 184);
}

TEST_CASE("clearing LCDC bit 1 hides objects") {
    Ppu ppu;
    setUpObjects(ppu);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x00);
    ppu.write(0xFF40, 0x91); // objects off
    runLineObjects(ppu);
    CHECK(ppu.frame()[16] == 1);
}
