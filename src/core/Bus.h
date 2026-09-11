#pragma once

#include "core/Types.h"

#include <optional>

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
    // One M-cycle of a halted CPU. If an interrupt is pending once the cycle's
    // hardware has advanced, the CPU wakes within this M-cycle and its opcode
    // fetch from `address` happens in it: the opcode is returned. Otherwise
    // the bus sees no access, as in idle(), and nothing is returned.
    virtual std::optional<u8> haltedCycle(u16 address) = 0;

    // The interrupt lines. Neither costs a cycle: the CPU samples them at the
    // end of an M-cycle (after an opcode fetch, or while halted) rather than
    // reading IE and IF over the bus.
    virtual u8 pendingInterrupts() = 0;             // IE & IF & 0x1F
    virtual void acknowledgeInterrupt(int bit) = 0; // clears that IF bit
};

} // namespace fourshades
