#include "core/Joypad.h"

namespace fourshades {

namespace {
constexpr u8 kSelectDpad = 0x10;
constexpr u8 kSelectAction = 0x20;
} // namespace

u8 Joypad::lines() const {
    u8 value = 0x0F; // nothing pulling a line down
    if ((select_ & kSelectDpad) == 0) {
        value = static_cast<u8>(value & ~(pressed_ & 0x0F));
    }
    if ((select_ & kSelectAction) == 0) {
        value = static_cast<u8>(value & ~((pressed_ >> 4) & 0x0F));
    }
    return static_cast<u8>(value & 0x0F);
}

u8 Joypad::read() const {
    return static_cast<u8>(0xC0 | select_ | lines());
}

bool Joypad::setButtons(u8 pressed) {
    const u8 before = lines();
    pressed_ = pressed;
    return (before & ~lines() & 0x0F) != 0;
}

bool Joypad::write(u8 value) {
    const u8 before = lines();
    select_ = static_cast<u8>(value & 0x30);
    return (before & ~lines() & 0x0F) != 0;
}

} // namespace fourshades
