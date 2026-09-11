#pragma once

#include "core/Types.h"

#include <vector>

namespace fourshades {

// SB and SC, per Pan Docs "Serial Data Transfer". On DMG the internal clock is
// 8192 Hz: one bit per falling edge of system-counter bit 8. With no link
// partner, the bits shifted in are 1.
class Serial {
public:
    // One M-cycle, given the system counter before and after it advanced.
    // Returns true when a transfer completes (the serial interrupt).
    bool tick(u16 counterBefore, u16 counterAfter);

    u8 read(u16 address) const;         // FF01-FF02
    void write(u16 address, u8 value);  // FF01-FF02

    // The byte in SB each time an internal-clock transfer is started, in order.
    // It is recorded when the transfer starts, so a transfer restarted before it
    // finishes still appears here even though its last bits never went out.
    // External-clock transfers are not recorded: with no partner they never run.
    const std::vector<u8>& sent() const { return sent_; }

private:
    u8 sb_ = 0x00;
    u8 sc_ = 0x7E;
    int bitsLeft_ = 0;
    std::vector<u8> sent_;
};

} // namespace fourshades
