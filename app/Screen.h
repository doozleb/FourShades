#pragma once

#include "core/Types.h"

#include <cstdint>

namespace app {

using fourshades::u8;

// The palette to shade Ppu::frame()'s shade indices (0-3) with. Grey is the
// default: it matches the four grey values baked into the reference images
// the scoreboard compares against, so what appears on screen is exactly what
// is being judged. Green is the classic DMG look.
enum class Palette { Grey, Green };

// Maps a shade index to a packed 0xRRGGBB colour for the given palette.
// Throws std::runtime_error if shade is outside 0-3: masking an out-of-range
// shade (e.g. `shade & 3`) would hide a real bug in the caller instead of
// surfacing it, and this project has been bitten by that before.
std::uint32_t shadeToRgb(Palette palette, u8 shade);

// Where and how large to draw the 160x144 picture inside a windowW x
// windowH window: the largest whole multiple of 160x144 that fits, centred,
// with any remainder left as a border. Integer scaling only, so a Game Boy
// pixel is always an exact N x N block of screen pixels, never a fraction of
// one. A window smaller than 160x144 in either dimension still gets scale 1
// (the picture is clipped, not shrunk to nothing).
struct Rect {
    int scale;
    int x;
    int y;
    int w;
    int h;
};

Rect fitRect(int windowW, int windowH);

} // namespace app
