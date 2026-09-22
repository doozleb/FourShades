#include "core/Ppu.h"

#include "core/Interrupts.h"

#include <algorithm>

namespace fourshades {

u8 Ppu::tick() {
    u8 requested = 0;
    // The M-cycle that was in progress has finished; anything the CPU did to
    // the OAM bus during it lands now, before the PPU moves on to the next
    // row. See flushOamCorruption.
    flushOamCorruption();
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

// Pan Docs, "Window: Window rendering criteria": "At the beginning of each
// scanline, if the value of WY is equal to LY, the Y condition becomes true
// (and remains so for subsequent scanlines)." LY is the drawn line's own
// number here; lines 144-153 are VBlank, where the window is not drawn and
// where the condition is cleared for the next frame anyway.
void Ppu::latchWindowY() {
    if (line_ == wy_) {
        windowReached_ = true;
    }
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
            // Pan Docs, "Window: Window rendering criteria": "At the
            // beginning of each scanline, if the value of WY is equal to LY,
            // the Y condition becomes true (and remains so for subsequent
            // scanlines)." That is the beginning of the line, not the
            // beginning of drawing, so a WY write landing during this line's
            // OAM scan no longer counts for this line. Nothing in the test
            // suite measures which of the two it is, so Pan Docs decides it,
            // per the rule at the top of docs/known-divergences.md.
            //
            // The check is deliberately not gated on LCDC bit 5:
            // PixelPipeline::stepDot tests window enable separately, when
            // the X counter reaches WX - 7. See docs/known-divergences.md,
            // "Timing model", for why the latch is ungated.
            latchWindowY();
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
            // extra cycles on every line except line 0, and in the one DMG
            // reference the four dots were measured against, line 0 then
            // comes out identical to line 1. The interrupt is not what
            // moves: a hardware-verified timing ROM times the gap from the
            // VBlank STAT interrupt to this one and pins it. No ROM in the
            // 165 fails either way with line 0's mode boundaries left where
            // they are, and none was found that arbitrates them, so they are
            // left alone and only the drawing moves - mode 3 still begins 80
            // dots in and still lasts at least 172. The line the LCD was
            // switched on is ordinary in this respect; the ROM that measures
            // that line measures it directly. See
            // docs/known-divergences.md, "Line 0 starts drawing four dots
            // early", for the ROMs, the macro and the evidence.
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
                if (mode_ == 3 && pipeline_.dotsRemaining(*this) <= lineRenderLag_) {
                    setMode(0); // the last pixels are still on their way out
                }
                if (lineDrawn) {
                    // Backstop: the line is finished, so HBlank has begun
                    // whatever the prediction above said. Without this, a
                    // prediction that never came true would leave mode_ at 3
                    // with the pipeline stopped - STAT reporting mode 3 and
                    // VRAM locked until the next line's mode 2. No path
                    // reaches it today: dotsRemaining() is exactly 0 at the
                    // dot the 160th pixel is emitted (pixelX_ has caught up
                    // to 160 and no activation is still pending), so the
                    // check above already fires on this dot or an earlier
                    // one. Kept as a guard against a future regression in
                    // that formula, not because anything currently needs it.
                    if (mode_ == 3) {
                        setMode(0);
                    }
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

namespace {
// OAM's data bus is 16 bits wide, so every step of the corruption patterns
// works on words. `index` counts words within an 8-byte row, 0-3.
u16 readWord(const u8* oam, int row, int index) {
    const int offset = row * 8 + index * 2;
    return static_cast<u16>(oam[offset] | (oam[offset + 1] << 8));
}

void writeWord(u8* oam, int row, int index, u16 value) {
    const int offset = row * 8 + index * 2;
    oam[offset] = static_cast<u8>(value & 0xFF);
    oam[offset + 1] = static_cast<u8>(value >> 8);
}

constexpr int kOamRows = 20;
} // namespace

int Ppu::oamScanRow() const {
    // Mode 2 is the first 80 dots of a drawn line, and the PPU reads one of
    // the 20 rows in each of its 20 M-cycles. This is asked at the end of an
    // M-cycle - GameBoy ticks the hardware and then performs the access - so
    // dot_ stands at that M-cycle's boundary, and the row that collides with
    // the access is dot_ / 4: the one whose read begins there.
    //
    // The OAM-bug test ROMs pin that window at both ends. Nineteen
    // consecutive M-cycles corrupt, which is rows 1-19 (row 0 has no
    // preceding row and cannot be corrupted), and the first of them is the
    // boundary one M-cycle into the line - dot_ == 4 here. A second ROM
    // closes both sides: it steps a 16-bit register at dot_ == 0 and at
    // dot_ == 80 on every visible line and requires OAM to come out
    // untouched. See docs/known-divergences.md, "The OAM corruption bug".
    //
    // The line the LCD was switched on has no mode 2 at all (see stepDot), so
    // nothing is being read on it and nothing can be corrupted.
    if (!lcdOn() || line_ >= 144 || lcdOnLine_) {
        return -1;
    }
    if (dot_ >= kOamScanDots) {
        return -1;
    }
    return dot_ / 4;
}

void Ppu::oamBusAccess(u16 address, Kind kind) {
    // Pan Docs: the whole of FE00-FEFF counts, the unusable FEA0-FEFF
    // stretch included - it sits on the same bus as OAM.
    if (address < 0xFE00 || address >= 0xFF00) {
        return;
    }
    const int row = oamScanRow();
    if (row < 0) {
        return;
    }
    corruptRow_ = row;
    if (kind == Kind::Read) {
        corruptRead_ = true;
    } else {
        corruptWrite_ = true;
    }
}

void Ppu::flushOamCorruption() {
    if (corruptRow_ < 0) {
        return;
    }
    const int row = corruptRow_;
    const bool read = corruptRead_;
    const bool write = corruptWrite_;
    corruptRow_ = -1;
    corruptRead_ = false;
    corruptWrite_ = false;
    if (read && write) {
        oamCorrupt(Kind::ReadWrite, row);
    } else if (read) {
        oamCorrupt(Kind::Read, row);
    } else {
        // Two writes in one M-cycle (ld (hl+),a, or the push whose write and
        // implied dec sp coincide) behave just like one: Pan Docs, "Write
        // During Increase/Decrease".
        oamCorrupt(Kind::Write, row);
    }
}

void Ppu::oamCorrupt(Kind kind, int row) {
    if (row <= 0 || row >= kOamRows) {
        return; // objects 0 and 1 are safe: row 0 has no preceding row
    }
    u8* oam = oam_.data();
    if (kind == Kind::ReadWrite) {
        // Pan Docs, "Read During Increase/Decrease": a read and a write in
        // the same M-cycle first corrupt the *preceding* row and splash it
        // over its two neighbours, and only then does an ordinary read
        // corruption follow. It is skipped for the first four rows and for
        // the last one.
        if (row >= 4 && row < kOamRows - 1) {
            const u16 a = readWord(oam, row - 2, 0);
            const u16 b = readWord(oam, row - 1, 0);
            const u16 c = readWord(oam, row, 0);
            const u16 d = readWord(oam, row - 1, 2);
            writeWord(oam, row - 1, 0, static_cast<u16>((b & (a | c | d)) | (a & c & d)));
            for (int index = 0; index < 4; ++index) {
                const u16 value = readWord(oam, row - 1, index);
                writeWord(oam, row, index, value);
                writeWord(oam, row - 2, index, value);
            }
        }
        kind = Kind::Read;
    }
    const u16 a = readWord(oam, row, 0);
    const u16 b = readWord(oam, row - 1, 0);
    const u16 c = readWord(oam, row - 1, 2);
    const u16 result = kind == Kind::Read
                           ? static_cast<u16>(b | (a & c))
                           : static_cast<u16>(((a ^ c) & (b ^ c)) ^ c);
    writeWord(oam, row, 0, result);
    for (int index = 1; index < 4; ++index) {
        writeWord(oam, row, index, readWord(oam, row - 1, index));
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
            // A palette write arms a one-dot short (see $FF47 below) that
            // only the next drawn dot disarms. With the LCD off no dot runs,
            // so clear it here rather than leave it armed across the whole
            // off period.
            paletteGlitch_ = 0x00;
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
            // This line begins here, one M-cycle in, rather than at a line
            // boundary the PPU walked through, so the Y condition is latched
            // here: it is the only "beginning of the scanline" this line
            // has. Switching the LCD off cleared it above.
            latchWindowY();
            // The comparison restarts inside this M-cycle, so LY=LYC can
            // raise the level line here, inside this same M-cycle - needed
            // by a hardware-verified LYC ROM whose fourth round switches the
            // LCD on with `di` as the very next instruction, so an interrupt
            // raised an M-cycle later would never be taken. See
            // docs/known-divergences.md, "Timing model".
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
    // A palette write shorts the old and new values together for one dot: a
    // Mealybug Tearoom reference, photographed from DMG hardware, shows the
    // one-pixel seam it leaves at every band edge, in a shade neither
    // palette can produce. The register itself takes the new value at once;
    // only what the pipeline shades with is affected, and only for the dot
    // after the write. See docs/known-divergences.md, "Palette writes short
    // the old and new values together for one dot".
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
