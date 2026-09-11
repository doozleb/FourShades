#pragma once

#include "core/Types.h"

namespace fourshades {

// A placeholder for the PPU, which is piece 3. It keeps only the timing that
// software waits on: LY advances every 114 M-cycles while the LCD is on, wraps
// after line 153, and VBlank is requested on entering line 144. STAT mode bits
// are approximate and there are no STAT interrupts or pixels.
class LcdTiming {
public:
    // One M-cycle. Returns the IF bits requested this cycle.
    u8 tick();

    u8 read(u16 address) const;         // FF40-FF45, FF47-FF4B
    void write(u16 address, u8 value);  // same

private:
    int mode() const;

    u8 lcdc_ = 0x91;
    u8 statSelect_ = 0x00; // STAT bits 3-6
    u8 scy_ = 0x00;
    u8 scx_ = 0x00;
    u8 ly_ = 0x00;
    u8 lyc_ = 0x00;
    u8 bgp_ = 0xFC;
    u8 obp0_ = 0xFF;
    u8 obp1_ = 0xFF;
    u8 wy_ = 0x00;
    u8 wx_ = 0x00;
    int lineCycle_ = 0; // M-cycles into the current line, 0-113
};

} // namespace fourshades
