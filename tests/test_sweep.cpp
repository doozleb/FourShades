#include <doctest/doctest.h>

#include "core/Timer.h"
#include "core/apu/Apu.h"
#include "core/apu/FrequencySweep.h"
#include "core/apu/PulseChannel.h"

using namespace fourshades;

namespace {

// NR10: bits 6-4 the period, bit 3 the direction (1 = decreasing), bits 2-0
// the shift.
u8 nr10(int period, bool decreasing, int shift) {
    return static_cast<u8>((period << 4) | (decreasing ? 0x08 : 0x00) | shift);
}

// A pulse channel sitting at one frequency, which is what the sweep copies
// into its shadow register on a trigger.
PulseChannel channelAt(int frequency) {
    PulseChannel channel;
    channel.writeFrequencyLow(static_cast<u8>(frequency & 0xFF));
    channel.writeFrequencyHigh(static_cast<u8>((frequency >> 8) & 0x07));
    return channel;
}

constexpr u16 kNr10 = 0xFF10;
constexpr u16 kNr12 = 0xFF12;
constexpr u16 kNr13 = 0xFF13;
constexpr u16 kNr14 = 0xFF14;
constexpr u16 kNr22 = 0xFF17;
constexpr u16 kNr23 = 0xFF18;
constexpr u16 kNr24 = 0xFF19;
constexpr u16 kNr52 = 0xFF26;

// One M-cycle of the pair, in the order the machine runs them.
void cycle(Timer& timer, Apu& apu) {
    timer.tick();
    apu.tick(timer);
}

// A falling edge of bit 12 of the system counter is one frame-sequencer step.
void sequencerStep(Timer& timer, Apu& apu) {
    timer.setCounter(static_cast<u16>(0x2000 - 4));
    cycle(timer, apu);
    REQUIRE(timer.counter() == 0x2000);
}

bool channelOn(const Apu& apu, int channel) {
    return (apu.read(kNr52) & (1u << channel)) != 0;
}

} // namespace

TEST_CASE("a sweep period of 0 reloads the timer with eight") {
    // Pan Docs "Audio Details", Obscure Behavior: "The volume envelope and
    // sweep timers treat a period of 0 as 8."
    PulseChannel channel = channelAt(0x300);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(0, false, 1)));
    CHECK_FALSE(sweep.trigger(channel));
    CHECK(sweep.timer() == 8);

    // And the reload after the timer runs out is the same 8, not a period of
    // zero that would fire on every clock.
    for (int clocks = 0; clocks < 7; ++clocks) {
        CAPTURE(clocks);
        CHECK_FALSE(sweep.clock(channel));
        CHECK(sweep.timer() == 7 - clocks);
    }
    CHECK_FALSE(sweep.clock(channel));
    CHECK(sweep.timer() == 8);

    // A period that is written is used as it stands.
    PulseChannel other = channelAt(0x300);
    FrequencySweep paced;
    CHECK_FALSE(paced.write(nr10(3, false, 1)));
    CHECK_FALSE(paced.trigger(other));
    CHECK(paced.timer() == 3);
}

TEST_CASE("a trigger with a non-zero shift runs the overflow check at once") {
    // Pan Docs "Audio Details", the trigger event: "If the individual step is
    // non-zero, frequency calculation and overflow check are performed
    // immediately." 2047 + (2047 >> 1) is over 2047, so the channel goes.
    PulseChannel channel = channelAt(0x7FF);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(0, false, 1)));
    CHECK(sweep.trigger(channel));
    // The check does not write anything back: only the overflow matters.
    CHECK(sweep.shadow() == 0x7FF);
    CHECK(channel.frequency() == 0x7FF);

    // A frequency whose shifted sum still fits is not an overflow. 1000 +
    // (1000 >> 1) is 1500.
    PulseChannel low = channelAt(1000);
    FrequencySweep fits;
    CHECK_FALSE(fits.write(nr10(0, false, 1)));
    CHECK_FALSE(fits.trigger(low));
    CHECK(fits.shadow() == 1000);
}

TEST_CASE("a trigger with shift 0 does not run the overflow check") {
    // The shift is what makes the check happen on a trigger; a period on its
    // own does not, however high the frequency sits.
    PulseChannel channel = channelAt(0x7FF);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(7, false, 0)));
    CHECK_FALSE(sweep.trigger(channel));
    CHECK(sweep.shadow() == 0x7FF);
    CHECK(sweep.enabled()); // a period on its own still enables the unit
}

TEST_CASE("a sweep step writes its new frequency back, and only with a shift") {
    // Pan Docs: "If the new frequency is 2047 or less and the individual step
    // is not zero, this new frequency is written back to the shadow register
    // and CH1 frequency in NR13 and NR14".
    PulseChannel channel = channelAt(1000);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(1, false, 2)));
    CHECK_FALSE(sweep.trigger(channel));
    CHECK_FALSE(sweep.clock(channel));
    CHECK(sweep.shadow() == 1250); // 1000 + (1000 >> 2)
    CHECK(channel.frequency() == 1250);

    // Decreasing subtracts instead: 1250 - (1250 >> 2).
    CHECK_FALSE(sweep.write(nr10(1, true, 2)));
    CHECK_FALSE(sweep.clock(channel));
    CHECK(sweep.shadow() == 938);
    CHECK(channel.frequency() == 938);
}

TEST_CASE("a sweep step with shift 0 leaves the frequency where it is") {
    PulseChannel channel = channelAt(1000);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(1, false, 0)));
    CHECK_FALSE(sweep.trigger(channel));
    CHECK_FALSE(sweep.clock(channel));
    CHECK(sweep.shadow() == 1000);
    CHECK(channel.frequency() == 1000);
}

TEST_CASE("a sweep step with a period of 0 does not step at all") {
    // Pan Docs: the sweep is calculated "if the 'enabled flag' is set and the
    // sweep pace is not zero". The timer still reloads with 8.
    PulseChannel channel = channelAt(1000);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(0, false, 2)));
    CHECK(sweep.trigger(channel) == false);
    for (int clocks = 0; clocks < 16; ++clocks) {
        CHECK_FALSE(sweep.clock(channel));
    }
    CHECK(sweep.shadow() == 1000);
    CHECK(channel.frequency() == 1000);
}

TEST_CASE("the second overflow check after a step does not write back") {
    // Pan Docs: "then frequency calculation and overflow check are run again
    // immediately using this new value, but this second new frequency is not
    // written back." 1024 + 512 is 1536, which fits; 1536 + 768 is 2304,
    // which does not, so the channel goes off carrying 1536.
    PulseChannel channel = channelAt(1024);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(1, false, 1)));
    CHECK_FALSE(sweep.trigger(channel));
    CHECK(sweep.clock(channel));
    CHECK(sweep.shadow() == 1536);
    CHECK(channel.frequency() == 1536);
}

TEST_CASE("a step whose first calculation overflows writes nothing back") {
    PulseChannel channel = channelAt(1400);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(1, false, 1)));
    CHECK(sweep.trigger(channel)); // 1400 + 700 is already over 2047
    CHECK(sweep.clock(channel));
    CHECK(sweep.shadow() == 1400);
    CHECK(channel.frequency() == 1400);
}

TEST_CASE("clearing the direction bit after a decreasing calculation disables the channel") {
    // Pan Docs "Audio Details", Obscure Behavior: "Clearing the sweep
    // direction bit in NR10 after at least one sweep calculation has been
    // made using the substraction mode since the last trigger causes the
    // channel to be immediately disabled."
    PulseChannel channel = channelAt(1000);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(0, true, 1)));
    CHECK_FALSE(sweep.trigger(channel)); // the immediate check is a calculation
    CHECK(sweep.write(nr10(0, false, 1)));
}

TEST_CASE("without a decreasing calculation, clearing the direction bit is harmless") {
    // The direction bit going 1 to 0 is not enough on its own: a calculation
    // has to have been made in decreasing mode since the last trigger.
    PulseChannel channel = channelAt(1000);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(1, false, 1)));
    CHECK_FALSE(sweep.trigger(channel));
    CHECK_FALSE(sweep.write(nr10(1, true, 1)));  // decreasing, but no clock yet
    CHECK_FALSE(sweep.write(nr10(1, false, 1))); // so this is harmless
}

TEST_CASE("a decreasing step arms the latch even when the shift is zero") {
    // The calculation happens whatever the shift is; only the write-back is
    // conditional on it, so a shift of 0 still arms the latch.
    PulseChannel channel = channelAt(1000);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(1, true, 0)));
    CHECK_FALSE(sweep.trigger(channel)); // no immediate check: shift is 0
    CHECK_FALSE(sweep.write(nr10(1, false, 0)));
    CHECK_FALSE(sweep.write(nr10(1, true, 0)));
    CHECK_FALSE(sweep.clock(channel)); // a decreasing calculation
    CHECK(sweep.write(nr10(1, false, 0)));
}

TEST_CASE("a trigger forgets the decreasing calculation") {
    // "since the last trigger": the latch is armed by a calculation and
    // disarmed by the trigger that starts the next run.
    PulseChannel channel = channelAt(1000);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(1, true, 1)));
    CHECK_FALSE(sweep.trigger(channel));       // a decreasing calculation
    CHECK(sweep.write(nr10(1, false, 1)));     // clearing the bit: off it goes
    CHECK_FALSE(sweep.trigger(channel));       // increasing now, so nothing is armed
    CHECK_FALSE(sweep.write(nr10(1, false, 1)));
}

TEST_CASE("the enabled flag follows the period and the shift") {
    PulseChannel channel = channelAt(1000);
    FrequencySweep sweep;
    CHECK_FALSE(sweep.write(nr10(0, false, 0)));
    CHECK_FALSE(sweep.trigger(channel));
    CHECK_FALSE(sweep.enabled());

    CHECK_FALSE(sweep.write(nr10(0, false, 1)));
    CHECK_FALSE(sweep.trigger(channel));
    CHECK(sweep.enabled());

    CHECK_FALSE(sweep.write(nr10(1, false, 0)));
    CHECK_FALSE(sweep.trigger(channel));
    CHECK(sweep.enabled());
}

TEST_CASE("channel 1's trigger can switch it straight back off") {
    // The same immediate check, this time through the APU: the channel is
    // triggered and reports itself off in the same write.
    Apu apu;
    apu.write(kNr10, nr10(0, false, 1));
    apu.write(kNr12, 0xF0); // the DAC on, so the trigger is not refused
    apu.write(kNr13, 0xFF);
    apu.write(kNr14, 0x87); // trigger, frequency 0x7FF
    CHECK_FALSE(channelOn(apu, 0));

    // A frequency whose shifted sum fits leaves it on.
    apu.write(kNr13, 0x00);
    apu.write(kNr14, 0x84); // trigger, frequency 0x400
    CHECK(channelOn(apu, 0));
}

TEST_CASE("channel 2 has no sweep") {
    // FF15, where channel 2's NR20 would be, is not a register: nothing a
    // program writes there can sweep channel 2 off.
    Apu apu;
    apu.write(0xFF15, nr10(0, false, 1));
    apu.write(kNr22, 0xF0);
    apu.write(kNr23, 0xFF);
    apu.write(kNr24, 0x87); // trigger, frequency 0x7FF
    CHECK(channelOn(apu, 1));
}

TEST_CASE("the frame sequencer's steps 2 and 6 clock the sweep") {
    Apu apu;
    Timer timer;
    // Period 1, increasing, shift 1: every sweep step is half again as much,
    // and the third one overflows. 1024 -> 1536 -> 2304.
    apu.write(kNr10, nr10(1, false, 1));
    apu.write(kNr12, 0xF0);
    apu.write(kNr13, 0x00);
    apu.write(kNr14, 0x84); // trigger, frequency 0x400
    REQUIRE(channelOn(apu, 0));
    REQUIRE(apu.sequencerStep() == 0);

    // Steps 0 and 1 are not sweep steps.
    sequencerStep(timer, apu);
    sequencerStep(timer, apu);
    REQUIRE(apu.sequencerStep() == 2);
    CHECK(apu.pulse(0).frequency() == 0x400);

    // Step 2 is: 1024 + 512 fits, and the second check on 1536 does not.
    sequencerStep(timer, apu);
    CHECK(apu.pulse(0).frequency() == 1536);
    CHECK_FALSE(channelOn(apu, 0));
    // NR13 and NR14 carry the new frequency, as the sweep wrote them.
    CHECK(apu.stored(kNr13) == 0x00);
    CHECK((apu.stored(kNr14) & 0x07) == 0x06);
}

TEST_CASE("powering the APU off forgets the sweep") {
    Apu apu;
    apu.write(kNr10, nr10(1, true, 1));
    apu.write(kNr12, 0xF0);
    apu.write(kNr13, 0x00);
    apu.write(kNr14, 0x84); // trigger: a decreasing calculation, latch armed
    REQUIRE(channelOn(apu, 0));
    apu.write(kNr52, 0x00);
    apu.write(kNr52, 0x80);
    // NR10 is zero again, and the latch went with it: writing a cleared
    // direction bit does not switch a fresh channel 1 off.
    apu.write(kNr10, nr10(0, false, 0));
    apu.write(kNr12, 0xF0);
    apu.write(kNr14, 0x80);
    CHECK(channelOn(apu, 0));
}
