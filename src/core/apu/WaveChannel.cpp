#include "core/apu/WaveChannel.h"

namespace fourshades {

void WaveChannel::writeLevel(u8 value) {
    level_ = (value >> 5) & 0x03;
}

void WaveChannel::writeFrequencyLow(u8 value) {
    frequency_ = (frequency_ & 0x700) | value;
}

void WaveChannel::writeFrequencyHigh(u8 value) {
    frequency_ = ((value & 0x07) << 8) | (frequency_ & 0x0FF);
}

void WaveChannel::trigger() {
    timer_ = period() + kTriggerDelay;
    position_ = 0;
}

// One T-cycle at a time, because which T-cycle of the M-cycle the channel
// reads in is the whole of the wave RAM access rule: a period as short as two
// T-cycles puts two reads inside one M-cycle, and only a read on the last of
// the four coincides with the CPU's access.
//
// The period is read afresh at every reload rather than held from the
// trigger, which is what makes a frequency written to NR33 or NR34 take
// effect only after the following sample read.
void WaveChannel::tick(int tCycles, const std::array<u8, 16>& wave) {
    readOnLastCycle_ = false;
    for (int cycle = 0; cycle < tCycles; ++cycle) {
        if (--timer_ > 0) {
            continue;
        }
        timer_ = period();
        position_ = (position_ + 1) & 31;
        const u8 byte = wave[readIndex()];
        // High nibble first: sample 0 is the top half of the first byte,
        // sample 1 the bottom half, sample 2 the top half of the second.
        sample_ = static_cast<u8>((position_ & 1) == 0 ? (byte >> 4) : (byte & 0x0F));
        readOnLastCycle_ = (cycle == tCycles - 1);
    }
}

void WaveChannel::powerOff() {
    level_ = 0;
    frequency_ = 0;
    position_ = 0;
    timer_ = kMaxPeriod;
    readOnLastCycle_ = false;
}

} // namespace fourshades
