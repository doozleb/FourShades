#include <doctest/doctest.h>

#include "roms/Screenshot.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
std::filesystem::path tempFile(const std::vector<fourshades::u8>& bytes) {
    const auto path = std::filesystem::temp_directory_path() / "fourshades-shades.bin";
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}
} // namespace

TEST_CASE("a reference file is 160x144 shade bytes") {
    const std::vector<fourshades::u8> good(roms::kFramePixels, 2);
    CHECK(roms::loadShades(tempFile(good)).size() == roms::kFramePixels);
    CHECK_THROWS_AS(roms::loadShades(tempFile({1, 2, 3})), std::runtime_error);
    std::vector<fourshades::u8> bad(roms::kFramePixels, 0);
    bad[5] = 9;
    CHECK_THROWS_AS(roms::loadShades(tempFile(bad)), std::runtime_error);
}

TEST_CASE("comparing counts differing pixels exactly") {
    std::array<fourshades::u8, roms::kFramePixels> frame{};
    std::vector<fourshades::u8> reference(roms::kFramePixels, 0);
    CHECK(roms::compareFrame(frame, reference) == 0);
    frame[100] = 3;
    CHECK(roms::compareFrame(frame, reference) == 1);
    frame[200] = 1;
    CHECK(roms::compareFrame(frame, reference) == 2);
}

TEST_CASE("a frame can be written out for inspection") {
    std::array<fourshades::u8, roms::kFramePixels> frame{};
    frame[0] = 3;
    const auto path = std::filesystem::temp_directory_path() / "fourshades-frame.pgm";
    roms::writePgm(path, frame);
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int width = 0;
    int height = 0;
    int maximum = 0;
    in >> magic >> width >> height >> maximum;
    CHECK(magic == "P5");
    CHECK(width == 160);
    CHECK(height == 144);
    CHECK(maximum == 255);
}
