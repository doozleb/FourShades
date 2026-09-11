#pragma once

#include "core/Types.h"

namespace fourshades {

// Everything the CPU can do to the outside world. Each call is exactly one
// M-cycle, which makes the CPU cycle-accurate by construction: it cannot
// spend a cycle the bus doesn't see.
class Bus {
public:
    virtual ~Bus() = default;
    virtual u8 read(u16 address) = 0;
    virtual void write(u16 address, u8 value) = 0;
    // A cycle with no memory access (16-bit arithmetic, a taken branch, ...).
    virtual void idle() = 0;
};

} // namespace fourshades
