#include "core/LcdTiming.h"

namespace fourshades {

u8 LcdTiming::tick() {
    if ((lcdc_ & 0x80) == 0) {
        return 0;
    }
    if (++lineCycle_ < 114) {
        return 0;
    }
    lineCycle_ = 0;
    ly_ = static_cast<u8>(ly_ == 153 ? 0 : ly_ + 1);
    return ly_ == 144 ? 0x01 : 0x00;
}

int LcdTiming::mode() const {
    if ((lcdc_ & 0x80) == 0) {
        return 0;
    }
    if (ly_ >= 144) {
        return 1;
    }
    if (lineCycle_ < 20) {
        return 2;
    }
    return lineCycle_ < 63 ? 3 : 0;
}

u8 LcdTiming::read(u16 address) const {
    switch (address) {
    case 0xFF40: return lcdc_;
    case 0xFF41:
        return static_cast<u8>(0x80 | statSelect_ | (ly_ == lyc_ ? 0x04 : 0x00) | mode());
    case 0xFF42: return scy_;
    case 0xFF43: return scx_;
    case 0xFF44: return ly_;
    case 0xFF45: return lyc_;
    case 0xFF47: return bgp_;
    case 0xFF48: return obp0_;
    case 0xFF49: return obp1_;
    case 0xFF4A: return wy_;
    case 0xFF4B: return wx_;
    default: return 0xFF;
    }
}

void LcdTiming::write(u16 address, u8 value) {
    switch (address) {
    case 0xFF40:
        lcdc_ = value;
        if ((value & 0x80) == 0) {
            ly_ = 0;
            lineCycle_ = 0;
        }
        break;
    case 0xFF41: statSelect_ = static_cast<u8>(value & 0x78); break;
    case 0xFF42: scy_ = value; break;
    case 0xFF43: scx_ = value; break;
    case 0xFF45: lyc_ = value; break;
    case 0xFF47: bgp_ = value; break;
    case 0xFF48: obp0_ = value; break;
    case 0xFF49: obp1_ = value; break;
    case 0xFF4A: wy_ = value; break;
    case 0xFF4B: wx_ = value; break;
    default: break; // LY (FF44) is read-only
    }
}

} // namespace fourshades
