#pragma once

#include "sst/SstTypes.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sst {

struct CpuSnapshot {
    u16 pc = 0, sp = 0;
    u8 a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, h = 0, l = 0;
    bool ime = false;
    // Only the EI tests record this ("ei" in the final state). Absent means
    // no EI is pending.
    std::optional<bool> imePending;
    std::vector<std::pair<u16, u8>> ram;
};

struct SstTest {
    std::string name;
    CpuSnapshot initial;
    CpuSnapshot final;
    std::vector<Cycle> cycles;
};

// Parses one test file. Throws std::runtime_error (or a JSON parse error) on
// any unknown, missing or out-of-range field, so nothing is silently ignored.
std::vector<SstTest> parseTests(std::string_view jsonText);

} // namespace sst
