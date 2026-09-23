#pragma once

#include "core/Types.h"

namespace fourshades {

// The volume envelope NRx2 describes, per Pan Docs "Audio Registers" and
// "Audio Details". Bits 7-4 are the volume a trigger loads, bit 3 the
// direction (1 counts up, 0 counts down) and bits 2-0 a period measured in
// envelope clocks -- the frame sequencer's step 7, 64 Hz.
//
// Two details are easy to get wrong and are the reason this is a type rather
// than three fields on a channel. A period of 0 is not a period of one: the
// envelope never steps at all. And the volume stops at each end of its range
// instead of wrapping, so a volume of 15 counting up stays at 15.
//
// Writing NRx2 does not move the volume the channel is playing at; it
// describes what the next trigger will load.
class VolumeEnvelope {
public:
    void write(u8 value) {
        initial_ = static_cast<u8>(value >> 4);
        up_ = (value & 0x08) != 0;
        period_ = static_cast<int>(value & 0x07);
    }

    // Bit 7 of NRx4: the volume and the timer both come back from NRx2.
    void trigger() {
        volume_ = initial_;
        timer_ = period_;
    }

    void clock() {
        if (period_ == 0) {
            return;
        }
        if (--timer_ > 0) {
            return;
        }
        timer_ = period_;
        if (up_) {
            if (volume_ < 15) {
                ++volume_;
            }
        } else if (volume_ > 0) {
            --volume_;
        }
    }

    u8 volume() const { return volume_; }

private:
    u8 initial_ = 0;
    bool up_ = false;
    int period_ = 0;
    u8 volume_ = 0;
    int timer_ = 0;
};

} // namespace fourshades
