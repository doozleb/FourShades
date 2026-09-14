#pragma once

#include "core/Types.h"

#include <array>
#include <cstdint>

namespace fourshades {

// The picture-processing unit. Stepped four dots per M-cycle by GameBoy, it
// walks the modes of a scanline, keeps the LCD registers, owns VRAM and OAM,
// and (from Task 4) draws the frame.
class Ppu {
public:
    static constexpr int kWidth = 160;
    static constexpr int kHeight = 144;
    static constexpr int kDotsPerLine = 456;
    static constexpr int kLines = 154;
    static constexpr int kOamScanDots = 80;
    static constexpr int kMinDrawDots = 172;

    // One M-cycle (4 dots). Returns the IF bits requested during it.
    u8 tick();

    u8 read(u16 address) const;        // FF40-FF4B
    void write(u16 address, u8 value); // FF40-FF4B

    // The CPU's view. Blocking by mode arrives in Task 3.
    u8 vramRead(u16 address) const;
    void vramWrite(u16 address, u8 value);
    u8 oamRead(u16 address) const;
    void oamWrite(u16 address, u8 value);

    // OAM DMA owns the bus while it runs, so it is never blocked.
    void dmaWriteOam(int index, u8 value) { oam_[static_cast<std::size_t>(index)] = value; }

    // The debugger and harness view: never blocked, no side effects.
    u8 peekVram(u16 address) const { return vram_[address - 0x8000]; }
    u8 peekOam(u16 address) const { return oam_[address - 0xFE00]; }

    bool vramBlocked() const;
    bool oamBlocked() const;
    int mode() const { return lcdOn() ? mode_ : 0; }
    u8 ly() const;
    bool lcdOn() const { return (lcdc_ & 0x80) != 0; }

    const std::array<u8, kWidth * kHeight>& frame() const { return frame_; }
    std::uint64_t frameCount() const { return frames_; }

private:
    void stepDot(u8& requested);
    void setMode(int mode);
    void updateStatLine(u8& requested);
    bool lycMatch() const;

    std::array<u8, 0x2000> vram_{};
    std::array<u8, 0xA0> oam_{};
    std::array<u8, kWidth * kHeight> frame_{};

    u8 lcdc_ = 0x91;
    u8 statSelect_ = 0x00; // STAT bits 6-3
    u8 scy_ = 0x00;
    u8 scx_ = 0x00;
    u8 lyc_ = 0x00;
    u8 bgp_ = 0xFC;
    u8 obp0_ = 0xFF;
    u8 obp1_ = 0xFF;
    u8 wy_ = 0x00;
    u8 wx_ = 0x00;

    int line_ = 0;   // 0-153, the real line; LY reads differently on line 153
    int dot_ = 0;    // 0-455 within the line
    int mode_ = 2;
    bool statLine_ = false;  // the level line: an interrupt fires on its rise
    int statQuirk_ = 0;      // M-cycles left of the DMG "write acts as 0xFF" quirk
    std::uint64_t frames_ = 0;
};

} // namespace fourshades
