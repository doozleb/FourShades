#pragma once

#include "core/Timer.h"
#include "core/Types.h"
#include "core/apu/LengthCounter.h"
#include "core/apu/PulseChannel.h"

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
    // The register block already holds what the boot ROM left behind, so the
    // channels are handed those bytes rather than starting from zero: a
    // channel 1 whose DAC read off here would refuse a ROM's first trigger.
    Apu();

    // One M-cycle. The frame sequencer is not a timer of its own: it steps on
    // a falling edge of bit 12 of the system counter, which is why this is
    // handed the counter's owner rather than counting cycles.
    void tick(const Timer& timer);

    // The same question asked a second time, because something wrote the
    // system counter inside this M-cycle and cleared a bit the increment had
    // left standing. The machine advances time first and performs the access
    // second, so an edge made by that write arrives after tick() has already
    // asked. An edge already acted on in this M-cycle is not acted on twice.
    void counterWritten(const Timer& timer);

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

    // Channels 1 and 2, in that order, for the same onlookers.
    const PulseChannel& pulse(std::size_t index) const { return pulse_[index]; }

private:
    static constexpr u16 kFirst = 0xFF10;     // NR10
    static constexpr u16 kNr30 = 0xFF1A;      // the wave channel's DAC bit
    static constexpr u16 kNr42 = 0xFF21;      // the noise channel's envelope
    static constexpr u16 kNr52 = 0xFF26;
    static constexpr u16 kWaveFirst = 0xFF30; // wave RAM, 16 bytes

    static constexpr int kSequencerBit = 12; // of the 16-bit system counter

    void powerOff();
    void powerOn();
    void clockFromCounter(const Timer& timer);
    void stepSequencer();
    void clockLengths();
    void clockEnvelopes();
    void tickChannels();
    // FF10-FF19: the two pulse channels' five registers each, which is why
    // FF15 is a hole -- channel 2 has no sweep register to put there.
    void writePulse(u16 address, u8 value);
    void trigger(std::size_t channel);
    // Whether a channel's DAC is on. A channel whose DAC is off is switched
    // off and cannot be triggered back on -- that much is true of all four,
    // so it lives here rather than with the two channels that exist.
    bool dacOn(std::size_t channel) const;
    // Which channel's DAC an address holds, or -1.
    static int dacChannel(u16 address);
    u8 channelFlags() const;
    // Which channel's length counter an address loads (NRx1) or controls
    // (NRx4), or -1 for every other address.
    static int lengthLoadChannel(u16 address);
    static int lengthControlChannel(u16 address);

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
    std::array<PulseChannel, 2> pulse_{};
    // Pan Docs' power-up table reads NR52 as 0xF1 on DMG, so channel 1 is
    // already running when the boot ROM hands the machine over. A trigger
    // switches one on, a length counter running out or a DAC going off
    // switches one back off, and powering the APU down switches them all off.
    std::array<bool, 4> channelOn_{true, false, false, false};
    // Channel 3 counts from 256; the other three count from 64.
    std::array<LengthCounter, 4> length_{
        LengthCounter{LengthCounter::kShort},
        LengthCounter{LengthCounter::kShort},
        LengthCounter{LengthCounter::kWave},
        LengthCounter{LengthCounter::kShort},
    };
    bool powered_ = true;
    int step_ = 0;
    bool steppedThisCycle_ = false;
};

} // namespace fourshades
