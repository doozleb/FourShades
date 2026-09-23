#include "app/Save.h"

#include <chrono>
#include <fstream>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace app {

namespace {

using fourshades::u8;

std::string lastErrorText(const char* what) {
    return std::string(what) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}

void setError(std::string* error, std::string text) {
    if (error != nullptr) {
        *error = std::move(text);
    }
}

// A handle that closes itself, so an early return on a failed write cannot
// leave the scratch file open -- and therefore unrenameable.
class Handle {
public:
    explicit Handle(HANDLE handle) : handle_(handle) {}
    ~Handle() { close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return handle_; }
    void close() {
        if (valid()) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE handle_;
};

// --- The clock footer -------------------------------------------------------
//
// See app/Save.h for the layout. Read and written a byte at a time rather than
// memcpy'd over a struct: the file's byte order is little-endian because the
// format says so, not because this machine happens to be.

// The layout is stated once, here, and everything else about it is derived.
// Writing 40 for the stamp's offset was the same number a third time, and a
// third place for a layout change to be missed.
constexpr std::size_t kRegisterBytes = 4; // each register is a u32 on disk
constexpr std::size_t kRegistersPerSet = 5; // seconds, minutes, hours, dayLow, dayHigh
constexpr std::size_t kRegisterSets = 2;    // live, then latched
constexpr std::size_t kStampBytes = 8;      // the Unix second, a little-endian u64
constexpr std::size_t kStampOffset = kRegisterSets * kRegistersPerSet * kRegisterBytes;
static_assert(kStampOffset + kStampBytes == kRtcFooterBytes,
              "the footer is the two register sets followed by the timestamp, and "
              "kRtcFooterBytes in app/Save.h is the sum of the two");

void appendU32(std::vector<u8>& bytes, u8 value) {
    // One byte's worth of register in four bytes of file: three zeroes, every
    // time. The other three bytes exist only because BGB's do.
    bytes.push_back(value);
    bytes.push_back(0x00);
    bytes.push_back(0x00);
    bytes.push_back(0x00);
}

void appendRegisters(std::vector<u8>& bytes, const fourshades::RtcRegisters& regs) {
    appendU32(bytes, regs.seconds);
    appendU32(bytes, regs.minutes);
    appendU32(bytes, regs.hours);
    appendU32(bytes, regs.dayLow);
    appendU32(bytes, regs.dayHigh);
    // Five registers is what kRegistersPerSet says; anything else and
    // kStampOffset above is pointing at the wrong bytes.
    static_assert(kRegistersPerSet == 5, "appendRegisters writes five registers");
}

void appendFooter(std::vector<u8>& bytes, const fourshades::RtcState& state, std::int64_t stamp) {
    appendRegisters(bytes, state.live);
    appendRegisters(bytes, state.latched);
    const std::uint64_t unixSeconds = static_cast<std::uint64_t>(stamp);
    for (std::size_t i = 0; i < kStampBytes; ++i) {
        bytes.push_back(static_cast<u8>((unixSeconds >> (8 * i)) & 0xFF));
    }
}

std::uint32_t readU32(const u8* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t readU64(const u8* p) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | static_cast<std::uint64_t>(p[i]);
    }
    return value;
}

fourshades::RtcRegisters readRegisters(const u8* p) {
    // Only the low byte of each u32 is a register; a file that put something
    // in the other three bytes is a file that disagrees with the chip, and
    // Cartridge::setRtcState narrows what survives even of this.
    fourshades::RtcRegisters regs;
    regs.seconds = static_cast<u8>(readU32(p + 0 * kRegisterBytes) & 0xFF);
    regs.minutes = static_cast<u8>(readU32(p + 1 * kRegisterBytes) & 0xFF);
    regs.hours = static_cast<u8>(readU32(p + 2 * kRegisterBytes) & 0xFF);
    regs.dayLow = static_cast<u8>(readU32(p + 3 * kRegisterBytes) & 0xFF);
    regs.dayHigh = static_cast<u8>(readU32(p + 4 * kRegisterBytes) & 0xFF);
    return regs;
}

fourshades::RtcState readState(const u8* footer) {
    fourshades::RtcState state;
    state.live = readRegisters(footer);
    state.latched = readRegisters(footer + kRegistersPerSet * kRegisterBytes);
    return state;
}

// How long the machine was off, in seconds, and never a negative number
// dressed up as a huge positive one. `then` in the future -- a host clock
// corrected backwards, a different time zone, a dead CMOS battery -- is worth
// nothing elapsed, because the alternative is winding a player's clock back.
// The subtraction is done unsigned on purpose: `now - then` in signed
// arithmetic can overflow on a hand-edited timestamp, while modular unsigned
// arithmetic gives the exact difference once `now > then` is known.
std::uint64_t elapsedSeconds(std::int64_t now, std::int64_t then) {
    if (now <= then) {
        return 0;
    }
    return static_cast<std::uint64_t>(now) - static_cast<std::uint64_t>(then);
}

} // namespace

std::filesystem::path savePathFor(const std::filesystem::path& romPath) {
    std::filesystem::path save = romPath;
    save.replace_extension(".sav");
    return save;
}

std::filesystem::path tempPathFor(const std::filesystem::path& savePath) {
    std::filesystem::path temp = savePath;
    temp += ".tmp";
    return temp;
}

bool mayWriteSave(LoadStatus status) {
    return status == LoadStatus::NoFile || status == LoadStatus::Loaded;
}

std::int64_t hostUnixSeconds() {
    const auto since = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(since).count());
}

LoadResult loadSave(fourshades::Cartridge& cart, const std::filesystem::path& savePath) {
    return loadSave(cart, savePath, hostUnixSeconds());
}

LoadResult loadSave(fourshades::Cartridge& cart, const std::filesystem::path& savePath,
                    std::int64_t nowUnixSeconds) {
    if (!cart.hasBattery()) {
        return {LoadStatus::NoBattery, {}};
    }
    std::error_code ec;
    if (!std::filesystem::exists(savePath, ec) || ec) {
        return {LoadStatus::NoFile, {}};
    }
    const std::uintmax_t size = std::filesystem::file_size(savePath, ec);
    if (ec) {
        return {LoadStatus::Refused, "cannot measure " + savePath.string() + ": " + ec.message()};
    }
    const std::size_t ramBytes = cart.ram().size();
    // The clock's 48 bytes are an alternative length, not an extra one: a
    // cartridge with no clock has nowhere to put them, so RAM + 48 is as
    // wrong for it as RAM + 47 is for anything.
    const bool withFooter = cart.hasTimer() && size == ramBytes + kRtcFooterBytes;
    if (size != ramBytes && !withFooter) {
        const std::string lengths =
            cart.hasTimer() ? (std::to_string(ramBytes) + " or " + std::to_string(ramBytes + kRtcFooterBytes) +
                               " bytes of RAM and clock")
                            : (std::to_string(ramBytes) + " bytes of RAM");
        return {LoadStatus::Refused,
                savePath.string() + " is " + std::to_string(size) + " bytes, but this cartridge has " +
                    lengths + " -- refusing to load it, and leaving it alone"};
    }
    const std::size_t expected = withFooter ? ramBytes + kRtcFooterBytes : ramBytes;
    std::ifstream in(savePath, std::ios::binary);
    if (!in) {
        return {LoadStatus::Refused, "cannot read " + savePath.string()};
    }
    std::vector<u8> bytes(expected);
    if (expected > 0) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(expected));
        if (in.gcount() != static_cast<std::streamsize>(expected)) {
            return {LoadStatus::Refused, "short read from " + savePath.string()};
        }
    }
    // Belt and braces: the core refuses a wrong size too, so a mistake here
    // cannot put a cartridge into a size it does not have.
    if (!cart.setRam(std::vector<u8>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(ramBytes)))) {
        return {LoadStatus::Refused, savePath.string() + " does not fit this cartridge's RAM"};
    }
    if (withFooter) {
        // setRtcState narrows every register to the width the chip really
        // has, so a hand-edited or foreign footer cannot get a bit into this
        // clock that the hardware could not hold.
        cart.setRtcState(readState(bytes.data() + ramBytes));
        const std::uint64_t elapsed =
            elapsedSeconds(nowUnixSeconds, static_cast<std::int64_t>(readU64(bytes.data() + ramBytes + kStampOffset)));
        if (elapsed > 0) {
            // The cartridge's own battery kept this clock running while the
            // machine was off; this is it being told how long that was.
            cart.advanceRtcSeconds(elapsed);
        }
    }
    return {LoadStatus::Loaded, {}};
}

bool writeTempFile(const std::filesystem::path& savePath, const std::vector<u8>& bytes, std::string* error) {
    const std::filesystem::path temp = tempPathFor(savePath);
    Handle file(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) {
        setError(error, lastErrorText(("creating " + temp.string()).c_str()));
        return false;
    }
    const char* data = reinterpret_cast<const char*>(bytes.data());
    std::size_t written = 0;
    while (written < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(
            (bytes.size() - written) > 0x1000000u ? 0x1000000u : (bytes.size() - written));
        DWORD done = 0;
        if (!WriteFile(file.get(), data + written, chunk, &done, nullptr) || done == 0) {
            setError(error, lastErrorText(("writing " + temp.string()).c_str()));
            return false;
        }
        written += done;
    }
    // The point of the scratch file: its bytes must be on the disk before the
    // rename makes it the save, or a power cut after the rename would leave a
    // file that exists and is empty.
    if (!FlushFileBuffers(file.get())) {
        setError(error, lastErrorText(("flushing " + temp.string()).c_str()));
        return false;
    }
    file.close();
    return true;
}

bool commitTempFile(const std::filesystem::path& savePath, std::string* error) {
    const std::filesystem::path temp = tempPathFor(savePath);
    // MOVEFILE_REPLACE_EXISTING is the swap itself; MOVEFILE_WRITE_THROUGH
    // waits for the directory change to reach the disk before returning, so a
    // save reported as written really is one.
    if (!MoveFileExW(temp.c_str(), savePath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        setError(error, lastErrorText(("renaming " + temp.string() + " over " + savePath.string()).c_str()));
        return false;
    }
    return true;
}

SaveResult writeSave(const fourshades::Cartridge& cart, const std::filesystem::path& savePath) {
    return writeSave(cart, savePath, hostUnixSeconds());
}

SaveResult writeSave(const fourshades::Cartridge& cart, const std::filesystem::path& savePath,
                     std::int64_t nowUnixSeconds) {
    if (!cart.hasBattery()) {
        return {SaveStatus::NoBattery, 0, {}};
    }
    // The RAM first, raw and in bank order, then the clock if there is one.
    std::vector<u8> bytes = cart.ram();
    if (cart.hasTimer()) {
        // Type 0x0F has a battery and a clock and no RAM at all, so `bytes`
        // is empty here and the file is the footer by itself. Nothing above
        // short-circuits on an empty RAM, for exactly that reason.
        bytes.reserve(bytes.size() + kRtcFooterBytes);
        const std::size_t beforeFooter = bytes.size();
        appendFooter(bytes, cart.rtcState(), nowUnixSeconds);
        // kRtcFooterBytes is what loadSave accepts and what every other
        // emulator's reader expects; appendFooter is what actually decides the
        // length. If those two ever disagree, the save is malformed, and the
        // place to find that out is here rather than the next time the file is
        // read back. Checked in every build, not just an asserting one: this
        // costs a subtraction, and the failure it catches costs a save.
        if (bytes.size() - beforeFooter != kRtcFooterBytes) {
            return {SaveStatus::Failed, 0,
                    "internal error: the clock footer is " +
                        std::to_string(bytes.size() - beforeFooter) + " bytes, not " +
                        std::to_string(kRtcFooterBytes) + " -- refusing to write a save this "
                        "cartridge could not read back"};
        }
    }
    std::string error;
    if (!writeTempFile(savePath, bytes, &error)) {
        // The previous save, if there was one, has not been touched.
        std::error_code ec;
        std::filesystem::remove(tempPathFor(savePath), ec);
        return {SaveStatus::Failed, 0, error};
    }
    if (!commitTempFile(savePath, &error)) {
        std::error_code ec;
        std::filesystem::remove(tempPathFor(savePath), ec);
        return {SaveStatus::Failed, 0, error};
    }
    return {SaveStatus::Written, bytes.size(), {}};
}

} // namespace app
