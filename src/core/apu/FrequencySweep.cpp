#include "core/apu/FrequencySweep.h"

#include "core/apu/PulseChannel.h"

namespace fourshades {

bool FrequencySweep::write(u8 value) {
    period_ = (value >> 4) & 0x07;
    decreasing_ = (value & 0x08) != 0;
    shift_ = value & 0x07;
    // The latch: a calculation has been made in decreasing mode and the
    // direction bit is now clear, so the channel goes. It stays armed until
    // the next trigger, which is what "since the last trigger" means -- the
    // write that switches the channel off does not disarm it.
    return negateUsed_ && !decreasing_;
}

bool FrequencySweep::trigger(PulseChannel& channel) {
    shadow_ = channel.frequency();
    timer_ = reload();
    enabled_ = period_ != 0 || shift_ != 0;
    negateUsed_ = false;
    if (shift_ == 0) {
        return false;
    }
    // The immediate overflow check. It calculates and throws the result away:
    // nothing is written back, not even when it fits.
    return calculate() > kMaxFrequency;
}

bool FrequencySweep::clock(PulseChannel& channel) {
    if (timer_ > 0) {
        --timer_;
    }
    if (timer_ > 0) {
        return false;
    }
    // The reload reads NR10 as it stands now, so a period written since the
    // last reload takes effect from here.
    timer_ = reload();
    // The unit steps only while it is enabled and the period is non-zero.
    // The timer above runs either way, which is what keeps a channel that is
    // written mid-sweep in step with the frame sequencer rather than with the
    // write.
    if (!enabled_ || period_ == 0) {
        return false;
    }
    const int next = calculate();
    if (next > kMaxFrequency) {
        return true;
    }
    // Only a non-zero shift writes back. The calculation above happened
    // whatever the shift was, which is how a decreasing step with a shift of
    // 0 still arms the negate latch.
    if (shift_ == 0) {
        return false;
    }
    shadow_ = next;
    channel.setFrequency(next);
    // A second calculation on the value just written, checked for overflow
    // and then thrown away: this one never reaches the shadow register.
    return calculate() > kMaxFrequency;
}

int FrequencySweep::calculate() {
    const int offset = shadow_ >> shift_;
    if (!decreasing_) {
        return shadow_ + offset;
    }
    negateUsed_ = true;
    return shadow_ - offset;
}

void FrequencySweep::powerOff() {
    *this = FrequencySweep{};
}

} // namespace fourshades
