#include <doctest/doctest.h>

#include "core/Ppu.h"

#include <array>
#include <cstdint>
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

// ---------------------------------------------------------------------------
// An object fetch is a sequence of dots, and LCDC can change under it
//
// Pan Docs, "Pixel FIFO", walks the object fetch dot by dot: the fetcher is
// advanced, then advanced twice more, then "the lower address for the row of
// pixels of the target object tile is now retrieved ... Once the address is
// retrieved this is the last chance for object fetch cancel to occur", and
// "Object fetching may be canceled if LCDC.1 is disabled while the PPU is
// fetching an object from OAM". So the OAM and VRAM the fetch reads are read
// near its end, not when it is triggered, and LCDC can change in between.
namespace {
// Runs one line with register writes landing at chosen line dots, and hands
// back the row that was drawn. Each write lands at the end of the M-cycle that
// ends on its dot, so the first dot that can see it is that dot + 1.
const u8* lineWithWrites(Ppu& ppu, int line, std::vector<std::array<int, 3>> writes) {
    for (const auto& write : writes) {
        while (ppu.lineNumber() != line || ppu.lineDot() != write[0]) { ppu.tick(); }
        static_cast<void>(ppu.write(static_cast<u16>(write[1]), static_cast<u8>(write[2])));
    }
    while (ppu.lineNumber() == line) { ppu.tick(); }
    return &ppu.frame()[static_cast<std::size_t>(line) * Ppu::kWidth];
}
} // namespace

TEST_CASE("clearing LCDC bit 1 part-way through an object fetch cancels it") {
    // The object at screen x = 16 is fetched once pixel 16 is due, on line dot
    // 116, and its fetch runs for eleven dots; it reads its tile on dot 125 and
    // pixel 16 is drawn on dot 127. Bit 1 cleared on dot 116 - visible from 117
    // - and set again on dot 120 is low across the middle of the fetch and high
    // again by the time the pixel is drawn, so the emission-time test cannot
    // hide the object: only a fetch that was cancelled while it ran leaves the
    // background showing.
    Ppu ppu;
    setUpObjects(ppu);
    putObject(ppu, 0, 16, 8 + 16, 1, 0x00); // screen x = 16, colours 3,3,3,3,0,0,0,0
    const u8* row = lineWithWrites(ppu, 1, {{116, 0xFF40, 0x91}, {120, 0xFF40, 0x93}});
    CHECK(row[16] == 1); // background, not the object's colour 3
    CHECK(row[17] == 1);
}

namespace {
// Tile 2 is colour 2 and tile 3 colour 1, so which of them an object drew says
// whether its fetch read LCDC bit 2 as 8x16 (index's low bit dropped: tile 2)
// or as 8x8 (tile 3). OBP0 straight through so the colour is the shade.
void setUpHeightRuler(Ppu& ppu) {
    setUpObjects(ppu);
    for (u16 row = 0; row < 16; row += 2) {
        ppu.vramWrite(static_cast<u16>(0x8020 + row), 0x00); // tile 2: colour 2
        ppu.vramWrite(static_cast<u16>(0x8021 + row), 0xFF);
        ppu.vramWrite(static_cast<u16>(0x8030 + row), 0xFF); // tile 3: colour 1
        ppu.vramWrite(static_cast<u16>(0x8031 + row), 0x00);
    }
    static_cast<void>(ppu.write(0xFF48, 0xE4)); // OBP0 straight through
}
} // namespace

TEST_CASE("an object fetch reads LCDC bit 2 to build each half of its row's address") {
    // The height goes into the fetch's VRAM address, so it is read on the dot
    // the address is built. The object at screen x = 16 is fetched once pixel 16
    // is due, on line dot 116, and its eleven dots put that pixel on dot 127,
    // its low bitplane's address on dot 124 (kObjectDataDots) and its high
    // bitplane's on 126 (kObjectDataHighDots). Bit 2 set on dot 120 is visible
    // from 121, before both of those, so the whole row is the 8x16 one.
    Ppu ppu;
    setUpHeightRuler(ppu);
    putObject(ppu, 0, 16, 8 + 16, 3, 0x00); // screen x = 16, tile 3
    const u8* row = lineWithWrites(ppu, 1, {{120, 0xFF40, 0x97}}); // 8x16 from dot 121
    CHECK(row[16] == 2); // tile 2's colour 2, not tile 3's colour 1
    CHECK(row[23] == 2);
}

TEST_CASE("that fetch's low half is read no later than three dots before its pixel") {
    // The same fetch, and the write moved one M-cycle later: bit 2 set on dot
    // 124 is visible from 125, which is after the low bitplane's address was
    // built on 124 and before the high one's on 126. So the row comes out mixed -
    // tile 3's low half under tile 2's high half, colour 3, where either height
    // alone gives 1 or 2. A low half read two dots before the pixel, which is
    // what Pan Docs' dot count suggests and what this was until two hardware
    // photographs were decoded, would make the whole row the 8x16 one instead.
    // See PixelPipeline::kObjectDataDots.
    Ppu ppu;
    setUpHeightRuler(ppu);
    putObject(ppu, 0, 16, 8 + 16, 3, 0x00); // screen x = 16, tile 3
    const u8* row = lineWithWrites(ppu, 1, {{124, 0xFF40, 0x97}});
    CHECK(row[16] == 3);
    CHECK(row[23] == 3);
}

TEST_CASE("an object off the left edge is read before pixel 0, not at the end of its stall") {
    // An object's fetch is timed to the dot its own leftmost pixel is due, and
    // for an object off the left edge that dot comes before pixel 0's: the eight
    // pixels of the row the fetcher throws away at the top of a line are the
    // line's first eight ticks of the pixel clock, at screen x = -8 to -1. So
    // the object at OAM X = 1, whose leftmost pixel is seven of them early,
    // reads its row seven dots before an object at the screen edge would - on
    // dots 100 and 102, where pixel 0 is not drawn until dot 110.
    //
    // Bit 2 set on dot 104 is visible from 105, after both of those dots, so
    // this object is the 8x8 one: its only visible pixel, the eighth, is tile
    // 3's colour 1. OBP0 shades that 3 so it cannot be confused with the
    // background, which is colour 1 too. See PixelPipeline::objectEarlyDots_ and
    // docs/known-divergences.md, "An object off the left edge is read on the dot
    // its own pixel is due".
    Ppu ppu;
    setUpHeightRuler(ppu);
    static_cast<void>(ppu.write(0xFF48, 0xEC)); // OBP0: colour 1 -> shade 3
    putObject(ppu, 0, 16, 1, 3, 0x00); // screen x = -7, tile 3
    const u8* row = lineWithWrites(ppu, 1, {{104, 0xFF40, 0x97}});
    CHECK(row[0] == 3); // tile 3's colour 1, read before the write landed
}

TEST_CASE("an object at the screen edge is read after pixel 0's own dot") {
    // The same write, the same line, one OAM X further right: the object at
    // OAM X = 8 has its leftmost pixel at screen x = 0, so its fetch is timed to
    // pixel 0 and reads its row on dots 108 and 110 - both after bit 2 became
    // visible on dot 105. This one is the 8x16 object, and it is what tells the
    // two cases apart: Pan Docs gives an OAM X of 0 and one of 8 the same
    // eleven-dot penalty, and they are still not read on the same dots.
    Ppu ppu;
    setUpHeightRuler(ppu);
    static_cast<void>(ppu.write(0xFF48, 0xEC)); // OBP0: colour 1 -> shade 3
    putObject(ppu, 0, 16, 8, 3, 0x00); // screen x = 0, tile 3
    const u8* row = lineWithWrites(ppu, 1, {{104, 0xFF40, 0x97}});
    CHECK(row[0] == 2); // tile 2's colour 2
    CHECK(row[7] == 2);
}

TEST_CASE("an object fetch reads LCDC bit 2 no later than that, and again a dot later") {
    // The other side of the same dot, which needs the M-cycle grid broken: a
    // write lands at the end of an M-cycle and mode 3 starts on a multiple of
    // four, so one object's address dot can only ever be separated from the dot
    // after it by an odd stall somewhere ahead of it. A second object supplies
    // one. The transparent object at screen x = 0 costs eleven dots, so pixel 0
    // is drawn on dot 111 and pixel 1 is due on 112; the object at screen x = 1
    // is fetched there, pays the flat six dots alone (its tile was already
    // counted), and so builds its low bitplane's address on dot 115, its high
    // bitplane's on 117, and draws on dot 118.
    //
    // Bit 2 set on dot 116 is visible from dot 117, so this is the write that
    // falls *between* the two bitplanes: the low half is the 8x8 object's and
    // the high half the 8x16 object's, and the row the object draws is mixed out
    // of both - colour 3, where either height alone gives 1 or 2. A high bitplane
    // built one dot earlier would make the whole row the 8x8 one, colour 1. See
    // docs/known-divergences.md, "An object fetch reads its two bitplanes on two
    // dots, and builds each address the way the hardware does".
    Ppu ppu;
    setUpHeightRuler(ppu);
    for (u16 row = 0; row < 16; ++row) {
        ppu.vramWrite(static_cast<u16>(0x8040 + row), 0x00); // tile 4: transparent
    }
    putObject(ppu, 0, 16, 8, 4, 0x00);     // screen x = 0, draws nothing
    putObject(ppu, 1, 16, 8 + 1, 3, 0x00); // screen x = 1, tile 3
    const u8* row = lineWithWrites(ppu, 1, {{116, 0xFF40, 0x97}});
    CHECK(row[1] == 3); // tile 3's low half over tile 2's high half
    CHECK(row[8] == 3);
}
