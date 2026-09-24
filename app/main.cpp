// The playable window. Loads a ROM, runs it, presents each frame Ppu
// produces and feeds it the keyboard, paced to the DMG's own 59.727 Hz
// rather than the monitor's 60 Hz: piece 5's audio will be clocked off the
// same real-time pacing this loop establishes, and any drift here would
// become audible drift there.
//
// With a ROM path on the command line, this behaves exactly as a script
// expects: load it, run it, or fail loudly and exit non-zero. With no
// argument -- the double-click case, where a failed process has no console
// to explain itself in -- the window opens anyway, waiting for a ROM to be
// dropped onto it. A ROM that fails to load, from a drop, reports
// Cartridge::load's message and returns to waiting rather than closing the
// window.
//
// Sound comes out of the same loop. The machine is pumped into
// app::AudioResampler after every step and the frame's samples are handed
// to app::Audio once a frame; if the sound device will not open, the
// emulator runs anyway, silently, with the reason printed once. A missing
// or busy sound card is not a reason to refuse to play a game.
//
// Keys: arrows, Z, X, Enter and Backspace are the joypad; P toggles the
// palette; M mutes; Space pauses; R resets.
#include "app/AppController.h"
#include "app/Audio.h"
#include "app/AudioResampler.h"
#include "app/FramePacer.h"
#include "app/Input.h"
#include "app/Save.h"
#include "app/Screen.h"
#include "core/Ppu.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

using app::AppController;
using app::AppState;
using fourshades::GameBoy;
using fourshades::Ppu;
using fourshades::u8;

// The frame budget, from the DMG's real frame rate (4194304 Hz / 70224
// dots-per-frame) rather than the monitor's 60 Hz. See the file comment and
// app/FramePacer.h; the arithmetic lives there so it can be tested.
const Uint64 kFrameNs = app::framePeriodNs(app::kDmgFrameHz);

// SDL_RenderDebugText's font, enlarged so the waiting prompt is legible
// rather than squinted at (see SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE).
constexpr float kPromptTextScale = 2.0f;

const std::string kPrompt = "Drop a Game Boy ROM here";

// What counts as a burst in the log below: more than two frames' worth of
// samples out of a single pass round the loop. Two rather than one because
// the number of samples a frame is due is not an integer, so an ordinary
// frame lands on 803 or 804 and a threshold of one frame would fire on
// arithmetic rather than on anything real.
constexpr std::size_t kBurstSamples = static_cast<std::size_t>(2.0 * app::kSamplesPerFrame);

std::optional<std::vector<u8>> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::vector<u8>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The ROM that is running, and what its save file turned out to be. The
// status is what decides whether this session is allowed to write that file
// when it ends -- see app::mayWriteSave.
struct Session {
    std::string romPath;
    app::LoadStatus saveStatus = app::LoadStatus::NoBattery;
};

// Writes the running machine's battery RAM back to disk, and says on stdout
// that it did. Called on a clean exit and before a dropped ROM replaces the
// machine that holds the RAM.
void saveSession(AppController& controller, const Session& session) {
    if (controller.state() != AppState::Running || session.romPath.empty()) {
        return;
    }
    if (!app::mayWriteSave(session.saveStatus)) {
        return;
    }
    const std::filesystem::path savePath = app::savePathFor(session.romPath);
    const app::SaveResult result = app::writeSave(controller.gameBoy().cartridge(), savePath);
    if (result.status == app::SaveStatus::Written) {
        std::printf("saved %zu bytes to %s\n", result.bytes, savePath.string().c_str());
        std::fflush(stdout);
    } else if (result.status == app::SaveStatus::Failed) {
        std::fprintf(stderr, "could not save: %s\n", result.message.c_str());
    }
}

// Restores the freshly loaded machine's battery RAM, and reports what it
// found. A save that cannot belong to this cartridge is left untouched on
// disk, and the status it returns stops this session writing over it.
app::LoadStatus restoreSession(AppController& controller, const std::string& romPath) {
    const std::filesystem::path savePath = app::savePathFor(romPath);
    const app::LoadResult result = app::loadSave(controller.gameBoy().cartridge(), savePath);
    switch (result.status) {
    case app::LoadStatus::Loaded:
        std::printf("loaded save %s\n", savePath.string().c_str());
        std::fflush(stdout);
        break;
    case app::LoadStatus::NoFile:
        std::printf("no save yet; one will be written to %s on exit\n", savePath.string().c_str());
        std::fflush(stdout);
        break;
    case app::LoadStatus::Refused:
        std::fprintf(stderr, "%s\nthis session will not write to it: move it aside to start a new save\n",
                     result.message.c_str());
        break;
    case app::LoadStatus::NoBattery:
        break;
    }
    return result.status;
}

// Loads the ROM at `path` into `controller`. On failure, leaves `controller`
// waiting and fills `error` with the text to show: either the read failure,
// or Cartridge::load's message verbatim (via AppController::lastError()).
//
// On success the ROM's save is restored, and `session` becomes that ROM's --
// after the machine it replaces has written its own RAM back, since loading
// destroys it.
bool loadRomFromPath(AppController& controller, const std::string& path, std::string& error,
                     Session& session) {
    std::optional<std::vector<u8>> rom = readFile(path);
    if (!rom.has_value()) {
        error = "cannot read " + path;
        return false;
    }
    saveSession(controller, session);
    if (!controller.loadRom(std::move(*rom))) {
        error = controller.lastError();
        session = Session{};
        return false;
    }
    session.romPath = path;
    session.saveStatus = restoreSession(controller, path);
    error.clear();
    return true;
}

// Prints what the frame loop actually achieved, when FOURSHADES_PACE_LOG is
// set to anything. Opt-in rather than always on: the numbers are how the
// pacing claim in the design spec is kept honest, so they have to be
// reproducible by anyone, but a player closing a game does not want a
// statistics line.
void reportPacing(const app::FrameRateMeter& meter) {
    if (SDL_getenv("FOURSHADES_PACE_LOG") == nullptr || meter.intervals() == 0) {
        return;
    }
    std::printf("pacing: %llu frames, mean %.4f Hz (%.1f ns), sd %.1f ns, min %llu ns, max %llu ns\n",
                static_cast<unsigned long long>(meter.intervals() + 1), meter.meanHz(),
                meter.meanIntervalNs(), meter.stddevIntervalNs(),
                static_cast<unsigned long long>(meter.minIntervalNs()),
                static_cast<unsigned long long>(meter.maxIntervalNs()));
    std::fflush(stdout);
}

// What the drift policy actually had to do, when FOURSHADES_AUDIO_LOG is
// set to anything. Opt-in for the same reason as reportPacing: the numbers
// are how the claim that the water marks are in the right place stays
// honest, and a player closing a game does not want a statistics line.
//
// What a working policy looks like here, measured: 100 seconds undisturbed
// is 5,955 frames, 7 drops and 5 repeats, and a queue sawtoothing between
// 1,300 and 1,800 -- the two-frame target, corrected 0.2% of the time.
//
// What a broken one looks like: a queue that climbs past the high-water
// mark and stays there with the drop count rising every single frame, which
// is the policy saturated and losing, or a minimum of zero outside a pause,
// which is the device starving. A single step up of a couple of thousand
// samples is neither -- it is a pass that emulated more than one frame's
// worth of time, which the machine really did run, and the policy spends
// half a minute absorbing it.
void reportAudio(const app::Audio& audio, const app::AudioResampler& resampler) {
    if (SDL_getenv("FOURSHADES_AUDIO_LOG") == nullptr) {
        return;
    }
    if (!audio.isOpen()) {
        std::printf("audio: device not open (%s)\n",
                    audio.failure().empty() ? "never attempted" : audio.failure().c_str());
        std::fflush(stdout);
        return;
    }
    std::printf("audio: %llu samples emitted, %llu pushed, queue min %zu max %zu "
                "(marks %zu..%zu), %llu drops, %llu repeats over %llu frames%s\n",
                static_cast<unsigned long long>(resampler.emitted()),
                static_cast<unsigned long long>(audio.pushedSamples()), audio.minQueued(),
                audio.maxQueued(), app::kLowWaterSamples, app::kHighWaterSamples,
                static_cast<unsigned long long>(audio.drops()),
                static_cast<unsigned long long>(audio.repeats()),
                static_cast<unsigned long long>(audio.observations()),
                resampler.muted() ? ", muted" : "");
    std::fflush(stdout);
}

void setDrawColor(SDL_Renderer* renderer, std::uint32_t rgb) {
    SDL_SetRenderDrawColor(renderer, static_cast<Uint8>((rgb >> 16) & 0xFF),
                            static_cast<Uint8>((rgb >> 8) & 0xFF), static_cast<Uint8>(rgb & 0xFF), 255);
}

constexpr float kCharSize = static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);

// Draws `text` as one line at `scale`, vertically at device-pixel `topPx`,
// centred horizontally unless it's wider than the window, in which case it
// is pinned to a small left margin instead of running off both edges.
// Returns the line's height in device pixels.
float drawTextLine(SDL_Renderer* renderer, const std::string& text, float scale, int windowW, float topPx) {
    SDL_SetRenderScale(renderer, scale, scale);
    const float textW = kCharSize * static_cast<float>(text.size());
    const float scaledW = static_cast<float>(windowW) / scale;
    const float x = (textW <= scaledW) ? (scaledW - textW) / 2.0f : (8.0f / scale);
    SDL_RenderDebugText(renderer, x, topPx / scale, text.c_str());
    return kCharSize * scale;
}

// The waiting state: a plain window in the current palette's lightest
// shade, inviting a ROM to be dropped in. Not a splash screen -- just the
// prompt, and the last drop's error underneath it, if there was one.
void renderWaiting(SDL_Renderer* renderer, app::Palette palette, int windowW, int windowH,
                    const std::string& message) {
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    setDrawColor(renderer, app::shadeToRgb(palette, 0));
    SDL_RenderClear(renderer);
    setDrawColor(renderer, app::shadeToRgb(palette, 3));

    // The prompt is short and fixed, so it always fits at kPromptTextScale.
    // The message is whatever Cartridge::load (or a bad path) produced, and
    // can be long, so its scale shrinks -- down to 1x -- to keep it on
    // screen rather than clipped at both edges.
    const float promptHeightPx = kCharSize * kPromptTextScale;
    float messageScale = kPromptTextScale;
    float messageHeightPx = 0.0f;
    const float gapPx = promptHeightPx;
    if (!message.empty()) {
        const float rawWidth = kCharSize * static_cast<float>(message.size());
        const float maxWidth = static_cast<float>(windowW) - 16.0f;
        if (rawWidth * messageScale > maxWidth && maxWidth > 0.0f) {
            messageScale = std::max(1.0f, maxWidth / rawWidth);
        }
        messageHeightPx = kCharSize * messageScale;
    }

    const float blockHeightPx = promptHeightPx + (message.empty() ? 0.0f : gapPx + messageHeightPx);
    const float topPx = (static_cast<float>(windowH) - blockHeightPx) / 2.0f;

    drawTextLine(renderer, kPrompt, kPromptTextScale, windowW, topPx);
    if (!message.empty()) {
        drawTextLine(renderer, message, messageScale, windowW, topPx + promptHeightPx + gapPx);
    }

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

// The pause badge: a dark plate and one word over the top of the frozen
// frame, because a still picture with no label looks exactly like an
// emulator that has hung.
void renderPausedBadge(SDL_Renderer* renderer, int windowW) {
    const std::string label = "PAUSED";
    constexpr float kScale = 2.0f;
    const float textW = kCharSize * static_cast<float>(label.size()) * kScale;
    const float textH = kCharSize * kScale;
    const SDL_FRect plate{(static_cast<float>(windowW) - textW) / 2.0f - 8.0f, 8.0f, textW + 16.0f,
                          textH + 8.0f};
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, &plate);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    drawTextLine(renderer, label, kScale, windowW, 12.0f);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::fprintf(stderr, "usage: %s [rom-path]\n", argc > 0 ? argv[0] : "fourshades_app");
        return 1;
    }

    AppController controller;
    Session session;
    if (argc == 2) {
        // Unchanged from before: a bad path or a bad ROM on the command
        // line is a script's problem, reported and fatal -- never the
        // waiting window.
        const std::string romPath = argv[1];
        std::string error;
        if (!loadRomFromPath(controller, romPath, error, session)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    constexpr int kInitialScale = 4;
    SDL_Window* window = SDL_CreateWindow("FourShades", Ppu::kWidth * kInitialScale,
                                          Ppu::kHeight * kInitialScale, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, Ppu::kWidth, Ppu::kHeight);
    if (texture == nullptr) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    // Nearest-neighbour: a Game Boy pixel is a hard-edged block of screen
    // pixels, never blurred.
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    // The sound device, and the thing that turns cycles into samples for
    // it. A device that will not open says so once and is then a no-op
    // everywhere: the resampler still runs, the loop is unchanged, and the
    // game plays silently. Unlike the window, this is never fatal.
    app::Audio audio;
    app::AudioResampler resampler;
    if (!audio.open()) {
        std::fprintf(stderr, "no sound: %s\nthe emulator will run silently\n",
                     audio.failure().c_str());
    }
    // Read once: the summary at exit is not enough to tell a queue that
    // oscillates inside the band from one that walks steadily towards a
    // mark, and only a series of readings over a long run can. Every five
    // seconds, and only when asked for.
    //
    // Five seconds is right for watching for a walk, which takes tens of
    // seconds to show, and far too coarse for watching a burst, which the
    // ceiling clears inside a frame. FOURSHADES_AUDIO_LOG_EVERY overrides
    // the interval in frames, so the same instrument serves both: 300 to
    // see a drift, 6 to see a step and its recovery ten times a second.
    const bool audioLog = SDL_getenv("FOURSHADES_AUDIO_LOG") != nullptr;
    const std::uint64_t kAudioLogEveryFrames = [] {
        const char* every = SDL_getenv("FOURSHADES_AUDIO_LOG_EVERY");
        if (every == nullptr) {
            return std::uint64_t{300};
        }
        const long long frames = std::atoll(every);
        return frames > 0 ? static_cast<std::uint64_t>(frames) : std::uint64_t{300};
    }();
    std::uint64_t audioLogFrames = 0;
    // Every moment the queue stops belonging to the machine that filled it:
    // a reset, a dropped ROM, and coming back from a pause.
    const auto reprimeAudio = [&audio, &audioLog](const char* why) {
        const std::size_t added = audio.reprime();
        if (audioLog) {
            std::printf("audio: reprimed on %s, %zu samples of silence added\n", why, added);
            std::fflush(stdout);
        }
    };

    app::Palette palette = app::Palette::Grey;
    std::array<std::uint32_t, Ppu::kWidth * Ppu::kHeight> pixels{};
    // The most recent drop's failure, shown under the prompt until the next
    // successful load. Empty otherwise, including at startup.
    std::string waitingMessage;
    // Space freezes the machine where it stands; the window keeps drawing
    // the last frame it produced. Nothing about the machine changes, so
    // resuming continues the same instruction stream.
    bool paused = false;

    // What the loop achieved, as opposed to what it aimed at. See
    // reportPacing.
    app::FrameRateMeter meter;

    bool running = true;
    Uint64 nextFrameDeadline = SDL_GetTicksNS() + kFrameNs;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_P) {
                palette = (palette == app::Palette::Grey) ? app::Palette::Green : app::Palette::Grey;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                       event.key.key == SDLK_SPACE) {
                if (controller.state() == AppState::Running) {
                    paused = !paused;
                    if (!paused) {
                        // Resuming. The pause drained the device to empty --
                        // correctly, since nothing was falling due -- and
                        // coming back into an empty queue means every late
                        // frame underruns until the one-sample-a-frame
                        // correction has walked it back up, which measured
                        // at eighteen seconds of it. So the queue is primed
                        // with silence again, exactly as it was when the
                        // device opened: 33 ms of nothing at the moment the
                        // player pressed the key, instead of eighteen
                        // seconds of a starved device.
                        reprimeAudio("resume");
                    } else {
                        // A pause is the one moment the player knows they
                        // are safe, so it is worth making that true: the
                        // battery RAM goes to disk here, through the same
                        // atomic write a clean exit uses. Nothing is read
                        // back and the machine is not touched, so this can
                        // only ever give the save one more chance to
                        // survive.
                        saveSession(controller, session);
                    }
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_M) {
                // Mute lives in the resampler, not on the device: samples
                // keep falling due and keep being pushed, they are just
                // zeroes. The queue therefore behaves identically muted and
                // unmuted, so the drift policy never has to know about
                // this, and the stream is never starved by a mute.
                resampler.setMuted(!resampler.muted());
                std::printf("%s\n", resampler.muted() ? "muted" : "unmuted");
                std::fflush(stdout);
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_R) {
                // Rebuilt from the cartridge by AppController, never poked
                // back into shape here. A reset always resumes: coming back
                // from R to a still picture would look like a crash.
                if (controller.reset()) {
                    paused = false;
                    // The machine underneath the resampler is a new one and
                    // its cycle count restarts at zero, so re-anchor and
                    // discharge the capacitor. Whatever the old program had
                    // already queued is thrown away rather than played over
                    // the top of the new one's first frames.
                    resampler.reset(controller.gameBoy().cycles());
                    reprimeAudio("reset");
                }
            } else if (event.type == SDL_EVENT_DROP_FILE) {
                const std::string path = event.drop.data != nullptr ? event.drop.data : "";
                std::string error;
                if (loadRomFromPath(controller, path, error, session)) {
                    waitingMessage.clear();
                    paused = false;
                    // Same as a reset: a different machine, a cycle count
                    // that restarts, and a queue that belongs to the
                    // program that just went away.
                    resampler.reset(controller.gameBoy().cycles());
                    reprimeAudio("a dropped ROM");
                } else {
                    waitingMessage = error;
                }
            }
        }
        if (!running) {
            break;
        }

        int windowW = 0;
        int windowH = 0;
        SDL_GetWindowSizeInPixels(window, &windowW, &windowH);

        // Whether this pass round the loop actually advanced the machine by
        // a frame. A paused or ROM-less pass still draws and still paces,
        // but it is not a frame of emulation and must not be measured as
        // one.
        bool emulatedFrame = false;

        if (controller.state() == AppState::Running) {
            GameBoy& gameBoy = controller.gameBoy();

            if (!paused) {
                // The whole keyboard, once per frame, just before the frame that
                // will see it runs. SDL has already drained this frame's key
                // events into that array above, so a press and a release inside
                // one frame is the one case this misses -- 16.7 ms of held key,
                // which no human produces and no game could act on anyway.
                int numKeys = 0;
                const bool* keys = SDL_GetKeyboardState(&numKeys);
                gameBoy.setButtons(app::buttonMask(keys, numKeys));

                // Driven by the frame counter, not a fixed cycle count, so this
                // loop stays correct if the PPU's own timing is ever refined.
                const std::uint64_t before = gameBoy.ppu().frameCount();
                while (gameBoy.ppu().frameCount() == before) {
                    gameBoy.step();
                    // After each step, not once at the end of the frame:
                    // Apu::sample() answers "what is the level right now",
                    // so asking it 800 times spread across the frame is
                    // what makes the output the machine's waveform rather
                    // than one reading of it per frame.
                    resampler.pump(gameBoy.cycles(), gameBoy.apu());
                }
                emulatedFrame = true;
            }

            // A pass should emulate one frame and produce one frame's
            // samples. One that produces several frames' worth has emulated
            // several frames of machine time in a single pass, and that --
            // not drift -- is what puts a step in the queue. The step is
            // invisible in the queue depth once it has been absorbed, and
            // indistinguishable from a device that fell behind while it is
            // there, so the log names it where it happens instead of leaving
            // it to be inferred afterwards.
            if (audioLog && resampler.pending() > kBurstSamples) {
                std::printf("audio: one pass emulated %zu samples (%.0f ms of machine time)\n",
                            resampler.pending(),
                            1000.0 * static_cast<double>(resampler.pending()) /
                                static_cast<double>(app::kAudioSampleRate));
                std::fflush(stdout);
            }

            // One frame's samples, once a frame, with at most one sample of
            // drift correction -- see app/Audio.h for the marks and the
            // reasoning. The queue is read once and the same number is both
            // logged and handed to the policy, so the log reports what the
            // policy actually saw.
            //
            // A paused pass reaches here with an empty buffer and
            // emulatedFrame false, so nothing is pushed and nothing is
            // repeated: the device runs dry and plays silence, which is
            // what a pause should sound like. Within this Running branch,
            // the clear() sits outside the paused/unpaused split above it,
            // because the buffer has to be drained whether or not there is
            // a device to drain it into.
            const std::size_t queuedSamples = audio.queued();
            audio.observeQueued(queuedSamples);

            // The ceiling first, and only then the drift policy -- two
            // different jobs in the order that makes them independent. The
            // ceiling asks "is this still drift at all"; below it the answer
            // is yes and it does nothing, so the drift policy gets every
            // frame it was designed for. Above it the backlog is cut and the
            // queue is back at the target, so the drift policy is then
            // handed a queue inside the band and has nothing to correct.
            // Neither ever sees a frame the other has already acted on.
            const std::size_t trimmed = audio.trimBacklog(queuedSamples);
            if (trimmed > 0 && audioLog) {
                std::printf("audio: backlog trimmed, %zu samples (%.0f ms) discarded from %zu\n",
                            trimmed,
                            1000.0 * static_cast<double>(trimmed) /
                                static_cast<double>(app::kAudioSampleRate),
                            queuedSamples);
                std::fflush(stdout);
            }
            const std::size_t correctedFrom = trimmed > 0 ? audio.queued() : queuedSamples;
            audio.push(resampler.samples(), app::driftCorrection(emulatedFrame, correctedFrom));
            resampler.clear();

            if (audioLog && ++audioLogFrames % kAudioLogEveryFrames == 0) {
                std::printf("audio @%llu: queued %zu, drops %llu, repeats %llu%s\n",
                            static_cast<unsigned long long>(audioLogFrames), queuedSamples,
                            static_cast<unsigned long long>(audio.drops()),
                            static_cast<unsigned long long>(audio.repeats()),
                            resampler.muted() ? ", muted" : "");
                std::fflush(stdout);
            }

            // Outside the pause: the frame is re-coloured and re-uploaded
            // every time round, so P still repaints a frozen picture.
            const std::array<u8, Ppu::kWidth * Ppu::kHeight>& frame = gameBoy.ppu().frame();
            for (std::size_t i = 0; i < frame.size(); ++i) {
                pixels[i] = app::shadeToRgb(palette, frame[i]);
            }
            SDL_UpdateTexture(texture, nullptr, pixels.data(),
                              Ppu::kWidth * static_cast<int>(sizeof(std::uint32_t)));

            const app::Rect rect = app::fitRect(windowW, windowH);
            const SDL_FRect dst{static_cast<float>(rect.x), static_cast<float>(rect.y),
                                static_cast<float>(rect.w), static_cast<float>(rect.h)};

            SDL_SetRenderScale(renderer, 1.0f, 1.0f);
            SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255); // neutral border colour
            SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer, texture, nullptr, &dst);
            if (paused) {
                renderPausedBadge(renderer, windowW);
            }
        } else {
            renderWaiting(renderer, palette, windowW, windowH, waitingMessage);
        }
        SDL_RenderPresent(renderer);

        const Uint64 now = SDL_GetTicksNS();
        if (emulatedFrame) {
            meter.sample(now);
        } else {
            meter.gap();
        }

        // Pace to the DMG's real rate. app::paceFrame owns the rule for what
        // a late frame costs; this only reads the clock and sleeps.
        const app::PaceStep step = app::paceFrame(now, nextFrameDeadline, kFrameNs);
        if (step.sleepNs > 0) {
            SDL_DelayNS(step.sleepNs);
        }
        nextFrameDeadline = step.nextDeadline;
    }

    reportPacing(meter);
    reportAudio(audio, resampler);

    // A clean exit: the window was closed, so the battery RAM goes back to
    // disk before anything is torn down.
    saveSession(controller, session);

    // Explicit, like the three below it: SDL_DestroyAudioStream must run
    // before SDL_Quit() tears the library down, not after it, during stack
    // unwinding, once SDL_Quit() may have already freed what it reaches
    // into.
    audio.close();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
