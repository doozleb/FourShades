#include <doctest/doctest.h>

#include "app/AudioResampler.h"

#include "core/Cartridge.h"
#include "core/GameBoy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

using app::AudioResampler;
using app::dcBlockCharge;
using app::kAudioSampleRate;
using app::kMCyclesPerSecond;

namespace {

// One second of the machine, in M-cycles. Spelled out rather than taken from
// the header the implementation uses, so a wrong constant cannot agree with
// itself.
constexpr std::uint64_t kSecond = 1048576;

// One frame of the machine: 70,224 dots, four dots to an M-cycle. This is
// the step the real loop feeds, so the rate tests feed it too rather than
// jumping a whole second at once.
constexpr std::uint64_t kFrame = 17556;

// A level source holding a constant, the same on both sides.
app::LevelSource constant(float value) {
    return [value]() { return fourshades::Apu::Sample{value, value}; };
}

// A symmetric square wave of `halfPeriod` samples, +amplitude then
// -amplitude. It counts calls, so one call is one sample.
app::LevelSource square(float amplitude, int halfPeriod) {
    return [amplitude, halfPeriod, n = 0]() mutable {
        const int phase = (n / halfPeriod) % 2;
        ++n;
        const float v = phase == 0 ? amplitude : -amplitude;
        return fourshades::Apu::Sample{v, v};
    };
}

// Runs `total` M-cycles through the resampler in steps of `step`, the way
// the frame loop does: the cycle count only ever goes up, and the last step
// lands exactly on the end.
void feed(AudioResampler& r, std::uint64_t total, std::uint64_t step, const app::LevelSource& level,
          std::uint64_t from = 0) {
    std::uint64_t c = from;
    const std::uint64_t end = from + total;
    while (c < end) {
        c = std::min(c + step, end);
        r.pump(c, level);
    }
}

} // namespace

TEST_CASE("a second of emulated time is 48000 samples") {
    AudioResampler r;
    CHECK(r.sampleRate() == 48000);
    feed(r, kSecond, kFrame, constant(0.0f));
    CHECK(r.emitted() >= 47999u);
    CHECK(r.emitted() <= 48001u);
    // Interleaved stereo: two floats to the sample.
    CHECK(r.samples().size() == 2u * r.emitted());
    CHECK(r.pending() == r.emitted());
}

TEST_CASE("two seconds are 96000 samples, not 96000 minus the drift") {
    // The point of keeping the ratio exact. 1048576/48000 is 21.8453...,
    // and a float accumulated one sample at a time loses low bits as it
    // grows -- invisible over a frame, and a real error over minutes.
    AudioResampler r;
    feed(r, 2u * kSecond, kFrame, constant(0.0f));
    CHECK(r.emitted() >= 95999u);
    CHECK(r.emitted() <= 96001u);
}

TEST_CASE("a minute of emulated time does not drift by a single sample") {
    // Two seconds can hide a drift that a minute cannot. 60 seconds is
    // 2,880,000 samples; the buffer is drained each second, the way the app
    // drains it, so this measures the counter and not the vector.
    AudioResampler r;
    std::uint64_t c = 0;
    const app::LevelSource level = constant(0.0f);
    for (int s = 0; s < 60; ++s) {
        feed(r, kSecond, kFrame, level, c);
        c += kSecond;
        r.clear();
        CHECK(r.pending() == 0u);
    }
    CHECK(r.emitted() >= 2879999u);
    CHECK(r.emitted() <= 2880001u);
}

TEST_CASE("irregular steps produce the same samples as even ones") {
    // The frame loop does not hand over equal cycle counts: a frame that
    // ends mid-instruction, a paused frame, a reset. However the same span
    // is chopped up, the total must be identical.
    const app::LevelSource level = constant(0.0f);
    AudioResampler even;
    feed(even, 2u * kSecond, kFrame, level);

    AudioResampler ragged;
    // A deterministic, deliberately uneven walk: one cycle here, 40,000 there.
    std::uint64_t c = 0;
    std::uint64_t x = 12345;
    while (c < 2u * kSecond) {
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        const std::uint64_t step = 1u + (x >> 40) % 40000u;
        c = std::min(c + step, 2u * kSecond);
        ragged.pump(c, level);
    }
    CHECK(ragged.emitted() == even.emitted());
    CHECK(ragged.samples().size() == even.samples().size());
}

TEST_CASE("a cycle count that goes backwards is a reset, not an underflow") {
    // GameBoy::cycles() restarts at zero when the machine is reset, and a
    // subtraction that wrapped would ask for 1.8e19 samples.
    AudioResampler r;
    feed(r, kSecond, kFrame, constant(0.0f));
    const std::uint64_t before = r.emitted();
    r.pump(1000, constant(0.0f));
    CHECK(r.emitted() == before);
}

TEST_CASE("a constant level decays to silence through the DC blocker") {
    // A DMG's output sits behind a capacitor: a channel holding a steady
    // level fades out rather than holding an offset. Without it, four
    // channels' offsets stack into a click on every trigger.
    AudioResampler r;
    feed(r, kSecond, kFrame, constant(1.0f));
    const std::vector<float>& s = r.samples();
    REQUIRE(s.size() >= 2u * 48000u);
    // The first sample is the step itself, passed through.
    CHECK(s[0] == doctest::Approx(1.0f).epsilon(1e-6));
    // ... and a second later it is gone.
    CHECK(std::fabs(s[2u * 47999u]) < 1e-3f);
}

TEST_CASE("the capacitor's time constant comes from the machine's clock, not the sample rate") {
    // charge = 0.999958 ^ (T-cycles per sample), so the decay measured in
    // seconds is the same whatever rate we emit at: 1/e after
    // -1 / (4194304 * ln 0.999958) = 5.6757 ms, which is 272.4 samples at
    // 48 kHz. Computing the charge from 44100 while emitting at 48000 would
    // put the crossing at 250.3 samples instead, so this is the assertion
    // that tells those two apart.
    AudioResampler r;
    feed(r, kSecond, kFrame, constant(1.0f));
    const std::vector<float>& s = r.samples();
    REQUIRE(s.size() >= 2u * 1000u);
    std::size_t crossing = 0;
    for (std::size_t k = 0; k < 1000u; ++k) {
        if (s[2u * k] < static_cast<float>(1.0 / 2.718281828459045)) {
            crossing = k;
            break;
        }
    }
    CHECK(crossing >= 271u);
    CHECK(crossing <= 275u);
}

TEST_CASE("the charge is computed from the rate rather than written down") {
    // Same hardware constant, different rates: emitting twice as often must
    // halve the per-sample decay exponent, i.e. charge(24000) is charge(48000)^2.
    const double c48 = dcBlockCharge(48000);
    const double c24 = dcBlockCharge(24000);
    CHECK(c48 > 0.0);
    CHECK(c48 < 1.0);
    CHECK(c24 == doctest::Approx(c48 * c48).epsilon(1e-12));
    CHECK(AudioResampler(48000).charge() == doctest::Approx(c48).epsilon(1e-12));
}

TEST_CASE("a symmetric square wave keeps its peak-to-peak amplitude") {
    // The filter blocks DC and passes the signal. A 4 kHz square at +/-0.5
    // has a peak-to-peak of 1.0 going in; after a high pass whose corner is
    // 28 Hz it is still 1.0 to within a couple of percent.
    constexpr int kHalfPeriod = 6; // 48000 / (2 * 4000)
    AudioResampler r;
    feed(r, kSecond, kFrame, square(0.5f, kHalfPeriod));
    const std::vector<float>& s = r.samples();
    REQUIRE(s.size() >= 2u * 48000u);
    // Measured in the second half, once the capacitor has settled.
    float lo = s[2u * 24000u];
    float hi = lo;
    for (std::size_t k = 24000u; k < 47999u; ++k) {
        lo = std::min(lo, s[2u * k]);
        hi = std::max(hi, s[2u * k]);
    }
    CHECK(static_cast<double>(hi - lo) == doctest::Approx(1.0).epsilon(0.03));
}

TEST_CASE("the two sides are filtered independently") {
    AudioResampler r;
    std::uint64_t c = 0;
    while (c < kSecond) {
        c = std::min(c + kFrame, kSecond);
        r.pump(c, []() { return fourshades::Apu::Sample{1.0f, -0.25f}; });
    }
    const std::vector<float>& s = r.samples();
    REQUIRE(s.size() >= 2u * 48000u);
    CHECK(s[0] == doctest::Approx(1.0f).epsilon(1e-6));
    CHECK(s[1] == doctest::Approx(-0.25f).epsilon(1e-6));
    // Both decay, and neither leaks into the other.
    CHECK(std::fabs(s[2u * 47999u]) < 1e-3f);
    CHECK(std::fabs(s[2u * 47999u + 1u]) < 1e-3f);
}

TEST_CASE("mute silences the output without stopping the clock") {
    // Mute silences a running machine: the game keeps playing, so the
    // samples keep coming and they are all zero. A mute that stopped the
    // counter as well would starve whatever is feeding the device.
    AudioResampler r;
    CHECK_FALSE(r.muted());
    r.setMuted(true);
    CHECK(r.muted());
    feed(r, kSecond, kFrame, constant(1.0f));
    CHECK(r.emitted() >= 47999u);
    CHECK(r.emitted() <= 48001u);
    CHECK(r.samples().size() == 2u * r.emitted());
    for (float v : r.samples()) {
        CHECK(v == 0.0f);
    }
}

TEST_CASE("the filter keeps running while muted, so unmuting does not click") {
    // Roughly 100 samples of mute against a constant 1.0: the capacitor has
    // discharged to 0.69 by the time the sound comes back. A filter that
    // was skipped while muted would hand back a full 1.0 step instead.
    constexpr std::uint64_t kHundredSamples = 2185; // 100 * 1048576 / 48000
    AudioResampler r;
    r.setMuted(true);
    feed(r, kHundredSamples, 100, constant(1.0f));
    const std::uint64_t silent = r.emitted();
    CHECK(silent >= 99u);
    r.setMuted(false);
    r.clear();
    feed(r, kHundredSamples, 100, constant(1.0f), kHundredSamples);
    REQUIRE(r.pending() > 0u);
    CHECK(r.samples()[0] < 0.9f);

    // And it is exactly where an unmuted run would have been.
    AudioResampler open;
    feed(open, 2u * kHundredSamples, 100, constant(1.0f));
    REQUIRE(open.pending() > silent);
    CHECK(static_cast<double>(r.samples()[0]) ==
          doctest::Approx(static_cast<double>(open.samples()[2u * static_cast<std::size_t>(silent)])).epsilon(1e-5));
}

TEST_CASE("reset silences the filter and empties the buffer") {
    AudioResampler r;
    feed(r, kSecond / 4u, kFrame, constant(1.0f));
    CHECK(r.emitted() > 0u);
    r.reset();
    CHECK(r.emitted() == 0u);
    CHECK(r.pending() == 0u);
    feed(r, kSecond / 4u, kFrame, constant(1.0f));
    // The capacitor was discharged, so the step comes through whole again.
    REQUIRE(r.pending() > 0u);
    CHECK(r.samples()[0] == doctest::Approx(1.0f).epsilon(1e-6));
}

TEST_CASE("the APU overload asks the machine for its level") {
    // The shape the app actually uses: hand it the machine's APU rather
    // than a lambda.
    auto cart = fourshades::Cartridge::load(std::vector<fourshades::u8>(0x8000, 0x00), nullptr);
    REQUIRE(cart.has_value());
    fourshades::GameBoy gb{std::move(*cart)};
    AudioResampler r;
    while (gb.cycles() < kSecond / 10u) {
        gb.step();
        r.pump(gb.cycles(), gb.apu());
    }
    CHECK(r.emitted() >= 4790u);
    CHECK(r.emitted() <= 4810u);
    CHECK(r.samples().size() == 2u * r.emitted());
}

TEST_CASE("the rates are the machine's own, not the monitor's or the CD's") {
    CHECK(kMCyclesPerSecond == 1048576u);
    CHECK(kAudioSampleRate == 48000);
}
