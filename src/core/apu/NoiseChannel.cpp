#include "core/apu/NoiseChannel.h"

namespace fourshades {

void NoiseChannel::writeEnvelope(u8 value) {
    envelope_.write(value);
}

// NR43: bits 7-4 the clock shift, bit 3 the width, bits 2-0 the divisor code.
// A write takes effect at the next reload rather than moving the timer that
// is already running, which is the same rule the frequency registers follow.
void NoiseChannel::writeControl(u8 value) {
    shift_ = (value >> 4) & 0x0F;
    narrow_ = (value & 0x08) != 0;
    code_ = value & 0x07;
}

void NoiseChannel::trigger() {
    lfsr_ = kAllOnes;
    timer_ = period();
    envelope_.trigger();
}

// A period is never shorter than eight T-cycles, so an M-cycle's four can
// cross at most one boundary and the loop always ends.
void NoiseChannel::tick(int tCycles) {
    if (stopped()) {
        return;
    }
    timer_ -= tCycles;
    while (timer_ <= 0) {
        timer_ += period();
        step();
    }
}

// Pan Docs "Audio Details -- Noise channel (CH4)": bits 0 and 1 are XORed,
// every bit shifts right, and the result becomes bit 14 -- and bit 6 as well
// when the width bit is set, which closes the low seven bits into a sequence
// of their own that repeats every 127 steps.
void NoiseChannel::step() {
    const bool feedback = ((lfsr_ ^ (lfsr_ >> 1)) & 1u) != 0;
    lfsr_ = static_cast<u16>((lfsr_ >> 1) | (feedback ? kFeedbackHigh : 0u));
    if (narrow_) {
        lfsr_ = static_cast<u16>((lfsr_ & ~kFeedbackNarrow) |
                                 (feedback ? kFeedbackNarrow : 0u));
    }
}

// NR41 to NR44 go to zero with the rest of the block, so the channel goes
// back to everything those registers describe being zero. The shift register
// is not one of them -- nothing a register holds survives, and nothing says
// the power switch reaches the register itself -- and a trigger refills it
// before it can be heard again in any case.
void NoiseChannel::powerOff() {
    shift_ = 0;
    narrow_ = false;
    code_ = 0;
    timer_ = period();
    envelope_ = VolumeEnvelope{};
}

} // namespace fourshades
