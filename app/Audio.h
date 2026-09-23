#pragma once

// The sound device, the drift policy and the mute key's other half. This is
// the SDL side of piece 5b; app/AudioResampler.h is the side with no device
// in it, where the arithmetic that decides the emulator's pitch lives.
//
// Only the drift policy is testable without a sound card, so only the drift
// policy is written as free functions: they are constexpr, they are in this
// header, and tests/test_audio_drift.cpp links them without ever touching
// SDL. The `Audio` class below is the thin part that cannot be tested here
// -- opening a device, handing it bytes -- and it makes no decisions of its
// own: it asks driftCorrection() what to do and correctedSampleCount() how
// much that is, so there is one statement of the policy, not two.
//
// SDL3 is deliberately not included here. SDL_AudioStream is an opaque
// struct, so a forward declaration is enough for the pointer, and that
// keeps this header -- and the test executable that includes it -- free of
// SDL entirely.

#include "app/AudioResampler.h" // kAudioSampleRate
#include "app/FramePacer.h"     // kDmgFrameHz

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace app {

// Stereo samples the device eats in one DMG frame: 48000 / 59.7275, or
// 48000 * 70224 / 4194304 = 803.65. Everything below is a multiple of this.
inline constexpr double kSamplesPerFrame = static_cast<double>(kAudioSampleRate) / kDmgFrameHz;

// The water marks, and why they are where they are.
//
// The frame pacer is the master clock -- it sleeps to hold 59.7275 Hz --
// and the sound card consumes samples at its own crystal's rate. The two
// will pull apart, so the queue is held inside a band rather than at a
// point.
//
//   low  = one frame (16.7 ms). Below this a single late frame empties the
//          device and it plays silence, which is an audible click rather
//          than a quiet moment.
//   high = three frames (50.2 ms). Above this a keypress is half a tenth of
//          a second ahead of the sound it causes, which starts to be
//          noticeable in a game that beeps when you jump.
//
// The target sits between them at two frames, which is what the queue is
// primed to when the device opens.
//
// The band is two frames -- 1,607 samples -- wide, and the correction moves
// one sample a frame, i.e. 59.7 samples a second. Two consumer crystals
// pulling apart by a generous 200 ppm produce 48000 * 0.0002 = 9.6 samples
// a second, so the correction is six times faster than the worst drift it
// has to answer, and it takes 27 seconds of unopposed correcting to cross
// the whole band. That is why one sample a frame is enough, and one sample
// -- 21 microseconds -- is far below anything that can be heard.
//
// Piece 3b's measurement is what makes this cheap: the sleep-based pacer
// was measured at 59.7275 Hz rather than the display's 60.000 Hz, so the
// only drift left for this to correct is the crystals'. Measured here over
// 100 undisturbed seconds, with FOURSHADES_PACE_LOG and
// FOURSHADES_AUDIO_LOG both on: the loop held 59.7130 Hz, the queue
// sawtoothed between 1,300 and 1,800 samples, and the policy corrected 12
// frames out of 5,955 -- 0.2%.
//
// Three frames rather than two for the high mark, because the queue does
// not only drift, it also steps. A pass round the frame loop runs until the
// PPU completes a frame, so a program that switches the LCD off for a
// moment -- which they do at boot and on a reset -- gets several frames'
// worth of emulated time, and several frames' worth of samples, in one
// pass. Measured at up to 2,100 samples in a single step. The machine
// really did run that long, so the samples are real and must not be thrown
// away; the queue simply has to be able to hold them. At two frames the
// high mark would be underneath the step, and the policy would spend the
// next half-minute saturated. At three it absorbs the step with room over,
// and takes about 30 seconds to walk back down. That is a transient, not a
// latency: the resting depth is still two frames.
inline constexpr std::size_t kLowWaterSamples = static_cast<std::size_t>(kSamplesPerFrame);
inline constexpr std::size_t kTargetQueuedSamples = static_cast<std::size_t>(2.0 * kSamplesPerFrame);
inline constexpr std::size_t kHighWaterSamples = static_cast<std::size_t>(3.0 * kSamplesPerFrame);

// What one frame's worth of samples should have done to it before it is
// handed to the device.
enum class DriftCorrection { Drop, None, Repeat };

// The policy, as arithmetic.
//
// `emulatedFrame` is whether this pass round the loop actually stepped the
// machine. A paused pass -- or one with no ROM loaded -- produced no
// samples, and its queue is draining because it is supposed to. Propping it
// up would hold the last sample as a DC level for the length of the pause,
// which is precisely the noise a pause must not make. So a pass that
// emulated nothing corrects nothing, and the device runs dry into clean
// silence.
//
// `queuedSamples` is stereo samples the device still has to play.
constexpr DriftCorrection driftCorrection(bool emulatedFrame, std::size_t queuedSamples) {
    if (!emulatedFrame) {
        return DriftCorrection::None;
    }
    if (queuedSamples > kHighWaterSamples) {
        return DriftCorrection::Drop;
    }
    if (queuedSamples < kLowWaterSamples) {
        return DriftCorrection::Repeat;
    }
    return DriftCorrection::None;
}

// How many stereo samples a frame of `stereoSamples` becomes once
// `correction` is applied: one more, one fewer, or the same, and never any
// further than that. An empty frame stays empty -- there is no last sample
// to repeat, and nothing to drop.
constexpr std::size_t correctedSampleCount(std::size_t stereoSamples, DriftCorrection correction) {
    if (stereoSamples == 0) {
        return 0;
    }
    switch (correction) {
    case DriftCorrection::Drop:
        return stereoSamples - 1;
    case DriftCorrection::Repeat:
        return stereoSamples + 1;
    case DriftCorrection::None:
        break;
    }
    return stereoSamples;
}

// The playback device: 48 kHz, stereo, 32-bit float, fed from the frame
// loop rather than from a callback.
//
// Fed, not pulled, on purpose. Driving the emulator from the audio callback
// would make the sound card the master clock and the picture the thing that
// stutters, which is the wrong way round for an emulator whose frame timing
// has already been measured and is already correct.
//
// A device that will not open is not fatal. open() returns false, says why
// once, and every other call becomes a no-op: the game still plays,
// silently. A missing or busy sound card is not a reason to refuse to run a
// program, any more than a missing ROM argument is a reason to refuse to
// open a window.
class Audio {
public:
    Audio() = default;
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Opens the default playback device and primes it with two frames of
    // silence, so the queue starts in the middle of the band instead of
    // spending thirteen seconds walking up to it one sample a frame.
    // Returns false and fills failure() if anything refused; safe to call
    // on a machine with no sound card at all.
    bool open();

    bool isOpen() const { return stream_ != nullptr; }

    // SDL's message from the failed open(), or empty.
    const std::string& failure() const { return failure_; }

    // Stereo samples the device still has to play. Zero when closed, or if
    // SDL cannot say.
    std::size_t queued() const;

    // Hands one frame's samples -- interleaved L, R -- to the device with
    // `correction` applied. A closed device and an empty frame are both
    // no-ops.
    void push(const std::vector<float>& samples, DriftCorrection correction);

    // Throws away what has not been played yet and tops the queue back up
    // to the target, returning how many stereo samples of silence that
    // took. Used when the machine is replaced under the device -- a reset,
    // or a new ROM -- so the old program's last 30 ms does not play over
    // the new one's first, and on resuming from a pause, which drained the
    // queue to nothing on purpose.
    std::size_t reprime();

    // Counters, for FOURSHADES_AUDIO_LOG. `drops` and `repeats` are how
    // many frames the drift policy corrected; min and max are the extremes
    // the queue reached, and are the numbers that say whether the marks are
    // in the right place.
    std::uint64_t pushedSamples() const { return pushedSamples_; }
    std::uint64_t drops() const { return drops_; }
    std::uint64_t repeats() const { return repeats_; }
    std::uint64_t observations() const { return observations_; }
    std::size_t minQueued() const { return observations_ == 0 ? 0 : minQueued_; }
    std::size_t maxQueued() const { return maxQueued_; }

    // Records `queuedSamples` in the min and max above. Called once a frame
    // with the same number the policy was asked about, so the log reports
    // what the policy actually saw.
    void observeQueued(std::size_t queuedSamples);

private:
    // Puts `stereoSamples` pairs of zeroes in front of whatever is there.
    void primeWithSilence(std::size_t stereoSamples);

    SDL_AudioStream* stream_ = nullptr;
    std::string failure_;
    std::uint64_t pushedSamples_ = 0;
    std::uint64_t drops_ = 0;
    std::uint64_t repeats_ = 0;
    std::uint64_t observations_ = 0;
    std::size_t minQueued_ = 0;
    std::size_t maxQueued_ = 0;
};

} // namespace app
