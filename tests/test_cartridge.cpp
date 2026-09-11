#include <doctest/doctest.h>

#include "core/Cartridge.h"

#include <string>
#include <vector>

using namespace fourshades;

namespace {
// A ROM of `banks` 16 KiB banks whose every bank starts with its own number,
// with a valid header of the given type, ROM-size and RAM-size codes.
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

Cartridge loadOk(std::vector<u8> rom) {
    std::string error;
    auto cart = Cartridge::load(std::move(rom), &error);
    INFO(error);
    REQUIRE(cart.has_value());
    return *cart;
}
} // namespace

TEST_CASE("a plain 32 KiB ROM maps both halves and ignores writes") {
    Cartridge cart = loadOk(makeRom(2, 0x00, 0x00, 0x00));
    CHECK(cart.kind() == Cartridge::Kind::RomOnly);
    CHECK(cart.headerChecksumOk());
    CHECK(cart.read(0x0000) == 0);
    CHECK(cart.read(0x4000) == 1);
    cart.write(0x2000, 0x05);
    CHECK(cart.read(0x4000) == 1);
    CHECK(cart.read(0xA000) == 0xFF); // no cartridge RAM
}

TEST_CASE("loading rejects tiny images and unsupported controllers") {
    std::string error;
    CHECK_FALSE(Cartridge::load(std::vector<u8>(0x100, 0), &error).has_value());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(Cartridge::load(makeRom(2, 0x13, 0x00, 0x00), &error).has_value()); // MBC3
    CHECK(error.find("0x13") != std::string::npos);
}

TEST_CASE("a bad header checksum is reported but not fatal") {
    auto rom = makeRom(2, 0x00, 0x00, 0x00);
    rom[0x014D] ^= 0xFF;
    Cartridge cart = loadOk(rom);
    CHECK_FALSE(cart.headerChecksumOk());
}

TEST_CASE("MBC1 switches ROM banks, and bank 0 selects bank 1") {
    Cartridge cart = loadOk(makeRom(32, 0x01, 0x04, 0x00)); // 512 KiB
    CHECK(cart.kind() == Cartridge::Kind::Mbc1);
    CHECK(cart.read(0x4000) == 1);
    cart.write(0x2000, 0x05);
    CHECK(cart.read(0x4000) == 5);
    cart.write(0x2000, 0x00);
    CHECK(cart.read(0x4000) == 1);
    cart.write(0x2000, 0xE3); // only the low 5 bits count
    CHECK(cart.read(0x4000) == 3);
    CHECK(cart.read(0x0000) == 0);
}

TEST_CASE("MBC1 masks the bank number to the ROM size") {
    Cartridge cart = loadOk(makeRom(8, 0x01, 0x02, 0x00)); // 128 KiB = 8 banks
    cart.write(0x2000, 0x09);
    CHECK(cart.read(0x4000) == 1); // 9 & 7
    cart.write(0x2000, 0x10); // 0x10 & 7 = 0, and 0x10 isn't 0, so bank 0 appears here
    CHECK(cart.read(0x4000) == 0);
}

TEST_CASE("MBC1 upper bits reach banks above 0x1F, and mode 1 remaps 0000-3FFF") {
    Cartridge cart = loadOk(makeRom(128, 0x01, 0x06, 0x00)); // 2 MiB
    cart.write(0x4000, 0x01);
    cart.write(0x2000, 0x02);
    CHECK(cart.read(0x4000) == 0x22);
    CHECK(cart.read(0x0000) == 0); // mode 0: fixed bank 0
    cart.write(0x6000, 0x01);
    CHECK(cart.read(0x0000) == 0x20); // mode 1: bank 0x20
    cart.write(0x2000, 0x00);
    CHECK(cart.read(0x4000) == 0x21); // 0x20 is unreachable here
}

TEST_CASE("MBC1 RAM needs enabling and banks only in mode 1") {
    Cartridge cart = loadOk(makeRom(4, 0x03, 0x01, 0x03)); // 64 KiB ROM, 32 KiB RAM
    CHECK(cart.read(0xA000) == 0xFF); // disabled
    cart.write(0xA000, 0x12);
    cart.write(0x0000, 0x0A); // enable
    CHECK(cart.read(0xA000) == 0x00); // the earlier write was ignored
    cart.write(0xA000, 0x34);
    CHECK(cart.read(0xA000) == 0x34);
    cart.write(0x4000, 0x02); // RAM bank 2, but mode 0 locks bank 0
    CHECK(cart.read(0xA000) == 0x34);
    cart.write(0x6000, 0x01); // mode 1: bank 2 is fresh
    CHECK(cart.read(0xA000) == 0x00);
    cart.write(0xA000, 0x56);
    cart.write(0x4000, 0x00);
    CHECK(cart.read(0xA000) == 0x34);
    cart.write(0x0000, 0x00); // disable
    CHECK(cart.read(0xA000) == 0xFF);
}

TEST_CASE("an MBC1+RAM header claiming no RAM still gets 8 KiB") {
    Cartridge cart = loadOk(makeRom(2, 0x02, 0x00, 0x00));
    cart.write(0x0000, 0x0A);
    cart.write(0xBFFF, 0x77);
    CHECK(cart.read(0xBFFF) == 0x77);
}
