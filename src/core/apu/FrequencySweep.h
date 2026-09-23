#pragma once

#include "core/Types.h"

namespace fourshades {

class PulseChannel;

// Channel 1's frequency sweep, the unit NR10 describes, per Pan Docs "Audio
// Details". Only channel 1 has one -- FF15, where channel 2's would sit, is
// not a register at all -- so this is a type used once rather than something
// every pulse channel carries.
//
// NR10 is bits 6-4 the period, bit 3 the direction (1 = decreasing) and bits
// 2-0 the shift. The unit keeps three things of its own: a "sweep timer"
// clocked at 128 Hz (the frame sequencer's steps 2 and 6), an "enabled flag",
// and a "shadow register" holding the frequency it is working from.
//
// A calculation takes the shadow register, shifts it right by the shift,
// adds or subtracts it, and produces a new frequency. The overflow check is
// that calculation plus one rule: over 2047 and the channel is switched off.
// Every method that can switch the channel off says so in its return value,
// because the channel-enabled flag belongs to the APU, which owns one for all
// four channels.
//
// Three details are the whole reason this is a class:
//
//  - A period of 0 reloads the timer with 8, not 0 ("Audio Details", Obscure
//    Behavior: "The volume envelope and sweep timers treat a period of 0 as
//    8"). A period of 0 still stops the unit from stepping.
//  - A trigger runs the overflow check immediately when the shift is
//    non-zero, so a channel can be switched off by the very write that
//    triggered it, before any sweep step has happened.
//  - The negate latch: "Clearing the sweep direction bit in NR10 after at
//    least one sweep calculation has been made using the substraction mode
//    since the last trigger causes the channel to be immediately disabled."
class FrequencySweep {
public:
    // NR10. Returns true when the write switches the channel off, which is
    // the negate latch and nothing else.
    [[nodiscard]] bool write(u8 value);

    // Bit 7 of NR14. Copies the channel's frequency into the shadow register,
    // reloads the timer, sets the enabled flag, and runs the overflow check
    // when the shift is non-zero. Returns true when that check switches the
    // channel off.
    [[nodiscard]] bool trigger(PulseChannel& channel);

    // The frame sequencer's steps 2 and 6, 128 Hz. Returns true when either
    // of this step's two overflow checks switches the channel off.
    [[nodiscard]] bool clock(PulseChannel& channel);

    // NR52's power bit going low zeroes NR10, and the unit behind it goes
    // with the register.
    void powerOff();

    // What a test or a debugger would want to look at.
    int shadow() const { return shadow_; }
    int timer() const { return timer_; }
    bool enabled() const { return enabled_; }

private:
    static constexpr int kMaxFrequency = 2047; // 0x7FF

    // A period of 0 is a reload of 8.
    int reload() const { return period_ != 0 ? period_ : 8; }

    // One frequency calculation. It is not const: a calculation made in
    // decreasing mode is what arms the negate latch.
    int calculate();

    int period_ = 0;
    bool decreasing_ = false;
    int shift_ = 0;
    int shadow_ = 0;
    int timer_ = 0;
    bool enabled_ = false;
    bool negateUsed_ = false;
};

} // namespace fourshades
