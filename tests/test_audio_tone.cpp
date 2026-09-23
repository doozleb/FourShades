#include <doctest/doctest.h>

#include "app/AudioResampler.h"

#include "core/Cartridge.h"
#include "core/GameBoy.h"
#include "core/Types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The pitch test. Every sound ROM in the suite passes and not one of them
// checks what note comes out: an APU can satisfy all twelve and still play an
// octave high, or alias into noise, and nothing else in this project would
// notice. This is the only test that would, because it is the only one that
// drives a real machine through the whole chain -- APU, mixer, resampler,
// capacitor -- and measures the frequency at the far end.
//
// It writes the sound registers through the machine's own bus, so nothing
// here reaches past GameBoy into the core's internals.

namespace {

using fourshades::u16;
using fourshades::u8;

// The sound registers, per Pan Docs "Audio Registers". Spelled out here
// rather than taken from the core, so a wrong constant in the core cannot
// agree with itself.
constexpr u16 kNr10 = 0xFF10; // channel 1 sweep
constexpr u16 kNr11 = 0xFF11; // channel 1 duty and length load
constexpr u16 kNr12 = 0xFF12; // channel 1 envelope -- and its DAC
constexpr u16 kNr13 = 0xFF13; // channel 1 frequency, low eight bits
constexpr u16 kNr14 = 0xFF14; // channel 1 trigger, length enable, high three
constexpr u16 kNr22 = 0xFF17; // channel 2's DAC
constexpr u16 kNr30 = 0xFF1A; // channel 3's DAC
constexpr u16 kNr42 = 0xFF21; // channel 4's DAC
constexpr u16 kNr50 = 0xFF24; // the two master volumes
constexpr u16 kNr51 = 0xFF25; // which channel reaches which side
constexpr u16 kNr52 = 0xFF26; // the power bit

// One second of the machine, in M-cycles: 4,194,304 T-cycles, four to an
// M-cycle. Spelled out for the same reason as the register addresses.
constexpr std::uint64_t kSecond = 1048576;

// The rate this test believes the output is at. Deliberately a literal and
// not app::kAudioSampleRate: the measurement below divides by it, so a
// resampler that quietly emitted at 44,100 is caught rather than agreeing
// with itself.
constexpr int kExpectedRate = 48000;

// A pulse channel's pitch, per Pan Docs "Audio Registers": the frequency
// timer counts (2048 - frequency) * 4 T-cycles per duty step and there are
// eight steps to a cycle, which comes to 4194304 / (32 * (2048 - frequency)).
double pulseHz(int frequency) {
    return 131072.0 / static_cast<double>(2048 - frequency);
}

// A 32 KiB ROM with a valid header -- built the way tests/test_cartridge.cpp
// builds one -- whose entry point is `jr -2`. The CPU spins there forever and
// never touches a sound register, so everything the APU does is what this
// test asked for and nothing else.
std::vector<u8> makeRom() {
    std::vector<u8> rom(2 * 0x4000, 0x00);
    rom[0x0100] = 0x18; // jr
    rom[0x0101] = 0xFE; // -2: back to itself
    rom[0x0147] = 0x00; // ROM only
    rom[0x0148] = 0x00; // 32 KiB
    rom[0x0149] = 0x00; // no RAM
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    rom[0x014D] = sum;
    return rom;
}

// GameBoy holds a Bus reference inside its CPU, so it neither copies nor
// moves; the machine is handed out behind a pointer instead.
std::unique_ptr<fourshades::GameBoy> machine() {
    std::string error;
    auto cart = fourshades::Cartridge::load(makeRom(), &error);
    INFO(error);
    REQUIRE(cart.has_value());
    return std::make_unique<fourshades::GameBoy>(std::move(*cart));
}

// Channel 1 at `frequency`, routed by `nr51`, set up so that the only thing
// that can move the output is the duty generator:
//
//   NR10 = 0x00   no sweep, so the frequency stays where it was put
//   NR11 = 0x80   duty 2, the 50% pattern -- four steps high, four low, so
//                 the crossings are evenly spaced and there is one of each
//                 direction per cycle
//   NR12 = 0xF0   volume 15, envelope period 0, so the amplitude holds; the
//                 top five bits are non-zero, so the DAC is on
//   NR14 = 0x80   trigger, and bit 6 clear so no length counter switches the
//                 channel off part-way through the second
//
// The other three channels' DACs are switched off, which is what makes them
// contribute nothing at all: a channel that is merely switched off but whose
// DAC is live sits at +1, not at silence.
void startChannel1(fourshades::GameBoy& gb, int frequency, u8 nr51) {
    gb.write(kNr52, 0x80);
    gb.write(kNr50, 0x77); // both master volumes at 7, no Vin
    gb.write(kNr51, nr51);
    gb.write(kNr22, 0x00);
    gb.write(kNr30, 0x00);
    gb.write(kNr42, 0x00);
    gb.write(kNr10, 0x00);
    gb.write(kNr11, 0x80);
    gb.write(kNr12, 0xF0);
    gb.write(kNr13, static_cast<u8>(frequency & 0xFF));
    gb.write(kNr14, static_cast<u8>(0x80 | ((frequency >> 8) & 0x07)));
}

// Every channel's DAC off, which is what silence means at this mixer.
void silenceEveryDac(fourshades::GameBoy& gb) {
    gb.write(kNr52, 0x80);
    gb.write(kNr12, 0x00);
    gb.write(kNr22, 0x00);
    gb.write(kNr30, 0x00);
    gb.write(kNr42, 0x00);
}

// Runs the machine the way the app's loop does: step, then ask the resampler
// for whatever samples that step made due.
void run(fourshades::GameBoy& gb, app::AudioResampler& r, std::uint64_t mCycles) {
    const std::uint64_t end = gb.cycles() + mCycles;
    while (gb.cycles() < end) {
        gb.step();
        r.pump(gb.cycles(), gb.apu());
    }
}

constexpr int kLeft = 0;
constexpr int kRight = 1;

// Crossings in one direction only -- rising -- so one is counted per cycle
// rather than two. Counted after the DC blocker, because the raw mix carries
// a large offset by design: a live DAC's resting level is +1, not 0.
int risingCrossings(const std::vector<float>& samples, int side) {
    int count = 0;
    bool havePrevious = false;
    float previous = 0.0f;
    for (std::size_t i = static_cast<std::size_t>(side); i < samples.size(); i += 2) {
        if (havePrevious && previous < 0.0f && samples[i] >= 0.0f) {
            ++count;
        }
        previous = samples[i];
        havePrevious = true;
    }
    return count;
}

// The pitch the buffer is carrying, in hertz: crossings per cycle, over the
// length of the buffer in seconds at the rate this test expects. Expressing
// it through the rate rather than through the emulated second is deliberate
// -- it is what makes a resampler running at the wrong rate show up here as
// the wrong note, which is exactly how a listener would notice it.
double measuredHz(const std::vector<float>& samples, int side) {
    const double frames = static_cast<double>(samples.size() / 2);
    return static_cast<double>(risingCrossings(samples, side)) * kExpectedRate / frames;
}

// One percent, and no more.
//
// Over a window of one second a periodic wave gives either floor(f) or
// ceil(f) rising crossings, because at most one partial period is cut off at
// each end -- an error of at most one count. That is 1/439.8 = 0.23% at the
// 440 Hz fixture and 1/128 = 0.78% at the 128 Hz one, so 1% covers the
// truncation at both with a little room for the capacitor nudging a crossing
// by a sample, and nothing else. It is not a knob: a half-semitone is 2.9%
// and an octave is 100%, so anything this test cannot see is inaudible, and
// widening it past 1% would start hiding errors a listener could hear.
constexpr double kTolerance = 0.01;

} // namespace

TEST_CASE("channel 1 at frequency 1750 comes out at 439.8 Hz") {
    // 131072 / (2048 - 1750) = 131072 / 298, near enough concert A to be a
    // sensible fixture.
    const int frequency = 1750;
    const double expected = pulseHz(frequency);
    CHECK(expected == doctest::Approx(439.8).epsilon(0.001));

    auto gb = machine();
    startChannel1(*gb, frequency, 0x11); // channel 1 to both sides
    app::AudioResampler r;
    CHECK(r.sampleRate() == kExpectedRate);
    r.reset(gb->cycles());

    run(*gb, r, kSecond);

    // A second of emulated time is a second of samples: that is the
    // denominator of the measurement, so it is checked rather than assumed.
    CHECK(r.pending() >= 47999u);
    CHECK(r.pending() <= 48001u);

    const double left = measuredHz(r.samples(), kLeft);
    const double right = measuredHz(r.samples(), kRight);
    MESSAGE("frequency 1750: expected " << expected << " Hz, left " << left << " Hz, right " << right << " Hz");
    CHECK(left == doctest::Approx(expected).epsilon(kTolerance));
    CHECK(right == doctest::Approx(expected).epsilon(kTolerance));
}

TEST_CASE("channel 1 at frequency 1024 comes out at 128 Hz") {
    // A second fixture, nearly two octaves below the first, so the test
    // cannot pass by coincidence at one value.
    const int frequency = 1024;
    const double expected = pulseHz(frequency);
    CHECK(expected == doctest::Approx(128.0).epsilon(1e-9));

    auto gb = machine();
    startChannel1(*gb, frequency, 0x11);
    app::AudioResampler r;
    CHECK(r.sampleRate() == kExpectedRate);
    r.reset(gb->cycles());

    run(*gb, r, kSecond);

    CHECK(r.pending() >= 47999u);
    CHECK(r.pending() <= 48001u);

    const double left = measuredHz(r.samples(), kLeft);
    const double right = measuredHz(r.samples(), kRight);
    MESSAGE("frequency 1024: expected " << expected << " Hz, left " << left << " Hz, right " << right << " Hz");
    CHECK(left == doctest::Approx(expected).epsilon(kTolerance));
    CHECK(right == doctest::Approx(expected).epsilon(kTolerance));
}

TEST_CASE("NR51 routes the tone to one side and leaves the other silent") {
    auto gb = machine();
    startChannel1(*gb, 1750, 0x10); // channel 1 left only
    app::AudioResampler r;
    r.reset(gb->cycles());

    run(*gb, r, kSecond);

    const std::vector<float>& samples = r.samples();
    CHECK(r.pending() >= 47999u);
    CHECK(r.pending() <= 48001u);
    CHECK(measuredHz(samples, kLeft) == doctest::Approx(pulseHz(1750)).epsilon(kTolerance));

    // The right side never had the channel routed to it, so nothing reached
    // its DAC sum and nothing reached its capacitor either.
    std::size_t nonZeroRight = 0;
    for (std::size_t i = 1; i < samples.size(); i += 2) {
        if (samples[i] != 0.0f) {
            ++nonZeroRight;
        }
    }
    CHECK(nonZeroRight == 0u);
}

TEST_CASE("with every channel's DAC off a second of samples is all zeroes") {
    // Silence at this mixer is every DAC off, not "nothing triggered": a
    // channel that is switched off but whose DAC is live contributes +1.
    auto gb = machine();
    silenceEveryDac(*gb);
    app::AudioResampler r;
    r.reset(gb->cycles());

    run(*gb, r, kSecond);

    // The machine really did advance a second -- otherwise this would pass
    // on an empty buffer.
    CHECK(r.pending() >= 47999u);
    CHECK(r.pending() <= 48001u);

    std::size_t nonZero = 0;
    for (float sample : r.samples()) {
        if (sample != 0.0f) {
            ++nonZero;
        }
    }
    CHECK(nonZero == 0u);
}
