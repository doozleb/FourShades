#include <doctest/doctest.h>

#include "app/Screen.h"

#include <stdexcept>

using app::Palette;
using app::fitRect;
using app::shadeToRgb;

TEST_CASE("grey shades are the four values the reference images use") {
    CHECK(shadeToRgb(Palette::Grey, 0) == 0xFFFFFFu);
    CHECK(shadeToRgb(Palette::Grey, 1) == 0xAAAAAAu);
    CHECK(shadeToRgb(Palette::Grey, 2) == 0x555555u);
    CHECK(shadeToRgb(Palette::Grey, 3) == 0x000000u);
}

TEST_CASE("green shades are the classic DMG palette") {
    CHECK(shadeToRgb(Palette::Green, 0) == 0x9BBC0Fu);
    CHECK(shadeToRgb(Palette::Green, 1) == 0x8BAC0Fu);
    CHECK(shadeToRgb(Palette::Green, 2) == 0x306230u);
    CHECK(shadeToRgb(Palette::Green, 3) == 0x0F380Fu);
}

TEST_CASE("a shade above 3 is rejected, not masked") {
    // A masking bug (e.g. shade & 3) would make shade 4 alias shade 0 and
    // shade 7 alias shade 3, silently returning a colour instead of failing.
    // Assert against exactly those aliases so such a bug cannot pass by luck.
    CHECK_THROWS_AS(shadeToRgb(Palette::Grey, 4), std::runtime_error);
    CHECK_THROWS_AS(shadeToRgb(Palette::Grey, 7), std::runtime_error);
    CHECK_THROWS_AS(shadeToRgb(Palette::Grey, 255), std::runtime_error);
    CHECK_THROWS_AS(shadeToRgb(Palette::Green, 4), std::runtime_error);
}

TEST_CASE("640x576 is exactly scale 4 with no border") {
    const app::Rect rect = fitRect(640, 576);
    CHECK(rect.scale == 4);
    CHECK(rect.w == 640);
    CHECK(rect.h == 576);
    CHECK(rect.x == 0);
    CHECK(rect.y == 0);
}

TEST_CASE("700x600 is scale 4 centred with a border, not a fractional scale") {
    // 700/160 = 4.375 and 600/144 = 4.166..: a fractional scaler might round
    // to something between 4 and 5, or crop unevenly. Only an exact integer
    // scale of 4, with the exact leftover centred, is correct.
    const app::Rect rect = fitRect(700, 600);
    CHECK(rect.scale == 4);
    CHECK(rect.w == 640);
    CHECK(rect.h == 576);
    CHECK(rect.x == 30);
    CHECK(rect.y == 12);
}

TEST_CASE("a window smaller than 160x144 gets scale 1, not scale 0") {
    const app::Rect rect = fitRect(100, 90);
    CHECK(rect.scale == 1);
    CHECK(rect.w == 160);
    CHECK(rect.h == 144);
    // The picture is larger than the window, so it is centred off the edges
    // rather than shrunk to nothing.
    CHECK(rect.x == -30);
    CHECK(rect.y == -27);
}

TEST_CASE("an exact 160x144 window is scale 1 with no border") {
    const app::Rect rect = fitRect(160, 144);
    CHECK(rect.scale == 1);
    CHECK(rect.w == 160);
    CHECK(rect.h == 144);
    CHECK(rect.x == 0);
    CHECK(rect.y == 0);
}
