#include <doctest/doctest.h>

#include "core/Timer.h"
#include "core/apu/Apu.h"
#include "core/apu/WaveChannel.h"

#include <array>
#include <cstddef>

using namespace fourshades;

namespace {

constexpr u16 kNr30 = 0xFF1A;
constexpr u16 kNr32 = 0xFF1C;
constexpr u16 kNr33 = 0xFF1D;
constexpr u16 kNr34 = 0xFF1E;
constexpr u16 kNr52 = 0xFF26;
constexpr u16 kWave = 0xFF30;

// The first period after a trigger is this much longer than the ones after
// it. Not a figure Pan Docs gives; see the wave-channel entry in
// docs/known-divergences.md.
constexpr int kTriggerDelay = 6;

// $00 $11 $22 ... $FF, so a byte names its own index and a nibble names both
// the byte it came from and which half of it.
std::array<u8, 16> pattern() {
    std::array<u8, 16> wave{};
    for (std::size_t index = 0; index < wave.size(); ++index) {
        wave[index] = static_cast<u8>(index * 0x11);
    }
    return wave;
}

// One M-cycle of the pair, in the order the machine runs them: time first,
// then whatever access the CPU was making.
void cycle(Timer& timer, Apu& apu) {
    timer.tick();
    apu.tick(timer);
}

void cycles(Timer& timer, Apu& apu, int count) {
    for (int index = 0; index < count; ++index) {
        cycle(timer, apu);
    }
}

// The sixteen bytes, written while nothing is playing, which is the only time
// the CPU can put them there.
void loadWave(Apu& apu, const std::array<u8, 16>& wave) {
    for (std::size_t index = 0; index < wave.size(); ++index) {
        apu.write(static_cast<u16>(kWave + index), wave[index]);
    }
}

// DAC on, silent output level, a frequency, and a trigger. `frequency` fixes
// the period at (2048 - frequency) * 2 T-cycles per sample.
void start(Apu& apu, int frequency) {
    apu.write(kNr30, 0x80);
    apu.write(kNr32, 0x00);
    apu.write(kNr33, static_cast<u8>(frequency & 0xFF));
    apu.write(kNr34, static_cast<u8>(0x80 | ((frequency >> 8) & 0x07)));
}

bool channelOn(const Apu& apu) { return (apu.read(kNr52) & 0x04) != 0; }

// A period of six T-cycles. With the trigger's own six on top of the first
// one, the channel's reads land on the last T-cycle of every third M-cycle,
// which is the only T-cycle the CPU can reach wave RAM on: this frequency
// puts both answers, 0xFF and a byte, within a few cycles of each other.
constexpr int kPeriodSix = 2045;

// A period of four T-cycles, which puts every read two T-cycles ahead of the
// CPU's access instead: wave RAM is never reachable, and a trigger always
// lands while the channel is about to read.
constexpr int kPeriodFour = 2046;

} // namespace

TEST_CASE("the 32 samples are read in order, high nibble first, and wrap") {
    // Pan Docs "Audio Registers", FF30-FF3F: "As CH3 plays, it reads wave RAM
    // left to right, upper nibble first. That is, $FF30's upper nibble,
    // $FF30's lower nibble, $FF31's upper nibble, and so on."
    const std::array<u8, 16> wave = pattern();
    WaveChannel channel;
    channel.writeFrequencyLow(0xFF);
    channel.writeFrequencyHigh(0x07); // frequency 2047: two T-cycles a sample
    channel.trigger();
    // Pan Docs, "ACCESS ORDER": "When CH3 is started, the first sample read is
    // the one at index 1, i.e. the lower nibble of the first byte, NOT the
    // upper nibble." The index is reset to 0 by the trigger and the channel
    // reads only after it increments.
    CHECK(channel.position() == 0);
    channel.tick(2 + kTriggerDelay, wave);
    for (int sample = 1; sample <= 64; ++sample) {
        const int index = sample & 31;
        CAPTURE(sample);
        CHECK(channel.position() == index);
        CHECK(channel.readIndex() == static_cast<std::size_t>(index) / 2);
        const u8 byte = wave[static_cast<std::size_t>(index) / 2];
        const u8 expected = (index % 2 == 0) ? static_cast<u8>(byte >> 4)
                                             : static_cast<u8>(byte & 0x0F);
        CHECK(channel.sample() == expected);
        channel.tick(2, wave);
    }
}

TEST_CASE("the frequency timer period is (2048 - frequency) * 2") {
    // Half a pulse channel's (2048 - frequency) * 4, which is why the same
    // frequency value sounds an octave higher -- Pan Docs, "Audio Registers",
    // NR33/NR34.
    const std::array<u8, 16> wave = pattern();
    for (const int frequency : {2040, 2044, 2046, 2047}) {
        CAPTURE(frequency);
        const int period = (2048 - frequency) * 2;
        WaveChannel channel;
        channel.writeFrequencyLow(static_cast<u8>(frequency & 0xFF));
        channel.writeFrequencyHigh(static_cast<u8>((frequency >> 8) & 0x07));
        channel.trigger();
        // One T-cycle short of the first period, the channel has not read yet.
        for (int cycle = 0; cycle < period + kTriggerDelay - 1; ++cycle) {
            channel.tick(1, wave);
        }
        CHECK(channel.position() == 0);
        channel.tick(1, wave);
        CHECK(channel.position() == 1);
        // And the second sample is one plain period after the first: only the
        // period a trigger starts carries the extra six T-cycles.
        for (int cycle = 0; cycle < period - 1; ++cycle) {
            channel.tick(1, wave);
        }
        CHECK(channel.position() == 1);
        channel.tick(1, wave);
        CHECK(channel.position() == 2);
    }
}

TEST_CASE("the four output levels shift the sample by 4, 0, 1 and 2") {
    // Pan Docs "Audio Registers", NR32 bits 6-5: mute, 100%, 50%, 25%, done
    // as a right shift of the digital value rather than an analog change.
    const std::array<int, 4> shifts{4, 0, 1, 2};
    std::array<u8, 16> wave{};
    wave.fill(0xC0); // every high nibble is 0xC
    for (int level = 0; level < 4; ++level) {
        CAPTURE(level);
        WaveChannel channel;
        channel.writeLevel(static_cast<u8>(level << 5));
        channel.writeFrequencyLow(0xFF);
        channel.writeFrequencyHigh(0x07);
        channel.trigger();
        channel.tick(2 + kTriggerDelay, wave); // index 1: the low nibble, 0x0
        channel.tick(2, wave);                 // index 2: a high nibble, 0xC
        REQUIRE(channel.sample() == 0x0C);
        CHECK(channel.output() == static_cast<u8>(0x0C >> shifts[static_cast<std::size_t>(level)]));
    }
}

TEST_CASE("clearing bit 7 of NR30 switches the channel off") {
    Timer timer;
    Apu apu;
    start(apu, kPeriodSix);
    REQUIRE(channelOn(apu));
    cycles(timer, apu, 4);
    CHECK(channelOn(apu));
    apu.write(kNr30, 0x00);
    CHECK_FALSE(channelOn(apu));
    // And bringing the DAC back does not bring the channel back: only a
    // trigger does that.
    apu.write(kNr30, 0x80);
    CHECK_FALSE(channelOn(apu));
    apu.write(kNr34, 0x80);
    CHECK(channelOn(apu));
}

TEST_CASE("a channel that is not playing leaves wave RAM alone") {
    // Pan Docs, FF30-FF3F: "Wave RAM can be accessed normally even if the DAC
    // is on, as long as the channel is not active."
    Timer timer;
    Apu apu;
    apu.write(kNr30, 0x80); // the DAC, with no trigger behind it
    REQUIRE_FALSE(channelOn(apu));
    loadWave(apu, pattern());
    cycles(timer, apu, 16);
    for (std::size_t index = 0; index < 16; ++index) {
        CAPTURE(index);
        CHECK(apu.read(static_cast<u16>(kWave + index)) == static_cast<u8>(index * 0x11));
    }
    apu.write(static_cast<u16>(kWave + 5), 0xA5);
    CHECK(apu.read(static_cast<u16>(kWave + 5)) == 0xA5);
}

TEST_CASE("while the channel plays, wave RAM answers only on its own read") {
    // Pan Docs, FF30-FF3F: "On monochrome consoles, wave RAM can only be
    // accessed on the same cycle that CH3 does. Otherwise, reads return $FF,
    // and writes are ignored." The byte reached is the one the channel is
    // reading, whatever address the CPU names.
    Timer timer;
    Apu apu;
    loadWave(apu, pattern());
    start(apu, kPeriodSix);
    REQUIRE(channelOn(apu));

    // Six T-cycles of period plus the trigger's six: the first read is on the
    // last T-cycle of the third M-cycle, and the CPU can reach wave RAM only
    // there.
    cycles(timer, apu, 2);
    CHECK(apu.read(kWave) == 0xFF);
    apu.write(kWave, 0x5A); // dropped, not deferred

    cycle(timer, apu); // sample 1: the second half of byte 0
    REQUIRE(apu.wave().position() == 1);
    CHECK(apu.read(kWave) == 0x00);
    // The address the CPU names is not the one it gets.
    CHECK(apu.read(static_cast<u16>(kWave + 9)) == 0x00);

    cycles(timer, apu, 2); // sample 2, but two T-cycles out of reach
    CHECK(apu.read(kWave) == 0xFF);

    cycle(timer, apu); // sample 3: the second half of byte 1
    REQUIRE(apu.wave().position() == 3);
    CHECK(apu.read(kWave) == 0x11);

    // A write on that same cycle lands on the byte being read, wherever the
    // CPU aimed it.
    apu.write(static_cast<u16>(kWave + 9), 0xA5);
    CHECK(apu.read(kWave) == 0xA5);

    apu.write(kNr30, 0x00); // stop, and look at what is really there
    REQUIRE_FALSE(channelOn(apu));
    CHECK(apu.read(kWave) == 0x00); // the dropped write left no mark
    CHECK(apu.read(static_cast<u16>(kWave + 1)) == 0xA5);
    CHECK(apu.read(static_cast<u16>(kWave + 9)) == 0x99);
}

TEST_CASE("a trigger while the channel is about to read corrupts wave RAM") {
    // Pan Docs "Audio Details", Obscure Behavior: "Triggering the wave channel
    // on the DMG while it reads a sample byte will alter the first four bytes
    // of wave RAM. If the channel was reading one of the first four bytes, the
    // only first byte will be rewritten with the byte being read. If the
    // channel was reading one of the later 12 bytes, the first FOUR bytes of
    // wave RAM will be rewritten with the four aligned bytes that the read was
    // from (bytes 4-7, 8-11, or 12-15)."
    //
    // With a four T-cycle period the channel reads two T-cycles ahead of every
    // CPU access, so it is about to read on every M-cycle from the third on,
    // and the byte is the one that read is about to come out of.
    SUBCASE("a read from one of the first four bytes rewrites only the first") {
        Timer timer;
        Apu apu;
        loadWave(apu, pattern());
        start(apu, kPeriodFour);
        cycles(timer, apu, 5); // sample 3 read, sample 4 -- byte 2 -- next
        apu.write(kNr34, 0x80 | 0x07);
        apu.write(kNr30, 0x00);
        CHECK(apu.read(kWave) == 0x22);
        for (std::size_t index = 1; index < 16; ++index) {
            CAPTURE(index);
            CHECK(apu.read(static_cast<u16>(kWave + index)) == static_cast<u8>(index * 0x11));
        }
    }
    SUBCASE("a read from a later byte rewrites the first four with its group") {
        Timer timer;
        Apu apu;
        loadWave(apu, pattern());
        start(apu, kPeriodFour);
        cycles(timer, apu, 19); // sample 18 next, which is byte 9
        apu.write(kNr34, 0x80 | 0x07);
        apu.write(kNr30, 0x00);
        CHECK(apu.read(kWave) == 0x88);
        CHECK(apu.read(static_cast<u16>(kWave + 1)) == 0x99);
        CHECK(apu.read(static_cast<u16>(kWave + 2)) == 0xAA);
        CHECK(apu.read(static_cast<u16>(kWave + 3)) == 0xBB);
        for (std::size_t index = 4; index < 16; ++index) {
            CAPTURE(index);
            CHECK(apu.read(static_cast<u16>(kWave + index)) == static_cast<u8>(index * 0x11));
        }
    }
    SUBCASE("a trigger away from a read leaves wave RAM alone") {
        Timer timer;
        Apu apu;
        loadWave(apu, pattern());
        start(apu, kPeriodSix);
        cycles(timer, apu, 6); // a read landed here, so the next is not close
        apu.write(kNr34, 0x80 | 0x07);
        apu.write(kNr30, 0x00);
        for (std::size_t index = 0; index < 16; ++index) {
            CAPTURE(index);
            CHECK(apu.read(static_cast<u16>(kWave + index)) == static_cast<u8>(index * 0x11));
        }
    }
    SUBCASE("a trigger with the channel already stopped leaves wave RAM alone") {
        Timer timer;
        Apu apu;
        loadWave(apu, pattern());
        apu.write(kNr30, 0x80); // the DAC alone: nothing is playing
        apu.write(kNr33, static_cast<u8>(kPeriodFour & 0xFF));
        cycles(timer, apu, 5);
        apu.write(kNr34, 0x80 | 0x07);
        apu.write(kNr30, 0x00);
        for (std::size_t index = 0; index < 16; ++index) {
            CAPTURE(index);
            CHECK(apu.read(static_cast<u16>(kWave + index)) == static_cast<u8>(index * 0x11));
        }
    }
}

TEST_CASE("a trigger resets the sample index without re-reading wave RAM") {
    // Pan Docs, NR34: "Wave RAM index is reset, but its not refilled", and
    // "Triggering the wave channel does not immediately start playing wave
    // RAM; instead, the last sample ever read ... is output until the channel
    // next reads a sample."
    Timer timer;
    Apu apu;
    loadWave(apu, pattern());
    start(apu, kPeriodFour);
    cycles(timer, apu, 4); // sample 2: the first half of byte 1, 0x1
    REQUIRE(apu.wave().position() == 2);
    REQUIRE(apu.wave().sample() == 0x1);
    apu.write(kNr34, 0x80 | 0x07);
    CHECK(apu.wave().position() == 0);
    CHECK(apu.wave().sample() == 0x1); // the buffer is untouched
}

TEST_CASE("powering the APU down clears the wave channel's registers") {
    Timer timer;
    Apu apu;
    loadWave(apu, pattern());
    start(apu, kPeriodSix);
    apu.write(kNr32, 0x40);
    cycles(timer, apu, 4);
    REQUIRE(apu.wave().frequency() == kPeriodSix);
    apu.write(kNr52, 0x00);
    CHECK(apu.wave().frequency() == 0);
    CHECK(apu.read(kNr32) == 0x9F);
    CHECK_FALSE(channelOn(apu));
    // Wave RAM itself is on the far side of the power switch.
    for (std::size_t index = 0; index < 16; ++index) {
        CAPTURE(index);
        CHECK(apu.read(static_cast<u16>(kWave + index)) == static_cast<u8>(index * 0x11));
    }
    apu.write(kNr52, 0x80);
    CHECK(apu.wave().sample() == 0x0);
}
