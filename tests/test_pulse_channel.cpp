#include <doctest/doctest.h>

#include "core/Timer.h"
#include "core/apu/Apu.h"
#include "core/apu/PulseChannel.h"

#include <array>
#include <vector>

using namespace fourshades;

namespace {

// NRx1 bits 7-6 pick the duty; the rest of the byte is the length load, which
// is not the channel's business.
u8 duty(int selection) { return static_cast<u8>(selection << 6); }

// The shortest period there is: frequency 2047 gives (2048 - 2047) * 4, so
// the duty position advances once per M-cycle and a test can count steps
// rather than cycles.
void setShortestPeriod(PulseChannel& channel) {
    channel.writeFrequencyLow(0xFF);
    channel.writeFrequencyHigh(0x07);
}

// T-cycles, handed over an M-cycle at a time, which is how the APU feeds them.
void advance(PulseChannel& channel, int tCycles) {
    for (int cycle = 0; cycle < tCycles; cycle += 4) {
        channel.tick(4);
    }
}

// One M-cycle of the pair, in the order the machine runs them.
void cycle(Timer& timer, Apu& apu) {
    timer.tick();
    apu.tick(timer);
}

// Puts the counter four T-cycles below `target` and runs the cycle that
// reaches it, so the APU sees whatever that crossing does to bit 12.
void cycleTo(Timer& timer, Apu& apu, u16 target) {
    timer.setCounter(static_cast<u16>(target - 4));
    cycle(timer, apu);
    REQUIRE(timer.counter() == target);
}

// A falling edge of bit 12 is one frame-sequencer step.
void sequencerStep(Timer& timer, Apu& apu) { cycleTo(timer, apu, 0x2000); }

constexpr u16 kNr12 = 0xFF12;
constexpr u16 kNr14 = 0xFF14;
constexpr u16 kNr22 = 0xFF17;
constexpr u16 kNr24 = 0xFF19;
constexpr u16 kNr52 = 0xFF26;

} // namespace

TEST_CASE("each duty setting produces its own eight-step pattern") {
    // Pan Docs "Audio Registers", NR11 bits 7-6: 12.5%, 25%, 50%, 75%.
    const std::array<std::array<int, 8>, 4> patterns = {{
        {{0, 0, 0, 0, 0, 0, 0, 1}},
        {{1, 0, 0, 0, 0, 0, 0, 1}},
        {{1, 0, 0, 0, 0, 1, 1, 1}},
        {{0, 1, 1, 1, 1, 1, 1, 0}},
    }};
    for (int selection = 0; selection < 4; ++selection) {
        CAPTURE(selection);
        PulseChannel channel;
        channel.writeDuty(duty(selection));
        setShortestPeriod(channel);
        channel.trigger();
        REQUIRE(channel.position() == 0);
        for (int step = 0; step < 8; ++step) {
            CAPTURE(step);
            CHECK(channel.dutyOutput() == (patterns[selection][step] != 0));
            channel.tick(4);
        }
        CHECK(channel.position() == 0); // eight steps and back to the start
    }
}

TEST_CASE("the eleven-bit frequency is NRx3 plus the low three bits of NRx4") {
    PulseChannel channel;
    channel.writeFrequencyLow(0x5A);
    channel.writeFrequencyHigh(0xFF); // only bits 2-0 are frequency
    CHECK(channel.frequency() == 0x75A);
    channel.writeFrequencyHigh(0x00);
    CHECK(channel.frequency() == 0x05A);
}

TEST_CASE("the frequency timer's period is (2048 - frequency) times four") {
    PulseChannel fastest;
    setShortestPeriod(fastest); // 2048 - 2047 = 1, so four T-cycles
    fastest.trigger();
    advance(fastest, 4);
    CHECK(fastest.position() == 1);
    advance(fastest, 4);
    CHECK(fastest.position() == 2);

    PulseChannel slowest; // frequency 0: 2048 * 4 = 8192 T-cycles
    slowest.writeFrequencyLow(0x00);
    slowest.writeFrequencyHigh(0x00);
    slowest.trigger();
    advance(slowest, 8188);
    CHECK(slowest.position() == 0);
    advance(slowest, 4);
    CHECK(slowest.position() == 1);
}

TEST_CASE("trigger reloads the frequency timer") {
    PulseChannel channel;
    channel.writeFrequencyLow(0xF8);
    channel.writeFrequencyHigh(0x07); // 2048 - 2040 = 8, so 32 T-cycles
    channel.trigger();
    advance(channel, 16); // half way to the next step
    REQUIRE(channel.position() == 0);
    channel.trigger();
    advance(channel, 16); // the same half again, from a reloaded timer
    CHECK(channel.position() == 0);
    advance(channel, 16);
    CHECK(channel.position() == 1);
}

TEST_CASE("the envelope counts down to zero and stops there") {
    PulseChannel channel;
    channel.writeEnvelope(0x81); // volume 8, counting down, period 1
    channel.trigger();
    CHECK(channel.volume() == 8);
    for (int expected = 7; expected >= 0; --expected) {
        CAPTURE(expected);
        channel.clockEnvelope();
        CHECK(channel.volume() == expected);
    }
    channel.clockEnvelope(); // zero is the end: it does not wrap to 15
    CHECK(channel.volume() == 0);
}

TEST_CASE("the envelope counts up to fifteen and stops there") {
    PulseChannel channel;
    channel.writeEnvelope(0xD9); // volume 13, counting up, period 1
    channel.trigger();
    CHECK(channel.volume() == 13);
    channel.clockEnvelope();
    CHECK(channel.volume() == 14);
    channel.clockEnvelope();
    CHECK(channel.volume() == 15);
    channel.clockEnvelope(); // fifteen is the end: it does not wrap to 0
    CHECK(channel.volume() == 15);
}

TEST_CASE("the envelope steps once per period, not once per clock") {
    PulseChannel channel;
    channel.writeEnvelope(0x83); // volume 8, counting down, period 3
    channel.trigger();
    channel.clockEnvelope();
    CHECK(channel.volume() == 8);
    channel.clockEnvelope();
    CHECK(channel.volume() == 8);
    channel.clockEnvelope();
    CHECK(channel.volume() == 7);
}

TEST_CASE("an envelope period of zero never steps at all") {
    PulseChannel channel;
    channel.writeEnvelope(0x80); // volume 8, counting down, period 0
    channel.trigger();
    for (int clock = 0; clock < 64; ++clock) {
        channel.clockEnvelope();
    }
    CHECK(channel.volume() == 8);
}

TEST_CASE("writing NRx2 does not move the volume until the next trigger") {
    PulseChannel channel;
    channel.writeEnvelope(0xF0); // volume 15, counting down, period 0
    channel.trigger();
    REQUIRE(channel.volume() == 15);
    channel.writeEnvelope(0x30); // volume 3 from the next trigger onwards
    CHECK(channel.volume() == 15);
    channel.trigger();
    CHECK(channel.volume() == 3);
}

TEST_CASE("the DAC is off when the top five bits of NRx2 are zero") {
    // The DAC is read out of the stored NRx2 byte rather than kept in the
    // channel, so what it does is visible only through the APU: a trigger is
    // refused while it is off, and accepted while it is on.
    struct Case {
        u8 envelope;
        bool dacOn;
    };
    const std::vector<Case> cases = {
        {0x00, false}, // nothing set: volume 0, counting down
        {0x08, true},  // volume 0, counting up: the DAC is on
        {0x10, true},  // volume 1, counting down
        {0x07, false}, // only the envelope period: still off
    };
    for (const Case& entry : cases) {
        CAPTURE(entry.envelope);
        Apu apu;
        apu.write(kNr12, 0x00); // channel 1 off, whatever the boot left
        apu.write(kNr12, entry.envelope);
        apu.write(kNr14, 0x80); // a trigger only a live DAC accepts
        CHECK(((apu.read(kNr52) & 0x01) != 0) == entry.dacOn);
    }
}

TEST_CASE("turning a DAC off switches its channel off, and on does not switch it on") {
    Apu apu;
    // Channel 1 is the one the boot ROM leaves running, and NR12 leaves its
    // DAC on, so the flag going out has to be this write's doing.
    REQUIRE((apu.read(kNr52) & 0x01) != 0);
    apu.write(kNr12, 0x00);
    CHECK((apu.read(kNr52) & 0x01) == 0);
    apu.write(kNr12, 0xF8); // the DAC comes back; the channel does not
    CHECK((apu.read(kNr52) & 0x01) == 0);
    apu.write(kNr14, 0x80); // only a trigger does that
    CHECK((apu.read(kNr52) & 0x01) != 0);
}

TEST_CASE("a trigger switches a pulse channel on unless its DAC is off") {
    Apu apu;
    REQUIRE((apu.read(kNr52) & 0x02) == 0); // channel 2 starts silent
    apu.write(kNr24, 0x80);                 // trigger with NR22 still zero
    CHECK((apu.read(kNr52) & 0x02) == 0);   // the DAC is off, so it stays off
    apu.write(kNr22, 0xF0);
    CHECK((apu.read(kNr52) & 0x02) == 0);
    apu.write(kNr24, 0x80);
    CHECK((apu.read(kNr52) & 0x02) != 0);
    // A DAC is on whenever any of the top five bits is set, so a channel
    // whose envelope is zero but counting up triggers like any other.
    apu.write(kNr22, 0x00);
    REQUIRE((apu.read(kNr52) & 0x02) == 0);
    apu.write(kNr22, 0x08);
    CHECK((apu.read(kNr52) & 0x02) == 0);
    apu.write(kNr24, 0x80);
    CHECK((apu.read(kNr52) & 0x02) != 0);
}

TEST_CASE("a trigger reloads the envelope through the registers") {
    Apu apu;
    apu.write(kNr22, 0x81); // volume 8, counting down, period 1
    apu.write(kNr24, 0x80);
    CHECK(apu.pulse(1).volume() == 8);
    apu.write(kNr22, 0x21); // volume 2 from the next trigger
    CHECK(apu.pulse(1).volume() == 8);
    apu.write(kNr24, 0x80);
    CHECK(apu.pulse(1).volume() == 2);
}

TEST_CASE("the frame sequencer clocks the envelope on step 7 only") {
    Apu apu;
    Timer timer;
    apu.write(kNr22, 0x81); // volume 8, counting down, period 1
    apu.write(kNr24, 0x80); // trigger, length disabled
    REQUIRE(apu.pulse(1).volume() == 8);
    REQUIRE(apu.sequencerStep() == 0);
    // Steps 0 to 6 leave the envelope alone; the eighth edge runs step 7.
    for (int step = 0; step < 7; ++step) {
        CAPTURE(step);
        sequencerStep(timer, apu);
        CHECK(apu.pulse(1).volume() == 8);
    }
    sequencerStep(timer, apu);
    CHECK(apu.sequencerStep() == 0);
    CHECK(apu.pulse(1).volume() == 7);
}

TEST_CASE("the APU steps both pulse channels' frequency timers") {
    Apu apu;
    Timer timer;
    // Channel 1 through NR13/NR14, channel 2 through NR23/NR24, both at the
    // shortest period so one M-cycle is one duty step.
    apu.write(0xFF13, 0xFF);
    apu.write(kNr14, 0x87); // trigger, frequency high = 7
    apu.write(0xFF18, 0xFF);
    apu.write(kNr22, 0xF0);
    apu.write(kNr24, 0x87);
    REQUIRE(apu.pulse(0).position() == 0);
    REQUIRE(apu.pulse(1).position() == 0);
    for (int step = 1; step <= 3; ++step) {
        CAPTURE(step);
        cycle(timer, apu);
        CHECK(apu.pulse(0).position() == step);
        CHECK(apu.pulse(1).position() == step);
    }
}

TEST_CASE("the pulse channels stop while the APU is powered down") {
    Apu apu;
    Timer timer;
    apu.write(0xFF13, 0xFF);
    apu.write(kNr14, 0x87);
    cycle(timer, apu);
    REQUIRE(apu.pulse(0).position() == 1);
    apu.write(kNr52, 0x00);
    CHECK(apu.pulse(0).position() == 0); // the zeroed registers take it with them
    CHECK(apu.stored(0xFF12) == 0x00);   // NR12 with it, so the DAC is off too
    CHECK(apu.pulse(0).frequency() == 0);
    for (int step = 0; step < 4; ++step) {
        cycle(timer, apu);
    }
    CHECK(apu.pulse(0).position() == 0);
}

TEST_CASE("the boot ROM's NR11 and NR12 reach channel 1") {
    // Pan Docs' power-up table has NR11 at 0xBF -- duty 2 -- and NR12 at
    // 0xF3, whose top five bits are not zero. A channel 1 that reported its
    // DAC off here would refuse the first trigger a ROM gives it.
    Apu apu;
    REQUIRE((apu.read(kNr52) & 0x01) != 0); // channel 1 comes up running
    // A trigger a dead DAC refuses would switch that channel back off, since
    // a trigger sets the enable flag to whatever the DAC says. It stays on.
    apu.write(kNr14, 0x80);
    CHECK((apu.read(kNr52) & 0x01) != 0);
    CHECK(apu.pulse(0).dutyOutput()); // duty 2 starts its pattern at 1
}
