#pragma once

#include "core/Types.h"
#include "core/apu/VolumeEnvelope.h"

#include <array>
#include <cstddef>

namespace fourshades {

// The noise channel, per Pan Docs "Audio Details -- Noise channel (CH4)" and
// "Audio Registers -- FF22". A fifteen-bit linear feedback shift register
// stepped by a frequency timer, with the same volume envelope the two pulse
// channels carry. It has no frequency: NR43 describes how often the register
// steps, not what pitch comes out.
//
// As with the other three generators, the length counter, the channel-enabled
// flag and the DAC are deliberately not here. All four channels have them, so
// the APU owns them for all four -- including this channel's DAC, which is
// the top five bits of NR42 and is read there uniformly with NR12 and NR22.
class NoiseChannel {
public:
    void writeEnvelope(u8 value); // NR42
    void writeControl(u8 value);  // NR43: the shift, the width and the divisor

    // Bit 7 of NR44. Every bit of the shift register goes to 1, the frequency
    // timer reloads, and the envelope takes its volume from NR42.
    void trigger();

    // The frequency timer, in T-cycles. The shortest period is eight, so an
    // M-cycle's four T-cycles can cross at most one boundary.
    void tick(int tCycles);

    // The frame sequencer's step 7.
    void clockEnvelope() { envelope_.clock(); }

    // NR52's power bit going low zeroes NR41 to NR44.
    void powerOff();

    // The shift register itself, fifteen bits wide.
    u16 lfsr() const { return lfsr_; }

    // Pan Docs: "the channel's output is bit 0 of the LFSR, INVERTED". So a
    // freshly triggered channel, whose register is all ones, outputs a
    // digital zero.
    bool output() const { return (lfsr_ & 1) == 0; }

    u8 volume() const { return envelope_.volume(); }

    // T-cycles between steps: the divisor NR43's low three bits name, shifted
    // left by its high four.
    int period() const {
        return kDivisors[static_cast<std::size_t>(code_)] << shift_;
    }

    // Pan Docs, NR43: a clock shift of 14 or 15 leaves the channel receiving
    // no clocks at all, so the shift register stands still.
    bool stopped() const { return shift_ >= kFirstInvalidShift; }

private:
    // One step of the register: bits 0 and 1 are XORed, everything shifts
    // right, and the result goes into bit 14 -- and into bit 6 as well when
    // NR43's width bit is set, which closes the sequence into a seven-bit one
    // that repeats every 127 steps instead of every 32767.
    void step();

    // NR43 bits 2-0. Code 0 is the one that is not the code times sixteen:
    // it is half of code 1 rather than zero, so the register can step at all.
    static constexpr std::array<int, 8> kDivisors{8, 16, 32, 48, 64, 80, 96, 112};

    static constexpr int kFirstInvalidShift = 14;

    // What a trigger loads. Zero is a state the sequence never reaches and
    // never leaves, which is why the register is filled rather than cleared.
    static constexpr u16 kAllOnes = 0x7FFF;

    static constexpr u16 kFeedbackHigh = 1u << 14;
    static constexpr u16 kFeedbackNarrow = 1u << 6;

    int shift_ = 0;
    int code_ = 0;
    bool narrow_ = false;
    u16 lfsr_ = kAllOnes;
    // NR43 reads zero at reset, which is divisor code 0 and no shift.
    int timer_ = kDivisors[0];
    VolumeEnvelope envelope_;
};

} // namespace fourshades
