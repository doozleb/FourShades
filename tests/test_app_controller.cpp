#include <doctest/doctest.h>

#include "app/AppController.h"
#include "core/Cartridge.h"
#include "core/GameBoy.h"

#include <cstdint>
#include <string>
#include <vector>

using app::AppController;
using app::AppState;
using fourshades::Cartridge;
using fourshades::u16;
using fourshades::u8;

namespace {
// A minimal valid 32 KiB ROM-only image: enough for Cartridge::load to
// accept it. Mirrors tests/test_cartridge.cpp's helper.
std::vector<u8> makeRom(std::size_t banks, u8 type, u8 romCode, u8 ramCode = 0x00) {
    std::vector<u8> rom(banks * 0x4000, 0x00);
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

std::vector<u8> validRom() {
    return makeRom(2, 0x00, 0x00);
}
} // namespace

TEST_CASE("a fresh controller starts waiting, with no machine") {
    AppController controller;
    CHECK(controller.state() == AppState::Waiting);
    CHECK(controller.lastError().empty());
}

TEST_CASE("loading a valid rom moves to running") {
    AppController controller;
    CHECK(controller.loadRom(validRom()));
    CHECK(controller.state() == AppState::Running);
    CHECK(controller.lastError().empty());
}

TEST_CASE("a rom that fails to load reports Cartridge::load's message verbatim and stays waiting") {
    AppController controller;
    std::string expectedError;
    // Same tiny image Cartridge::load rejects in test_cartridge.cpp.
    Cartridge::load(std::vector<u8>(0x100, 0), &expectedError);
    REQUIRE_FALSE(expectedError.empty());

    CHECK_FALSE(controller.loadRom(std::vector<u8>(0x100, 0)));
    CHECK(controller.state() == AppState::Waiting);
    CHECK(controller.lastError() == expectedError);
}

TEST_CASE("a failed load after a running machine returns to waiting, not to running") {
    AppController controller;
    REQUIRE(controller.loadRom(validRom()));
    REQUIRE(controller.state() == AppState::Running);

    CHECK_FALSE(controller.loadRom(std::vector<u8>(0x100, 0)));
    CHECK(controller.state() == AppState::Waiting);
    CHECK_FALSE(controller.lastError().empty());
}

TEST_CASE("dropping a second valid rom replaces the machine with a fresh one") {
    AppController controller;
    REQUIRE(controller.loadRom(validRom()));
    controller.gameBoy().step();
    CHECK(controller.gameBoy().cycles() > 0);

    CHECK(controller.loadRom(makeRom(4, 0x01, 0x00)));
    CHECK(controller.state() == AppState::Running);
    // A fresh machine, not the stepped one: cycles restart from zero.
    CHECK(controller.gameBoy().cycles() == 0);
}

TEST_CASE("reset with no rom loaded does nothing") {
    AppController controller;
    CHECK_FALSE(controller.reset());
    CHECK(controller.state() == AppState::Waiting);
}

TEST_CASE("reset rebuilds the machine and leaves none of the old one behind") {
    AppController controller;
    // 64 KiB MBC1 with a marker byte in bank 2, so the bank register's own
    // state is visible through a read.
    std::vector<u8> rom = makeRom(4, 0x01, 0x01);
    rom[0x8000] = 0xAB; // first byte of ROM bank 2
    REQUIRE(controller.loadRom(rom));

    fourshades::GameBoy& before = controller.gameBoy();
    // Machine state: cycles, a work-RAM byte and the PPU's frame counter.
    const u8 wramAtPowerOn = before.peek(0xC000);
    before.write(0xC000, static_cast<u8>(wramAtPowerOn ^ 0x5A));
    before.cpu().regs.a = 0x5A;
    const std::uint64_t framesBefore = before.ppu().frameCount();
    while (before.ppu().frameCount() == framesBefore) {
        before.step();
    }
    // Cartridge state: select ROM bank 2.
    before.cartridge().write(0x2000, 0x02);
    REQUIRE(before.cartridge().read(0x4000) == 0xAB);

    REQUIRE(controller.reset());
    CHECK(controller.state() == AppState::Running);

    fourshades::GameBoy& after = controller.gameBoy();
    CHECK(after.cycles() == 0);
    CHECK(after.ppu().frameCount() == 0);
    CHECK(after.cpu().regs.pc == 0x0100);
    CHECK(after.peek(0xC000) == wramAtPowerOn);
    // The bank register is in the cartridge, not the machine, and a power
    // cycle clears it too: bank 1 is back at 0x4000.
    CHECK(after.cartridge().read(0x4000) == 0x00);
}

TEST_CASE("reset keeps battery-backed cartridge ram, the way a battery does") {
    AppController controller;
    REQUIRE(controller.loadRom(makeRom(4, 0x03, 0x01, 0x02))); // MBC1+RAM+BATTERY, 8 KiB
    REQUIRE(controller.gameBoy().cartridge().hasBattery());

    fourshades::Cartridge& cart = controller.gameBoy().cartridge();
    cart.write(0x0000, 0x0A); // enable RAM
    cart.write(0xA000, 0x77);
    REQUIRE(cart.read(0xA000) == 0x77);

    REQUIRE(controller.reset());

    fourshades::Cartridge& fresh = controller.gameBoy().cartridge();
    // RAM enable is an MBC register, so it is off again after the reset.
    CHECK(fresh.read(0xA000) == 0xFF);
    fresh.write(0x0000, 0x0A);
    CHECK(fresh.read(0xA000) == 0x77);
}

TEST_CASE("reset clears cartridge ram with no battery behind it") {
    AppController controller;
    REQUIRE(controller.loadRom(makeRom(4, 0x02, 0x01, 0x02))); // MBC1+RAM, no battery
    REQUIRE_FALSE(controller.gameBoy().cartridge().hasBattery());

    fourshades::Cartridge& cart = controller.gameBoy().cartridge();
    cart.write(0x0000, 0x0A);
    cart.write(0xA000, 0x77);
    REQUIRE(cart.read(0xA000) == 0x77);

    REQUIRE(controller.reset());

    fourshades::Cartridge& fresh = controller.gameBoy().cartridge();
    fresh.write(0x0000, 0x0A);
    CHECK(fresh.read(0xA000) == 0x00);
}

TEST_CASE("a rom dropped after a failed load can still be reset") {
    AppController controller;
    REQUIRE(controller.loadRom(validRom()));
    REQUIRE_FALSE(controller.loadRom(std::vector<u8>(0x100, 0)));
    // The failed load took the cartridge with it: there is nothing to reset
    // to, and reset must not resurrect the machine that was running.
    CHECK_FALSE(controller.reset());
    CHECK(controller.state() == AppState::Waiting);
}
