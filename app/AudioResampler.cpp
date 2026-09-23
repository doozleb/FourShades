#include "app/AudioResampler.h"

#include <cmath>
#include <numeric>
#include <stdexcept>

namespace app {

double dcBlockCharge(int sampleRateHz) {
    if (sampleRateHz <= 0) {
        throw std::invalid_argument("dcBlockCharge: sample rate must be positive");
    }
    // T-cycles per sample, not M-cycles: 0.999958 is the per-T-cycle figure,
    // so this is what keeps the decay a property of the machine's clock
    // rather than of the rate we happen to emit at.
    const double tCyclesPerSample = static_cast<double>(kTCyclesPerSecond) / static_cast<double>(sampleRateHz);
    return std::pow(kDcBlockPerTCycle, tCyclesPerSample);
}

namespace {

// The reduced fraction for "one sample every 1048576 / rate M-cycles". At
// 48 kHz that is 8192 / 375, and keeping it reduced is what stops the
// accumulator growing anywhere near a 64-bit overflow: the numerator would
// have to be fed 4.9e16 M-cycles -- a billion years of emulated time -- in
// a single step before it could wrap.
std::uint64_t reducedDenominator(int sampleRateHz) {
    const std::uint64_t rate = static_cast<std::uint64_t>(sampleRateHz);
    return rate / std::gcd(kMCyclesPerSecond, rate);
}

std::uint64_t reducedNumerator(int sampleRateHz) {
    const std::uint64_t rate = static_cast<std::uint64_t>(sampleRateHz);
    return kMCyclesPerSecond / std::gcd(kMCyclesPerSecond, rate);
}

} // namespace

AudioResampler::AudioResampler(int sampleRateHz)
    : sampleRate_(sampleRateHz),
      // Throws for a rate that is not positive, which is the only validation
      // this needs: everything below divides by it.
      charge_(dcBlockCharge(sampleRateHz)),
      left_(charge_),
      right_(charge_),
      cyclesNum_(reducedNumerator(sampleRateHz)),
      cyclesDen_(reducedDenominator(sampleRateHz)) {}

std::size_t AudioResampler::pump(std::uint64_t cycles, const LevelSource& level) {
    if (cycles < lastCycles_) {
        // The machine was reset under us: GameBoy::cycles() restarts at
        // zero, and subtracting would ask for 1.8e19 samples. Re-anchor and
        // emit nothing; reset() is how you also discharge the capacitor.
        lastCycles_ = cycles;
        accumulator_ = 0;
        return 0;
    }

    // The exact ratio, in units of 1/cyclesDen_ of an M-cycle. Nothing is
    // rounded, so a second of M-cycles is 48,000 samples however the second
    // was chopped up, and an hour of them is still exact.
    accumulator_ += (cycles - lastCycles_) * cyclesDen_;
    lastCycles_ = cycles;

    std::size_t appended = 0;
    while (accumulator_ >= cyclesNum_) {
        accumulator_ -= cyclesNum_;
        emit(level);
        ++appended;
    }
    return appended;
}

std::size_t AudioResampler::pump(std::uint64_t cycles, const fourshades::Apu& apu) {
    return pump(cycles, [&apu]() { return apu.sample(); });
}

void AudioResampler::reset(std::uint64_t cycles) {
    buffer_.clear();
    left_.reset();
    right_.reset();
    accumulator_ = 0;
    lastCycles_ = cycles;
    emitted_ = 0;
}

void AudioResampler::emit(const LevelSource& level) {
    const fourshades::Apu::Sample in = level();
    // The filter runs whether or not anyone is listening, so unmuting
    // resumes where the signal actually is rather than stepping out of
    // silence into a charged capacitor.
    const float left = left_(in.left);
    const float right = right_(in.right);
    buffer_.push_back(muted_ ? 0.0f : left);
    buffer_.push_back(muted_ ? 0.0f : right);
    ++emitted_;
}

} // namespace app
