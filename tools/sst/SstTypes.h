#pragma once

#include "core/Types.h"

namespace sst {

using fourshades::u16;
using fourshades::u8;

enum class CycleKind { Read, Write, Idle };

// One M-cycle of bus activity, as recorded by SingleStepTests or by
// RecordingBus.
struct Cycle {
    u16 address = 0;
    u8 value = 0;
    CycleKind kind = CycleKind::Idle;

    bool operator==(const Cycle&) const = default;
};

} // namespace sst
