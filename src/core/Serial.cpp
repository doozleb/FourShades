#include "core/Serial.h"

namespace fourshades {

bool Serial::tick(u16 counterBefore, u16 counterAfter) {
    if (bitsLeft_ == 0 || (sc_ & 0x81) != 0x81) {
        return false;
    }
    const bool fallingEdge = (counterBefore & 0x0100) != 0 && (counterAfter & 0x0100) == 0;
    if (!fallingEdge) {
        return false;
    }
    sb_ = static_cast<u8>((sb_ << 1) | 0x01);
    if (--bitsLeft_ == 0) {
        sc_ = static_cast<u8>(sc_ & 0x7F);
        return true;
    }
    return false;
}

u8 Serial::read(u16 address) const {
    return address == 0xFF01 ? sb_ : static_cast<u8>(sc_ | 0x7E);
}

void Serial::write(u16 address, u8 value) {
    if (address == 0xFF01) {
        sb_ = value;
        return;
    }
    sc_ = static_cast<u8>(value | 0x7E);
    if ((value & 0x81) == 0x81) {
        bitsLeft_ = 8;
        sent_.push_back(sb_);
    } else if ((value & 0x80) != 0) {
        bitsLeft_ = 8; // external clock: waits for a partner that never clocks
    } else {
        bitsLeft_ = 0;
    }
}

} // namespace fourshades
