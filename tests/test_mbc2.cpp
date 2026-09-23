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

Cartridge loadOk(std::vector<u8> rom) {
    std::string error;
    auto cart = Cartridge::load(std::move(rom), &error);
    INFO(error);
    REQUIRE(cart.has_value());
    return *cart;
}
} // namespace

TEST_CASE("MBC2 control writes are decoded by bit 8 of the address, not by half") {
    Cartridge cart = loadOk(makeRom(16, 0x05, 0x03, 0x00)); // 256 KiB, 16 banks
    // Bit 8 clear: RAM enable, wherever it lands below 0x4000.
    cart.write(0x0000, 0x0A);
    CHECK(cart.read(0xA000) != 0xFF); // enabled: no longer open bus
    cart.write(0x0000, 0x00); // disable again
    CHECK(cart.read(0xA000) == 0xFF);
    // Bit 8 set: a bank write, even though the address is still below 0x4000.
    cart.write(0x0100, 0x0A);
    CHECK(cart.read(0xA000) == 0xFF); // RAM enable untouched: still disabled
    CHECK(cart.read(0x4000) == 10);   // but the bank changed to 10
}

TEST_CASE("MBC2 selects a ROM bank only when bit 8 of the address is set") {
    Cartridge cart = loadOk(makeRom(16, 0x05, 0x03, 0x00));
    cart.write(0x2100, 0x01); // bit 8 set: bank write
    CHECK(cart.read(0x4000) == 1);
    cart.write(0x2000, 0x01); // bit 8 clear: RAM enable, not a bank write
    CHECK(cart.read(0x4000) == 1); // unchanged
}

TEST_CASE("MBC2 bank 0 selects bank 1") {
    Cartridge cart = loadOk(makeRom(16, 0x05, 0x03, 0x00));
    cart.write(0x2100, 0x05);
    CHECK(cart.read(0x4000) == 5);
    cart.write(0x2100, 0x00);
    CHECK(cart.read(0x4000) == 1);
}

TEST_CASE("MBC2 only the low 4 bits of the value reach the bank") {
    Cartridge cart = loadOk(makeRom(16, 0x05, 0x03, 0x00)); // 16 banks
    cart.write(0x2100, 0x1F); // 0x1F & 0x0F = 0x0F = 15
    CHECK(cart.read(0x4000) == 15);
}

TEST_CASE("MBC2 0000-3FFF stays at bank 0 whatever is selected") {
    Cartridge cart = loadOk(makeRom(16, 0x05, 0x03, 0x00));
    cart.write(0x2100, 0x09);
    CHECK(cart.read(0x0000) == 0);
}

TEST_CASE("MBC2 RAM reads fill the unused upper nibble with 1s") {
    Cartridge cart = loadOk(makeRom(2, 0x06, 0x00, 0x00));
    cart.write(0x0000, 0x0A); // enable
    cart.write(0xA000, 0x05);
    CHECK(cart.read(0xA000) == 0xF5);
    cart.write(0xA000, 0xFF);
    CHECK(cart.read(0xA000) == 0xFF);
    cart.write(0xA000, 0x00);
    CHECK(cart.read(0xA000) == 0xF0);
}

TEST_CASE("MBC2 RAM mirrors every 0x200 bytes across A000-BFFF") {
    Cartridge cart = loadOk(makeRom(2, 0x06, 0x00, 0x00));
    cart.write(0x0000, 0x0A); // enable
    cart.write(0xA000, 0x07);
    CHECK(cart.read(0xA200) == 0xF7);
    CHECK(cart.read(0xA400) == 0xF7);
    CHECK(cart.read(0xBE00) == 0xF7);

    cart.write(0xB1FF, 0x03); // same cell as 0xA1FF
    CHECK(cart.read(0xA1FF) == 0xF3);
}

TEST_CASE("MBC2 RAM needs enabling, and a dropped write does not clobber the old value") {
    Cartridge cart = loadOk(makeRom(2, 0x06, 0x00, 0x00));
    CHECK(cart.read(0xA000) == 0xFF); // disabled: open bus
    cart.write(0x0000, 0x0A); // enable
    cart.write(0xA000, 0x06);
    CHECK(cart.read(0xA000) == 0xF6);
    cart.write(0x0000, 0x00); // disable
    CHECK(cart.read(0xA000) == 0xFF); // open bus while disabled
    cart.write(0xA000, 0x09); // dropped: RAM is disabled
    cart.write(0x0000, 0x0A); // re-enable
    CHECK(cart.read(0xA000) == 0xF6); // the old value, not the dropped 0x09
}

TEST_CASE("MBC2 stores only the low nibble of a RAM write, not the whole byte") {
    // Checked against the raw storage (Cartridge::ram()), not through
    // Cartridge::read(): a read masks the upper nibble back to 1s regardless
    // of what got stored, so only looking at the stored byte itself catches
    // a write path that forgets to drop the upper nibble.
    Cartridge cart = loadOk(makeRom(2, 0x06, 0x00, 0x00));
    cart.write(0x0000, 0x0A); // enable
    cart.write(0xA000, 0xFF);
    CHECK(cart.ram()[0] == 0x0F);
}

TEST_CASE("MBC2+BATTERY gets 512 bytes of RAM regardless of the header's RAM code") {
    Cartridge cart = loadOk(makeRom(2, 0x06, 0x00, 0x00)); // header RAM code 0x00
    CHECK(cart.kind() == Cartridge::Kind::Mbc2);
    CHECK(cart.hasBattery());
    CHECK(cart.ram().size() == 512);
}

TEST_CASE("MBC2 without BATTERY has no battery but still gets its 512 bytes") {
    Cartridge cart = loadOk(makeRom(2, 0x05, 0x00, 0x00));
    CHECK_FALSE(cart.hasBattery());
    CHECK(cart.ram().size() == 512);
}
