#include "app/Save.h"

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

LoadResult loadSave(fourshades::Cartridge& cart, const std::filesystem::path& savePath) {
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
    const std::size_t expected = cart.ram().size();
    if (size != expected) {
        return {LoadStatus::Refused,
                savePath.string() + " is " + std::to_string(size) + " bytes, but this cartridge has " +
                    std::to_string(expected) + " bytes of RAM -- refusing to load it, and leaving it alone"};
    }
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
    if (!cart.setRam(bytes)) {
        return {LoadStatus::Refused, savePath.string() + " does not fit this cartridge's RAM"};
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
    if (!cart.hasBattery()) {
        return {SaveStatus::NoBattery, 0, {}};
    }
    const std::vector<u8>& ram = cart.ram();
    std::string error;
    if (!writeTempFile(savePath, ram, &error)) {
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
    return {SaveStatus::Written, ram.size(), {}};
}

} // namespace app
