#pragma once

#include "core/PixelPipeline.h"
#include "core/Types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace fourshades {

// The picture-processing unit. Stepped four dots per M-cycle by GameBoy, it
// walks the modes of a scanline, keeps the LCD registers, owns VRAM and OAM,
// and draws the frame.
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
    // switching the LCD on. The caller must act on these bits (there is
    // exactly one caller in src/), so this is [[nodiscard]]; a call made only
    // for its side effects (as tests do, to land register writes) must say so
    // explicitly with static_cast<void>(...).
    [[nodiscard]] u8 write(u16 address, u8 value); // FF40-FF4B

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
    // The palette the pixel pipeline shades with. On DMG a write to a
    // palette register leaves the old and new values shorted together for
    // one dot, so the pixel drawn on the dot a write lands on is shaded with
    // their bitwise OR; $FF47-$FF49 still read back the value written. See
    // docs/known-divergences.md, "Palette writes short the old and
    // new values together for one dot".
    u8 bgp() const { return (paletteGlitch_ & 0x01) != 0 ? bgpGlitch_ : bgp_; }
    u8 obp(int which) const {
        if (which != 0) {
            return (paletteGlitch_ & 0x04) != 0 ? obp1Glitch_ : obp1_;
        }
        return (paletteGlitch_ & 0x02) != 0 ? obp0Glitch_ : obp0_;
    }
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

    // The DMG OAM corruption bug (Pan Docs, "OAM Corruption Bug"). Pan Docs
    // describes the PPU as reading one of OAM's 20 rows of 8 bytes per
    // M-cycle during mode 2, row 0 first, and a CPU access anywhere in
    // FE00-FEFF during one of those M-cycles - including the one the 16-bit
    // increment/decrement unit makes on its own, because the IDU is tied
    // straight to the address bus - as scrambling the row the PPU is reading
    // right then. FourShades has no such per-row read to collide with: mode
    // 2 here is `scanOam()`, which runs once, over all 40 objects, at dot 80,
    // with no internal row pointer. What actually happens is that the access
    // scrambles the row `oamScanRow()` names for the M-cycle it landed in - a
    // convention fitted to place the OAM-bug ROMs' corruptions where they
    // measure them, not a row a PPU read is ever caught mid-flight on.
    // Neither the address used nor the value written has any effect on the
    // result.
    //
    // Kind is what the CPU did to the bus in that M-cycle. Read and Write
    // are Pan Docs' two patterns; ReadWrite is its "Read During
    // Increase/Decrease", which is what `ld a,(hl+)` and an opcode fetch
    // from OAM produce: a read and an IDU write land in the same M-cycle.
    enum class Kind { Read, Write, ReadWrite };

    // Applies a pattern to `row` at once. Rows outside 1-19 are left alone:
    // row 0 has no preceding row to be glitched with.
    void oamCorrupt(Kind kind, int row);

    // The bug's entry points. Both record the access; the pattern is applied
    // at the end of the M-cycle, because a read and a write in the same
    // M-cycle together mean something other than either of them alone.
    // oamCorruptIfScanning is what the IDU reports: the unit drives the
    // address without asserting a read, which OAM sees as a write.
    void oamCorruptIfScanning(u16 address) { oamBusAccess(address, Kind::Write); }
    void oamBusAccess(u16 address, Kind kind);

    // The row a CPU access lands on if it collides with the M-cycle that has
    // just been ticked, or -1 if no row collides there. This names a
    // convention fitted to where the OAM-bug ROMs place their corruptions
    // (see docs/known-divergences.md, "The OAM corruption bug"), not a row
    // the PPU is actually reading - there is no per-row OAM read in this
    // model to agree or disagree with; `scanOam()` reads all 40 objects at
    // once, at dot 80. Only meaningful between two ticks.
    int oamScanRow() const;

private:
    void stepDot(u8& requested);
    // Latches the window's "Y condition" for the line that is beginning.
    void latchWindowY();
    void setMode(int mode);
    void updateStatLine(u8& requested);
    bool statConditions(u8 select) const;
    bool oamSourceHigh() const;
    bool lycMatch() const;
    void scanOam();
    void flushOamCorruption();

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
    // One dot's worth of "old OR new" for each palette, and which of them are
    // live: bit 0 BGP, bit 1 OBP0, bit 2 OBP1.
    u8 bgpGlitch_ = 0x00;
    u8 obp0Glitch_ = 0x00;
    u8 obp1Glitch_ = 0x00;
    u8 paletteGlitch_ = 0x00;

    // Power-on is where the boot ROM leaves the PPU. Pan Docs' Power Up
    // Sequence gives DMG at PC = $0100 as STAT = $85, LY = $00: mode 1 with
    // LY already reading 0, which is line 153 past its first few dots (the
    // LY=153 quirk). STAT's bit 2 is then set because LY and LYC both read 0.
    // Pan Docs does not give the dot within line 153, and it is not free: it
    // sets the phase of every later line against the CPU. The value below was
    // solved from a register walk's two reads and is one of 49 admissible
    // multiples of four; see docs/known-divergences.md, "The PPU's power-on
    // phase within line 153".
    int line_ = 153; // 0-153, the real line; LY reads differently on line 153
    int dot_ = 356;  // 0-455 within the line; past line 153's first dots
    int mode_ = 1;        // the PPU's own mode: VBlank
    int visibleMode_ = 1; // what the CPU sees: mode_ one M-cycle ago
    bool rendering_ = false;  // the pixel pipeline is running (it outlives mode 3)
    int renderLag_ = 0;       // dots still to wait before the fetcher starts
    int lineRenderLag_ = 0;   // the lag this line began with, in dots
    bool lcdOnLine_ = false;  // this line began when the LCD was switched on
    bool lycSuppressed_ = false; // the first M-cycle of a line compares as "no match"
    bool lycFrozen_ = false;     // the comparison's last result before the LCD went off
    bool statLine_ = false;  // the level line: an interrupt fires on its rise
    std::uint64_t frames_ = 0;
    bool windowReached_ = false; // WY has matched LY somewhere in this frame
    int windowLine_ = 0;         // the window's own line counter
    std::vector<Object> lineObjects_;
    // The OAM corruption pending for the M-cycle now in progress: the row
    // the PPU is reading, and what the CPU did to the bus during it.
    int corruptRow_ = -1;
    bool corruptRead_ = false;
    bool corruptWrite_ = false;
};

} // namespace fourshades
