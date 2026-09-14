#include "roms/Screenshot.h"

#include <fstream>
#include <stdexcept>

namespace roms {

std::vector<u8> loadShades(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::vector<u8> shades((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (shades.size() != kFramePixels) {
        throw std::runtime_error(path.string() + ": expected " + std::to_string(kFramePixels) +
                                 " bytes, found " + std::to_string(shades.size()));
    }
    for (const u8 shade : shades) {
        if (shade > 3) {
            throw std::runtime_error(path.string() + ": not a shade index");
        }
    }
    return shades;
}

int compareFrame(const std::array<u8, kFramePixels>& frame, const std::vector<u8>& reference) {
    int differing = 0;
    for (std::size_t i = 0; i < kFramePixels; ++i) {
        if (frame[i] != reference[i]) {
            ++differing;
        }
    }
    return differing;
}

void writePgm(const std::filesystem::path& path, const std::array<u8, kFramePixels>& frame) {
    static constexpr u8 kGrey[4] = {255, 170, 85, 0};
    std::ofstream out(path, std::ios::binary);
    out << "P5\n" << kFrameWidth << " " << kFrameHeight << "\n255\n";
    for (const u8 shade : frame) {
        out.put(static_cast<char>(kGrey[shade & 3]));
    }
}

} // namespace roms
