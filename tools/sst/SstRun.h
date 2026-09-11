#pragma once

#include "sst/RecordingBus.h"
#include "sst/SstCompare.h"
#include "sst/SstLoader.h"

#include <optional>

namespace sst {

enum class Status { Pass, Fail, Unimplemented };

struct TestOutcome {
    Status status = Status::Fail;
    std::optional<Mismatch> mismatch;
};

// Runs one test on a fresh CPU, using `bus` (reset first) as its memory.
TestOutcome runTest(const SstTest& test, RecordingBus& bus);

} // namespace sst
