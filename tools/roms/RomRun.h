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

} // namespace roms
