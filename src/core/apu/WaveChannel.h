#pragma once

#include "core/Types.h"

#include <array>
#include <cstddef>

namespace fourshades {

// The wave channel, per Pan Docs "Audio Details -- Wave channel (CH3)" and
// "Audio Registers -- FF30-FF3F". It plays 32 four-bit samples packed into
// sixteen bytes, high nibble first, stepped by a frequency timer that runs at
// twice the rate of a pulse channel's: (2048 - frequency) * 2 T-cycles per
// sample, which is why the same frequency value sounds an octave higher.
//
// Two things this class deliberately does not own. The sixteen bytes live on
// the APU, because they sit on the far side of the power switch and the CPU
// reaches them through the same bus the registers are on; they are handed in
// per tick. And the DAC -- NR30 bit 7 -- is asked about uniformly for all
// four channels by the APU, the same as the three envelope registers, so the
// rule that a DAC going off switches its channel off is written once there
// rather than four times.
class WaveChannel {
public:
    void writeLevel(u8 value);         // NR32 bits 6-5
    void writeFrequencyLow(u8 value);  // NR33: the low eight bits
    void writeFrequencyHigh(u8 value); // NR34: bits 2-0 are the high three

    // Bit 7 of NR34. The sample index is reset but wave RAM is not re-read,
    // so the sample buffer keeps playing the last sample read until the
    // channel next reads one -- and, because the index is reset to 0 and the
    // timer reads only after it increments, the first sample played after a
    // trigger is the one at index 1, the low nibble of the first byte.
    //
    // The first period after a trigger is six T-cycles longer than the ones
    // after it; see the wave-channel entry in docs/known-divergences.md.
    void trigger();

    // The frequency timer, in T-cycles, handed one M-cycle at a time. The
    // sixteen bytes come in from the APU: the channel reads one sample out of
    // them every time the timer expires.
    void tick(int tCycles, const std::array<u8, 16>& wave);

    // NR52's power bit going low zeroes NR30 to NR34.
    void powerOff();

    // ... and coming back up clears the sample buffer, so a freshly powered
    // APU emits a digital zero until the channel reads its first sample.
    void powerOn() { sample_ = 0; }

    int frequency() const { return frequency_; }

    // Which of the 32 samples was read last, 0-31.
    int position() const { return position_; }

    // Which of the sixteen bytes that sample came out of, 0-15.
    std::size_t readIndex() const { return static_cast<std::size_t>(position_) >> 1; }

    // The sample buffer: the last nibble read, unshifted.
    u8 sample() const { return sample_; }

    // What the DAC is handed: the sample buffer shifted by the output level.
    // Level 0 shifts by 4, which is silence; 1, 2 and 3 shift by 0, 1 and 2.
    u8 output() const { return static_cast<u8>(sample_ >> shift()); }

    // Whether the CPU's access this M-cycle lands on the channel's own read
    // of wave RAM. On a monochrome console that is the only moment the CPU
    // can reach those sixteen bytes while the channel is playing; every other
    // moment reads 0xFF and drops writes. The channel reads on the last
    // T-cycle of the M-cycles it reads in, and the CPU's access is the last
    // thing in an M-cycle too, so this is the one that coincides.
    bool readingNow() const { return readOnLastCycle_; }

    // Whether the channel is about to read: its next sample is two T-cycles
    // away, which is where an access this M-cycle sits relative to it. That
    // is the moment a trigger corrupts wave RAM, and it is two T-cycles
    // earlier than the moment the CPU can reach wave RAM -- see the
    // wave-channel entry in docs/known-divergences.md.
    bool aboutToRead() const { return timer_ == kAboutToRead; }

    // The byte that read will come out of, 0-15.
    std::size_t nextReadIndex() const {
        return static_cast<std::size_t>((position_ + 1) & 31) >> 1;
    }

private:
    // The timer counts (2048 - frequency) * 2 T-cycles, so the shortest
    // period is two and the longest is the value a never-written channel
    // starts at.
    static constexpr int kMaxPeriod = 2048 * 2;

    // NR32 bits 6-5, in order: mute, full, half, quarter.
    static constexpr std::array<int, 4> kShifts{4, 0, 1, 2};

    // How much longer than a period the wait for the first sample after a
    // trigger is, in T-cycles. Not a figure any document gives; see the
    // wave-channel entry in docs/known-divergences.md for where it comes
    // from and what pins it down.
    static constexpr int kTriggerDelay = 6;

    // How far ahead of a sample read the channel counts as about to read it,
    // in T-cycles. Not a figure any document gives either; the same entry
    // covers it.
    static constexpr int kAboutToRead = 2;

    int period() const { return (2048 - frequency_) * 2; }
    int shift() const { return kShifts[static_cast<std::size_t>(level_)]; }

    int level_ = 0;
    int frequency_ = 0;
    int position_ = 0;
    int timer_ = kMaxPeriod;
    u8 sample_ = 0;
    bool readOnLastCycle_ = false;
};

} // namespace fourshades
