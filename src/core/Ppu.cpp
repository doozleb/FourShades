#include "core/Ppu.h"

#include "core/Interrupts.h"

#include <algorithm>

namespace fourshades {

u8 Ppu::tick() {
    u8 requested = 0;
    if (!lcdOn()) {
        return requested;
    }
    // STAT's mode field, the STAT interrupt sources and the memory locks the
    // CPU sees trail the PPU's own mode by one M-cycle, so the value the CPU
    // reads back during this M-cycle is the mode the PPU held during the last
    // one. The hardware-verified LCD timing ROMs measure the gap directly: LY
    // has already incremented and OAM is already locked for an M-cycle before
    // STAT reports mode 2. See docs/known-divergences.md, "Timing model".
    visibleMode_ = mode_;
    for (int i = 0; i < 4; ++i) {
        stepDot(requested);
    }
    return requested;
}

void Ppu::stepDot(u8& requested) {
    ++dot_;
    if (dot_ >= kDotsPerLine) {
        dot_ = 0;
        lcdOnLine_ = false;
        line_ = line_ + 1 >= kLines ? 0 : line_ + 1;
        if (line_ == 0) {
            windowReached_ = false;
            windowLine_ = 0;
        }
        if (line_ == 144) {
            ++frames_;
            requested = static_cast<u8>(requested | irq::VBlank);
            setMode(1);
        } else if (line_ < 144) {
            setMode(2);
        }
    } else if (line_ < 144) {
        // On the line the LCD was switched on there is no mode 2: the PPU
        // reports mode 0 and goes straight to mode 3 at the usual dot, and
        // the line is 452 dots long. With no OAM scan, no objects are
        // selected for that line.
        if (dot_ == kOamScanDots && (mode_ == 2 || lcdOnLine_)) {
            setMode(3);
            lineBuffer_.fill(0);
            // Ppu::windowReached() promises only that WY matched LY at some
            // point this frame, independent of whether the window happens to
            // be enabled at that instant; PixelPipeline::stepDot separately
            // checks LCDC bit 5 (window enable) before it ever draws the
            // window on a given line.
            if (line_ == wy_) {
                windowReached_ = true;
            }
            if (lcdOnLine_) {
                lineObjects_.clear();
            } else {
                scanOam();
            }
            pipeline_.startLine(*this);
        } else if (mode_ == 3) {
            if (pipeline_.stepDot(*this, lineBuffer_)) {
                std::copy(lineBuffer_.begin(), lineBuffer_.end(),
                          frame_.begin() + static_cast<std::size_t>(line_) * kWidth);
                setMode(0);
            }
        }
    }
    // Pan Docs "STAT": LY=LYC is compared continuously. Hardware holds the
    // comparison off for the M-cycle in which LY changes, so the flag reads 0
    // there and only turns 1 an M-cycle later, while it still drops to 0 on
    // the M-cycle LY changes away: a one-M-cycle hole, not a delay.
    lycSuppressed_ = dot_ < 4;
    updateStatLine(requested);
}

void Ppu::setMode(int mode) {
    mode_ = mode;
}

bool Ppu::lycMatch() const {
    // The comparison runs off the PPU's own clock: with the LCD off it does
    // not run at all and the last result is retained, so changing LYC while
    // the LCD is off has no effect, and switching the LCD back on to the same
    // result produces no fresh interrupt.
    if (!lcdOn()) {
        return lycFrozen_;
    }
    return !lycSuppressed_ && ly() == lyc_;
}

bool Ppu::oamSourceHigh() const {
    // Pan Docs "STAT" lists the mode 2 source as "OAM". On DMG it is also
    // pulsed at the top of line 144, on the very dot VBlank begins: hardware
    // times the two as simultaneous.
    return visibleMode_ == 2 || (line_ == 144 && dot_ == 0);
}

bool Ppu::statConditions(u8 select) const {
    return ((select & 0x40) != 0 && lycMatch())
        || ((select & 0x20) != 0 && oamSourceHigh())
        || ((select & 0x10) != 0 && visibleMode_ == 1)
        || ((select & 0x08) != 0 && visibleMode_ == 0);
}

void Ppu::updateStatLine(u8& requested) {
    // Pan Docs "STAT": the selected conditions are OR-ed into one level line,
    // and an interrupt is requested only when that line rises.
    const bool line = statConditions(statSelect_);
    if (line && !statLine_) {
        requested = static_cast<u8>(requested | irq::Lcd);
    }
    statLine_ = line;
}

void Ppu::scanOam() {
    lineObjects_.clear();
    const int height = objectHeight();
    for (int index = 0; index < 40 && lineObjects_.size() < 10; ++index) {
        const u16 base = static_cast<u16>(0xFE00 + index * 4);
        const u8 y = peekOam(base);
        const int top = static_cast<int>(y) - 16;
        if (line_ >= top && line_ < top + height) {
            lineObjects_.push_back(Object{y, peekOam(static_cast<u16>(base + 1)),
                                          peekOam(static_cast<u16>(base + 2)),
                                          peekOam(static_cast<u16>(base + 3)), index});
        }
    }
}

u8 Ppu::ly() const {
    // Pan Docs: on line 153 LY reads 153 only for the first few dots.
    if (line_ == 153 && dot_ >= 4) {
        return 0;
    }
    return static_cast<u8>(line_);
}

// The locks the CPU runs into. Reads are refused from the dot the PPU starts
// using the bus, which is one M-cycle before STAT admits to the new mode;
// writes are refused only while STAT reports the mode, with one exception:
// the M-cycle in which the PPU has already left mode 2 for mode 3 lets an OAM
// write through. The hardware-verified LCD timing ROMs measure every one of
// these edges on lines 0, 1 and 2; see docs/known-divergences.md.
bool Ppu::vramBlocked() const {
    return lcdOn() && (visibleMode_ == 3 || (mode_ == 3 && visibleMode_ == 2));
}

bool Ppu::oamBlocked() const {
    // mode_ == 2 covers the M-cycle in which the scan has begun but STAT
    // still reports the mode before it (mode 0 on a drawn line, mode 1 on
    // line 0); visibleMode_ covers the rest, through to the M-cycle in which
    // STAT catches up with mode 0 at the end of mode 3.
    return lcdOn() && (mode_ == 2 || visibleMode_ == 2 || visibleMode_ == 3);
}

bool Ppu::vramWriteBlocked() const {
    return lcdOn() && visibleMode_ == 3;
}

bool Ppu::oamWriteBlocked() const {
    return lcdOn() && (visibleMode_ == 3 || (visibleMode_ == 2 && mode_ == 2));
}

u8 Ppu::vramRead(u16 address) const {
    return vramBlocked() ? 0xFF : peekVram(address);
}

void Ppu::vramWrite(u16 address, u8 value) {
    if (!vramWriteBlocked()) {
        vram_[address - 0x8000] = value;
    }
}

u8 Ppu::oamRead(u16 address) const {
    return oamBlocked() ? 0xFF : peekOam(address);
}

void Ppu::oamWrite(u16 address, u8 value) {
    if (!oamWriteBlocked()) {
        oam_[address - 0xFE00] = value;
    }
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

u8 Ppu::write(u16 address, u8 value) {
    u8 requested = 0;
    switch (address) {
    case 0xFF40: {
        const bool wasOn = lcdOn();
        const bool matched = lycMatch(); // still under the old LCDC
        lcdc_ = value;
        if (wasOn && !lcdOn()) {
            // The comparison and the STAT level line both stop where they
            // are; neither is cleared, so switching the LCD back on with the
            // same result produces no new edge.
            lycFrozen_ = matched;
            line_ = 0;
            dot_ = 0;
            mode_ = 0;
            visibleMode_ = 0;
            lcdOnLine_ = false;
            lycSuppressed_ = false;
            frame_.fill(0);
            windowReached_ = false;
            windowLine_ = 0;
        } else if (!wasOn && lcdOn()) {
            // The PPU picks the line up one M-cycle in, in mode 0: line 0 is
            // 452 dots long and mode 3 still starts 80 dots into the line.
            line_ = 0;
            dot_ = 4;
            mode_ = 0;
            visibleMode_ = 0;
            lcdOnLine_ = true;
            lycSuppressed_ = false;
            // The comparison restarts inside this M-cycle, so LY=LYC can
            // raise the level line here, inside this same M-cycle.
            updateStatLine(requested);
        }
        break;
    }
    case 0xFF41:
        // Pan Docs "STAT" (DMG bug): a write behaves as if 0xFF had been
        // written for one cycle, so every condition that holds right now
        // feeds the level line. It happens inside the writing M-cycle, not
        // the one after it.
        if (statConditions(0x78) && !statLine_) {
            requested = static_cast<u8>(requested | irq::Lcd);
        }
        statLine_ = statConditions(0x78);
        statSelect_ = static_cast<u8>(value & 0x78);
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
    return requested;
}

} // namespace fourshades
