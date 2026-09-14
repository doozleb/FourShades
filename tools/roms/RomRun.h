#pragma once

#include "core/Types.h"
#include "roms/Detectors.h"
#include "roms/RomTests.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "roms/Screenshot.h"

namespace roms {

constexpr std::uint64_t kCyclesPerSecond = 1048576; // M-cycles per emulated second

struct RomOutcome {
    Verdict status = Verdict::Fail;
    std::string reason;
    double emulatedSeconds = 0.0;
    std::string serial; // printable serial output, at most 2 KB
};

// Runs one test ROM headless until its author's pass/fail signal or its time
// limit. Pass/fail is decided here, never by the core. Screenshot tests run to
// the Shootout's own capture time and compare the frame with `references`,
// writing the produced frame into `frameDir` when they fail.
RomOutcome runRomTest(const RomTest& test, std::vector<u8> romImage,
                      const std::vector<std::vector<u8>>& references = {},
                      const std::filesystem::path& frameDir = {});

// Formats a Blargg failure reason from its final line of text. Blargg's own
// text often already starts with "Failed" (e.g. "Failed #3"), so this avoids
// stuttering "Failed: Failed #3": the "Failed: " prefix is added only when
// the line doesn't already start with "Failed".
std::string blarggFailureReason(const std::string& text);

} // namespace roms
