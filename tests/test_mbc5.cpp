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

TEST_CASE("MBC5 selects a 9-bit bank, with bank 0 genuinely selectable") {
    Cartridge cart = loadOk(makeRom(512, 0x19, 0x08, 0x00)); // 8 MiB, 512 banks
    CHECK(cart.kind() == Cartridge::Kind::Mbc5);
    cart.write(0x2000, 0xFF);
    cart.write(0x3000, 0x01);
    CHECK(bankAt(cart, 0x4000) == 511);

    // Unlike MBC1, writing 0 maps bank 0 at 4000-7FFF, not bank 1.
    cart.write(0x2000, 0x00);
    cart.write(0x3000, 0x00);
    CHECK(bankAt(cart, 0x4000) == 0);
}

TEST_CASE("MBC5's two bank halves move independently") {
    Cartridge cart = loadOk(makeRom(512, 0x19, 0x08, 0x00)); // 8 MiB, 512 banks
    cart.write(0x3000, 0x01);   // bit 8 set
    cart.write(0x2000, 0x05);   // low byte 0x05
    CHECK(bankAt(cart, 0x4000) == 0x105);
    cart.write(0x2000, 0x10);   // only the low byte changes
    CHECK(bankAt(cart, 0x4000) == 0x110); // bit 8 is still set
    cart.write(0x3000, 0x00);   // now clear bit 8, low byte untouched
    CHECK(bankAt(cart, 0x4000) == 0x10);
}

TEST_CASE("MBC5 wraps the bank number to the ROM size") {
    Cartridge cart = loadOk(makeRom(4, 0x19, 0x01, 0x00)); // 64 KiB, 4 banks
    cart.write(0x2000, 0x05); // 5 & 3 = 1
    CHECK(bankAt(cart, 0x4000) == 1);
}

TEST_CASE("MBC5 powers on with bank 1 visible at 4000-7FFF, not bank 0") {
    // Power-on is a different question from the write path above: like every
    // other MBC, the chip comes up showing bank 1 before software ever
    // touches the bank register. Mooneye's mbc5/rom_*.gb (hardware-verified)
    // jump into switchable ROM before writing it at all, and hang against a
    // reset default of bank 0.
    Cartridge cart = loadOk(makeRom(4, 0x19, 0x01, 0x00)); // 64 KiB, 4 banks
    CHECK(bankAt(cart, 0x4000) == 1);
}

TEST_CASE("MBC5 leaves 0000-3FFF at bank 0 whatever is selected") {
    Cartridge cart = loadOk(makeRom(512, 0x19, 0x08, 0x00));
    cart.write(0x2000, 0xFF);
    cart.write(0x3000, 0x01);
    CHECK(bankAt(cart, 0x0000) == 0);
}

TEST_CASE("MBC5 RAM needs enabling") {
    Cartridge cart = loadOk(makeRom(2, 0x1A, 0x00, 0x00)); // MBC5+RAM, 8 KiB
    CHECK(cart.read(0xA000) == 0xFF); // disabled
    cart.write(0x0000, 0x05);         // wrong pattern: must not enable
    CHECK(cart.read(0xA000) == 0xFF);
    cart.write(0xA000, 0x12);         // ignored, still disabled
    cart.write(0x0000, 0x0A);         // enable
    CHECK(cart.read(0xA000) == 0x00); // the earlier write never landed
    cart.write(0xA000, 0x34);
    CHECK(cart.read(0xA000) == 0x34);
    cart.write(0x0000, 0x00);         // disable
    CHECK(cart.read(0xA000) == 0xFF);
}

TEST_CASE("MBC5+RAM+BATTERY banks RAM independently, four banks") {
    Cartridge cart = loadOk(makeRom(2, 0x1B, 0x00, 0x03)); // 4 RAM banks
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

TEST_CASE("MBC5 non-rumble types keep all four bits of the RAM bank register") {
    // 16 RAM banks so bit 3 of the register is a real bank-select bit here,
    // not just something the RAM-size mask would remove anyway.
    Cartridge cart = loadOk(makeRom(2, 0x1B, 0x00, 0x04));
    cart.write(0x0000, 0x0A); // enable
    cart.write(0x4000, 0x09); // bank 9 needs bit 3
    cart.write(0xA000, 0x99);
    cart.write(0x4000, 0x01);
    cart.write(0xA000, 0x11);
    cart.write(0x4000, 0x09); // back to bank 9
    CHECK(cart.read(0xA000) == 0x99);
}

TEST_CASE("MBC5 rumble types mask the motor bit out of the RAM bank number") {
    // 16 declared RAM banks (ramCode 0x04) so masking against the RAM size
    // alone (& 0x0F) would NOT remove bit 3 - only the rumble-specific mask
    // does, which is what this test is checking.
    Cartridge cart = loadOk(makeRom(2, 0x1E, 0x00, 0x04));
    cart.write(0x0000, 0x0A); // enable
    cart.write(0x4000, 0x03); // bank 3, directly
    cart.write(0xA000, 0xAA);
    cart.write(0x4000, 0x0B); // motor bit (bit 3) set: must still land on bank 3
    CHECK(cart.read(0xA000) == 0xAA);
}
