// The save file is the first thing FourShades writes that a person would be
// upset to lose, so these tests are about what happens when the write is
// interrupted, not about the happy path.
#include <doctest/doctest.h>

#include "app/Save.h"
#include "core/Cartridge.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using app::LoadStatus;
using app::SaveStatus;
using fourshades::Cartridge;
using fourshades::u16;
using fourshades::u8;

namespace {

// A 32 KiB ROM with a valid header. `type` 0x03 is MBC1+RAM+BATTERY, 0x02 is
// MBC1+RAM with no battery, 0x00 is a plain ROM with no RAM at all.
std::vector<u8> makeRom(u8 type, u8 ramCode) {
    std::vector<u8> rom(0x8000, 0x00);
    rom[0x0147] = type;
    rom[0x0148] = 0x00;
    rom[0x0149] = ramCode;
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    rom[0x014D] = sum;
    return rom;
}

Cartridge loadCart(u8 type, u8 ramCode) {
    std::string error;
    auto cart = Cartridge::load(makeRom(type, ramCode), &error);
    INFO(error);
    REQUIRE(cart.has_value());
    return *cart;
}

// A directory of its own per test, so one test's leftovers cannot make
// another pass.
std::filesystem::path freshDir(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / ("fourshades-save-" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::vector<u8> readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::vector<u8>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeAll(const std::filesystem::path& path, const std::vector<u8>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    REQUIRE(std::filesystem::file_size(path) == bytes.size());
}

// Fills cartridge RAM through the cartridge's own bus writes, so the test
// exercises the same path a game does: enable RAM, write, disable.
void fillRam(Cartridge& cart, u8 value) {
    cart.write(0x0000, 0x0A);
    for (u16 a = 0xA000; a < 0xC000; ++a) {
        cart.write(a, value);
    }
    cart.write(0x0000, 0x00);
}

} // namespace

TEST_CASE("the save sits beside the ROM with a .sav extension") {
    CHECK(app::savePathFor("C:/games/tetris.gb") == std::filesystem::path("C:/games/tetris.sav"));
    CHECK(app::savePathFor("C:/games/tetris.gbc") == std::filesystem::path("C:/games/tetris.sav"));
    // No extension, and a dot in a directory name: the extension is replaced
    // on the file name only, never on the directory.
    CHECK(app::savePathFor("C:/my.games/tetris") == std::filesystem::path("C:/my.games/tetris.sav"));
    // The scratch file is in the same directory -- a rename across
    // directories is not the atomic operation this relies on.
    CHECK(app::tempPathFor("C:/games/tetris.sav").parent_path() ==
          std::filesystem::path("C:/games/tetris.sav").parent_path());
    CHECK(app::tempPathFor("C:/games/tetris.sav") != std::filesystem::path("C:/games/tetris.sav"));
}

TEST_CASE("RAM written, saved and loaded into a fresh cartridge comes back identical") {
    const auto dir = freshDir("roundtrip");
    const auto save = app::savePathFor(dir / "game.gb");

    Cartridge cart = loadCart(0x03, 0x02);
    REQUIRE(cart.hasBattery());
    fillRam(cart, 0x5A);
    // A distinguishable byte, so a save of all-one-value cannot pass by luck.
    cart.write(0x0000, 0x0A);
    cart.write(0xA123, 0xC7);
    cart.write(0x0000, 0x00);
    const std::vector<u8> expected = cart.ram();
    REQUIRE(expected.size() == 0x2000);

    const app::SaveResult wrote = app::writeSave(cart, save);
    CHECK(wrote.status == SaveStatus::Written);
    CHECK(wrote.bytes == expected.size());
    REQUIRE(std::filesystem::exists(save));
    CHECK(std::filesystem::file_size(save) == expected.size());
    // Raw RAM, in order, so other emulators can read it too.
    CHECK(readAll(save) == expected);

    Cartridge fresh = loadCart(0x03, 0x02);
    REQUIRE(fresh.ram() != expected);
    const app::LoadResult loaded = app::loadSave(fresh, save);
    CHECK(loaded.status == LoadStatus::Loaded);
    CHECK(fresh.ram() == expected);
    CHECK(app::mayWriteSave(loaded.status));
    // And through the bus, not just the accessor.
    fresh.write(0x0000, 0x0A);
    CHECK(fresh.read(0xA123) == 0xC7);
    CHECK(fresh.read(0xA000) == 0x5A);

    // The scratch file does not outlive a successful save.
    CHECK_FALSE(std::filesystem::exists(app::tempPathFor(save)));
}

TEST_CASE("a save interrupted before the rename leaves the previous save intact") {
    const auto dir = freshDir("interrupted");
    const auto save = app::savePathFor(dir / "game.gb");
    const auto temp = app::tempPathFor(save);

    Cartridge first = loadCart(0x03, 0x02);
    fillRam(first, 0x11);
    const std::vector<u8> original = first.ram();
    REQUIRE(app::writeSave(first, save).status == SaveStatus::Written);

    // The kill happens here: the scratch file is written and the rename never
    // runs. This is writeSave's own first half, not an imitation of it.
    Cartridge second = loadCart(0x03, 0x02);
    fillRam(second, 0x22);
    const std::vector<u8> replacement = second.ram();
    REQUIRE(original != replacement);
    std::string error;
    INFO(error);
    REQUIRE(app::writeTempFile(save, replacement, &error));

    // The interrupted write really did happen -- otherwise this test would
    // pass against an implementation that writes nothing at all.
    REQUIRE(std::filesystem::exists(temp));
    CHECK(readAll(temp) == replacement);

    // And the player's save is untouched, and still loads.
    REQUIRE(std::filesystem::exists(save));
    CHECK(readAll(save) == original);
    Cartridge restored = loadCart(0x03, 0x02);
    CHECK(app::loadSave(restored, save).status == LoadStatus::Loaded);
    CHECK(restored.ram() == original);

    // Completing the second half is what makes the new save visible, and it
    // clears the scratch file away.
    CHECK(app::commitTempFile(save, &error));
    CHECK(readAll(save) == replacement);
    CHECK_FALSE(std::filesystem::exists(temp));
}

TEST_CASE("a cartridge with no battery writes no file at all") {
    const auto dir = freshDir("nobattery");

    // MBC1 with RAM but no battery: it has RAM to write, and must still not.
    Cartridge volatileRam = loadCart(0x02, 0x02);
    CHECK_FALSE(volatileRam.hasBattery());
    CHECK(volatileRam.ram().size() == 0x2000);
    fillRam(volatileRam, 0x77);
    const auto savePath = app::savePathFor(dir / "volatile.gb");
    CHECK(app::writeSave(volatileRam, savePath).status == SaveStatus::NoBattery);
    CHECK_FALSE(std::filesystem::exists(savePath));
    CHECK_FALSE(std::filesystem::exists(app::tempPathFor(savePath)));
    // Not an empty file, and not any file: the directory is still empty.
    CHECK(std::filesystem::is_empty(dir));

    // A plain ROM, with no RAM either.
    Cartridge plain = loadCart(0x00, 0x00);
    CHECK_FALSE(plain.hasBattery());
    const auto plainPath = app::savePathFor(dir / "plain.gb");
    CHECK(app::writeSave(plain, plainPath).status == SaveStatus::NoBattery);
    CHECK(std::filesystem::is_empty(dir));

    // And nothing is loaded for one either, so a stray file beside the ROM
    // cannot get into a cartridge that has no battery to hold it.
    writeAll(plainPath, std::vector<u8>(0x2000, 0x99));
    CHECK(app::loadSave(plain, plainPath).status == LoadStatus::NoBattery);
    CHECK(plain.ram().empty());
    CHECK_FALSE(app::mayWriteSave(LoadStatus::NoBattery));
}

TEST_CASE("a truncated or oversized save is refused, and is left on disk") {
    const auto dir = freshDir("corrupt");
    const auto save = app::savePathFor(dir / "game.gb");

    Cartridge cart = loadCart(0x03, 0x02);
    const std::vector<u8> zeroed = cart.ram();
    REQUIRE(zeroed.size() == 0x2000);

    // Truncated: the shape an interrupted write in place would leave.
    const std::vector<u8> truncated(0x1000, 0x42);
    writeAll(save, truncated);
    const app::LoadResult shortLoad = app::loadSave(cart, save);
    CHECK(shortLoad.status == LoadStatus::Refused);
    CHECK_FALSE(shortLoad.message.empty());
    // Nothing of it got in: not a prefix, not padded, not garbage.
    CHECK(cart.ram() == zeroed);
    // The file is still there, byte for byte, for the player to rescue.
    CHECK(readAll(save) == truncated);
    // And this session must not overwrite it on exit.
    CHECK_FALSE(app::mayWriteSave(shortLoad.status));

    // Oversized: a save from a bigger cartridge, which must not be cropped.
    const std::vector<u8> oversized(0x8000, 0x43);
    writeAll(save, oversized);
    const app::LoadResult longLoad = app::loadSave(cart, save);
    CHECK(longLoad.status == LoadStatus::Refused);
    CHECK(cart.ram() == zeroed);
    CHECK(readAll(save) == oversized);

    // Empty: a zero-byte file is refused too, not read as an empty save.
    writeAll(save, {});
    CHECK(app::loadSave(cart, save).status == LoadStatus::Refused);
    CHECK(cart.ram() == zeroed);

    // A save of exactly the right size is the only thing accepted.
    writeAll(save, std::vector<u8>(0x2000, 0x44));
    CHECK(app::loadSave(cart, save).status == LoadStatus::Loaded);
    CHECK(cart.ram() == std::vector<u8>(0x2000, 0x44));
}

TEST_CASE("a missing save is a new game, not a failure") {
    const auto dir = freshDir("missing");
    Cartridge cart = loadCart(0x03, 0x02);
    const app::LoadResult result = app::loadSave(cart, app::savePathFor(dir / "game.gb"));
    CHECK(result.status == LoadStatus::NoFile);
    CHECK(cart.ram() == std::vector<u8>(0x2000, 0x00));
    CHECK(app::mayWriteSave(result.status));
}

TEST_CASE("setRam refuses a size the cartridge cannot hold") {
    Cartridge cart = loadCart(0x03, 0x03); // 4 banks, 32 KiB
    REQUIRE(cart.ram().size() == 0x8000);
    CHECK_FALSE(cart.setRam(std::vector<u8>(0x2000, 0x01)));
    CHECK(cart.ram() == std::vector<u8>(0x8000, 0x00));
    CHECK(cart.setRam(std::vector<u8>(0x8000, 0x01)));
    CHECK(cart.ram() == std::vector<u8>(0x8000, 0x01));
}
