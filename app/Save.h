#pragma once

// Battery-backed cartridge RAM on disk. This is the first thing FourShades
// writes that a person would be upset to lose, so the whole design is about
// the interrupted write: the bytes go to a scratch file beside the target
// first, are flushed to the disk, and only then is that file renamed over the
// save. A rename within a directory is atomic; a write in place is not, and a
// kill halfway through one leaves the player with neither the old save nor a
// whole new one.
//
// The file is raw cartridge RAM in bank order, with no header of our own, so
// other emulators can read a FourShades save and FourShades can read theirs.
// That means the only thing a save can be checked against is its length --
// there is nothing else in it to check -- which is why a wrong length is
// refused outright rather than padded or cropped.
//
// Nothing here is in src/core: the core knows whether a cartridge declares a
// battery and hands out its RAM, and knows nothing about files.

#include "core/Cartridge.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace app {

// Beside the ROM, same name, `.sav`: tetris.gb -> tetris.sav.
std::filesystem::path savePathFor(const std::filesystem::path& romPath);

// The scratch file a save is built in. Always in the save's own directory,
// because renaming across directories is a copy, not the atomic swap this
// depends on.
std::filesystem::path tempPathFor(const std::filesystem::path& savePath);

enum class LoadStatus {
    NoBattery, // nothing to restore, and nothing will be saved either
    NoFile,    // no save yet: a new game
    Loaded,    // restored
    Refused,   // something is there, but it is not this cartridge's save
};

struct LoadResult {
    LoadStatus status = LoadStatus::NoBattery;
    std::string message; // always set when the status is Refused
};

// Restores `cart`'s RAM from `savePath` if a save of exactly the right size
// is there. Leaves the cartridge untouched in every other case, including a
// truncated or oversized file -- that file is the player's only copy of their
// progress, so it is read, rejected and left exactly where it is.
LoadResult loadSave(fourshades::Cartridge& cart, const std::filesystem::path& savePath);

// Whether a session that started with `status` is allowed to write its RAM
// over that save file when it ends. False for Refused: a save we could not
// understand must not be replaced by one we invented, or refusing to read it
// would destroy it just as surely as reading it wrong. The player can move
// the bad file aside and start again, and until they do, this session's RAM
// is never written.
bool mayWriteSave(LoadStatus status);

enum class SaveStatus {
    NoBattery, // no file is written -- not an empty one, none at all
    Written,
    Failed,
};

struct SaveResult {
    SaveStatus status = SaveStatus::NoBattery;
    std::size_t bytes = 0;
    std::string message; // always set when the status is Failed
};

// Writes `cart`'s RAM to `savePath`, through the scratch file. A cartridge
// with no battery writes nothing.
SaveResult writeSave(const fourshades::Cartridge& cart, const std::filesystem::path& savePath);

// The two halves of writeSave, in order. They are separate, and public,
// because the gap between them is exactly where a kill or a power cut lands,
// and a test that wants to stand in that gap should stand in the real one.
//
// writeTempFile writes the bytes to tempPathFor(savePath) and flushes them to
// the disk; commitTempFile renames that file over savePath. Until the second
// one returns, savePath still holds the previous save, whole.
bool writeTempFile(const std::filesystem::path& savePath, const std::vector<fourshades::u8>& bytes,
                   std::string* error);
bool commitTempFile(const std::filesystem::path& savePath, std::string* error);

} // namespace app
