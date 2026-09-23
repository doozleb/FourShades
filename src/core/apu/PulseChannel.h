#pragma once

#include "core/Types.h"
#include "core/apu/VolumeEnvelope.h"

namespace fourshades {

// One of the two pulse channels, per Pan Docs "Audio Details": a duty pattern
// stepped by a frequency timer, and a volume envelope. Channel 1 carries a
// frequency sweep on top of this and channel 2 does not; nothing else differs,
// so this is one type used twice.
//
// The length counter, the channel-enabled flag and the DAC are deliberately
// not here. All four channels have them, so the APU owns them for all four --
// and it reads every DAC out of the stored register byte, so there is no
// second copy of NRx2's top five bits here to fall out of step with it.
class PulseChannel {
public:
    // NRx1 bits 7-6. The rest of the byte is the length load, which belongs
    // to the length counter rather than to the duty generator.
    void writeDuty(u8 value);
    void writeEnvelope(u8 value);      // NRx2
    void writeFrequencyLow(u8 value);  // NRx3: the low eight bits
    void writeFrequencyHigh(u8 value); // NRx4: bits 2-0 are the high three

    // All eleven bits at once. Channel 1's sweep writes its new frequency
    // here, which is the same thing as writing NR13 and NR14.
    void setFrequency(int frequency);

    // Bit 7 of NRx4. Reloads the frequency timer and, from NRx2, the
    // envelope's timer and volume. The duty position is not reset: of the
    // four channels only the wave channel's position is.
    void trigger();

    // The frequency timer, in T-cycles.
    void tick(int tCycles);

    // The frame sequencer's step 7.
    void clockEnvelope() { envelope_.clock(); }

    // NR52's power bit going low zeroes NRx1 to NRx4, so the channel goes
    // back to everything those registers describe being zero.
    void powerOff();

    int frequency() const { return frequency_; }
    int position() const { return position_; }
    bool dutyOutput() const;
    u8 volume() const { return envelope_.volume(); }

private:
    // The frequency timer counts (2048 - frequency) * 4 T-cycles, so the
    // shortest period is four and the longest is the value a never-written
    // channel starts at.
    static constexpr int kMaxPeriod = 2048 * 4;

    int period() const { return (2048 - frequency_) * 4; }

    int duty_ = 0;
    int frequency_ = 0;
    int position_ = 0;
    int timer_ = kMaxPeriod;
    VolumeEnvelope envelope_;
};

} // namespace fourshades
