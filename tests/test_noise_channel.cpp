#include <doctest/doctest.h>

#include "core/Timer.h"
#include "core/apu/Apu.h"
#include "core/apu/NoiseChannel.h"

#include <array>
#include <cstddef>
#include <vector>

using namespace fourshades;

namespace {

constexpr u16 kNr41 = 0xFF20;
constexpr u16 kNr42 = 0xFF21;
constexpr u16 kNr43 = 0xFF22;
constexpr u16 kNr44 = 0xFF23;
constexpr u16 kNr52 = 0xFF26;

// NR43 from its three fields: bits 7-4 the shift, bit 3 the width, bits 2-0
// the divisor code.
u8 nr43(int shift, bool narrow, int code) {
    return static_cast<u8>((shift << 4) | (narrow ? 0x08 : 0x00) | code);
}

// A channel with NR43 set and triggered, which is the state every case below
// starts from.
NoiseChannel started(int shift, bool narrow, int code) {
    NoiseChannel channel;
    channel.writeControl(nr43(shift, narrow, code));
    channel.trigger();
    return channel;
}

// One full period of the frequency timer, so the LFSR steps exactly once.
void step(NoiseChannel& channel) { channel.tick(channel.period()); }

// The output bit of `count` consecutive steps.
std::vector<bool> outputs(NoiseChannel& channel, int count) {
    std::vector<bool> bits;
    bits.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        step(channel);
        bits.push_back(channel.output());
    }
    return bits;
}

bool channelOn(const Apu& apu) { return (apu.read(kNr52) & 0x08) != 0; }

} // namespace

TEST_CASE("the LFSR starts all 1 on a trigger") {
    // Pan Docs "Audio Details -- Noise channel (CH4)": "when the channel is
    // triggered, all 15 bits of the LFSR are set to 1".
    NoiseChannel channel = started(0, false, 0);
    CHECK(channel.lfsr() == 0x7FFF);
    // ... and the output is the inverted bit 0, so an LFSR of all ones is a
    // digital zero rather than a digital one.
    CHECK(channel.output() == false);
}

TEST_CASE("a step XORs bits 0 and 1 and puts the result in bit 14") {
    // Pan Docs: "the XOR of bits 0 and 1 is computed, all bits are shifted
    // right by one, and bit 14 is set to that XOR". Hand-computed from an
    // all-ones start rather than by a second copy of the same loop.
    const std::array<u16, 20> expected{
        0x3FFF, 0x1FFF, 0x0FFF, 0x07FF, 0x03FF, 0x01FF, 0x00FF, 0x007F,
        0x003F, 0x001F, 0x000F, 0x0007, 0x0003, 0x0001, 0x4000, 0x2000,
        0x1000, 0x0800, 0x0400, 0x0200,
    };
    NoiseChannel channel = started(0, false, 0);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CAPTURE(index);
        step(channel);
        CHECK(channel.lfsr() == expected[index]);
    }
}

TEST_CASE("the output is the inverted bit 0") {
    // Pan Docs: "the channel's output is bit 0 of the LFSR, INVERTED".
    NoiseChannel channel = started(0, false, 0);
    // Fifteen steps walk the single run of ones down past bit 0, so bit 0 is
    // set the whole way and the output stays low.
    for (int index = 0; index < 15; ++index) {
        CAPTURE(index);
        REQUIRE((channel.lfsr() & 1) == 1);
        CHECK(channel.output() == false);
        step(channel);
    }
    // The fifteenth step is the first one to feed a 1 back into bit 14, and
    // it is the first to leave bit 0 clear.
    REQUIRE(channel.lfsr() == 0x4000);
    CHECK(channel.output() == true);
}

TEST_CASE("width mode also writes bit 6, which shortens the sequence to 127") {
    // Pan Docs, NR43 bit 3: "if the width bit is set, the XOR result is ALSO
    // written to bit 6, making the LFSR 7 bits wide" -- and a seven-bit
    // maximal LFSR repeats every 127 steps.
    SUBCASE("narrow: the low seven bits and the output both repeat after 127") {
        NoiseChannel channel = started(0, true, 0);
        const std::vector<bool> first = outputs(channel, 127);
        CHECK((channel.lfsr() & 0x7F) == 0x7F); // back where it started
        const std::vector<bool> second = outputs(channel, 127);
        CHECK(first == second);
    }
    SUBCASE("wide: 127 steps is nowhere near the 32767-step sequence") {
        NoiseChannel channel = started(0, false, 0);
        const std::vector<bool> first = outputs(channel, 127);
        CHECK(channel.lfsr() != 0x7FFF);
        const std::vector<bool> second = outputs(channel, 127);
        CHECK(first != second);
    }
    SUBCASE("the bit the width sets is bit 6, and it is set to what bit 14 is") {
        // The feedback goes to both bits at once, so in narrow mode bit 6 and
        // bit 14 agree after every step for as long as the channel runs.
        NoiseChannel narrow = started(0, true, 0);
        for (int index = 0; index < 300; ++index) {
            CAPTURE(index);
            step(narrow);
            CHECK(((narrow.lfsr() >> 6) & 1) == ((narrow.lfsr() >> 14) & 1));
        }
        // In wide mode bit 6 is only whatever bit 7 was, and the very first
        // step already tells the two apart.
        NoiseChannel wide = started(0, false, 0);
        step(wide);
        CHECK(((wide.lfsr() >> 6) & 1) != ((wide.lfsr() >> 14) & 1));
    }
}

TEST_CASE("divisor code 0 is 8, codes 1-7 are the code times 16, and the shift doubles") {
    // Pan Docs, NR43: "the divisor code 0 means a divisor of 8; codes 1 to 7
    // mean the code times 16", and the period is that divisor shifted left by
    // the clock shift.
    const std::array<int, 8> divisors{8, 16, 32, 48, 64, 80, 96, 112};
    for (int code = 0; code < 8; ++code) {
        for (int shift = 0; shift < 14; ++shift) {
            CAPTURE(code);
            CAPTURE(shift);
            NoiseChannel channel = started(shift, false, code);
            CHECK(channel.period() == (divisors[static_cast<std::size_t>(code)] << shift));
        }
    }
}

TEST_CASE("the frequency timer steps the LFSR exactly once a period") {
    const std::array<int, 8> divisors{8, 16, 32, 48, 64, 80, 96, 112};
    for (int code = 0; code < 8; ++code) {
        for (const int shift : {0, 1, 5, 13}) {
            CAPTURE(code);
            CAPTURE(shift);
            const int period = divisors[static_cast<std::size_t>(code)] << shift;
            NoiseChannel channel = started(shift, false, code);
            // One T-cycle short of the period, the LFSR has not moved.
            channel.tick(period - 1);
            CHECK(channel.lfsr() == 0x7FFF);
            channel.tick(1);
            CHECK(channel.lfsr() == 0x3FFF);
            // ... and the next step is a whole period later, not sooner.
            channel.tick(period - 1);
            CHECK(channel.lfsr() == 0x3FFF);
            channel.tick(1);
            CHECK(channel.lfsr() == 0x1FFF);
        }
    }
}

TEST_CASE("a divisor code of 0 is a period of 8, not of 0 and not of 16") {
    // The one code that is not the code times 16. Checked on its own because
    // reading it as 0 or as 16 both leave the other seven codes right.
    NoiseChannel channel = started(0, false, 0);
    CHECK(channel.period() == 8);
    channel.tick(7);
    CHECK(channel.lfsr() == 0x7FFF); // a period of 0 would have stepped here
    channel.tick(1);
    CHECK(channel.lfsr() == 0x3FFF);
    channel.tick(8);
    CHECK(channel.lfsr() == 0x1FFF); // a period of 16 would not have stepped
}

TEST_CASE("clock shifts 14 and 15 stop the channel") {
    // Pan Docs, NR43: "a clock shift of 14 or 15 results in the channel
    // receiving no clocks" -- the frequency timer produces nothing and the
    // LFSR stands still however long the channel is ticked.
    for (const int shift : {14, 15}) {
        CAPTURE(shift);
        NoiseChannel channel = started(shift, false, 0);
        CHECK(channel.stopped());
        for (int mCycle = 0; mCycle < 100000; ++mCycle) {
            channel.tick(4);
        }
        CHECK(channel.lfsr() == 0x7FFF);
    }
    // Shift 13 is the last valid one and does step, eventually.
    NoiseChannel valid = started(13, false, 0);
    CHECK_FALSE(valid.stopped());
    valid.tick(valid.period());
    CHECK(valid.lfsr() == 0x3FFF);
}

TEST_CASE("a trigger loads the volume from NR42 and the envelope steps it") {
    NoiseChannel channel;
    channel.writeEnvelope(0xA1); // volume 10, counting down, period 1
    CHECK(channel.volume() == 0);
    channel.trigger();
    CHECK(channel.volume() == 10);
    channel.clockEnvelope();
    CHECK(channel.volume() == 9);
}

TEST_CASE("a trigger restarts the LFSR from all ones") {
    NoiseChannel channel = started(0, false, 0);
    step(channel);
    step(channel);
    REQUIRE(channel.lfsr() == 0x1FFF);
    channel.trigger();
    CHECK(channel.lfsr() == 0x7FFF);
}

TEST_CASE("powering the APU down clears the noise channel's registers") {
    Timer timer;
    Apu apu;
    apu.write(kNr41, 0x20);
    apu.write(kNr42, 0xF0);
    apu.write(kNr43, nr43(3, true, 5));
    apu.write(kNr44, 0x80);
    REQUIRE(channelOn(apu));
    REQUIRE(apu.noise().period() == (80 << 3));
    apu.write(kNr52, 0x00);
    CHECK(apu.noise().period() == 8); // NR43 is zero again: code 0, shift 0
    CHECK(apu.noise().volume() == 0);
    CHECK_FALSE(channelOn(apu));
    CHECK(apu.read(kNr43) == 0x00);
    (void)timer;
}

TEST_CASE("the APU ticks the noise channel one M-cycle at a time") {
    Timer timer;
    Apu apu;
    apu.write(kNr42, 0xF0);            // DAC on, volume 15
    apu.write(kNr43, nr43(0, false, 0)); // a period of eight T-cycles
    apu.write(kNr44, 0x80);
    REQUIRE(channelOn(apu));
    REQUIRE(apu.noise().lfsr() == 0x7FFF);
    for (int mCycle = 0; mCycle < 2; ++mCycle) {
        timer.tick();
        apu.tick(timer);
    }
    CHECK(apu.noise().lfsr() == 0x3FFF); // eight T-cycles: one step
    for (int mCycle = 0; mCycle < 2; ++mCycle) {
        timer.tick();
        apu.tick(timer);
    }
    CHECK(apu.noise().lfsr() == 0x1FFF);
}

TEST_CASE("the frame sequencer clocks the noise channel's envelope") {
    // Step 7 of the eight, 64 Hz, the same step that clocks the two pulse
    // channels' envelopes.
    Timer timer;
    Apu apu;
    apu.write(kNr42, 0xF1); // volume 15, counting down, period 1
    apu.write(kNr44, 0x80);
    REQUIRE(apu.noise().volume() == 15);
    for (int step = 0; step < 8; ++step) {
        CAPTURE(step);
        timer.setCounter(0x1FFC);
        timer.tick();
        apu.tick(timer);
    }
    CHECK(apu.sequencerStep() == 0);
    CHECK(apu.noise().volume() == 14);
}
