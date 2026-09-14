#pragma once

#include "core/Types.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <vector>

namespace roms {

using fourshades::u8;

constexpr std::size_t kFrameWidth = 160;
constexpr std::size_t kFrameHeight = 144;
constexpr std::size_t kFramePixels = kFrameWidth * kFrameHeight;

// Reads a reference produced by fetch_roms.py: one shade (0-3) per pixel.
// Throws std::runtime_error on the wrong size or an out-of-range shade.
std::vector<u8> loadShades(const std::filesystem::path& path);

// The number of pixels that differ. Exact: there is no tolerance.
int compareFrame(const std::array<u8, kFramePixels>& frame, const std::vector<u8>& reference);

// Writes the frame as a binary PGM, so a failure can be looked at.
void writePgm(const std::filesystem::path& path, const std::array<u8, kFramePixels>& frame);

} // namespace roms
