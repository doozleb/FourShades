#pragma once

#include <cstdint>

namespace fourshades {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using i8 = std::int8_t;

constexpr u16 make16(u8 high, u8 low) { return static_cast<u16>((high << 8) | low); }
constexpr u8 hi(u16 value) { return static_cast<u8>(value >> 8); }
constexpr u8 lo(u16 value) { return static_cast<u8>(value & 0xFF); }

} // namespace fourshades
