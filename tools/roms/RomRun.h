#pragma once

#include "core/Types.h"
#include "roms/Detectors.h"
#include "roms/RomTests.h"

#include <cstdint>
#include <string>
#include <vector>

namespace roms {

constexpr std::uint64_t kCyclesPerSecond = 1048576; // M-cycles per emulated second

struct RomOutcome {
    Verdict status = Verdict::Fail;
    std::string reason;
    double emulatedSeconds = 0.0;
    std::string serial; // printable serial output, at most 2 KB
};

// Runs one test ROM headless until its author's pass/fail signal or its time
// limit. Pass/fail is decided here, never by the core.
RomOutcome runRomTest(const RomTest& test, std::vector<u8> romImage);

// Formats a Blargg failure reason from its final line of text. Blargg's own
// text often already starts with "Failed" (e.g. "Failed #3"), so this avoids
// stuttering "Failed: Failed #3": the "Failed: " prefix is added only when
// the line doesn't already start with "Failed".
std::string blarggFailureReason(const std::string& text);

} // namespace roms
