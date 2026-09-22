#include <doctest/doctest.h>

#include "app/FramePacer.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

using app::FrameRateMeter;
using app::PaceStep;
using app::framePeriodNs;
using app::kDmgFrameHz;
using app::paceFrame;

namespace {

// One frame period at the DMG's rate: 70,224 dots at 4,194,304 Hz is
// 16.7427 ms. Spelled out rather than recomputed from the same expression
// the implementation uses, so a wrong expression cannot agree with itself.
constexpr std::uint64_t kPeriod = 16742706;

// Runs the real frame loop's shape against paceFrame, with a synthetic
// clock: each pass does `workNs` of work, then paces, then sleeps for as
// long as it was told plus `oversleepNs` (a sleep that returns late, which
// is what a host timer actually does). Returns the presentation timestamps,
// one per frame, and fills `sleeps` with the sleep each pass was given.
std::vector<std::uint64_t> runLoop(std::size_t frames, std::uint64_t workNs, std::uint64_t oversleepNs,
                                   std::vector<std::uint64_t>& sleeps, std::uint64_t period = kPeriod) {
    std::vector<std::uint64_t> presents;
    std::uint64_t now = 0;
    std::uint64_t deadline = period;
    for (std::size_t i = 0; i < frames; ++i) {
        now += workNs;
        presents.push_back(now);
        const PaceStep step = paceFrame(now, deadline, period);
        sleeps.push_back(step.sleepNs);
        if (step.sleepNs > 0) {
            now += step.sleepNs + oversleepNs;
        }
        deadline = step.nextDeadline;
    }
    return presents;
}

} // namespace

TEST_CASE("the frame budget comes from the DMG's rate, not the monitor's") {
    CHECK(framePeriodNs(kDmgFrameHz) == kPeriod);
    // 60 Hz is 16,666,667 ns. A pacer that used the monitor's rate would run
    // the machine 0.46% fast, which is what piece 5's audio would inherit as
    // pitch, so the two must not be confusable.
    CHECK(framePeriodNs(60.0) == 16666667u);
    CHECK(framePeriodNs(kDmgFrameHz) != framePeriodNs(60.0));
}

TEST_CASE("the frame budget rounds to nearest rather than truncating") {
    // 1e9 / 3 is 333,333,333.33 and 1e9 / 7 is 142,857,142.86: truncation
    // would give ...142 for the second, rounding gives ...143.
    CHECK(framePeriodNs(3.0) == 333333333u);
    CHECK(framePeriodNs(7.0) == 142857143u);
}

TEST_CASE("a frame rate with no period is rejected, not silently accepted") {
    CHECK_THROWS_AS(framePeriodNs(0.0), std::invalid_argument);
    CHECK_THROWS_AS(framePeriodNs(-60.0), std::invalid_argument);
    CHECK_THROWS_AS(paceFrame(0, 0, 0), std::invalid_argument);
}

TEST_CASE("an early frame sleeps exactly onto its deadline") {
    const PaceStep step = paceFrame(1000000, kPeriod, kPeriod);
    CHECK(step.sleepNs == kPeriod - 1000000u);
    CHECK(step.nextDeadline == 2u * kPeriod);
    CHECK_FALSE(step.resynced);
}

TEST_CASE("sleeps that always overshoot do not accumulate into a drift") {
    // The property the whole design rests on: the deadlines are a fixed
    // grid, so a sleep that returns 300 us late costs that frame 300 us and
    // the next frame nothing. A pacer that instead restarted the grid from
    // wherever it woke up would be 300 us slow *every* frame -- 1.8% here,
    // 31 cents of pitch error in piece 5 -- so this asserts the total, not
    // an average that a per-frame error could hide in.
    constexpr std::uint64_t kOversleep = 300000;
    constexpr std::size_t kFrames = 600;
    std::vector<std::uint64_t> sleeps;
    const std::vector<std::uint64_t> presents = runLoop(kFrames, 5000000, kOversleep, sleeps);
    const std::uint64_t span = presents.back() - presents.front();
    const std::uint64_t ideal = static_cast<std::uint64_t>(kFrames - 1) * kPeriod;
    CHECK(span >= ideal - kOversleep);
    CHECK(span <= ideal + kOversleep);
    // and no frame was ever denied its sleep to pay for the overshoot
    for (std::uint64_t sleepNs : sleeps) {
        CHECK(sleepNs > 0);
    }
}

TEST_CASE("one slow frame is repaid by the next frame's sleep, not by a burst") {
    // Frame 3 takes 20 ms, 3.26 ms over budget. The frame after it must get
    // a sleep that is 3.26 ms shorter -- not zero, which would be a
    // catch-up burst, and not a full sleep, which would leave the 3.26 ms
    // permanently in the emulator's clock.
    constexpr std::uint64_t kWork = 5000000;
    constexpr std::uint64_t kSlowWork = 20000000;
    std::uint64_t now = 0;
    std::uint64_t deadline = kPeriod;
    std::vector<std::uint64_t> sleeps;
    for (int i = 0; i < 6; ++i) {
        now += (i == 3) ? kSlowWork : kWork;
        const PaceStep step = paceFrame(now, deadline, kPeriod);
        sleeps.push_back(step.sleepNs);
        CHECK_FALSE(step.resynced);
        now += step.sleepNs;
        deadline = step.nextDeadline;
    }
    // The slow frame itself gets no sleep: it already used its budget.
    CHECK(sleeps[3] == 0u);
    // The one after it is shortened by exactly the overrun, and is still a
    // real sleep rather than a skipped one.
    const std::uint64_t normalSleep = kPeriod - kWork;
    CHECK(sleeps[4] < normalSleep);
    CHECK(sleeps[4] > 0u);
    CHECK(sleeps[4] == normalSleep - (kSlowWork - kPeriod));
    // And the frame after that is back to normal: one slow frame costs one
    // frame's sleep and nothing more.
    CHECK(sleeps[5] == normalSleep);
}

TEST_CASE("a frame late by less than a period keeps the grid") {
    const std::uint64_t deadline = 10u * kPeriod;
    const PaceStep step = paceFrame(deadline + kPeriod - 1, deadline, kPeriod);
    CHECK(step.sleepNs == 0u);
    CHECK(step.nextDeadline == 11u * kPeriod);
    CHECK_FALSE(step.resynced);
}

TEST_CASE("a frame late by a whole period drops the lag instead of chasing it") {
    const std::uint64_t deadline = 10u * kPeriod;
    const std::uint64_t now = deadline + kPeriod;
    const PaceStep step = paceFrame(now, deadline, kPeriod);
    CHECK(step.sleepNs == 0u);
    CHECK(step.nextDeadline == now + kPeriod);
    CHECK(step.resynced);
}

TEST_CASE("a host that cannot keep up never accumulates a debt it will burst to repay") {
    // Every frame takes three periods. A pacer that advanced the deadline by
    // one period regardless would fall two periods further behind each
    // frame, and the instant the host recovered it would run flat out
    // through hundreds of frames -- the emulator fast-forwarding through the
    // game. The deadline must stay exactly one period ahead of the clock.
    constexpr std::uint64_t kWork = 3u * kPeriod;
    std::uint64_t now = 0;
    std::uint64_t deadline = kPeriod;
    for (int i = 0; i < 200; ++i) {
        now += kWork;
        const PaceStep step = paceFrame(now, deadline, kPeriod);
        CHECK(step.sleepNs == 0u);
        CHECK(step.resynced);
        CHECK(step.nextDeadline == now + kPeriod);
        deadline = step.nextDeadline;
    }
    // Having resynced, the host recovering gets one ordinary sleep, not a
    // run of zero-sleep frames.
    now += 5000000;
    const PaceStep step = paceFrame(now, deadline, kPeriod);
    CHECK(step.sleepNs == kPeriod - 5000000u);
}

TEST_CASE("the meter reports nothing until it has two samples") {
    FrameRateMeter meter;
    CHECK(meter.intervals() == 0u);
    CHECK(meter.meanHz() == 0.0);
    meter.sample(1000);
    CHECK(meter.intervals() == 0u);
    CHECK(meter.meanIntervalNs() == 0.0);
    CHECK(meter.minIntervalNs() == 0u);
    CHECK(meter.maxIntervalNs() == 0u);
    meter.sample(1000 + kPeriod);
    CHECK(meter.intervals() == 1u);
    CHECK(meter.meanIntervalNs() == doctest::Approx(static_cast<double>(kPeriod)));
    CHECK(meter.meanHz() == doctest::Approx(kDmgFrameHz).epsilon(1e-6));
}

TEST_CASE("the meter's mean, extremes and spread are of the intervals, not the timestamps") {
    // Intervals of 10, 20 and 30 ms: mean 20 ms, population sd 8.1650 ms.
    FrameRateMeter meter;
    std::uint64_t t = 5000000000;
    meter.sample(t);
    for (std::uint64_t interval : {10000000u, 20000000u, 30000000u}) {
        t += interval;
        meter.sample(t);
    }
    CHECK(meter.intervals() == 3u);
    CHECK(meter.meanIntervalNs() == doctest::Approx(20000000.0));
    CHECK(meter.minIntervalNs() == 10000000u);
    CHECK(meter.maxIntervalNs() == 30000000u);
    CHECK(meter.stddevIntervalNs() == doctest::Approx(8164965.8).epsilon(1e-6));
    CHECK(meter.meanHz() == doctest::Approx(50.0));
}

TEST_CASE("a gap is not measured as one enormous frame") {
    // The window keeps drawing while paused, and those passes are not
    // frames. Counting the pause as an interval would drag the measured
    // rate down by however long the player left it paused.
    FrameRateMeter meter;
    meter.sample(0);
    meter.sample(kPeriod);
    meter.gap();
    meter.sample(30000000000);
    CHECK(meter.intervals() == 1u);
    meter.sample(30000000000 + kPeriod);
    CHECK(meter.intervals() == 2u);
    CHECK(meter.meanIntervalNs() == doctest::Approx(static_cast<double>(kPeriod)));
    CHECK(meter.maxIntervalNs() == kPeriod);
}
