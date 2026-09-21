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
        rendering_ = false;
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
        // the line is 452 dots long - that much the hardware ROM measures
        // directly. That there is therefore no OAM scan, and so no objects
        // are selected for that line, is this code's inference from it, not
        // a separate measurement: see docs/known-divergences.md.
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
            // Rendering trails the mode-3 window by PixelPipeline::kRenderLag
            // dots at both ends: the fetcher starts that many dots after mode
            // 3 does, and the pixels still in the FIFO when mode 3 ends reach
            // the LCD over the first dots of HBlank. The Mealybug Tearoom
            // images measure the near end of that gap directly, while the
            // mode boundaries, which the hardware-verified LCD timing ROMs
            // measure, do not move. See docs/known-divergences.md,
            // "Rendering runs seven dots behind the mode-3 window".
            //
            // Line 0 draws four dots ahead of every other line. Every
            // Mealybug Tearoom ppu test measures this from the outside: each
            // runs its handler off the mode-2 STAT interrupt and spends four
            // extra cycles on every line except line 0 (inc/utils.asm's
            // line_0_fix, "line 0 timing is different by 4 cycles"), and in
            // every DMG reference image line 0 then comes out identical to
            // line 1. The interrupt is not what moves: the hardware-verified
            // intr_1_2_timing-GS times the gap from the VBlank STAT interrupt
            // to this one and pins it. Nothing measured here says line 0's
            // mode boundaries move either, so only the drawing does - mode 3
            // still begins 80 dots in and still lasts 172. The line the LCD
            // was switched on is ordinary in this respect; lcdon_timing-GS
            // measures that one directly. See docs/known-divergences.md,
            // "Line 0 starts drawing four dots early".
            renderLag_ = PixelPipeline::kRenderLag - (line_ == 0 && !lcdOnLine_ ? 4 : 0);
            lineRenderLag_ = renderLag_;
            rendering_ = true;
        } else if (rendering_) {
            if (renderLag_ > 0) {
                if (--renderLag_ == 0) {
                    pipeline_.startLine(*this);
                }
            } else {
                const bool lineDrawn = pipeline_.stepDot(*this, lineBuffer_);
                if (mode_ == 3 && pipeline_.finishesWithin(*this, lineRenderLag_)) {
                    setMode(0); // the last pixels are still on their way out
                }
                if (lineDrawn) {
                    std::copy(lineBuffer_.begin(), lineBuffer_.end(),
                              frame_.begin() + static_cast<std::size_t>(line_) * kWidth);
                    rendering_ = false;
                }
            }
        }
    }
    // Pan Docs "STAT": LY=LYC is compared continuously. Hardware holds the
    // comparison off for the M-cycle in which LY changes, so the flag reads 0
    // there and only turns 1 an M-cycle later, while it still drops to 0 on
    // the M-cycle LY changes away: a one-M-cycle hole, not a delay.
    lycSuppressed_ = dot_ < 4;
    // The palette short lasts one dot: the write lands at the end of the
    // CPU's M-cycle, so the dot it colours is the first of the next one.
    paletteGlitch_ = 0x00;
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
            rendering_ = false;
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
            // raise the level line here, inside this same M-cycle - needed
            // for stat_lyc_onoff's round 4, which switches the LCD on with
            // `di` as the very next instruction.
            //
            // The mode-0 (HBlank) source is excluded from this one
            // evaluation: visibleMode_ is forced to 0 as the PPU's real
            // starting mode for this line, but no test measures whether
            // enabling the LCD with the HBlank source selected raises an
            // interrupt from it at this exact instant, so it is gated here
            // the same way the $FF41 write quirk is gated above. The mode-0
            // source still applies from the very next M-cycle onward, via
            // the normal per-tick evaluation in stepDot. See
            // docs/known-divergences.md, "Timing model".
            if (statConditions(statSelect_ & 0x70) && !statLine_) {
                requested = static_cast<u8>(requested | irq::Lcd);
            }
            statLine_ = statConditions(statSelect_ & 0x70);
        }
        break;
    }
    case 0xFF41:
        // Pan Docs "STAT" (DMG bug): a write behaves as if 0xFF had been
        // written for one cycle, so every condition that holds right now
        // feeds the level line. It happens inside the writing M-cycle, not
        // the one after it.
        //
        // Gated on the LCD being on: while it is off, visibleMode_ is forced
        // to 0, which satisfies the mode-0 (HBlank) source unconditionally,
        // so this quirk would otherwise raise irq::Lcd on any $FF41 write
        // during the off period whenever the level line happens to be low -
        // a path no test reaches. See docs/known-divergences.md, "Timing
        // model", for the evidence this restores rather than removes.
        if (lcdOn()) {
            if (statConditions(0x78) && !statLine_) {
                requested = static_cast<u8>(requested | irq::Lcd);
            }
            statLine_ = statConditions(0x78);
        }
        statSelect_ = static_cast<u8>(value & 0x78);
        break;
    case 0xFF42: scy_ = value; break;
    case 0xFF43: scx_ = value; break;
    case 0xFF45:
        // Unlike FF40 and FF41 above, a LYC write does not re-evaluate the
        // STAT level line within this M-cycle: a write that creates a fresh
        // LY=LYC match raises the interrupt one M-cycle late. This asymmetry
        // is deliberate and unmeasured - no test in the suite writes LYC into
        // a match either way - and is left alone because changing it risks
        // the hardware-verified tables that pin the other two writes (see
        // docs/known-divergences.md, "Timing model").
        lyc_ = value;
        break;
    // A palette write shorts the old and new values together for one dot:
    // Mealybug Tearoom's m3_bgp_change photographs the one-pixel seam it
    // leaves at every band edge, in a shade neither palette can produce. The
    // register itself takes the new value at once; only what the pipeline
    // shades with is affected, and only for the dot after the write.
    case 0xFF47:
        bgpGlitch_ = static_cast<u8>(bgp_ | value);
        paletteGlitch_ = static_cast<u8>(paletteGlitch_ | 0x01);
        bgp_ = value;
        break;
    case 0xFF48:
        obp0Glitch_ = static_cast<u8>(obp0_ | value);
        paletteGlitch_ = static_cast<u8>(paletteGlitch_ | 0x02);
        obp0_ = value;
        break;
    case 0xFF49:
        obp1Glitch_ = static_cast<u8>(obp1_ | value);
        paletteGlitch_ = static_cast<u8>(paletteGlitch_ | 0x04);
        obp1_ = value;
        break;
    case 0xFF4A: wy_ = value; break;
    case 0xFF4B: wx_ = value; break;
    default: break; // LY (FF44) is read-only
    }
    return requested;
}

} // namespace fourshades
