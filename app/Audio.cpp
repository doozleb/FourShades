#include "app/Audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

namespace app {

namespace {

// One stereo sample on the wire: two floats.
constexpr std::size_t kBytesPerStereoSample = 2 * sizeof(float);

} // namespace

Audio::~Audio() {
    // Only a fallback: main() calls close() explicitly, before SDL_Quit(),
    // so by the time this runs stream_ is already null. Left here for any
    // caller that does not, so a live stream is never destroyed unclosed.
    close();
}

void Audio::close() {
    if (stream_ != nullptr) {
        // Closes the device too -- that is what SDL_OpenAudioDeviceStream's
        // stream owns.
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

bool Audio::open() {
    if (stream_ != nullptr) {
        return true;
    }

    // Its own subsystem, initialised separately from video, so a machine
    // with no sound card fails here and nowhere else. main() has already
    // brought up SDL_INIT_VIDEO and will keep running whatever this says.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        failure_ = SDL_GetError();
        return false;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = kAudioSampleRate;

    // No callback: the frame loop pushes. SDL resamples to whatever the
    // device's own rate turns out to be, which is why 48 kHz can be chosen
    // for how kindly it divides the machine's clock rather than for what
    // any particular card wants.
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream_ == nullptr) {
        failure_ = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    primeWithSilence(kTargetQueuedSamples);

    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        failure_ = SDL_GetError();
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    failure_.clear();
    return true;
}

std::size_t Audio::queued() const {
    if (stream_ == nullptr) {
        return 0;
    }
    const int bytes = SDL_GetAudioStreamQueued(stream_);
    if (bytes <= 0) {
        // -1 is SDL failing to say; 0 is a device that has caught up. Both
        // are "nothing is waiting", which is the answer that makes the
        // policy repeat a sample, which is the right thing to do when the
        // queue is empty.
        return 0;
    }
    return static_cast<std::size_t>(bytes) / kBytesPerStereoSample;
}

void Audio::push(const std::vector<float>& samples, DriftCorrection correction) {
    if (stream_ == nullptr || samples.empty()) {
        return;
    }
    const std::size_t stereo = samples.size() / 2;
    // correctedSampleCount is the only statement of how much a correction
    // moves things, so this cannot disagree with what the tests check.
    const std::size_t want = correctedSampleCount(stereo, correction);
    const std::size_t whole = std::min(want, stereo);

    if (whole > 0) {
        SDL_PutAudioStreamData(stream_, samples.data(),
                               static_cast<int>(whole * kBytesPerStereoSample));
    }
    if (want > stereo) {
        // Repeat: the last stereo pair once more. Holding the level for one
        // extra sample is the smallest edit that lengthens a frame, and at
        // 21 microseconds it is a step the ear cannot resolve.
        SDL_PutAudioStreamData(stream_, samples.data() + (stereo - 1) * 2,
                               static_cast<int>(kBytesPerStereoSample));
    }

    if (want > stereo) {
        ++repeats_;
    } else if (want < stereo) {
        ++drops_;
    }
    pushedSamples_ += want;
}

std::size_t Audio::reprime() {
    if (stream_ == nullptr) {
        return 0;
    }
    SDL_ClearAudioStream(stream_);
    // Topped up to the target rather than given a flat two frames: the
    // queue is read back after the clear, so if anything survived it --
    // data already handed to the device, a clear that raced the device
    // thread -- this adds the shortfall rather than stacking two frames on
    // top of whatever was left. Blindly adding is how a reprime turns into
    // an overrun.
    const std::size_t left = queued();
    if (left >= kTargetQueuedSamples) {
        return 0;
    }
    const std::size_t shortfall = kTargetQueuedSamples - left;
    primeWithSilence(shortfall);
    return shortfall;
}

void Audio::observeQueued(std::size_t queuedSamples) {
    if (observations_ == 0 || queuedSamples < minQueued_) {
        minQueued_ = queuedSamples;
    }
    if (queuedSamples > maxQueued_) {
        maxQueued_ = queuedSamples;
    }
    ++observations_;
}

void Audio::primeWithSilence(std::size_t stereoSamples) {
    if (stream_ == nullptr || stereoSamples == 0) {
        return;
    }
    // Two frames of zeroes, so the first real frame arrives into a queue
    // that is already in the middle of the band. Without it the queue
    // starts empty and the +1-a-frame correction needs thirteen seconds to
    // walk it up to the low-water mark, thirteen seconds during which every
    // late frame is an underrun.
    const std::vector<float> silence(stereoSamples * 2, 0.0f);
    SDL_PutAudioStreamData(stream_, silence.data(),
                           static_cast<int>(stereoSamples * kBytesPerStereoSample));
}

} // namespace app
