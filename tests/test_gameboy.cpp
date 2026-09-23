#include <doctest/doctest.h>

#include "core/GameBoy.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
// STAT = $85), so a case that wants the OAM scan running must idle the
// machine to the top of a frame first. Idling rather than cycling the LCD is
// deliberate: the line the LCD is switched on for has no mode 2 at all.
void idleToTopOfFrame(GameBoy& gb) {
    for (int i = 0; i < Ppu::kLines * 114 + 1; ++i) {
        if (gb.ppu().lineNumber() == 0 && gb.ppu().lineDot() == 0) {
            return;
        }
        gb.idle();
    }
    FAIL("the PPU never reached the top of a frame");
}
// A 32 KiB plain ROM whose header logo area (0x0104-0x0133) holds `logo`, with
// a valid header checksum and no program.
std::unique_ptr<GameBoy> makeGameBoyWithLogo(const std::array<u8, 48>& logo) {
    std::vector<u8> rom(0x8000, 0x00);
    std::copy(logo.begin(), logo.end(), rom.begin() + 0x0104);
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    rom[0x014D] = sum;
    auto cart = Cartridge::load(std::move(rom), nullptr);
    REQUIRE(cart.has_value());
    return std::make_unique<GameBoy>(std::move(*cart));
}

// The logo Nintendo's own cartridges carry, as Pan Docs lists it under "The
// Cartridge Header". Nothing in src/ holds these bytes; they are here only so
// that a test can say what a cartridge's header contains.
constexpr std::array<u8, 48> kNintendoLogo{
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
    0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
    0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

// A nibble with each of its four bits doubled: bit 3 becomes the byte's top
// two bits and bit 0 its bottom two. Written out rather than computed, so this
// does not repeat the arithmetic under test. E.g. 0xE = 1110 -> 11111100.
constexpr std::array<u8, 16> kDoubledNibble{
    0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
    0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF,
};

// The VRAM a DMG boot ROM leaves behind for a cartridge carrying `logo`,
// 0x8000-0x9FFF. The boot ROM clears all of VRAM first, so every address this
// does not name is zero.
std::array<u8, 0x2000> expectedPostBootVram(const std::array<u8, 48>& logo) {
    std::array<u8, 0x2000> vram{};
    // Two header bytes per tile, four tile rows per header byte, one byte
    // written every other address: only bit plane 0 is touched, so the logo is
    // drawn in colour 1 - which BGP = 0xFC shades black.
    for (std::size_t i = 0; i < logo.size(); ++i) {
        const u8 high = kDoubledNibble[logo[i] >> 4];
        const u8 low = kDoubledNibble[logo[i] & 0x0F];
        const std::size_t base = 0x0010 + 8 * i; // 0x8010 onwards
        vram[base + 0] = high;
        vram[base + 2] = high;
        vram[base + 4] = low;
        vram[base + 6] = low;
    }
    // Tile 0x19, the (R) glyph: the boot ROM's own data, which the cartridge
    // has no say in, written the same way - one byte every other address.
    constexpr std::array<u8, 8> kTrademark{0x3C, 0x42, 0xB9, 0xA5, 0xB9, 0xA5, 0x42, 0x3C};
    for (std::size_t row = 0; row < kTrademark.size(); ++row) {
        vram[0x0190 + 2 * row] = kTrademark[row];
    }
    // The background map: tiles 0x01-0x0C across 0x9904-0x990F, 0x0D-0x18
    // across 0x9924-0x992F, and the glyph at 0x9910, beside the top row.
    for (int i = 0; i < 12; ++i) {
        vram[static_cast<std::size_t>(0x1904 + i)] = static_cast<u8>(0x01 + i);
        vram[static_cast<std::size_t>(0x1924 + i)] = static_cast<u8>(0x0D + i);
    }
    vram[0x1910] = 0x19;
    return vram;
}

// The first address at which the machine's VRAM differs from `expected`, or -1.
int firstVramDifference(const GameBoy& gb, const std::array<u8, 0x2000>& expected) {
    for (int i = 0; i < 0x2000; ++i) {
        if (gb.peek(static_cast<u16>(0x8000 + i)) != expected[static_cast<std::size_t>(i)]) {
            return 0x8000 + i;
        }
    }
    return -1;
}
} // namespace

TEST_CASE("power-on state matches a DMG after its boot ROM") {
    auto gb = makeGameBoy({0x00});
    const Registers& r = gb->cpu().regs;
    CHECK(r.a == 0x01);
    CHECK(r.f() == 0xB0); // the header checksum byte is non-zero here
    CHECK(r.bc() == 0x0013);
    CHECK(r.de() == 0x00D8);
    CHECK(r.hl() == 0x014D);
    CHECK(r.sp == 0xFFFE);
    CHECK(r.pc == 0x0100);
    CHECK(gb->peek(0xFF00) == 0xCF);
    CHECK(gb->peek(0xFF02) == 0x7E);
    CHECK(gb->peek(0xFF04) == 0xAB);
    CHECK(gb->peek(0xFF07) == 0xF8);
    CHECK(gb->peek(0xFF0F) == 0xE1);
    CHECK(gb->peek(0xFF40) == 0x91);
    CHECK(gb->peek(0xFF46) == 0xFF);
    CHECK(gb->peek(0xFF47) == 0xFC);
    CHECK(gb->peek(0xFF01) == 0x00); // SB
    CHECK(gb->peek(0xFF05) == 0x00); // TIMA
    CHECK(gb->peek(0xFF06) == 0x00); // TMA
    CHECK(gb->peek(0xFF44) == 0x00); // LY
    // Pan Docs: STAT = 0x85 (mode 1 with LY already 0, the end of line 153).
    CHECK(gb->peek(0xFF41) == 0x85);
}

// A DMG's boot ROM reads the 48 bytes of the cartridge header's logo area,
// "doubles up" every bit into a 2x2 block of pixels and leaves the result in
// VRAM as tiles, with a background map laying them out, before handing control
// to the cartridge at 0x0100 (Pan Docs, Power Up Sequence: "The monochrome
// boot ROMs read the logo from the header, unpack it into VRAM, and then start
// slowly scrolling it down"). FourShades runs no boot ROM, so it has to arrive
// at that state directly.
TEST_CASE("power-on VRAM holds the logo unpacked from the cartridge header") {
    auto gb = makeGameBoyWithLogo(kNintendoLogo);
    // Worked out by hand from the first two header bytes, as an anchor for the
    // whole-VRAM comparison below. 0xCE is 1100 1110: the high nibble doubles
    // to 0xF0 and fills tile 0x01's rows 0 and 1, the low nibble to 0xFC and
    // fills rows 2 and 3. 0xED then fills rows 4-7 with 0xFC and 0xF3.
    CHECK(gb->peek(0x8010) == 0xF0);
    CHECK(gb->peek(0x8012) == 0xF0);
    CHECK(gb->peek(0x8014) == 0xFC);
    CHECK(gb->peek(0x8016) == 0xFC);
    CHECK(gb->peek(0x8018) == 0xFC);
    CHECK(gb->peek(0x801A) == 0xFC);
    CHECK(gb->peek(0x801C) == 0xF3);
    CHECK(gb->peek(0x801E) == 0xF3);
    CHECK(gb->peek(0x8011) == 0x00); // bit plane 1 is left alone
    CHECK(gb->peek(0x8000) == 0x00); // tile 0x00 is not part of the logo
    CHECK(gb->peek(0x818E) == 0xFC); // the last row of the 24th logo tile
    CHECK(gb->peek(0x8190) == 0x3C); // the (R) glyph, tile 0x19
    CHECK(gb->peek(0x81A0) == 0x00); // nothing past it
    CHECK(gb->peek(0x9904) == 0x01); // the map's top row
    CHECK(gb->peek(0x990F) == 0x0C);
    CHECK(gb->peek(0x9910) == 0x19);
    CHECK(gb->peek(0x9924) == 0x0D); // and its second row
    CHECK(gb->peek(0x992F) == 0x18);
    CHECK(gb->peek(0x9800) == 0x00);

    const int differs = firstVramDifference(*gb, expectedPostBootVram(kNintendoLogo));
    CHECK_MESSAGE(differs == -1, "first VRAM address that differs: ", differs);
}

// The tiles come from the cartridge in the slot, because that is where the
// hardware reads them: the boot ROM unpacks the header's bytes into VRAM and
// only afterwards compares them against its own copy. A cartridge whose logo
// area holds something else therefore leaves that something else in VRAM.
TEST_CASE("the logo in VRAM follows the cartridge, byte for byte") {
    std::array<u8, 48> logo = kNintendoLogo;
    logo[0] = 0x24;  // 0x0C then 0x30, rather than 0xF0 then 0xFC
    logo[23] = 0xFF; // 0xFF four times over
    logo[47] = 0x00; // nothing at all
    auto gb = makeGameBoyWithLogo(logo);

    CHECK(gb->peek(0x8010) == 0x0C);
    CHECK(gb->peek(0x8014) == 0x30);
    CHECK(gb->peek(0x818C) == 0x00);
    CHECK(gb->peek(0x818E) == 0x00);
    const int differs = firstVramDifference(*gb, expectedPostBootVram(logo));
    CHECK_MESSAGE(differs == -1, "first VRAM address that differs: ", differs);

    // The glyph and the map are the boot ROM's, not the cartridge's, so they do
    // not move with the header.
    auto standard = makeGameBoyWithLogo(kNintendoLogo);
    CHECK(gb->peek(0x8190) == standard->peek(0x8190));
    CHECK(gb->peek(0x9904) == standard->peek(0x9904));
    CHECK(gb->peek(0x992F) == standard->peek(0x992F));
    CHECK(gb->peek(0x8010) != standard->peek(0x8010));
}

// Pan Docs gives DIV = $AB at PC = $0100 but not its phase. Hardware (DMG
// A-C, MGB) sees DIV turn $AC in the 14th M-cycle from the fetch at $0100,
// so before that fetch the system counter is $ABC8.
TEST_CASE("DIV first increments in the 14th M-cycle after power-on") {
    auto gb = makeGameBoy({0x00});
    for (int i = 0; i < 13; ++i) {
        gb->idle();
    }
    CHECK(gb->peek(0xFF04) == 0xAB);
    gb->idle();
    CHECK(gb->peek(0xFF04) == 0xAC);
}

TEST_CASE("every bus call advances the machine by one M-cycle") {
    auto gb = makeGameBoy({0x00, 0x00});
    gb->step(); // NOP: one fetch
    CHECK(gb->cycles() == 1);
    gb->step();
    CHECK(gb->cycles() == 2);
}

TEST_CASE("memory regions route correctly, and echo RAM mirrors WRAM") {
    auto gb = makeGameBoy({0x00});
    gb->write(0xFF40, 0x11); // LCD off, so VRAM and OAM are reachable
    gb->write(0xC123, 0x42);
    CHECK(gb->peek(0xE123) == 0x42);
    gb->write(0xE200, 0x17);
    CHECK(gb->peek(0xC200) == 0x17);
    gb->write(0x8000, 0x11);
    CHECK(gb->peek(0x8000) == 0x11);
    gb->write(0xFE00, 0x22);
    CHECK(gb->peek(0xFE00) == 0x22);
    gb->write(0xFF80, 0x33);
    CHECK(gb->peek(0xFF80) == 0x33);
    CHECK(gb->peek(0xFEA0) == 0x00); // unusable area on DMG
    CHECK(gb->peek(0xFF03) == 0xFF); // unmapped I/O
    CHECK(gb->peek(0x0100) == 0x00); // ROM
    gb->write(0x0100, 0x99);         // ROM writes go to the (absent) MBC
    CHECK(gb->peek(0x0100) == 0x00);
}

TEST_CASE("the CPU sees the PPU's blocking, and FEA0-FEFF follows OAM") {
    auto gb = makeGameBoy({0x00});
    idleToTopOfFrame(*gb); // line 0, dot 0: the OAM scan has begun
    // Each read below is itself an M-cycle, so these land inside mode 2:
    // OAM blocked, VRAM readable.
    CHECK(gb->read(0xFE00) == 0xFF);
    CHECK(gb->peek(0xFE00) == 0x00);  // the debugger view is never blocked
    CHECK(gb->read(0xFEA0) == 0xFF);  // the unusable area follows OAM
    gb->write(0xFF40, 0x11);          // LCD off
    CHECK(gb->read(0xFE00) == 0x00);
    CHECK(gb->read(0xFEA0) == 0x00);
    gb->write(0xFE00, 0x42);
    CHECK(gb->peek(0xFE00) == 0x42);
}

TEST_CASE("IF and IE drive the CPU's interrupt lines") {
    auto gb = makeGameBoy({0x00});
    CHECK(gb->pendingInterrupts() == 0x00); // IF=E1 but IE=0
    gb->write(0xFFFF, 0x01);
    CHECK(gb->pendingInterrupts() == 0x01);
    gb->acknowledgeInterrupt(0);
    CHECK(gb->peek(0xFF0F) == 0xE0);
    gb->write(0xFF0F, 0xFF);
    CHECK(gb->peek(0xFF0F) == 0xFF);
    CHECK(gb->pendingInterrupts() == 0x01);
}

TEST_CASE("a timer overflow raises IF bit 2 through the bus") {
    auto gb = makeGameBoy({0x00});
    gb->write(0xFF0F, 0x00);
    gb->write(0xFF06, 0x00);
    gb->write(0xFF05, 0xFF);
    gb->write(0xFF07, 0x05); // every 4 M-cycles
    for (int i = 0; i < 8; ++i) {
        gb->idle();
    }
    CHECK((gb->peek(0xFF0F) & 0x04) != 0);
}

TEST_CASE("the serial port records bytes and interrupts when done") {
    auto gb = makeGameBoy({0x00});
    gb->write(0xFF0F, 0x00);
    gb->write(0xFF01, 'P');
    gb->write(0xFF02, 0x81);
    for (int i = 0; i < 1100; ++i) {
        gb->idle();
    }
    REQUIRE(gb->serialOutput().size() == 1);
    CHECK(gb->serialOutput()[0] == 'P');
    CHECK((gb->peek(0xFF0F) & 0x08) != 0);
}

TEST_CASE("OAM DMA from WRAM copies 160 bytes and blocks WRAM, ROM and OAM, but VRAM and HRAM stay readable") {
    auto gb = makeGameBoy({0x00});
    gb->write(0xFF40, 0x11); // LCD off: the DMA's own bus blocking is what's under test here
    for (int i = 0; i < 0xA0; ++i) {
        gb->write(static_cast<u16>(0xC000 + i), static_cast<u8>(i + 1));
    }
    gb->write(0xFF80, 0x5A);
    gb->write(0xFF46, 0xC0); // source 0xC000: the external bus
    CHECK(gb->peek(0xFF46) == 0xC0);
    gb->idle(); // start-up cycle
    gb->idle(); // first byte copied
    CHECK(gb->read(0xC000) == 0xFF); // WRAM: blocked (external bus)
    CHECK(gb->read(0x0000) == 0xFF); // ROM: blocked (external bus)
    CHECK(gb->read(0xFE00) == 0xFF); // OAM: always blocked
    CHECK(gb->read(0x8000) == 0x00); // VRAM: readable (video bus, not in use)
    CHECK(gb->read(0xFF80) == 0x5A); // HRAM: readable
    for (int i = 0; i < 170; ++i) {
        gb->idle();
    }
    CHECK(gb->read(0xC000) == 0x01); // no longer blocked
    CHECK(gb->peek(0xFE00) == 0x01);
    CHECK(gb->peek(0xFE9F) == 0xA0);
}

TEST_CASE("OAM DMA from VRAM blocks VRAM and OAM, but ROM and WRAM stay readable") {
    auto gb = makeGameBoy({0x00});
    gb->write(0xFF40, 0x11); // LCD off: the DMA's own bus blocking is what's under test here
    gb->write(0xC000, 0x01);
    gb->write(0xFF46, 0x80); // source 0x8000: the video bus
    gb->idle(); // start-up cycle
    gb->idle(); // first byte copied
    CHECK(gb->read(0x8000) == 0xFF); // VRAM: blocked (video bus)
    CHECK(gb->read(0xFE00) == 0xFF); // OAM: always blocked
    CHECK(gb->read(0x0000) == 0x00); // ROM: readable (external bus, not in use)
    CHECK(gb->read(0xC000) == 0x01); // WRAM: readable (external bus, not in use)
    for (int i = 0; i < 170; ++i) {
        gb->idle();
    }
    CHECK(gb->read(0x8000) == 0x00); // no longer blocked
}

// Pan Docs: the transfer takes 160 M-cycles and starts after the M-cycle that
// wrote FF46; the CPU is locked out for all 160 of them, the last included.
TEST_CASE("OAM DMA blocks the CPU through the M-cycle that copies the last byte") {
    auto gb = makeGameBoy({0x00});
    gb->write(0xC000, 0x01);
    gb->write(0xFF46, 0xC0); // M-cycle W
    for (int i = 0; i < 160; ++i) {
        gb->idle(); // W+1 (start-up) .. W+160
    }
    CHECK(gb->read(0xC000) == 0xFF); // W+161: byte 159 is being copied
    CHECK(gb->read(0xC000) == 0x01); // W+162: the transfer is over
}

TEST_CASE("a restarted OAM DMA keeps the CPU blocked through the new start-up cycle") {
    auto gb = makeGameBoy({0x00});
    gb->write(0xC000, 0x01);
    gb->write(0xFF46, 0xC0);
    for (int i = 0; i < 9; ++i) {
        gb->idle();
    }
    gb->write(0xFF46, 0xC0);         // restart, M-cycle W
    CHECK(gb->read(0xC000) == 0xFF); // W+1: the old transfer is still copying
    for (int i = 0; i < 159; ++i) {
        gb->idle(); // W+2 .. W+160
    }
    CHECK(gb->read(0xC000) == 0xFF); // W+161: the new transfer's last byte
    CHECK(gb->read(0xC000) == 0x01); // W+162
}

TEST_CASE("the CPU runs a program through the memory map") {
    // LD A,0x12 ; LD (0xC000),A ; LD HL,0xC000 ; INC (HL) ; HALT
    auto gb = makeGameBoy({0x3E, 0x12, 0xEA, 0x00, 0xC0, 0x21, 0x00, 0xC0, 0x34, 0x76});
    for (int i = 0; i < 5; ++i) {
        gb->step();
    }
    CHECK(gb->peek(0xC000) == 0x13);
    CHECK(gb->cpu().state() == Cpu::State::Halted);
}

TEST_CASE("a halted CPU is woken by the VBlank interrupt") {
    // EI ; HALT ; (handler at 0x40 is NOP bytes of a zero-filled ROM)
    auto gb = makeGameBoy({0xFB, 0x76, 0x00});
    gb->write(0xFF0F, 0x00);
    gb->write(0xFFFF, 0x01);
    gb->step(); // EI
    gb->step(); // HALT
    CHECK(gb->cpu().state() == Cpu::State::Halted);
    for (int i = 0; i < 154 * 114 && gb->cpu().regs.pc != 0x0040; ++i) {
        gb->step();
    }
    CHECK(gb->cpu().regs.pc == 0x0040);
}

namespace {
// Every pixel shade 0: what a DMG panel reads when nothing drives it (Pan
// Docs, LCDC: "When the display is disabled the screen is blank, which on DMG
// is displayed as a white 'whiter' than color #0").
bool blank(const GameBoy& gb) {
    const auto& frame = gb.ppu().frame();
    return std::all_of(frame.begin(), frame.end(), [](u8 pixel) { return pixel == 0; });
}

// A frame's worth of M-cycles with the CPU left where it is, so that a case
// can get a picture on the screen without running the instruction it is about
// to step into.
void idleFrame(GameBoy& gb) {
    for (int i = 0; i < Ppu::kLines * 114; ++i) {
        gb.idle();
    }
}

// The same, with the CPU spending them: running, halted or stopped.
void stepFrames(GameBoy& gb, int frames) {
    for (int i = 0; i < frames * Ppu::kLines * 114; ++i) {
        gb.step();
    }
}

// LD A,0x0F / LDH (47),A: BGP maps colour 0 to shade 3, so a zero-filled
// background is drawn dark and a blank screen is distinguishable from a drawn
// one. Then LD A,0x20 / LDH (00),A, which selects the d-pad group so a press
// can end STOP, and STOP itself.
const std::vector<u8> kDarkThenStop = {0x3E, 0x0F, 0xE0, 0x47, 0x3E, 0x20,
                                       0xE0, 0x00, 0x10, 0x00, 0x3C};
} // namespace

// Pan Docs, Reducing Power Consumption: STOP "is intended to switch the Game
// Boy into VERY low power standby mode", and on CGB "leaving the LCD enabled
// when invoking STOP will result in a black screen". The whole machine's
// clock stops, so the PPU stops with it and the panel goes blank. See
// docs/known-divergences.md, "STOP stops the PPU and blanks the LCD".
TEST_CASE("STOP blanks the screen and stops the PPU") {
    auto gb = makeGameBoy(kDarkThenStop);
    for (int i = 0; i < 4; ++i) {
        gb->step(); // the two loads and the two that select the d-pad
    }
    idleFrame(*gb);
    REQUIRE_FALSE(blank(*gb));

    gb->step(); // STOP
    REQUIRE(gb->cpu().state() == Cpu::State::Stopped);
    const std::uint64_t frames = gb->ppu().frameCount();
    gb->step();
    CHECK(blank(*gb));

    const int line = gb->ppu().lineNumber();
    const int dot = gb->ppu().lineDot();
    stepFrames(*gb, 2);
    for (int i = 0; i < 37; ++i) {
        gb->step(); // and a bit: not a whole number of frames, so a PPU still
                    // running would land on another line and dot
    }
    REQUIRE(gb->cpu().state() == Cpu::State::Stopped);
    CHECK(gb->ppu().lineNumber() == line); // the PPU is not advancing either
    CHECK(gb->ppu().lineDot() == dot);
    CHECK(gb->ppu().frameCount() == frames);
    CHECK(blank(*gb));
}

TEST_CASE("leaving STOP puts the picture back") {
    auto gb = makeGameBoy(kDarkThenStop);
    for (int i = 0; i < 4; ++i) {
        gb->step();
    }
    idleFrame(*gb); // something has to be on the screen for it to come back
    REQUIRE_FALSE(blank(*gb));
    gb->step(); // STOP
    REQUIRE(gb->cpu().state() == Cpu::State::Stopped);
    gb->step();
    REQUIRE(blank(*gb));

    gb->setButtons(button::Down);
    gb->step();
    REQUIRE(gb->cpu().state() == Cpu::State::Running);
    stepFrames(*gb, 1); // the INC A after STOP, then the ROM's NOPs
    CHECK_FALSE(blank(*gb));
}

// HALT is not STOP: the clock keeps running, so the PPU keeps drawing.
TEST_CASE("HALT leaves the screen alone") {
    // LD A,0x0F / LDH (47),A / HALT, with IE clear so nothing wakes it.
    auto gb = makeGameBoy({0x3E, 0x0F, 0xE0, 0x47, 0x76});
    gb->step();
    gb->step();
    idleFrame(*gb);
    REQUIRE_FALSE(blank(*gb));

    gb->step(); // HALT
    REQUIRE(gb->cpu().state() == Cpu::State::Halted);
    const std::uint64_t frames = gb->ppu().frameCount();
    stepFrames(*gb, 1);
    REQUIRE(gb->cpu().state() == Cpu::State::Halted);
    CHECK(gb->ppu().frameCount() > frames);
    CHECK_FALSE(blank(*gb));
}
