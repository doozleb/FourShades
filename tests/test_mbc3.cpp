#include <doctest/doctest.h>

#include "core/Cartridge.h"

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
    cart.write(0x4000, 0x08); // a clock register: nothing built for it yet
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
