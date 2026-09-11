#pragma once

#include "core/Types.h"

namespace fourshades::irq {

// IF/IE bits. The handler for bit n is at 0x40 + 8n.
constexpr u8 VBlank = 0x01;
constexpr u8 Lcd = 0x02;
constexpr u8 Timer = 0x04;
constexpr u8 Serial = 0x08;
constexpr u8 Joypad = 0x10;

} // namespace fourshades::irq
