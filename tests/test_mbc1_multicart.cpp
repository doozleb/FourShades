#include <doctest/doctest.h>

#include "core/Cartridge.h"

#include <array>
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

// The 48-byte Nintendo logo a header carries at 0x0104-0x0133, exactly as
// Pan Docs' cartridge header page gives it.
constexpr std::array<u8, 48> kLogo = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
    0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
    0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

void putLogo(std::vector<u8>& rom, std::size_t offset) {
    std::copy(kLogo.begin(), kLogo.end(), rom.begin() + static_cast<std::ptrdiff_t>(offset));
}

Cartridge loadOk(std::vector<u8> rom) {
    std::string error;
    auto cart = Cartridge::load(std::move(rom), &error);
    INFO(error);
    REQUIRE(cart.has_value());
    return *cart;
}

// The bank number is stamped as its own value in the bank's first byte, so
// reading it back through the address translation says which bank is
// actually mapped. Every bank number this file writes fits in one byte and
// stays under the cartridge's own bank count, so no masking distorts it.
u8 bankAt(const Cartridge& cart, u16 address) {
    return cart.read(address);
}
} // namespace

TEST_CASE("MBC1 multicart: four logos at the quarter boundaries is detected") {
    auto rom = makeRom(64, 0x01, 0x05, 0x00); // 1 MiB
    putLogo(rom, 0x00104);
    putLogo(rom, 0x40104);
    putLogo(rom, 0x80104);
    putLogo(rom, 0xC0104);
    Cartridge cart = loadOk(rom);
    cart.write(0x2000, 0x0F);
    cart.write(0x4000, 0x03);
    CHECK(bankAt(cart, 0x4000) == 0x3F);
}

TEST_CASE("MBC1 multicart: three logos is still detected") {
    auto rom = makeRom(64, 0x01, 0x05, 0x00); // 1 MiB
    putLogo(rom, 0x00104);
    putLogo(rom, 0x40104);
    putLogo(rom, 0x80104);
    // 0xC0104 left blank: one cartridge's outer menu leaves a slot empty.
    Cartridge cart = loadOk(rom);
    cart.write(0x2000, 0x0F);
    cart.write(0x4000, 0x03);
    CHECK(bankAt(cart, 0x4000) == 0x3F);
}

TEST_CASE("MBC1 multicart: two logos is not detected, and banks as ordinary MBC1") {
    auto rom = makeRom(64, 0x01, 0x05, 0x00); // 1 MiB
    putLogo(rom, 0x00104);
    putLogo(rom, 0x40104);
    Cartridge cart = loadOk(rom);
    cart.write(0x2000, 0x1F); // ordinary MBC1: all 5 bits count
    CHECK(bankAt(cart, 0x4000) == 0x1F);
}

TEST_CASE("MBC1 multicart: a 512 KiB ROM with both its boundaries logoed is not detected") {
    auto rom = makeRom(32, 0x01, 0x04, 0x00); // 512 KiB; only two boundaries exist
    putLogo(rom, 0x00104);
    putLogo(rom, 0x40104);
    Cartridge cart = loadOk(rom);
    cart.write(0x2000, 0x1F); // ordinary MBC1: all 5 bits count
    CHECK(bankAt(cart, 0x4000) == 0x1F);
}

TEST_CASE("MBC1 multicart: a 2 MiB ROM with all four boundaries logoed is still not "
          "detected — the size condition applies regardless of the logo count") {
    auto rom = makeRom(128, 0x01, 0x06, 0x00); // 2 MiB; large enough to hold all four
                                                // boundary offsets in bounds
    putLogo(rom, 0x00104);
    putLogo(rom, 0x40104);
    putLogo(rom, 0x80104);
    putLogo(rom, 0xC0104);
    Cartridge cart = loadOk(rom);
    cart.write(0x2000, 0x1F); // ordinary MBC1: all 5 bits count
    CHECK(bankAt(cart, 0x4000) == 0x1F);
}

TEST_CASE("MBC1 multicart: the same writes bank differently on a detected and an undetected cartridge") {
    auto detected = makeRom(64, 0x01, 0x05, 0x00); // 1 MiB
    putLogo(detected, 0x00104);
    putLogo(detected, 0x40104);
    putLogo(detected, 0x80104);
    putLogo(detected, 0xC0104);
    Cartridge detectedCart = loadOk(detected);
    detectedCart.write(0x2000, 0x0F);
    detectedCart.write(0x4000, 0x03);
    CHECK(bankAt(detectedCart, 0x4000) == 0x3F);

    // 2 MiB: the wrong size for detection regardless of what its bytes hold,
    // and large enough (128 banks) that the undetected, 5-bit-wide register's
    // bank number is not itself masked down by the cartridge's own bank count.
    auto undetected = makeRom(128, 0x01, 0x06, 0x00);
    Cartridge undetectedCart = loadOk(undetected);
    undetectedCart.write(0x2000, 0x0F);
    undetectedCart.write(0x4000, 0x03);
    CHECK(bankAt(undetectedCart, 0x4000) == 0x6F);
}

TEST_CASE("MBC1 multicart: the fifth bit of the low register is not wired") {
    auto rom = makeRom(64, 0x01, 0x05, 0x00); // 1 MiB
    putLogo(rom, 0x00104);
    putLogo(rom, 0x40104);
    putLogo(rom, 0x80104);
    putLogo(rom, 0xC0104);
    Cartridge cart = loadOk(rom);
    cart.write(0x2000, 0x1F); // bit 4 (0x10) is dropped, not just masked to bank 1
    CHECK(bankAt(cart, 0x4000) == 0x0F);
}

TEST_CASE("MBC1 multicart: the zero substitution reads the full 5-bit register, "
          "not the 4-bit value the bit drop leaves behind") {
    // Mooneye's multicart_rom_8Mb.gb (verified against a genuine MBC1B1 chip)
    // measures this directly: writing 0x10 leaves the 5-bit register at 16,
    // which is not zero, so the "0 acts as 1" substitution does not fire —
    // only afterwards does the multicart wiring drop bit 4, leaving a bank
    // contribution of 0, not 1. See docs/known-divergences.md.
    auto rom = makeRom(64, 0x01, 0x05, 0x00); // 1 MiB
    putLogo(rom, 0x00104);
    putLogo(rom, 0x40104);
    putLogo(rom, 0x80104);
    putLogo(rom, 0xC0104);
    Cartridge cart = loadOk(rom);
    cart.write(0x2000, 0x10);
    CHECK(bankAt(cart, 0x4000) == 0x00);
}
