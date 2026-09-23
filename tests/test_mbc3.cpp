#include <doctest/doctest.h>

#include "core/Cartridge.h"
#include "core/GameBoy.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace fourshades;

namespace {
// A ROM of `banks` 16 KiB banks whose every bank starts with its own number,
// with a valid header of the given type, ROM-size and RAM-size codes. Same
// shape as tests/test_cartridge.cpp's helper of the same name.
std::vector<u8> makeRom(std::size_t banks, u8 type, u8 romCode, u8 ramCode) {
    std::vector<u8> rom(banks * 0x4000, 0x00);
    for (std::size_t bank = 0; bank < banks; ++bank) {
        rom[bank * 0x4000] = static_cast<u8>(bank);
        rom[bank * 0x4000 + 1] = static_cast<u8>(bank >> 8);
    }
    rom[0x0147] = type;
    rom[0x0148] = romCode;
    rom[0x0149] = ramCode;
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    rom[0x014D] = sum;
    return rom;
}

u16 bankAt(const Cartridge& cart, u16 address) {
    return make16(cart.read(static_cast<u16>(address + 1)), cart.read(address));
}

Cartridge loadOk(std::vector<u8> rom) {
    std::string error;
    auto cart = Cartridge::load(std::move(rom), &error);
    INFO(error);
    REQUIRE(cart.has_value());
    return *cart;
}
} // namespace

TEST_CASE("MBC3 selects a 7-bit bank, and writing 0x7F maps bank 127") {
    Cartridge cart = loadOk(makeRom(128, 0x11, 0x06, 0x00)); // 2 MiB, 128 banks
    CHECK(cart.kind() == Cartridge::Kind::Mbc3);
    cart.write(0x2000, 0x7F);
    CHECK(bankAt(cart, 0x4000) == 127);
}

TEST_CASE("MBC3 only the low 7 bits of the value reach the bank") {
    Cartridge cart = loadOk(makeRom(128, 0x11, 0x06, 0x00));
    cart.write(0x2000, 0xFF); // 0xFF & 0x7F = 0x7F = 127
    CHECK(bankAt(cart, 0x4000) == 127);
}

TEST_CASE("MBC3 bank 0 selects bank 1") {
    Cartridge cart = loadOk(makeRom(128, 0x11, 0x06, 0x00));
    CHECK(bankAt(cart, 0x4000) == 1); // power-on default
    cart.write(0x2000, 0x05);
    CHECK(bankAt(cart, 0x4000) == 5);
    cart.write(0x2000, 0x00);
    CHECK(bankAt(cart, 0x4000) == 1);
}

TEST_CASE("MBC3 leaves 0000-3FFF at bank 0 whatever is selected") {
    Cartridge cart = loadOk(makeRom(128, 0x11, 0x06, 0x00));
    cart.write(0x2000, 0x7F);
    CHECK(bankAt(cart, 0x0000) == 0);
}

TEST_CASE("MBC3 RAM reads 0xFF until enabled") {
    Cartridge cart = loadOk(makeRom(2, 0x13, 0x00, 0x03)); // MBC3+RAM+BATTERY, 4 banks
    CHECK(cart.read(0xA000) == 0xFF);
    cart.write(0xA000, 0x12); // ignored: still disabled
    CHECK(cart.read(0xA000) == 0xFF);
    cart.write(0x0000, 0x0A); // enable
    CHECK(cart.read(0xA000) == 0x00); // the earlier write never landed
    cart.write(0xA000, 0x34);
    CHECK(cart.read(0xA000) == 0x34);
    cart.write(0x0000, 0x00); // disable
    CHECK(cart.read(0xA000) == 0xFF);
}

TEST_CASE("MBC3+RAM+BATTERY banks RAM independently, four banks") {
    Cartridge cart = loadOk(makeRom(2, 0x13, 0x00, 0x03)); // ramCode 0x03: 4 banks
    CHECK(cart.hasBattery());
    cart.write(0x0000, 0x0A); // enable
    for (u8 bank = 0; bank < 4; ++bank) {
        cart.write(0x4000, bank);
        cart.write(0xA000, static_cast<u8>(0x10 + bank));
    }
    for (u8 bank = 0; bank < 4; ++bank) {
        cart.write(0x4000, bank);
        CHECK(cart.read(0xA000) == static_cast<u8>(0x10 + bank));
    }
}

TEST_CASE("MBC3 selecting bank 0x08 makes the RAM window read 0xFF, and a write there does not disturb RAM bank 0") {
    Cartridge cart = loadOk(makeRom(2, 0x13, 0x00, 0x03));
    cart.write(0x0000, 0x0A); // enable
    cart.write(0x4000, 0x00); // RAM bank 0
    cart.write(0xA000, 0x55);
    cart.write(0x4000, 0x08); // a clock register, and this cartridge has no clock
    CHECK(cart.read(0xA000) == 0xFF);
    cart.write(0xA000, 0x99); // ignored: must not land on RAM bank 0
    cart.write(0x4000, 0x00); // back to RAM bank 0
    CHECK(cart.read(0xA000) == 0x55);
}

TEST_CASE("a type 0x0F cartridge (TIMER+BATTERY) loads with a battery and no RAM") {
    Cartridge cart = loadOk(makeRom(2, 0x0F, 0x00, 0x00));
    CHECK(cart.kind() == Cartridge::Kind::Mbc3);
    CHECK(cart.hasBattery());
    CHECK(cart.ram().empty());
}

// --- The clock ------------------------------------------------------------
//
// Types 0x0F and 0x10 carry a real-time clock beside the banking. 4000-5FFF
// values 0x08-0x0C select one of its registers, and the A000-BFFF window
// then reads and writes that register instead of RAM.

namespace {
// A cartridge with a timer, four RAM banks and the RAM-and-timer gate open.
Cartridge timerCart() {
    Cartridge cart = loadOk(makeRom(2, 0x10, 0x00, 0x03)); // MBC3+TIMER+RAM+BATTERY
    cart.write(0x0000, 0x0A);
    return cart;
}

void latch(Cartridge& cart) {
    cart.write(0x6000, 0x00);
    cart.write(0x6000, 0x01);
}
} // namespace

TEST_CASE("MBC3+TIMER selecting 0x08 reads the clock's seconds, not RAM") {
    Cartridge cart = timerCart();
    CHECK(cart.hasTimer());
    cart.write(0x4000, 0x00); // RAM bank 0
    cart.write(0xA000, 0x55);
    cart.write(0x4000, 0x08); // seconds
    cart.write(0xA000, 0x2A);
    latch(cart);
    CHECK(cart.read(0xA000) == 0x2A);
    // And the RAM byte underneath is untouched: the two are separate stores.
    cart.write(0x4000, 0x00);
    CHECK(cart.read(0xA000) == 0x55);
}

TEST_CASE("MBC3+TIMER a write to a clock register reaches the live copy, which only a latch shows") {
    Cartridge cart = timerCart();
    cart.write(0x4000, 0x08);
    cart.write(0xA000, 0x2A);
    CHECK(cart.read(0xA000) == 0x00); // nothing latched yet
    CHECK(cart.rtcState().live.seconds == 0x2A);
    latch(cart);
    CHECK(cart.read(0xA000) == 0x2A);
}

TEST_CASE("MBC3+TIMER a latch shows the time as of the latch, and time passing does not") {
    Cartridge cart = timerCart();
    cart.write(0x4000, 0x08);
    latch(cart);
    CHECK(cart.read(0xA000) == 0x00);
    cart.advanceRtcSeconds(5);
    CHECK(cart.read(0xA000) == 0x00); // still the old latched value
    latch(cart);
    CHECK(cart.read(0xA000) == 0x05);
}

TEST_CASE("MBC3+TIMER any write to 6000-7FFF latches, whatever the value") {
    Cartridge cart = timerCart();
    cart.write(0x4000, 0x08);
    cart.write(0xA000, 0x2A);
    cart.write(0x6000, 0xD6); // never part of a 0x00-then-0x01 sequence
    CHECK(cart.read(0xA000) == 0x2A);

    cart.write(0xA000, 0x33);
    cart.write(0x7FFF, 0x01); // the far end of the window, and no 0x00 before
    CHECK(cart.read(0xA000) == 0x33);

    // And a write elsewhere is not a latch: 4000-5FFF is the bank select.
    cart.write(0xA000, 0x11);
    cart.write(0x4000, 0x08);
    CHECK(cart.read(0xA000) == 0x33);
}

TEST_CASE("MBC3+TIMER all five registers are reachable through the RAM window") {
    Cartridge cart = timerCart();
    const u8 values[] = {0x01, 0x02, 0x03, 0x04, 0x01};
    for (u8 i = 0; i < 5; ++i) {
        cart.write(0x4000, static_cast<u8>(0x08 + i));
        cart.write(0xA000, values[i]);
    }
    latch(cart);
    for (u8 i = 0; i < 5; ++i) {
        cart.write(0x4000, static_cast<u8>(0x08 + i));
        CHECK(cart.read(0xA000) == values[i]);
    }
}

TEST_CASE("MBC3+TIMER the RAM-and-timer gate covers the clock too") {
    Cartridge cart = timerCart();
    cart.write(0x4000, 0x08);
    cart.write(0xA000, 0x2A);
    latch(cart);
    cart.write(0x0000, 0x00); // close the gate
    CHECK(cart.read(0xA000) == 0xFF);
    cart.write(0xA000, 0x11); // ignored
    cart.write(0x0000, 0x0A); // open it again
    CHECK(cart.read(0xA000) == 0x2A);
}

TEST_CASE("MBC3+TIMER a type 0x0F cartridge has a clock but no RAM") {
    Cartridge cart = loadOk(makeRom(2, 0x0F, 0x00, 0x00));
    CHECK(cart.hasTimer());
    CHECK(cart.ram().empty());
    cart.write(0x0000, 0x0A);
    cart.write(0x4000, 0x08);
    cart.write(0xA000, 0x2A);
    latch(cart);
    CHECK(cart.read(0xA000) == 0x2A);
    cart.write(0x4000, 0x00); // a RAM bank on a cartridge with no RAM
    CHECK(cart.read(0xA000) == 0xFF);
}

TEST_CASE("MBC3 without a timer keeps 0x08-0x0C on open bus") {
    Cartridge cart = loadOk(makeRom(2, 0x13, 0x00, 0x03)); // MBC3+RAM+BATTERY
    CHECK_FALSE(cart.hasTimer());
    cart.write(0x0000, 0x0A);
    for (u8 reg = 0x08; reg <= 0x0C; ++reg) {
        cart.write(0x4000, reg);
        CHECK(cart.read(0xA000) == 0xFF);
    }
    // And the clock API says so rather than inventing a clock.
    const RtcState state = cart.rtcState();
    CHECK(state.live.seconds == 0);
    CHECK(state.latched.seconds == 0);
    RtcState wanted;
    wanted.live.seconds = 0x2A;
    CHECK_FALSE(cart.setRtcState(wanted));
    cart.advanceRtcSeconds(5); // a no-op, not a crash
    CHECK(cart.rtcState().live.seconds == 0);
}

TEST_CASE("MBC3+TIMER the clock state round-trips, and survives a cartridge copy") {
    Cartridge cart = timerCart();
    RtcState wanted;
    wanted.live.seconds = 0x11;
    wanted.live.minutes = 0x22;
    wanted.latched.seconds = 0x33;
    CHECK(cart.setRtcState(wanted));
    CHECK(cart.rtcState().live.seconds == 0x11);
    CHECK(cart.rtcState().live.minutes == 0x22);

    Cartridge copy = cart;
    CHECK(copy.rtcState().live.seconds == 0x11);
    cart.write(0x4000, 0x08);
    latch(cart); // moves the original's latched copy on, not the copy's
    CHECK(cart.rtcState().latched.seconds == 0x11);
    CHECK(copy.rtcState().latched.seconds == 0x33);
}

TEST_CASE("MBC3+TIMER 1048576 M-cycles of a running machine advance the clock one second") {
    std::vector<u8> rom = makeRom(2, 0x10, 0x00, 0x03);
    rom[0x0100] = 0x18; // jr -2: sit still and let time pass
    rom[0x0101] = 0xFE;
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    rom[0x014D] = sum;
    std::string error;
    auto cart = Cartridge::load(std::move(rom), &error);
    INFO(error);
    REQUIRE(cart.has_value());
    GameBoy gb(std::move(*cart));
    for (std::uint64_t i = 0; i < 1048576; ++i) {
        gb.idle();
    }
    REQUIRE(gb.cycles() == 1048576);
    CHECK(gb.cartridge().rtcState().live.seconds == 1);
}
