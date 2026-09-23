#pragma once

#include "core/Types.h"

namespace fourshades {

// The length counter each of the four channels owns, per Pan Docs "Audio
// Details". It counts down on the frame sequencer's length steps -- 256 Hz
// under normal operation -- and switches its channel off when it reaches
// zero.
//
// Two obscure behaviours from that page are the whole point of this class,
// and both turn on which half of the length period an NRx4 write lands in.
// "First half" here means the sequencer's next step is one that does NOT
// clock length; the caller knows the step and answers that question.
//
//  - "Extra length clocking occurs when writing to NRx4 when the DIV-APU next
//    step is one that doesn't clock the length timer. In this case, if the
//    length timer was PREVIOUSLY disabled and now enabled and the length
//    timer is not zero, it is decremented. If this decrement makes it zero
//    and trigger is clear, the channel is disabled."
//  - "If a channel is triggered when the DIV-APU next step is one that
//    doesn't clock the length timer and the length timer is now enabled and
//    length is being set to 64 (256 for wave channel) because it was
//    previously zero, it is set to 63 instead (255 for wave channel)."
class LengthCounter {
public:
    static constexpr u16 kShort = 64; // channels 1, 2 and 4
    static constexpr u16 kWave = 256; // channel 3

    constexpr explicit LengthCounter(u16 maximum) : maximum_(maximum) {}

    u16 maximum() const { return maximum_; }
    u16 value() const { return counter_; }
    bool enabled() const { return enabled_; }

    // NRx1's length load: the counter becomes the maximum minus the value
    // written, so a load of 0 is the longest length rather than an expired
    // counter. Only the bits that fit the maximum are used -- six for the
    // channels that count from 64, all eight for the one that counts from
    // 256.
    void load(u8 value) {
        const u16 used = static_cast<u16>(value & (maximum_ - 1));
        counter_ = static_cast<u16>(maximum_ - used);
    }

    // A length step of the frame sequencer. Returns true when this clock took
    // the counter to zero, which is the channel switching off.
    [[nodiscard]] bool clock() {
        if (!enabled_ || counter_ == 0) {
            return false;
        }
        return --counter_ == 0;
    }

    // An NRx4 write: bit 6 is the enable, bit 7 the trigger. `firstHalf` says
    // the sequencer's next step does not clock length. Returns true when the
    // write leaves the channel switched off.
    [[nodiscard]] bool writeControl(bool enable, bool trigger, bool firstHalf) {
        const bool wasEnabled = enabled_;
        enabled_ = enable;
        bool expired = false;
        if (firstHalf && !wasEnabled && enabled_ && counter_ != 0) {
            expired = (--counter_ == 0) && !trigger;
        }
        if (trigger && counter_ == 0) {
            counter_ = maximum_;
            // The reload is itself caught by the extra clock, so a trigger in
            // the first half starts one short of the maximum.
            if (firstHalf && enabled_) {
                --counter_;
            }
        }
        return expired;
    }

    // NR52's power bit going low. The enable lives in NRx4, which is zeroed
    // with every other register, so it goes; the counter itself is untouched
    // on DMG and keeps counting from where it was when the power comes back.
    void powerOff() { enabled_ = false; }

private:
    u16 maximum_;
    u16 counter_ = 0;
    bool enabled_ = false;
};

} // namespace fourshades
