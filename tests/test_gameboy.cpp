#include <doctest/doctest.h>

#include "core/GameBoy.h"

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
    // The LCD placeholder can't model that and reads mode 2; recorded in
    // docs/known-divergences.md until the PPU (piece 3) replaces it.
    CHECK(gb->peek(0xFF41) == 0x86);
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
