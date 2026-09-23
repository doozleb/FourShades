#pragma once

#include "core/Types.h"

#include <array>
#include <cstddef>

namespace fourshades {

// The sound registers, FF10-FF3F: what each one stores, which of its bits
// read back, and what the power bit in NR52 does to the rest. Per Pan Docs
// "Audio Registers" and "Audio Details".
//
// A read returns the stored byte OR a per-register mask, because the bits a
// register does not use -- and the ones that are write-only, like a trigger
// -- read as 1. FF15, FF1F and FF27-FF2F are not registers at all and read
// 0xFF whatever is written to them.
class Apu {
public:
    // One M-cycle.
    void tick();

    u8 read(u16 address) const;         // FF10-FF3F
    void write(u16 address, u8 value);  // FF10-FF3F

    bool powered() const { return powered_; }

    // Which of the eight frame-sequencer steps comes next (0-7). Powering the
    // APU on restarts the sequence at 0.
    int sequencerStep() const { return step_; }

    // The byte behind a register, before the read-back mask puts the unused
    // bits up: what a debugger or a save state would want. Nothing inside the
    // machine reads through it.
    u8 stored(u16 address) const;

private:
    static constexpr u16 kFirst = 0xFF10;     // NR10
    static constexpr u16 kNr52 = 0xFF26;
    static constexpr u16 kWaveFirst = 0xFF30; // wave RAM, 16 bytes

    void powerOff();
    void powerOn();

    // FF10-FF25, in address order, as the DMG boot ROM leaves them: it writes
    // NR11, NR12, NR51 and NR50 on its way past and nothing else, so the rest
    // are still at their reset value. NR52 is not in here -- it is the power
    // bit plus the channel flags, which are state rather than a stored byte.
    std::array<u8, 0x16> nr_{
        0x00, 0x80, 0xF3, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x77, 0xF3,
    };
    std::array<u8, 0x10> wave_{};
    // Pan Docs' power-up table reads NR52 as 0xF1 on DMG, so channel 1 is
    // already running when the boot ROM hands the machine over. Nothing here
    // can turn a channel on -- there are none yet -- and powering the APU
    // down clears this with the rest.
    u8 channelsOn_ = 0x01;
    bool powered_ = true;
    int step_ = 0;
};

} // namespace fourshades
