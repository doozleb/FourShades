#include "core/Ppu.h"

#include "core/Interrupts.h"

namespace fourshades {

u8 Ppu::tick() {
    u8 requested = 0;
    if (!lcdOn()) {
        statQuirk_ = 0;
        return requested;
    }
    for (int i = 0; i < 4; ++i) {
        stepDot(requested);
    }
    if (statQuirk_ > 0) {
        --statQuirk_;
    }
    return requested;
}

void Ppu::stepDot(u8& requested) {
    ++dot_;
    if (dot_ >= kDotsPerLine) {
        dot_ = 0;
        line_ = line_ + 1 >= kLines ? 0 : line_ + 1;
        if (line_ == 144) {
            ++frames_;
            requested = static_cast<u8>(requested | irq::VBlank);
            setMode(1);
        } else if (line_ == 0) {
            setMode(2);
        }
    } else if (line_ < 144) {
        if (dot_ == kOamScanDots) {
            setMode(3);
        } else if (dot_ == kOamScanDots + kMinDrawDots) {
            setMode(0);
        }
    }
    if (dot_ == 0 && line_ < 144 && line_ != 0) {
        setMode(2);
    }
    updateStatLine(requested);
}

void Ppu::setMode(int mode) {
    mode_ = mode;
}

bool Ppu::lycMatch() const {
    return ly() == lyc_;
}

void Ppu::updateStatLine(u8& requested) {
    // Pan Docs "STAT": the selected conditions are OR-ed into one level line,
    // and an interrupt is requested only when that line rises. A DMG write to
    // STAT behaves as if 0xFF were written for one M-cycle, which can raise it.
    const u8 select = statQuirk_ > 0 ? 0x78 : statSelect_;
    const bool line = ((select & 0x40) != 0 && lycMatch())
                   || ((select & 0x20) != 0 && mode_ == 2)
                   || ((select & 0x10) != 0 && mode_ == 1)
                   || ((select & 0x08) != 0 && mode_ == 0);
    if (line && !statLine_) {
        requested = static_cast<u8>(requested | irq::Lcd);
    }
    statLine_ = line;
}

u8 Ppu::ly() const {
    // Pan Docs: on line 153 LY reads 153 only for the first few dots.
    if (line_ == 153 && dot_ >= 4) {
        return 0;
    }
    return static_cast<u8>(line_);
}

bool Ppu::vramBlocked() const {
    return false; // Task 3
}

bool Ppu::oamBlocked() const {
    return false; // Task 3
}

u8 Ppu::vramRead(u16 address) const {
    return peekVram(address);
}

void Ppu::vramWrite(u16 address, u8 value) {
    vram_[address - 0x8000] = value;
}

u8 Ppu::oamRead(u16 address) const {
    return peekOam(address);
}

void Ppu::oamWrite(u16 address, u8 value) {
    oam_[address - 0xFE00] = value;
}

u8 Ppu::read(u16 address) const {
    switch (address) {
    case 0xFF40: return lcdc_;
    case 0xFF41:
        return static_cast<u8>(0x80 | statSelect_ | (lycMatch() ? 0x04 : 0x00) | mode());
    case 0xFF42: return scy_;
    case 0xFF43: return scx_;
    case 0xFF44: return ly();
    case 0xFF45: return lyc_;
    case 0xFF47: return bgp_;
    case 0xFF48: return obp0_;
    case 0xFF49: return obp1_;
    case 0xFF4A: return wy_;
    case 0xFF4B: return wx_;
    default: return 0xFF;
    }
}

void Ppu::write(u16 address, u8 value) {
    switch (address) {
    case 0xFF40: {
        const bool wasOn = lcdOn();
        lcdc_ = value;
        if (wasOn && !lcdOn()) {
            line_ = 0;
            dot_ = 0;
            mode_ = 0;
            statLine_ = false;
            frame_.fill(0);
        } else if (!wasOn && lcdOn()) {
            // Pan Docs: drawing starts again immediately. The first line is
            // shorter than 456 dots; lcdon_timing-GS pins that in Task 8.
            line_ = 0;
            dot_ = 0;
            mode_ = 2;
            statLine_ = false;
        }
        break;
    }
    case 0xFF41:
        statSelect_ = static_cast<u8>(value & 0x78);
        statQuirk_ = 1;
        break;
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
