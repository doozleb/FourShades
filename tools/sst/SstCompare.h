#pragma once

#include "core/Cpu.h"
#include "sst/RecordingBus.h"
#include "sst/SstLoader.h"

#include <optional>
#include <string>

namespace sst {

struct Mismatch {
    std::string field;    // "a", "pc", "ei", "ram[0x1234]", "cycle count", "cycle kind", "cycle bus"
    std::string expected;
    std::string actual;
    int cycle = -1;       // index into the cycle list for cycle mismatches
};

// The first difference between what the test expects and what the CPU and
// bus ended up with, or nullopt if they match. Idle cycles are compared by
// kind only; their address and value are the generator's leftover bus
// contents, not something software can observe (design spec, "Match the
// test model").
std::optional<Mismatch> compareResult(const SstTest& test, const fourshades::Cpu& cpu, const RecordingBus& bus);

} // namespace sst
