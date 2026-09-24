#pragma once

// The sound device, the drift policy and the mute key's other half. This is
// the SDL side of piece 5b; app/AudioResampler.h is the side with no device
// in it, where the arithmetic that decides the emulator's pitch lives.
//
// There are two policies here and they answer two different problems. The
// water marks and driftCorrection() answer drift -- the emulator's clock and
// the sound card's crystal pulling apart -- with at most one sample a frame.
// kMaxQueuedSamples and excessQueuedSamples() answer a step: a backlog that
// arrived all at once and is far too large for one sample a frame ever to
// clear. The first corrects, the second cuts, and they are kept apart so it
// stays obvious which is which.
//
// Only those decisions are testable without a sound card, so only those
// decisions are written as free functions: they are constexpr, they are in
// this header, and tests/test_audio_drift.cpp links them without ever
// touching SDL. The `Audio` class below is the thin part that cannot be
// tested here -- opening a device, handing it bytes -- and it makes no
// decisions of its own: it asks driftCorrection() what to do,
// correctedSampleCount() how much that is and excessQueuedSamples() whether
// the backlog has stopped being drift, so there is one statement of each
// policy, not two.
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
// PPU completes a frame, so a program that switches the LCD off -- which
// they do at boot and at screen transitions -- gets several frames' worth
// of emulated time, and several frames' worth of samples, in one pass. The
// third frame buys the drift correction a little room above the resting
// depth to absorb a small step without clipping anything.
//
// It does not, and cannot, contain a step. See kMaxQueuedSamples: a step is
// not bounded by anything, and the band above was sized for drift.
inline constexpr std::size_t kLowWaterSamples = static_cast<std::size_t>(kSamplesPerFrame);
inline constexpr std::size_t kTargetQueuedSamples = static_cast<std::size_t>(2.0 * kSamplesPerFrame);
inline constexpr std::size_t kHighWaterSamples = static_cast<std::size_t>(3.0 * kSamplesPerFrame);

// The ceiling -- a different job from the marks above, and the reason they
// are not enough on their own.
//
// The band above answers drift: two crystals pulling apart by a few parts
// per million, which one sample a frame outruns six times over. It cannot
// answer a step, and the frame loop produces steps. A pass runs until the
// PPU completes a frame, and the PPU does not complete frames while the LCD
// is off, so a pass over an LCD-off stretch emulates however long the
// program leaves the LCD off and pushes all of it at once. Measured here
// with a ROM that switches the LCD off for 0.44 s: 22,332 samples out of a
// single pass, and the queue 21,000 samples deeper afterwards -- 437 ms of
// latency that one sample a frame needs six minutes to walk back, arriving
// once per screen transition. There is no number of frames that contains
// that, because the program chooses it.
//
// So above some depth the answer is not to correct more gently, it is to
// stop pretending the backlog is drift and cut it. Four frames, 66.9 ms:
//
//   - A frame above the high-water mark, so nothing that is merely drift or
//     one late frame can reach it. One sample a frame takes 13.5 seconds to
//     cross a frame's worth of samples, and the queue is read every frame,
//     so the gentle policy always gets its turn first and this never fires
//     on the thing that policy was written for.
//   - Low enough that the latency it tolerates is not something you can
//     hear against the picture. ITU-R BT.1359-1 puts the detectability
//     threshold for sound lagging picture at about 125 ms (and about 45 ms
//     the other way round, sound early); 66.9 ms sits inside it, and a
//     couple of frames more would not.
//   - Deliberately not "larger than the largest step seen", which is the
//     reasoning that produced the three-frame mark and is unsound: the
//     largest step is whatever ROM you run next.
//
// Crossing it costs one discontinuity: the device's queue is thrown away
// and refilled to the two-frame target, so the sound jumps forward by the
// excess and there is a hole of up to two frames -- 33 ms -- where the
// stale audio was. That is the trade, stated plainly: one 33 ms dropout,
// once per screen transition, against a lag that never goes away.
inline constexpr std::size_t kMaxQueuedSamples = static_cast<std::size_t>(4.0 * kSamplesPerFrame);

// How much latency to throw away, given a queue of `queuedSamples`.
//
// Zero at and below the ceiling: everything down there belongs to
// driftCorrection() below, and the two must never both act on the same
// frame. Above it, the whole excess over the target goes at once -- not a
// slice of it, and not down to the ceiling, because stopping at the ceiling
// would leave two frames for the one-sample-a-frame policy to walk off and
// put the resting depth at 67 ms instead of 33.
//
// So this is a step function, on purpose: nothing, nothing, nothing, then
// all of it. Trimming a little at a time is what the policy below already
// does, and it is what does not work here.
constexpr std::size_t excessQueuedSamples(std::size_t queuedSamples) {
    if (queuedSamples <= kMaxQueuedSamples) {
        return 0;
    }
    return queuedSamples - kTargetQueuedSamples;
}

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

    // The ceiling, applied. `queuedSamples` is what queued() just said, so
    // the caller reads the device once and both policies see the same
    // number. Returns the stereo samples of latency thrown away, or zero if
    // the backlog was still inside the range the drift policy owns -- in
    // which case nothing at all happened and the caller should go on to
    // apply driftCorrection() as usual.
    //
    // The cut itself is reprime()'s: clear what has not been played and
    // refill to the two-frame target. This frame's samples then land on top
    // of that, so the queue is one frame deep past the target for exactly
    // one frame before the device eats the difference.
    std::size_t trimBacklog(std::size_t queuedSamples);

    // Tears the device down early, before SDL_Quit(): destroys the audio
    // stream and marks the device closed, exactly what the destructor does
    // for a stream still open at that point. main() calls this explicitly,
    // alongside SDL_DestroyTexture/Renderer/Window, so the stream is never
    // destroyed during stack unwinding after SDL_Quit() has already run --
    // SDL_DestroyAudioStream reaches into memory SDL_Quit() may have freed.
    // Safe to call on a device that never opened, and safe to call twice.
    void close();

    // Counters, for FOURSHADES_AUDIO_LOG. `drops` and `repeats` are how
    // many frames the drift policy corrected; min and max are the extremes
    // the queue reached, and are the numbers that say whether the marks are
    // in the right place.
    // `trims` and `trimmedSamples` are the ceiling's, and they are the
    // numbers that say whether the ceiling is where it belongs: trims should
    // be zero on an undisturbed run and one per LCD-off stretch otherwise,
    // never one per frame. One per frame would be the ceiling doing the
    // drift policy's job, which means it is too low.
    std::uint64_t pushedSamples() const { return pushedSamples_; }
    std::uint64_t drops() const { return drops_; }
    std::uint64_t repeats() const { return repeats_; }
    std::uint64_t trims() const { return trims_; }
    std::uint64_t trimmedSamples() const { return trimmedSamples_; }
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
    std::uint64_t trims_ = 0;
    std::uint64_t trimmedSamples_ = 0;
    std::uint64_t observations_ = 0;
    std::size_t minQueued_ = 0;
    std::size_t maxQueued_ = 0;
};

} // namespace app
