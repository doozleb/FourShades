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

// Background tile 0 = colour 1; object tile 1 = colour 3 (left half) and
// colour 0 (right half), so transparency is visible.
void setUpObjects(Ppu& ppu) {
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off
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
    static_cast<void>(ppu.write(0xFF47, 0xE4)); // BGP straight through
    static_cast<void>(ppu.write(0xFF48, 0xE4)); // OBP0 straight through
    static_cast<void>(ppu.write(0xFF49, 0x1B)); // OBP1 reversed
    enableLcd(ppu, 0x93); // LCD on, BG on, objects on, 8x8
}

void putObject(Ppu& ppu, int index, u8 y, u8 x, u8 tile, u8 flags) {
    // Written the way OAM DMA writes it: never blocked, and - unlike the
    // LCD-off/LCD-on pair this used to use - it neither clobbers LCDC bits a
    // test set beforehand nor restarts the PPU on the special line that
    // switching the LCD on produces.
    ppu.dmaWriteOam(index * 4 + 0, y);
    ppu.dmaWriteOam(index * 4 + 1, x);
    ppu.dmaWriteOam(index * 4 + 2, tile);
    ppu.dmaWriteOam(index * 4 + 3, flags);
}

int runLineObjects(Ppu& ppu) {
    while (ppu.mode() != 3) { ppu.tick(); }
    int drawing = 0;
    while (ppu.mode() == 3) { ppu.tick(); drawing += 4; }
    // A line's last pixels reach the frame up to PixelPipeline::kRenderLag
    // dots after mode 3 ends - rendering trails the mode-3 window at both
    // ends (docs/known-divergences.md, "Rendering runs seven dots behind the
    // mode-3 window") - so let the pipeline finish before frame() is read.
    ppu.tick();
    ppu.tick();
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
    static_cast<void>(ppu.write(0xFF40, 0x11));
    ppu.vramWrite(0x801E, 0x00);
    ppu.vramWrite(0x801F, 0x00);
    enableLcd(ppu, 0x93);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x40); // Y flip: row 0 shows row 7
    for (const std::uint64_t frame = ppu.frameCount(); ppu.frameCount() == frame;) {
        ppu.tick(); // to the end of the frame that is being drawn now
    }
    CHECK(ppu.frame()[16] == 1); // row 7 of the tile is empty, so background
}

TEST_CASE("8x16 objects use two tiles and ignore the index's low bit") {
    Ppu ppu;
    setUpObjects(ppu);
    static_cast<void>(ppu.write(0xFF40, 0x97)); // 8x16 objects
    putObject(ppu, 0, 16, 8 + 16, 0x01, 0x00); // index 1 -> top tile 0
    runLineObjects(ppu);
    CHECK(ppu.frame()[16] == 1); // tile 0 is colour 1, drawn through OBP0
}

TEST_CASE("an 8x16 object reads tile + 1 once the screen row reaches 8") {
    Ppu ppu;
    setUpObjects(ppu);
    static_cast<void>(ppu.write(0xFF40, 0x97)); // 8x16 objects
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
    static_cast<void>(ppu.write(0xFF40, 0x97)); // 8x16 objects
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
    static_cast<void>(ppu.write(0xFF43, 0x03)); // SCX = 3: the line's first 3 background pixels are discarded
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
    // Pan Docs' "OBJ Penalty Algorithm": a flat 6 dots plus a tile term the
    // first time an object's leftmost pixel falls in a new tile. Screen
    // x = 16 with SCX = 0 is tile (0 + 16) / 8 = 2, and the object's pixel
    // sits 7 - (16 & 7) = 7 pixels short of that tile's right edge; since
    // 7 > 2, the tile term is 7 - 2 = 5, for 11 dots in all. Hardware then
    // charges three fewer dots for the first object fetched on a line
    // (Mooneye acceptance/ppu/intr_2_mode0_timing_sprites; see
    // docs/known-divergences.md), so this line costs 8 raw dots on top of
    // the plain line's 172: 180, which is already a whole number of
    // M-cycles and is reported as 180.
    CHECK(withOne == 180);
    // A second object in the same tile pays only the flat 6, and the
    // three-dot rebate is spent: 180 + 6 = 186 raw, reported as 188.
    putObject(ppu, 1, 16, 8 + 17, 1, 0x00); // screen x = 17: same tile 2
    CHECK(runLineObjects(ppu) == 188);
    // A third one in a different tile pays 6 plus its own tile term:
    // screen x = 24 is tile 3, 7 - (24 & 7) = 7, so 5 again: 186 + 11 = 197
    // raw, reported as 200.
    putObject(ppu, 2, 16, 8 + 24, 1, 0x00);
    CHECK(runLineObjects(ppu) == 200);
}

TEST_CASE("an object off the left edge is charged for its own tile, not for pixel 0's") {
    // Mooneye acceptance/ppu/intr_2_mode0_timing_sprites measures objects at
    // OAM X = 0-7 - entirely or mostly off the left edge - costing exactly
    // what their own X mod 8 says, and an object at OAM X = 0 and one at
    // OAM X = 8 paying two separate tile terms even though both are fetched
    // on the dot pixelX_ is still 0. The penalty therefore has to be taken
    // in background coordinates (SCX + OAM X - 8), which goes negative for
    // these, rather than at the clamped pixelX_ they trigger on.
    Ppu ppu;
    setUpObjects(ppu);
    CHECK(runLineObjects(ppu) == 172);

    // OAM X = 5: background x = -3, so (-3) & 7 = 5 and the tile term is
    // 7 - 5 = 2, which is not greater than 2 and so costs nothing. 6 dots,
    // less the line's three, is 3: 175 raw, reported as 176.
    putObject(ppu, 0, 16, 5, 1, 0x00);
    CHECK(runLineObjects(ppu) == 176);
    // Taken at pixelX_ == 0 instead, the term would have been 7 - 0 - 2 = 5
    // and the line would have come to 180.

    // OAM X = 0 (background tile -1) and OAM X = 8 (background tile 0) are
    // two different tiles, so both pay a tile term: 11 + 11 - 3 = 19 on top
    // of 172, which is 191 raw and is reported as 192.
    setUpObjects(ppu);
    putObject(ppu, 0, 16, 0, 1, 0x00);
    putObject(ppu, 1, 16, 8, 1, 0x00);
    CHECK(runLineObjects(ppu) == 192);
}

TEST_CASE("an object at OAM X = 0 always costs eleven dots, unlike the general formula") {
    Ppu ppu;
    setUpObjects(ppu);
    const int plain = runLineObjects(ppu);
    CHECK(plain == 172);
    putObject(ppu, 0, 16, 0, 1, 0x00); // OAM X = 0
    const int withOne = runLineObjects(ppu);
    // Pan Docs: "an OBJ with an OAM X position of 0 always incurs a 11-dot
    // penalty, regardless of SCX". Less the line's three-dot rebate that is
    // 8 raw dots on top of 172, reported as 180. At SCX = 0 the general
    // formula happens to give 11 too (background x = -8, (-8) & 7 = 0,
    // 7 - 0 - 2 = 5, plus the flat 6), so on its own this assertion cannot
    // tell the special case apart from falling through to the general path.
    // The SCX = 3 assertion below is what separates them.
    CHECK(withOne == 180);

    // SCX = 3: background x = 3 - 8 = -5, still tile -1 and still the first
    // object on the line. Raw dots are 172 (plain) + 3 (SCX's discard) + 11
    // (the flat X = 0 cost) - 3 (the line's rebate) = 183, reported as 184.
    //
    // If the X = 0 special case were deleted and execution fell through to
    // the general formula: (-5) & 7 = 3, so the term is 7 - 3 - 2 = 2 and
    // the penalty is 6 + 2 = 8; raw dots would be 172 + 3 + 8 - 3 = 180,
    // reported as 180 -- a different number from the correct 184, so unlike
    // the SCX = 0 case above this assertion does fail if the special case is
    // removed.
    static_cast<void>(ppu.write(0xFF43, 0x03)); // SCX = 3
    const int withOneScrolled = runLineObjects(ppu);
    CHECK(withOneScrolled == 184);
}

TEST_CASE("clearing LCDC bit 1 hides objects") {
    Ppu ppu;
    setUpObjects(ppu);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x00);
    static_cast<void>(ppu.write(0xFF40, 0x91)); // objects off
    runLineObjects(ppu);
    CHECK(ppu.frame()[16] == 1);
}
