#include <doctest/doctest.h>

#include "app/AppController.h"
#include "core/Cartridge.h"

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
std::vector<u8> makeRom(std::size_t banks, u8 type, u8 romCode) {
    std::vector<u8> rom(banks * 0x4000, 0x00);
    rom[0x0147] = type;
    rom[0x0148] = romCode;
    rom[0x0149] = 0x00;
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
