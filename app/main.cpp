// The playable window (no input yet -- that is the next task). Loads a ROM,
// runs it, and presents each frame Ppu produces, paced to the DMG's own
// 59.727 Hz rather than the monitor's 60 Hz: piece 5's audio will be clocked
// off the same real-time pacing this loop establishes, and any drift here
// would become audible drift there.
#include "app/Screen.h"
#include "core/Cartridge.h"
#include "core/GameBoy.h"
#include "core/Ppu.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

using fourshades::Cartridge;
using fourshades::GameBoy;
using fourshades::Ppu;
using fourshades::u8;

// The DMG's real frame rate (4194304 Hz / 70224 dots-per-frame), not the
// monitor's 60 Hz. See the file comment.
constexpr double kFrameHz = 59.727;
constexpr Uint64 kFrameNs = static_cast<Uint64>(1'000'000'000.0 / kFrameHz + 0.5);

std::optional<std::vector<u8>> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::vector<u8>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <rom-path>\n", argc > 0 ? argv[0] : "fourshades_app");
        return 1;
    }
    const std::string romPath = argv[1];

    std::optional<std::vector<u8>> rom = readFile(romPath);
    if (!rom.has_value()) {
        std::fprintf(stderr, "cannot read %s\n", romPath.c_str());
        return 1;
    }

    std::string error;
    std::optional<Cartridge> cartridge = Cartridge::load(std::move(*rom), &error);
    if (!cartridge.has_value()) {
        // Verbatim: whatever Cartridge::load put in *error, unaltered.
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    GameBoy gameBoy(std::move(*cartridge));

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

    bool running = true;
    Uint64 nextFrameDeadline = SDL_GetTicksNS() + kFrameNs;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_P) {
                palette = (palette == app::Palette::Grey) ? app::Palette::Green : app::Palette::Grey;
            }
        }
        if (!running) {
            break;
        }

        // Driven by the frame counter, not a fixed cycle count, so this loop
        // stays correct if the PPU's own timing is ever refined.
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

        int windowW = 0;
        int windowH = 0;
        SDL_GetWindowSizeInPixels(window, &windowW, &windowH);
        const app::Rect rect = app::fitRect(windowW, windowH);
        const SDL_FRect dst{static_cast<float>(rect.x), static_cast<float>(rect.y),
                            static_cast<float>(rect.w), static_cast<float>(rect.h)};

        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255); // neutral border colour
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, &dst);
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
