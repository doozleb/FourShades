#pragma once

// Frame pacing arithmetic, as pure functions of the clock. No SDL, no
// window, no sleeping: main.cpp reads the clock and does the sleeping, this
// decides how long to sleep for and where the next frame's deadline lands.
// Split out so the rule that keeps the emulator at the DMG's own frame rate
// -- and, in piece 5, its audio at the DMG's own pitch -- can be tested
// without a display attached.

#include <cstdint>

namespace app {

// The DMG's real frame rate: one frame is 70,224 dots and the machine runs
// at 4,194,304 Hz, so 59.7275 Hz, not the 60 Hz a monitor runs at. The
// design spec writes it as 59.727; this is that number before rounding.
inline constexpr double kDmgFrameHz = 4194304.0 / 70224.0;

// The wall-clock nanoseconds one frame at `hz` is allowed to take, rounded
// to nearest. Throws std::invalid_argument for a rate that is not positive
// and finite: a zero or negative frame rate has no period, and silently
// returning something would hand the loop a deadline it could never meet.
std::uint64_t framePeriodNs(double hz);

// What paceFrame decided for one frame boundary.
struct PaceStep {
    // How long to sleep before starting the next frame. Zero when this
    // frame already ran past its deadline.
    std::uint64_t sleepNs;
    // The deadline to hand back to the next call.
    std::uint64_t nextDeadline;
    // True when this frame was so late that the accumulated lag was thrown
    // away rather than repaid -- the host is not keeping up, and the
    // emulator is now running slower than a DMG.
    bool resynced;
};

// Decides what happens at the end of a frame that finished at `nowNs` and
// was due at `deadlineNs`, with a frame budget of `periodNs`.
//
// The deadlines form a fixed grid -- deadline, deadline + period, deadline +
// 2*period -- so a frame that finishes early simply sleeps onto the grid and
// the long-run rate is exactly `periodNs` per frame no matter how far a
// sleep overshoots. That is the property audio needs: an emulator that is a
// steady 0.3% slow produces a steady 5-cent-flat tone and a sample clock
// that drifts out of the sound card's for as long as it runs.
//
// When a frame overruns, the rule is: repay at most one frame of lateness,
// never more.
//
//  - Late by less than a period: stay on the grid. The next frame's sleep is
//    shortened by exactly how late this one was, so the pair of frames still
//    takes two periods, and the frame after that is back to a full sleep.
//    Nothing is emulated any faster -- each pass still runs exactly one
//    frame's worth of dots -- only the idle time shrinks.
//  - Late by a period or more: the lag is dropped and the grid is re-laid
//    from `nowNs` (resynced == true). Carrying it would mean a deadline
//    already in the past, so the next frame would be late too, by more, and
//    the loop would run flat out trying to catch a schedule it cannot reach
//    -- fast-forwarding through the game on a host that hiccupped once, and
//    never sleeping again on a host that is simply too slow.
//
// So a slow frame costs the next frame its sleep and nothing more: there is
// no burst of catch-up frames, and no error that accumulates without bound.
//
// Throws std::invalid_argument if periodNs is zero.
PaceStep paceFrame(std::uint64_t nowNs, std::uint64_t deadlineNs, std::uint64_t periodNs);

// Measures the frame rate actually achieved, which is not the same thing as
// the rate the pacer aims at: it includes every sleep that overshot and
// every frame the host was late with. Fed one timestamp per presented
// frame; the first one starts the clock and produces no interval.
class FrameRateMeter {
public:
    void sample(std::uint64_t nowNs);

    // Marks a break in the sequence: the next sample starts a new run and
    // produces no interval. Called when the loop stops running frames --
    // paused, or waiting for a ROM -- so the pause does not arrive in the
    // statistics as one enormous frame.
    void gap() { started_ = false; }

    // The number of frame-to-frame intervals seen, i.e. samples minus one.
    std::uint64_t intervals() const { return intervals_; }

    // Mean, smallest and largest interval in nanoseconds, and the
    // population standard deviation of the intervals. All zero before two
    // samples have arrived.
    double meanIntervalNs() const;
    std::uint64_t minIntervalNs() const { return intervals_ == 0 ? 0 : minNs_; }
    std::uint64_t maxIntervalNs() const { return intervals_ == 0 ? 0 : maxNs_; }
    double stddevIntervalNs() const;

    // The mean interval as a rate. Zero before two samples have arrived.
    double meanHz() const;

private:
    bool started_ = false;
    std::uint64_t lastNs_ = 0;
    std::uint64_t intervals_ = 0;
    std::uint64_t minNs_ = 0;
    std::uint64_t maxNs_ = 0;
    // Welford's running mean and sum of squared deviations: the intervals
    // are ~1.7e7 ns each and a minute of them is 3600 samples, so a naive
    // sum of squares would be working at 1e21 and losing the low bits that
    // the spread is made of.
    double mean_ = 0.0;
    double m2_ = 0.0;
};

} // namespace app
