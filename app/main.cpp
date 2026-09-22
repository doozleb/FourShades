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
// Keys: arrows, Z, X, Enter and Backspace are the joypad; P toggles the
// palette; Space pauses; R resets.
#include "app/AppController.h"
#include "app/FramePacer.h"
#include "app/Input.h"
#include "app/Save.h"
#include "app/Screen.h"
#include "core/Ppu.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
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
                    if (paused) {
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
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_R) {
                // Rebuilt from the cartridge by AppController, never poked
                // back into shape here. A reset always resumes: coming back
                // from R to a still picture would look like a crash.
                if (controller.reset()) {
                    paused = false;
                }
            } else if (event.type == SDL_EVENT_DROP_FILE) {
                const std::string path = event.drop.data != nullptr ? event.drop.data : "";
                std::string error;
                if (loadRomFromPath(controller, path, error, session)) {
                    waitingMessage.clear();
                    paused = false;
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
                }
                emulatedFrame = true;
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

    // A clean exit: the window was closed, so the battery RAM goes back to
    // disk before anything is torn down.
    saveSession(controller, session);

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
