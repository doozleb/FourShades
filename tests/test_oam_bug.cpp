#include <doctest/doctest.h>

#include "core/GameBoy.h"
#include "core/Ppu.h"

#include <memory>
#include <vector>

using namespace fourshades;

namespace {
// A 32 KiB plain ROM with `program` at 0x0100 and a valid header checksum.
std::unique_ptr<GameBoy> makeGameBoy(std::vector<u8> program, u8 type = 0x00, u8 ramCode = 0x00) {
    std::vector<u8> rom(0x8000, 0x00);
    for (std::size_t i = 0; i < program.size(); ++i) {
        rom[0x0100 + i] = program[i];
    }
    rom[0x0147] = type;
    rom[0x0149] = ramCode;
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    rom[0x014D] = sum;
    auto cart = Cartridge::load(std::move(rom), nullptr);
    REQUIRE(cart.has_value());
    return std::make_unique<GameBoy>(std::move(*cart));
}

// The PPU powers on part-way through line 153, in VBlank (Pan Docs' power-up
// STAT = $85), so a case that wants the OAM scan running from its first
// instruction must idle the machine to the top of a frame first. Idling
// rather than cycling the LCD is deliberate: the line the LCD is switched on
// for has no mode 2 at all, so nothing on it can be corrupted.
void idleToTopOfFrame(GameBoy& gb) {
    for (int i = 0; i < Ppu::kLines * 114 + 1; ++i) {
        if (gb.ppu().lineNumber() == 0 && gb.ppu().lineDot() == 0) {
            return;
        }
        gb.idle();
    }
    FAIL("the PPU never reached the top of a frame");
}

// Fills OAM with a pattern under which the bug's three formulas all give
// different answers on most rows - a run of 0x00..0x9F does not, and would
// let a test pass with the wrong pattern wired in. The LCD is off so the
// writes land.
u8 fillByte(int i) { return static_cast<u8>((i * 73 + 41) ^ (i >> 2)); }

void fillOam(Ppu& ppu) {
    static_cast<void>(ppu.write(0xFF40, 0x11));
    for (int i = 0; i < 0xA0; ++i) {
        ppu.oamWrite(static_cast<u16>(0xFE00 + i), fillByte(i));
    }
    static_cast<void>(ppu.write(0xFF40, 0x91));
}

// Ticks to the M-cycle boundary at which a CPU bus access collides with
// `row`: the boundary the PPU's read of that row begins on. Line 0 is skipped
// because switching the LCD on gives that line no mode 2 at all.
void runToScanRow(Ppu& ppu, int row) {
    for (int i = 0; i < 2000; ++i) {
        if (ppu.lineNumber() >= 1 && ppu.lineNumber() < 144 &&
            ppu.lineDot() == row * 4) {
            return;
        }
        static_cast<void>(ppu.tick());
    }
    FAIL("the PPU never reached the requested OAM scan row");
}

u16 word(const Ppu& ppu, int row, int index) {
    const u16 base = static_cast<u16>(0xFE00 + row * 8 + index * 2);
    return static_cast<u16>(ppu.peekOam(base) | (ppu.peekOam(static_cast<u16>(base + 1)) << 8));
}
} // namespace

TEST_CASE("a write pattern corrupts one row and copies the rest from the previous one") {
    Ppu ppu;
    fillOam(ppu);
    const u16 a = word(ppu, 3, 0);
    const u16 b = word(ppu, 2, 0);
    const u16 c = word(ppu, 2, 2);
    ppu.oamCorrupt(Ppu::Kind::Write, 3);
    CHECK(word(ppu, 3, 0) == static_cast<u16>(((a ^ c) & (b ^ c)) ^ c));
    CHECK(word(ppu, 3, 1) == word(ppu, 2, 1));
    CHECK(word(ppu, 3, 2) == word(ppu, 2, 2));
    CHECK(word(ppu, 3, 3) == word(ppu, 2, 3));
    // On this fill the formula is not the identity, so the row really moved.
    CHECK(word(ppu, 3, 0) != a);
}

TEST_CASE("a read pattern uses the other formula") {
    Ppu ppu;
    fillOam(ppu);
    const u16 a = word(ppu, 9, 0);
    const u16 b = word(ppu, 8, 0);
    const u16 c = word(ppu, 8, 2);
    ppu.oamCorrupt(Ppu::Kind::Read, 9);
    CHECK(word(ppu, 9, 0) == static_cast<u16>(b | (a & c)));
    CHECK(word(ppu, 9, 0) != a);
    CHECK(word(ppu, 9, 0) != static_cast<u16>(((a ^ c) & (b ^ c)) ^ c));
    CHECK(word(ppu, 9, 1) == word(ppu, 8, 1));
}

TEST_CASE("a read and a write in one M-cycle splash the preceding row over both neighbours") {
    Ppu ppu;
    fillOam(ppu);
    const int row = 7;
    const u16 a = word(ppu, row - 2, 0);
    const u16 b = word(ppu, row - 1, 0);
    const u16 c = word(ppu, row, 0);
    const u16 d = word(ppu, row - 1, 2);
    const u16 glitched = static_cast<u16>((b & (a | c | d)) | (a & c & d));
    ppu.oamCorrupt(Ppu::Kind::ReadWrite, row);
    // The preceding row's first word takes the four-input expression, and its
    // whole contents are copied to the rows on either side of it. The read
    // corruption that follows then rewrites the accessed row's first word,
    // with `a` and `b` both now equal to the copied value.
    CHECK(word(ppu, row - 1, 0) == glitched);
    CHECK(word(ppu, row - 2, 0) == glitched);
    CHECK(word(ppu, row - 2, 1) == word(ppu, row - 1, 1));
    const u16 c2 = word(ppu, row - 1, 2);
    CHECK(word(ppu, row, 0) == static_cast<u16>(glitched | (glitched & c2)));
    CHECK(glitched != b); // the expression moved the word on this fill
}

TEST_CASE("the read-write pattern spares the first four rows and the last one") {
    for (const int row : {1, 2, 3, 19}) {
        Ppu ppu;
        fillOam(ppu);
        std::vector<u8> before;
        for (int i = 0; i < 0xA0; ++i) {
            before.push_back(ppu.peekOam(static_cast<u16>(0xFE00 + i)));
        }
        ppu.oamCorrupt(Ppu::Kind::ReadWrite, row);
        // Only the plain read corruption ran, so nothing outside the accessed
        // row moved. The four-input expression would have rewritten the
        // preceding row and copied it two rows back.
        for (int i = 0; i < 0xA0; ++i) {
            if (i / 8 == row) {
                continue;
            }
            CHECK(ppu.peekOam(static_cast<u16>(0xFE00 + i)) == before[static_cast<std::size_t>(i)]);
        }
    }
}

TEST_CASE("row 0 and modes other than 2 are safe") {
    Ppu ppu;
    fillOam(ppu);
    runToScanRow(ppu, 0);
    const u16 before = word(ppu, 0, 0);
    ppu.oamCorruptIfScanning(0xFE00);
    static_cast<void>(ppu.tick());
    CHECK(word(ppu, 0, 0) == before);

    const u16 row3 = word(ppu, 3, 0);
    static_cast<void>(ppu.write(0xFF40, 0x11)); // LCD off
    ppu.oamCorruptIfScanning(0xFE18);
    static_cast<void>(ppu.tick());
    CHECK(word(ppu, 3, 0) == row3);
}

TEST_CASE("the row corrupted is the one the PPU is reading, not the one addressed") {
    // Pan Docs: "The actual read/write address used, or the written value
    // have no effect." The same address corrupts a different row depending on
    // how far into the scan the access lands.
    for (const int row : {9, 11}) {
        Ppu ppu;
        fillOam(ppu);
        runToScanRow(ppu, row);
        const u16 a = word(ppu, row, 0);
        const u16 b = word(ppu, row - 1, 0);
        const u16 c = word(ppu, row - 1, 2);
        const u16 row0 = word(ppu, 0, 0);
        ppu.oamCorruptIfScanning(0xFE00); // addresses row 0; row 0 is untouched
        static_cast<void>(ppu.tick());
        CHECK(word(ppu, row, 0) == static_cast<u16>(((a ^ c) & (b ^ c)) ^ c));
        CHECK(word(ppu, row, 0) != a);
        CHECK(word(ppu, 0, 0) == row0);
    }
}

TEST_CASE("an access after the last row of the scan is too late") {
    Ppu ppu;
    fillOam(ppu);
    runToScanRow(ppu, 19);
    const u16 last = word(ppu, 19, 0);
    ppu.oamCorruptIfScanning(0xFE90);
    static_cast<void>(ppu.tick());
    CHECK(word(ppu, 19, 0) != last); // row 19 is still in range

    const u16 after = word(ppu, 19, 0);
    ppu.oamCorruptIfScanning(0xFE90); // the M-cycle after the scan ended
    static_cast<void>(ppu.tick());
    CHECK(word(ppu, 19, 0) == after);
}

TEST_CASE("the CPU's address unit corrupts OAM through the bus") {
    // The machine is idled to line 0 dot 0, where the OAM scan has just
    // begun, so the scan is running from the first instruction. Four leading
    // NOPs, then LD HL,nn (three M-cycles) and INC HL (two) put the increment
    // unit's internal M-cycle - not the opcode fetch - on the boundary at dot
    // 36, row 9. The following NOP's fetch ends that M-cycle and the
    // corruption lands.
    auto gb = makeGameBoy({0x00, 0x00, 0x00, 0x00, 0x21, 0x20, 0xFE, 0x23, 0x00});
    // NOP x4 ; LD HL,FE20 ; INC HL ; NOP
    idleToTopOfFrame(*gb);
    for (int i = 0; i < 0xA0; ++i) {
        gb->ppu().dmaWriteOam(i, fillByte(i));
    }
    // STAT trails the PPU by an M-cycle and still says mode 1 here, so the
    // scan itself is what this asserts: row 0's read begins on this boundary.
    REQUIRE(gb->ppu().oamScanRow() == 0);
    REQUIRE(gb->ppu().lineDot() == 0);
    const u16 a = word(gb->ppu(), 9, 0);
    const u16 b = word(gb->ppu(), 8, 0);
    const u16 c = word(gb->ppu(), 8, 2);
    const u16 writePattern = static_cast<u16>(((a ^ c) & (b ^ c)) ^ c);
    const u16 readPattern = static_cast<u16>(b | (a & c));
    // The two patterns must differ on this fill, or this test would pin the
    // row and the wiring without pinning which pattern ran.
    REQUIRE(writePattern != readPattern);
    for (int i = 0; i < 4; ++i) {
        gb->step(); // NOP x4
    }
    gb->step(); // LD HL
    gb->step(); // INC HL
    REQUIRE(gb->ppu().lineDot() == 36);
    gb->step(); // NOP
    // The IDU-only access is Kind::Write, so row 9 takes the write pattern;
    // every other row is untouched, which also says the address INC HL
    // happened to hold had no say in which row it was.
    CHECK(word(gb->ppu(), 9, 0) == writePattern);
    for (int i = 0; i < 0xA0; ++i) {
        if (i / 8 == 9) {
            continue;
        }
        CHECK(gb->ppu().peekOam(static_cast<u16>(0xFE00 + i)) == fillByte(i));
    }
}

TEST_CASE("ld a,(hl+) drives the combined read-write pattern through a real instruction") {
    // LD HL,nn is three M-cycles; LD A,(HL+) is two, and its second M-cycle
    // is both the memory read and the IDU's increment of HL, landing on the
    // same boundary as the INC HL case above - dot 20, row 5 - but this time
    // as a genuine read plus an IDU write in one M-cycle, which
    // flushOamCorruption applies as Kind::ReadWrite (Pan Docs' "Read During
    // Increase/Decrease"). This is the only unit test that drives that
    // pattern through a real instruction rather than the primitive.
    auto gb = makeGameBoy({0x21, 0x20, 0xFE, 0x2A, 0x00}); // LD HL,FE20 ; LD A,(HL+) ; NOP
    idleToTopOfFrame(*gb);
    for (int i = 0; i < 0xA0; ++i) {
        gb->ppu().dmaWriteOam(i, fillByte(i));
    }
    REQUIRE(gb->ppu().oamScanRow() == 0);
    REQUIRE(gb->ppu().lineDot() == 0);
    const int row = 5;
    const u16 a = word(gb->ppu(), row - 2, 0);
    const u16 b = word(gb->ppu(), row - 1, 0);
    const u16 c = word(gb->ppu(), row, 0);
    const u16 d = word(gb->ppu(), row - 1, 2);
    const u16 glitched = static_cast<u16>((b & (a | c | d)) | (a & c & d));
    gb->step(); // LD HL
    gb->step(); // LD A,(HL+)
    REQUIRE(gb->ppu().lineDot() == 20);
    gb->step(); // NOP
    // The preceding row's first word takes the four-input expression and its
    // whole contents are copied to the rows on either side of it, then the
    // ordinary read corruption that follows rewrites the accessed row.
    CHECK(word(gb->ppu(), row - 1, 0) == glitched);
    CHECK(word(gb->ppu(), row - 2, 0) == glitched);
    const u16 c2 = word(gb->ppu(), row - 1, 2);
    CHECK(word(gb->ppu(), row, 0) == static_cast<u16>(glitched | (glitched & c2)));
    CHECK(gb->cpu().regs.hl() == 0xFE21); // HL still incremented
    // The PPU's lock refuses the read itself during mode 2 (see the plain
    // read/write case below), so A holds the locked-out value, not the byte
    // that was there - same as any other OAM read while the scan is running.
    CHECK(gb->cpu().regs.a == 0xFF);
}

TEST_CASE("a plain CPU read or write of OAM corrupts it too, each with its own pattern") {
    // The PPU's lock refuses the access itself in mode 2, but the address and
    // the read/write line still reach OAM. LD A,(nn) and LD (nn),A are four
    // M-cycles each, the access being the fourth, so both land at the
    // boundary at dot 16 - the row-4 read. The NOP that follows ends that
    // M-cycle and the corruption lands.
    struct Case {
        u8 opcode;
        bool write;
    };
    for (const Case& c : {Case{0xFA, false}, Case{0xEA, true}}) {
        auto gb = makeGameBoy({c.opcode, 0x20, 0xFE, 0x00});
        idleToTopOfFrame(*gb);
        for (int i = 0; i < 0xA0; ++i) {
            gb->ppu().dmaWriteOam(i, fillByte(i));
        }
        REQUIRE(gb->ppu().oamScanRow() == 0);
        const u16 a = word(gb->ppu(), 4, 0);
        const u16 b = word(gb->ppu(), 3, 0);
        const u16 cc = word(gb->ppu(), 3, 2);
        gb->step();
        REQUIRE(gb->ppu().lineDot() == 16);
        gb->step(); // NOP
        const u16 expected = c.write ? static_cast<u16>(((a ^ cc) & (b ^ cc)) ^ cc)
                                     : static_cast<u16>(b | (a & cc));
        CHECK(word(gb->ppu(), 4, 0) == expected);
        // The two patterns differ on this fill, so each case pins its own.
        CHECK(static_cast<u16>(((a ^ cc) & (b ^ cc)) ^ cc) != static_cast<u16>(b | (a & cc)));
    }
}

TEST_CASE("an address outside FE00-FEFF never corrupts OAM") {
    auto gb = makeGameBoy({0x21, 0x00, 0xC0, 0x23, 0x00}); // LD HL,C000 ; INC HL ; NOP
    idleToTopOfFrame(*gb);
    for (int i = 0; i < 0xA0; ++i) {
        gb->ppu().dmaWriteOam(i, fillByte(i));
    }
    // Without this the scan would not be running at all and the case would
    // pass for the wrong reason: the same sequence at an FE00-FEFF address is
    // what the cases above corrupt with.
    REQUIRE(gb->ppu().oamScanRow() == 0);
    gb->step();
    gb->step();
    gb->step();
    for (int i = 0; i < 0xA0; ++i) {
        CHECK(gb->ppu().peekOam(static_cast<u16>(0xFE00 + i)) == fillByte(i));
    }
}
