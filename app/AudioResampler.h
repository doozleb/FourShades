#pragma once

// Cycles into samples, and the capacitor. No SDL, no audio device: this is
// the half of piece 5b that can be measured without anything to listen
// with, split out for the same reason FramePacer was in piece 3b -- the
// arithmetic that decides the emulator's pitch is the part worth testing,
// and a test that needs a sound card is a test nobody runs.
//
// app/Audio.h owns the other half: the SDL stream, the device and the drift
// policy.

#include "core/apu/Apu.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace app {

// What we emit at. 48,000 rather than 44,100 because it divides the
// machine's clock more kindly and every device in use supports it; SDL3
// resamples if the device's own rate differs.
inline constexpr int kAudioSampleRate = 48000;

// The machine's clock. GameBoy::cycles() counts M-cycles, four T-cycles to
// an M-cycle.
inline constexpr std::uint64_t kTCyclesPerSecond = 4194304;
inline constexpr std::uint64_t kMCyclesPerSecond = kTCyclesPerSecond / 4; // 1048576

// The DMG's output sits behind a capacitor that blocks DC, which is why a
// channel holding a constant level fades to silence instead of holding an
// offset -- and why, without it, four channels' offsets stack into a click
// on every trigger.
//
//     out = in - capacitor
//     capacitor = in - out * charge
//
// `charge` is 0.999958 raised to the number of T-cycles in one sample, so
// the decay measured in seconds is a property of the machine rather than of
// whatever rate we happen to emit at. 0.999958 is the widely used DMG
// figure and is an approximation of a real capacitor, so it is a documented
// choice, not a measurement.
inline constexpr double kDcBlockPerTCycle = 0.999958;

// kDcBlockPerTCycle raised to the T-cycles per sample at `sampleRateHz`.
// Throws std::invalid_argument for a rate that is not positive.
double dcBlockCharge(int sampleRateHz);

// One side of the DC blocker. Two of these make a stereo pair; they share
// nothing, so one side's level cannot leak into the other.
class DcBlocker {
public:
    explicit DcBlocker(double charge) : charge_(charge) {}

    float operator()(float in) {
        const double out = static_cast<double>(in) - capacitor_;
        capacitor_ = static_cast<double>(in) - out * charge_;
        return static_cast<float>(out);
    }

    void reset() { capacitor_ = 0.0; }

private:
    double charge_;
    double capacitor_ = 0.0;
};

// Asked for the machine's output level once for every sample that falls
// due. Taking a callable rather than the APU itself is what lets the tests
// drive the filter with a square wave and no machine at all.
using LevelSource = std::function<fourshades::Apu::Sample()>;

// Turns a rising M-cycle count into samples.
//
// The core has no sample rate and is not getting one: Apu::sample() answers
// "what is the level right now", and this decides when to ask. A sample is
// due every 1048576 / 48000 = 21.8453... M-cycles, and that ratio is kept
// as a reduced integer fraction rather than a float the samples are
// accumulated into -- a float accumulator's error is invisible over a frame
// and audible over a minute, and tests/test_audio_resampler.cpp measures a
// minute for exactly that reason.
class AudioResampler {
public:
    explicit AudioResampler(int sampleRateHz = kAudioSampleRate);

    int sampleRate() const { return sampleRate_; }
    double charge() const { return charge_; }

    // Appends every sample now due -- given that the machine has reached
    // `cycles` M-cycles since power-on -- to the interleaved stereo buffer,
    // asking `level` once per sample. Returns how many samples it appended.
    //
    // Call it as often as you like: the answer for a given cycle count does
    // not depend on how the span was chopped up. A `cycles` that has gone
    // backwards is treated as the machine having been reset, and re-anchors
    // without emitting anything.
    std::size_t pump(std::uint64_t cycles, const LevelSource& level);

    // The same, asking a real machine's APU. This is the one the app uses.
    std::size_t pump(std::uint64_t cycles, const fourshades::Apu& apu);

    // What is waiting to be played, interleaved left, right, left, right.
    const std::vector<float>& samples() const { return buffer_; }
    // Stereo samples waiting, i.e. samples().size() / 2.
    std::size_t pending() const { return buffer_.size() / 2; }
    // Drops what is waiting. Whatever has queued the samples with the
    // device calls this once it has them.
    void clear() { buffer_.clear(); }

    // Stereo samples emitted since the last reset(), whether they were
    // audible or muted.
    std::uint64_t emitted() const { return emitted_; }

    // Silences the output without pausing the machine: samples keep falling
    // due, keep being counted, and are written as zeroes. The filter still
    // runs on the real levels, so unmuting resumes where the signal
    // actually is rather than stepping out of silence.
    void setMuted(bool muted) { muted_ = muted; }
    bool muted() const { return muted_; }

    // Back to power-on: empty buffer, discharged capacitor, zeroed counter,
    // and the cycle count re-anchored to `cycles`.
    void reset(std::uint64_t cycles = 0);

private:
    void emit(const LevelSource& level);

    int sampleRate_;
    double charge_;
    DcBlocker left_;
    DcBlocker right_;

    // The exact ratio, reduced: a sample is due every cyclesNum_ / cyclesDen_
    // M-cycles. accumulator_ carries the remainder in units of 1/cyclesDen_
    // of an M-cycle, so nothing is ever rounded away.
    std::uint64_t cyclesNum_;
    std::uint64_t cyclesDen_;
    std::uint64_t accumulator_ = 0;
    std::uint64_t lastCycles_ = 0;

    std::uint64_t emitted_ = 0;
    bool muted_ = false;
    std::vector<float> buffer_;
};

} // namespace app
