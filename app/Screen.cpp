#include "app/Screen.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace app {

namespace {
constexpr int kGameBoyWidth = 160;
constexpr int kGameBoyHeight = 144;
} // namespace

std::uint32_t shadeToRgb(Palette palette, u8 shade) {
    if (shade > 3) {
        throw std::runtime_error("shadeToRgb: shade " + std::to_string(static_cast<int>(shade)) +
                                  " is not a shade index");
    }
    static constexpr std::uint32_t kGrey[4] = {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000};
    static constexpr std::uint32_t kGreen[4] = {0x9BBC0F, 0x8BAC0F, 0x306230, 0x0F380F};
    return (palette == Palette::Green ? kGreen : kGrey)[shade];
}

Rect fitRect(int windowW, int windowH) {
    int scale = std::min(windowW / kGameBoyWidth, windowH / kGameBoyHeight);
    if (scale < 1) {
        scale = 1;
    }
    const int w = kGameBoyWidth * scale;
    const int h = kGameBoyHeight * scale;
    return Rect{scale, (windowW - w) / 2, (windowH - h) / 2, w, h};
}

} // namespace app
