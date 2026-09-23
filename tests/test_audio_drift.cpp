#include <doctest/doctest.h>

#include "app/Audio.h"

#include <cstddef>
#include <vector>

using app::DriftCorrection;
using app::correctedSampleCount;
using app::driftCorrection;

namespace {

// The water marks, spelled out rather than recomputed from the expression
// the implementation uses, so a wrong expression cannot agree with itself.
//
// A DMG frame is 70,224 dots at 4,194,304 Hz, i.e. 59.7275 Hz, so at 48 kHz
// one frame is 48000 * 70224 / 4194304 = 803.65 stereo samples. The marks
// are one frame and three frames of that, truncated.
constexpr std::size_t kLow = 803;
constexpr std::size_t kTarget = 1607;
constexpr std::size_t kHigh = 2410;

} // namespace

TEST_CASE("the water marks are one frame and three frames, around a two-frame target") {
    CHECK(app::kLowWaterSamples == kLow);
    CHECK(app::kTargetQueuedSamples == kTarget);
    CHECK(app::kHighWaterSamples == kHigh);
    CHECK(app::kLowWaterSamples < app::kTargetQueuedSamples);
    CHECK(app::kTargetQueuedSamples < app::kHighWaterSamples);

    // The band has to be wide enough that one sample a frame outruns any
    // drift two crystals can produce. One sample a frame is 59.7 samples a
    // second; 200 ppm -- a generous figure for two consumer crystals
    // pulling apart -- is 48000 * 0.0002 = 9.6 samples a second. The
    // correction is six times faster than the worst case it has to fix,
    // and crossing the whole band at 59.7 samples a second takes 27
    // seconds, so a burst of lateness is absorbed rather than clipped.
    CHECK(app::kHighWaterSamples - app::kLowWaterSamples >= 2 * kLow);
}

TEST_CASE("above the high-water mark, one sample is dropped") {
    CHECK(driftCorrection(true, kHigh + 1) == DriftCorrection::Drop);
    CHECK(driftCorrection(true, kHigh + 1000) == DriftCorrection::Drop);
    CHECK(driftCorrection(true, 100000) == DriftCorrection::Drop);
}

TEST_CASE("below the low-water mark, one sample is repeated") {
    CHECK(driftCorrection(true, 0) == DriftCorrection::Repeat);
    CHECK(driftCorrection(true, 1) == DriftCorrection::Repeat);
    CHECK(driftCorrection(true, kLow - 1) == DriftCorrection::Repeat);
}

TEST_CASE("between the marks, and exactly on them, nothing is corrected") {
    CHECK(driftCorrection(true, kLow) == DriftCorrection::None);
    CHECK(driftCorrection(true, kLow + 1) == DriftCorrection::None);
    CHECK(driftCorrection(true, kTarget) == DriftCorrection::None);
    CHECK(driftCorrection(true, kHigh - 1) == DriftCorrection::None);
    CHECK(driftCorrection(true, kHigh) == DriftCorrection::None);
}

TEST_CASE("a pass that did not step the machine corrects nothing") {
    // Paused, or waiting for a ROM: no samples fell due, so the queue is
    // draining on purpose. Repeating a sample to prop it up is exactly the
    // "stream repeats the last buffer" failure -- a held DC level for as
    // long as the pause lasts.
    CHECK(driftCorrection(false, 0) == DriftCorrection::None);
    CHECK(driftCorrection(false, kLow - 1) == DriftCorrection::None);
    CHECK(driftCorrection(false, kTarget) == DriftCorrection::None);
    CHECK(driftCorrection(false, kHigh + 1) == DriftCorrection::None);
    CHECK(driftCorrection(false, 100000) == DriftCorrection::None);
}

TEST_CASE("a correction moves the count by exactly one sample, in the right direction") {
    CHECK(correctedSampleCount(804, DriftCorrection::None) == 804);
    CHECK(correctedSampleCount(804, DriftCorrection::Drop) == 803);
    CHECK(correctedSampleCount(804, DriftCorrection::Repeat) == 805);
    CHECK(correctedSampleCount(1, DriftCorrection::Drop) == 0);
    CHECK(correctedSampleCount(1, DriftCorrection::Repeat) == 2);
}

TEST_CASE("no frame is ever corrected by more than one sample") {
    for (std::size_t n = 1; n <= 2000; ++n) {
        for (const DriftCorrection c :
             {DriftCorrection::Drop, DriftCorrection::None, DriftCorrection::Repeat}) {
            const std::size_t got = correctedSampleCount(n, c);
            const std::size_t delta = got > n ? got - n : n - got;
            CHECK(delta <= 1);
        }
    }
}

TEST_CASE("an empty frame stays empty, whatever the correction says") {
    // The paused case again, from the other end: nothing fell due, so there
    // is no last sample to repeat and nothing to drop. A correction must
    // never manufacture a sample out of an empty buffer.
    CHECK(correctedSampleCount(0, DriftCorrection::None) == 0);
    CHECK(correctedSampleCount(0, DriftCorrection::Drop) == 0);
    CHECK(correctedSampleCount(0, DriftCorrection::Repeat) == 0);
}
