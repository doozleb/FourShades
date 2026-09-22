#include "app/FramePacer.h"

#include <cmath>
#include <stdexcept>

namespace app {

std::uint64_t framePeriodNs(double hz) {
    if (!(hz > 0.0) || !std::isfinite(hz)) {
        throw std::invalid_argument("frame rate must be positive and finite");
    }
    return static_cast<std::uint64_t>(1'000'000'000.0 / hz + 0.5);
}

PaceStep paceFrame(std::uint64_t nowNs, std::uint64_t deadlineNs, std::uint64_t periodNs) {
    if (periodNs == 0) {
        throw std::invalid_argument("frame period must be non-zero");
    }
    if (nowNs < deadlineNs) {
        // Early: sleep onto the grid, and the grid does not move.
        return PaceStep{deadlineNs - nowNs, deadlineNs + periodNs, false};
    }
    const std::uint64_t lag = nowNs - deadlineNs;
    if (lag < periodNs) {
        // Late, but by less than the next frame's whole sleep: stay on the
        // grid and let that sleep be shorter by exactly `lag`.
        return PaceStep{0, deadlineNs + periodNs, false};
    }
    // Too late to repay. Drop the lag and re-lay the grid from here.
    return PaceStep{0, nowNs + periodNs, true};
}

void FrameRateMeter::sample(std::uint64_t nowNs) {
    if (!started_) {
        started_ = true;
        lastNs_ = nowNs;
        return;
    }
    // A clock that went backwards would otherwise wrap into an enormous
    // interval; count it as zero rather than as 1.8e19 ns.
    const std::uint64_t interval = nowNs > lastNs_ ? nowNs - lastNs_ : 0;
    lastNs_ = nowNs;
    if (intervals_ == 0 || interval < minNs_) {
        minNs_ = interval;
    }
    if (intervals_ == 0 || interval > maxNs_) {
        maxNs_ = interval;
    }
    ++intervals_;
    const double value = static_cast<double>(interval);
    const double delta = value - mean_;
    mean_ += delta / static_cast<double>(intervals_);
    m2_ += delta * (value - mean_);
}

double FrameRateMeter::meanIntervalNs() const {
    return intervals_ == 0 ? 0.0 : mean_;
}

double FrameRateMeter::stddevIntervalNs() const {
    if (intervals_ == 0) {
        return 0.0;
    }
    return std::sqrt(m2_ / static_cast<double>(intervals_));
}

double FrameRateMeter::meanHz() const {
    if (intervals_ == 0 || mean_ <= 0.0) {
        return 0.0;
    }
    return 1'000'000'000.0 / mean_;
}

} // namespace app
