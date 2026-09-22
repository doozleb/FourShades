// The playable window. Loads a ROM, runs it, and presents each frame Ppu
// produces, paced to the DMG's own 59.727 Hz rather than the monitor's 60
// Hz: piece 5's audio will be clocked off the same real-time pacing this
// loop establishes, and any drift here would become audible drift there.
//
// With a ROM path on the command line, this behaves exactly as a script
// expects: load it, run it, or fail loudly and exit non-zero. With no
// argument -- the double-click case, where a failed process has no console
// to explain itself in -- the window opens anyway, waiting for a ROM to be
// dropped onto it. A ROM that fails to load, from a drop, reports
// Cartridge::load's message and returns to waiting rather than closing the
// window.
#include "app/AppController.h"
#include "app/Screen.h"
#include "core/Ppu.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
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

// The DMG's real frame rate (4194304 Hz / 70224 dots-per-frame), not the
// monitor's 60 Hz. See the file comment.
constexpr double kFrameHz = 59.727;
constexpr Uint64 kFrameNs = static_cast<Uint64>(1'000'000'000.0 / kFrameHz + 0.5);

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

// Loads the ROM at `path` into `controller`. On failure, leaves `controller`
// waiting and fills `error` with the text to show: either the read failure,
// or Cartridge::load's message verbatim (via AppController::lastError()).
bool loadRomFromPath(AppController& controller, const std::string& path, std::string& error) {
    std::optional<std::vector<u8>> rom = readFile(path);
    if (!rom.has_value()) {
        error = "cannot read " + path;
        return false;
    }
    if (!controller.loadRom(std::move(*rom))) {
        error = controller.lastError();
        return false;
    }
    error.clear();
    return true;
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

} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::fprintf(stderr, "usage: %s [rom-path]\n", argc > 0 ? argv[0] : "fourshades_app");
        return 1;
    }

    AppController controller;
    if (argc == 2) {
        // Unchanged from before: a bad path or a bad ROM on the command
        // line is a script's problem, reported and fatal -- never the
        // waiting window.
        const std::string romPath = argv[1];
        std::string error;
        if (!loadRomFromPath(controller, romPath, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    constexpr int kInitialScale = 3;
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

    bool running = true;
    Uint64 nextFrameDeadline = SDL_GetTicksNS() + kFrameNs;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_P) {
                palette = (palette == app::Palette::Grey) ? app::Palette::Green : app::Palette::Grey;
            } else if (event.type == SDL_EVENT_DROP_FILE) {
                const std::string path = event.drop.data != nullptr ? event.drop.data : "";
                std::string error;
                if (loadRomFromPath(controller, path, error)) {
                    waitingMessage.clear();
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

        if (controller.state() == AppState::Running) {
            GameBoy& gameBoy = controller.gameBoy();

            // Driven by the frame counter, not a fixed cycle count, so this
            // loop stays correct if the PPU's own timing is ever refined.
            const std::uint64_t before = gameBoy.ppu().frameCount();
            while (gameBoy.ppu().frameCount() == before) {
                gameBoy.step();
            }

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
        } else {
            renderWaiting(renderer, palette, windowW, windowH, waitingMessage);
        }
        SDL_RenderPresent(renderer);

        // Pace to the DMG's real rate. If the host fell behind (a slow frame,
        // a debugger pause), don't try to burst-catch-up: just resume pacing
        // from now, one frame late, rather than spiralling.
        const Uint64 now = SDL_GetTicksNS();
        if (now < nextFrameDeadline) {
            SDL_DelayNS(nextFrameDeadline - now);
            nextFrameDeadline += kFrameNs;
        } else {
            nextFrameDeadline = now + kFrameNs;
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
