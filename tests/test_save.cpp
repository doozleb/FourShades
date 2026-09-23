// The save file is the first thing FourShades writes that a person would be
// upset to lose, so these tests are about what happens when the write is
// interrupted, not about the happy path.
#include <doctest/doctest.h>

#include "app/Save.h"
#include "core/Cartridge.h"

#include <cstdint>
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

// ---------------------------------------------------------------------------
// The clock on disk. A cartridge with an MBC3 real-time clock writes 48 more
// bytes after its RAM, and reading them back is what makes a save reloaded on
// Friday know that four days have passed since Monday.
// ---------------------------------------------------------------------------

namespace {

using fourshades::RtcRegisters;
using fourshades::RtcState;

RtcRegisters regs(u8 seconds, u8 minutes, u8 hours, u8 dayLow, u8 dayHigh) {
    RtcRegisters out;
    out.seconds = seconds;
    out.minutes = minutes;
    out.hours = hours;
    out.dayLow = dayLow;
    out.dayHigh = dayHigh;
    return out;
}

// The footer built by hand, from the format's own description rather than
// from the code under test: ten little-endian u32s, each holding one
// register's byte value, then a little-endian u64 of the Unix second. If
// Save.cpp ever writes these as plain bytes, or swaps the two halves, these
// bytes stop matching.
void appendU32(std::vector<u8>& bytes, u8 value) {
    bytes.push_back(value);
    bytes.push_back(0x00);
    bytes.push_back(0x00);
    bytes.push_back(0x00);
}

void appendRegs(std::vector<u8>& bytes, const RtcRegisters& r) {
    appendU32(bytes, r.seconds);
    appendU32(bytes, r.minutes);
    appendU32(bytes, r.hours);
    appendU32(bytes, r.dayLow);
    appendU32(bytes, r.dayHigh);
}

std::vector<u8> makeFooter(const RtcState& state, std::int64_t stamp) {
    std::vector<u8> bytes;
    appendRegs(bytes, state.live);
    appendRegs(bytes, state.latched);
    const std::uint64_t unixSeconds = static_cast<std::uint64_t>(stamp);
    for (int i = 0; i < 8; ++i) {
        bytes.push_back(static_cast<u8>((unixSeconds >> (8 * i)) & 0xFF));
    }
    REQUIRE(bytes.size() == 48);
    return bytes;
}

// A save file made by hand: cartridge RAM, then the footer after it.
std::vector<u8> makeSaveImage(const std::vector<u8>& ram, const RtcState& state, std::int64_t stamp) {
    std::vector<u8> bytes = ram;
    const std::vector<u8> footer = makeFooter(state, stamp);
    bytes.insert(bytes.end(), footer.begin(), footer.end());
    return bytes;
}

void checkRegs(const RtcRegisters& got, const RtcRegisters& want) {
    CHECK(static_cast<int>(got.seconds) == static_cast<int>(want.seconds));
    CHECK(static_cast<int>(got.minutes) == static_cast<int>(want.minutes));
    CHECK(static_cast<int>(got.hours) == static_cast<int>(want.hours));
    CHECK(static_cast<int>(got.dayLow) == static_cast<int>(want.dayLow));
    CHECK(static_cast<int>(got.dayHigh) == static_cast<int>(want.dayHigh));
}

// A fixed point in time, so none of this depends on when the suite runs.
constexpr std::int64_t kSaved = 1700000000;

} // namespace

TEST_CASE("a cartridge with a clock saves its RAM and a 48-byte footer after it") {
    const auto dir = freshDir("rtc-footer");
    const auto save = app::savePathFor(dir / "game.gb");

    Cartridge cart = loadCart(0x10, 0x02); // MBC3+TIMER+RAM+BATTERY, 8 KiB
    REQUIRE(cart.hasBattery());
    REQUIRE(cart.hasTimer());
    REQUIRE(cart.ram().size() == 0x2000);
    fillRam(cart, 0x3C);

    RtcState state;
    state.live = regs(1, 2, 3, 4, 0x01);
    state.latched = regs(30, 40, 20, 200, 0x00);
    REQUIRE(cart.setRtcState(state));

    const app::SaveResult wrote = app::writeSave(cart, save, kSaved);
    CHECK(wrote.status == SaveStatus::Written);
    CHECK(wrote.bytes == 0x2000 + app::kRtcFooterBytes);
    REQUIRE(std::filesystem::exists(save));
    CHECK(std::filesystem::file_size(save) == 0x2000 + app::kRtcFooterBytes);

    const std::vector<u8> onDisk = readAll(save);
    REQUIRE(onDisk.size() == 0x2000 + app::kRtcFooterBytes);
    // The RAM is still first and still raw, so an emulator that ignores the
    // footer reads the same save it always did.
    CHECK(std::vector<u8>(onDisk.begin(), onDisk.begin() + 0x2000) == cart.ram());
    CHECK(std::vector<u8>(onDisk.begin() + 0x2000, onDisk.end()) == makeFooter(state, kSaved));

    // Saving again writes that save's timestamp, not the first one's.
    REQUIRE(app::writeSave(cart, save, kSaved + 4242).status == SaveStatus::Written);
    const std::vector<u8> again = readAll(save);
    REQUIRE(again.size() == onDisk.size());
    CHECK(std::vector<u8>(again.begin() + 0x2000, again.end()) == makeFooter(state, kSaved + 4242));
}

TEST_CASE("a clock cartridge with no RAM at all saves the footer by itself") {
    const auto dir = freshDir("rtc-noram");
    const auto save = app::savePathFor(dir / "game.gb");

    Cartridge cart = loadCart(0x0F, 0x00); // MBC3+TIMER+BATTERY: a clock, no RAM
    REQUIRE(cart.hasBattery());
    REQUIRE(cart.hasTimer());
    REQUIRE(cart.ram().empty());

    RtcState state;
    state.live = regs(11, 22, 13, 77, 0x01);
    state.latched = regs(12, 23, 14, 78, 0x01);
    REQUIRE(cart.setRtcState(state));

    const app::SaveResult wrote = app::writeSave(cart, save, kSaved);
    CHECK(wrote.status == SaveStatus::Written);
    CHECK(wrote.bytes == app::kRtcFooterBytes);
    REQUIRE(std::filesystem::exists(save));
    CHECK(std::filesystem::file_size(save) == app::kRtcFooterBytes);
    CHECK(readAll(save) == makeFooter(state, kSaved));

    Cartridge fresh = loadCart(0x0F, 0x00);
    const app::LoadResult loaded = app::loadSave(fresh, save, kSaved);
    CHECK(loaded.status == LoadStatus::Loaded);
    CHECK(app::mayWriteSave(loaded.status));
    checkRegs(fresh.rtcState().live, state.live);
    checkRegs(fresh.rtcState().latched, state.latched);
}

TEST_CASE("the clock's live and latched registers survive a save and a load, separately") {
    const auto dir = freshDir("rtc-roundtrip");
    const auto save = app::savePathFor(dir / "game.gb");

    Cartridge cart = loadCart(0x10, 0x02);
    fillRam(cart, 0x91);
    RtcState state;
    state.live = regs(59, 58, 23, 0xFF, 0x01);
    state.latched = regs(5, 6, 7, 0x08, 0x00);
    REQUIRE(cart.setRtcState(state));
    REQUIRE(app::writeSave(cart, save, kSaved).status == SaveStatus::Written);

    // A fresh cartridge's clock is zeroed; nothing but that file can put
    // these numbers back.
    Cartridge fresh = loadCart(0x10, 0x02);
    checkRegs(fresh.rtcState().live, regs(0, 0, 0, 0, 0));
    const app::LoadResult loaded = app::loadSave(fresh, save, kSaved);
    CHECK(loaded.status == LoadStatus::Loaded);
    CHECK(fresh.ram() == cart.ram());
    // The same timestamp, so no time is caught up and the two copies stay
    // distinguishable from each other.
    checkRegs(fresh.rtcState().live, state.live);
    checkRegs(fresh.rtcState().latched, state.latched);
}

TEST_CASE("a clock catches up on the time the machine spent switched off") {
    const auto dir = freshDir("rtc-catchup");
    const auto save = app::savePathFor(dir / "game.gb");

    RtcState state;
    state.live = regs(0, 0, 0, 0, 0x00);
    state.latched = regs(7, 8, 9, 10, 0x01);
    writeAll(save, makeSaveImage(std::vector<u8>(0x2000, 0x5E), state, kSaved));

    Cartridge cart = loadCart(0x10, 0x02);
    const app::LoadResult loaded = app::loadSave(cart, save, kSaved + 3600);
    CHECK(loaded.status == LoadStatus::Loaded);
    // The RAM comes from in front of the footer, not out of it.
    CHECK(cart.ram() == std::vector<u8>(0x2000, 0x5E));
    // One hour later, to the second.
    checkRegs(cart.rtcState().live, regs(0, 0, 1, 0, 0x00));
    // The latch is what the program reads, and only the program moves it: a
    // catch-up never touches it.
    checkRegs(cart.rtcState().latched, state.latched);

    // Four days, two hours, two minutes and five seconds: the reason any of
    // this exists.
    Cartridge later = loadCart(0x10, 0x02);
    REQUIRE(app::loadSave(later, save, kSaved + 4 * 86400 + 7325).status == LoadStatus::Loaded);
    checkRegs(later.rtcState().live, regs(5, 2, 2, 4, 0x00));
}

TEST_CASE("a halted clock does not catch up") {
    const auto dir = freshDir("rtc-halted");
    const auto save = app::savePathFor(dir / "game.gb");

    RtcState state;
    // Bit 6 of dayHigh is the halt bit: the game stopped the clock, so no
    // amount of wall time may move it.
    state.live = regs(5, 0, 0, 0, 0x40);
    state.latched = regs(5, 0, 0, 0, 0x40);
    writeAll(save, makeFooter(state, kSaved));

    Cartridge cart = loadCart(0x0F, 0x00);
    CHECK(app::loadSave(cart, save, kSaved + 3600).status == LoadStatus::Loaded);
    checkRegs(cart.rtcState().live, regs(5, 0, 0, 0, 0x40));
}

TEST_CASE("a save stamped in the future advances the clock by nothing") {
    const auto dir = freshDir("rtc-future");
    const auto save = app::savePathFor(dir / "game.gb");

    RtcState state;
    state.live = regs(5, 0, 0, 0, 0x00);
    state.latched = regs(5, 0, 0, 0, 0x00);
    writeAll(save, makeFooter(state, kSaved));

    // The host clock moved backwards: a time zone, a correction, a flat CMOS
    // battery. The elapsed time is negative, and a negative number put
    // through an unsigned conversion would age the clock by 584 billion
    // years, so it must not be.
    Cartridge cart = loadCart(0x0F, 0x00);
    CHECK(app::loadSave(cart, save, kSaved - 100).status == LoadStatus::Loaded);
    checkRegs(cart.rtcState().live, regs(5, 0, 0, 0, 0x00));

    // Exactly zero elapsed is the same: nothing moves.
    Cartridge same = loadCart(0x0F, 0x00);
    CHECK(app::loadSave(same, save, kSaved).status == LoadStatus::Loaded);
    checkRegs(same.rtcState().live, regs(5, 0, 0, 0, 0x00));
}

TEST_CASE("only RAM, or RAM and the whole footer, is accepted; the rest is left on disk") {
    const auto dir = freshDir("rtc-lengths");
    const auto save = app::savePathFor(dir / "game.gb");

    Cartridge cart = loadCart(0x10, 0x02);
    const std::vector<u8> zeroed = cart.ram();
    REQUIRE(zeroed.size() == 0x2000);

    RtcState state;
    state.live = regs(1, 1, 1, 1, 0x00);
    state.latched = regs(2, 2, 2, 2, 0x00);
    const std::vector<u8> whole = makeSaveImage(std::vector<u8>(0x2000, 0x21), state, kSaved);
    REQUIRE(whole.size() == 0x2000 + 48);

    // One byte short of the footer: a half-written file from somewhere else,
    // not a save this cartridge can use.
    const std::vector<u8> shortByOne(whole.begin(), whole.end() - 1);
    writeAll(save, shortByOne);
    const app::LoadResult tooShort = app::loadSave(cart, save, kSaved);
    CHECK(tooShort.status == LoadStatus::Refused);
    CHECK_FALSE(tooShort.message.empty());
    CHECK_FALSE(app::mayWriteSave(tooShort.status));
    CHECK(cart.ram() == zeroed);
    checkRegs(cart.rtcState().live, regs(0, 0, 0, 0, 0));
    CHECK(readAll(save) == shortByOne);

    // One byte too many.
    std::vector<u8> longByOne = whole;
    longByOne.push_back(0x00);
    writeAll(save, longByOne);
    CHECK(app::loadSave(cart, save, kSaved).status == LoadStatus::Refused);
    CHECK(cart.ram() == zeroed);
    CHECK(readAll(save) == longByOne);

    // RAM plus a footer on a cartridge with no clock: there is nowhere to put
    // it, so it is not this cartridge's save.
    Cartridge noClock = loadCart(0x13, 0x02); // MBC3+RAM+BATTERY, no timer
    REQUIRE(noClock.hasBattery());
    REQUIRE_FALSE(noClock.hasTimer());
    REQUIRE(noClock.ram().size() == 0x2000);
    writeAll(save, whole);
    const app::LoadResult noTimer = app::loadSave(noClock, save, kSaved);
    CHECK(noTimer.status == LoadStatus::Refused);
    CHECK_FALSE(noTimer.message.empty());
    CHECK(noClock.ram() == zeroed);
    CHECK(readAll(save) == whole);

    // The two lengths that are accepted: RAM alone, from an emulator that
    // keeps no clock, and RAM with the footer.
    writeAll(save, std::vector<u8>(0x2000, 0x33));
    CHECK(app::loadSave(cart, save, kSaved).status == LoadStatus::Loaded);
    CHECK(cart.ram() == std::vector<u8>(0x2000, 0x33));
    writeAll(save, whole);
    CHECK(app::loadSave(cart, save, kSaved).status == LoadStatus::Loaded);
    CHECK(cart.ram() == std::vector<u8>(0x2000, 0x21));
    checkRegs(cart.rtcState().live, state.live);
}

TEST_CASE("a cartridge with no clock still saves exactly its RAM") {
    const auto dir = freshDir("rtc-noclock");
    const auto save = app::savePathFor(dir / "game.gb");

    Cartridge cart = loadCart(0x13, 0x02); // MBC3+RAM+BATTERY
    REQUIRE_FALSE(cart.hasTimer());
    fillRam(cart, 0x64);

    const app::SaveResult wrote = app::writeSave(cart, save, kSaved);
    CHECK(wrote.status == SaveStatus::Written);
    CHECK(wrote.bytes == 0x2000);
    CHECK(std::filesystem::file_size(save) == 0x2000);
    CHECK(readAll(save) == cart.ram());
}
