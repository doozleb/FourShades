#pragma once

#include "core/PixelPipeline.h"
#include "core/Types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace fourshades {

// The picture-processing unit. Stepped four dots per M-cycle by GameBoy, it
// walks the modes of a scanline, keeps the LCD registers, owns VRAM and OAM,
// and (from Task 4) draws the frame.
//
// Two mode numbers live side by side here. `mode_` is the PPU's own mode: it
// changes on the dot the PPU actually starts scanning OAM, fetching pixels or
// idling. `visibleMode_` is what the CPU sees - STAT's mode field, the STAT
// interrupt sources and (mostly) the VRAM and OAM locks - and it trails
// `mode_` by one M-cycle. The hardware-verified LCD timing ROMs pin both: LY
// increments and OAM locks one M-cycle before STAT reports mode 2, and VRAM
// locks one M-cycle before STAT reports mode 3. See docs/known-divergences.md,
// "Timing model (not a divergence: where Pan Docs is silent)".
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

    u8 read(u16 address) const;      // FF40-FF4B
    // Returns the IF bits the write itself requests: on DMG a STAT write can
    // raise the STAT level line inside the writing M-cycle, and so can
    // switching the LCD on.
    u8 write(u16 address, u8 value); // FF40-FF4B

    // The CPU's view: VRAM is unreadable in mode 3, OAM in modes 2 and 3
    // (except through dmaWriteOam). A blocked read gives 0xFF; a blocked
    // write is dropped. Writes and reads are not blocked over the same dots:
    // see oamWriteBlocked/vramWriteBlocked.
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
    bool vramWriteBlocked() const;
    bool oamWriteBlocked() const;
    int mode() const { return lcdOn() ? visibleMode_ : 0; }
    u8 ly() const;
    bool lcdOn() const { return (lcdc_ & 0x80) != 0; }

    u8 lcdc() const { return lcdc_; }
    u8 scx() const { return scx_; }
    u8 scy() const { return scy_; }
    u8 wx() const { return wx_; }
    u8 wy() const { return wy_; }
    u8 bgp() const { return bgp_; }
    u8 obp(int which) const { return which != 0 ? obp1_ : obp0_; }
    // The line being drawn. LY can read differently (line 153 reads 0).
    int lineNumber() const { return line_; }
    // Dots elapsed in the current line, 0-455.
    int lineDot() const { return dot_; }

    const std::array<u8, kWidth * kHeight>& frame() const { return frame_; }
    std::uint64_t frameCount() const { return frames_; }

    bool windowReached() const { return windowReached_; }
    int windowLine() const { return windowLine_; }
    void advanceWindowLine() { ++windowLine_; }

    struct Object {
        u8 y = 0;      // as stored in OAM: screen Y + 16
        u8 x = 0;      // as stored in OAM: screen X + 8
        u8 tile = 0;
        u8 flags = 0;
        int oamIndex = 0;
    };

    // The objects mode 2 picked for the line being drawn, in OAM order.
    const std::vector<Object>& lineObjects() const { return lineObjects_; }
    int objectHeight() const { return (lcdc_ & 0x04) != 0 ? 16 : 8; }

private:
    void stepDot(u8& requested);
    void setMode(int mode);
    void updateStatLine(u8& requested);
    bool statConditions(u8 select) const;
    bool oamSourceHigh() const;
    bool lycMatch() const;
    void scanOam();

    std::array<u8, 0x2000> vram_{};
    std::array<u8, 0xA0> oam_{};
    std::array<u8, kWidth * kHeight> frame_{};
    PixelPipeline pipeline_;
    std::array<u8, kWidth> lineBuffer_{};

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
    int mode_ = 2;        // the PPU's own mode
    int visibleMode_ = 2; // what the CPU sees: mode_ one M-cycle ago
    bool lcdOnLine_ = false;  // this line began when the LCD was switched on
    bool lycSuppressed_ = false; // the first M-cycle of a line compares as "no match"
    bool lycFrozen_ = false;     // the comparison's last result before the LCD went off
    bool statLine_ = false;  // the level line: an interrupt fires on its rise
    std::uint64_t frames_ = 0;
    bool windowReached_ = false; // WY has matched LY somewhere in this frame
    int windowLine_ = 0;         // the window's own line counter
    std::vector<Object> lineObjects_;
};

} // namespace fourshades
