#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sst {

// Minimal SHA-256 (FIPS 180-4). Used only to check the test data against the
// committed manifest, so the score is always measured on the real tests.
class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    std::string hexDigest(); // finalises; call once

private:
    void compress(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t bufferSize_ = 0;
    std::uint64_t totalBytes_ = 0;
};

std::string sha256Hex(std::string_view data);

} // namespace sst
