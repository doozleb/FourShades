#include "core/apu/Apu.h"

namespace fourshades {

namespace {

// Pan Docs "Audio Registers", FF10 to FF26 in order. A read returns the
// stored byte OR this: the bits a register does not use, and the ones that
// are write-only (a trigger, a frequency's low byte, a length load), read as
// 1. The two gaps in the block, FF15 and FF1F, are not registers and read
// 0xFF entirely.
constexpr u8 kReadMask[] = {
    0x80, // FF10 NR10
    0x3F, // FF11 NR11  (only the duty reads back)
    0x00, // FF12 NR12
    0xFF, // FF13 NR13  (write-only)
    0xBF, // FF14 NR14  (only the length enable reads back)
    0xFF, // FF15       (no register)
    0x3F, // FF16 NR21
    0x00, // FF17 NR22
    0xFF, // FF18 NR23
    0xBF, // FF19 NR24
    0x7F, // FF1A NR30
    0xFF, // FF1B NR31  (write-only)
    0x9F, // FF1C NR32
    0xFF, // FF1D NR33  (write-only)
    0xBF, // FF1E NR34
    0xFF, // FF1F       (no register)
    0xFF, // FF20 NR41  (write-only)
    0x00, // FF21 NR42
    0x00, // FF22 NR43
    0xBF, // FF23 NR44
    0x00, // FF24 NR50
    0x00, // FF25 NR51
    0x70, // FF26 NR52
};

bool isGap(u16 address) { return address == 0xFF15 || address == 0xFF1F; }

// One M-cycle is four T-cycles, and the frequency timers count T-cycles.
constexpr int kTicksPerMCycle = 4;

// The two pulse channels own five consecutive registers each, FF10-FF14 and
// FF15-FF19. FF15 is the hole where channel 2's sweep register would be.
constexpr u16 kPulseFirst = 0xFF10;
constexpr u16 kPulseLast = 0xFF19;
constexpr int kPulseRegisters = 5;

// The sequencer's eight steps. Steps 0, 2, 4 and 6 clock the length counters
// (256 Hz), steps 2 and 6 the sweep (128 Hz) and step 7 the envelope (64 Hz);
// those rates are what the table produces, not three separate timers.
bool stepClocksLength(int step) { return (step & 1) == 0; }

// NR11, NR21, NR31 and NR41: the register whose low bits load a channel's
// length counter. Returns the mask of the bits that load it, or 0 for the
// rest. Channel 3 counts to 256, so its load is the whole byte.
u8 lengthLoadMask(u16 address) {
    switch (address) {
    case 0xFF11:
    case 0xFF16:
    case 0xFF20: return 0x3F;
    case 0xFF1B: return 0xFF;
    default: return 0x00;
    }
}

} // namespace

Apu::Apu() {
    // nr_ starts at the bytes the boot ROM leaves behind, so the channels are
    // fed those same bytes: NR11's duty and NR12's DAC are already set when
    // the machine starts, and channel 1 is already reporting itself on.
    for (u16 address = kPulseFirst; address <= kPulseLast; ++address) {
        if (isGap(address)) {
            continue;
        }
        writePulse(address, nr_[static_cast<std::size_t>(address - kFirst)]);
    }
}

void Apu::tick(const Timer& timer) {
    steppedThisCycle_ = false;
    clockFromCounter(timer);
    tickChannels();
}

void Apu::counterWritten(const Timer& timer) {
    clockFromCounter(timer);
}

// The edge query is a level that stands for the whole M-cycle rather than
// something a reader consumes, so this asks whether it has already acted on
// it. Without that, a write to the counter in a cycle whose increment had
// already produced the edge would step the sequencer a second time.
void Apu::clockFromCounter(const Timer& timer) {
    if (!powered_ || steppedThisCycle_) {
        return;
    }
    if (!timer.counterBitFell(kSequencerBit)) {
        return;
    }
    steppedThisCycle_ = true;
    stepSequencer();
}

void Apu::stepSequencer() {
    const int step = step_;
    step_ = (step_ + 1) & 7;
    if (stepClocksLength(step)) {
        clockLengths();
    }
    if (step == 7) {
        clockEnvelopes();
    }
    // Steps 2 and 6 clock the sweep, which arrives with the channel that
    // owns it.
}

void Apu::clockLengths() {
    for (std::size_t channel = 0; channel < length_.size(); ++channel) {
        if (length_[channel].clock()) {
            channelOn_[channel] = false;
        }
    }
}

void Apu::clockEnvelopes() {
    for (PulseChannel& channel : pulse_) {
        channel.clockEnvelope();
    }
}

// The frequency timers run only while the APU has power.
void Apu::tickChannels() {
    if (!powered_) {
        return;
    }
    for (PulseChannel& channel : pulse_) {
        channel.tick(kTicksPerMCycle);
    }
}

// NR52's low four bits: one per channel, in channel order.
u8 Apu::channelFlags() const {
    u8 flags = 0x00;
    for (std::size_t channel = 0; channel < channelOn_.size(); ++channel) {
        if (channelOn_[channel]) {
            flags = static_cast<u8>(flags | (1u << channel));
        }
    }
    return flags;
}

int Apu::lengthLoadChannel(u16 address) {
    switch (address) {
    case 0xFF11: return 0;
    case 0xFF16: return 1;
    case 0xFF1B: return 2;
    case 0xFF20: return 3;
    default: return -1;
    }
}

int Apu::lengthControlChannel(u16 address) {
    switch (address) {
    case 0xFF14: return 0;
    case 0xFF19: return 1;
    case 0xFF1E: return 2;
    case 0xFF23: return 3;
    default: return -1;
    }
}

u8 Apu::read(u16 address) const {
    if (address >= kWaveFirst) {
        return wave_[address - kWaveFirst];
    }
    if (address > kNr52) {
        return 0xFF; // FF27-FF2F: nothing is there
    }
    if (address == kNr52) {
        return static_cast<u8>((powered_ ? 0x80 : 0x00) | 0x70 | channelFlags());
    }
    const std::size_t index = static_cast<std::size_t>(address - kFirst);
    return static_cast<u8>(nr_[index] | kReadMask[index]);
}

void Apu::write(u16 address, u8 value) {
    // Wave RAM is on the far side of the power switch: it neither loses its
    // contents when the APU goes down nor stops answering.
    if (address >= kWaveFirst) {
        wave_[address - kWaveFirst] = value;
        return;
    }
    if (address > kNr52 || isGap(address)) {
        return;
    }
    if (address == kNr52) {
        const bool wanted = (value & 0x80) != 0;
        if (wanted != powered_) {
            wanted ? powerOn() : powerOff();
        }
        // Bits 0-3 are the channels reporting in, not something to write.
        return;
    }
    const std::size_t index = static_cast<std::size_t>(address - kFirst);
    if (!powered_) {
        // With the APU down every register is held at zero -- except, on DMG
        // only, the bits that load a length counter, which keep working. The
        // rest of the byte (a duty, say) does not. The counter behind those
        // bits is loaded too: that is the point of their staying writable.
        const u8 kept = static_cast<u8>(value & lengthLoadMask(address));
        nr_[index] = kept;
        const int loads = lengthLoadChannel(address);
        if (loads >= 0) {
            length_[static_cast<std::size_t>(loads)].load(kept);
        }
        return;
    }
    nr_[index] = value;
    if (const int loads = lengthLoadChannel(address); loads >= 0) {
        length_[static_cast<std::size_t>(loads)].load(value);
    }
    writePulse(address, value);
    // A DAC that goes off takes its channel with it. One that comes on does
    // not bring the channel back: only a trigger does that.
    if (const int dac = dacChannel(address); dac >= 0) {
        const std::size_t channel = static_cast<std::size_t>(dac);
        if (!dacOn(channel)) {
            channelOn_[channel] = false;
        }
    }
    if (const int controls = lengthControlChannel(address); controls >= 0) {
        // Bit 6 enables the length counter, bit 7 triggers the channel. The
        // half of the length period the write lands in decides the two
        // obscure behaviours the counter implements: it is the first half
        // when the step that comes next does not clock length.
        const bool firstHalf = !stepClocksLength(step_);
        const std::size_t channel = static_cast<std::size_t>(controls);
        const bool triggered = (value & 0x80) != 0;
        if (length_[channel].writeControl((value & 0x40) != 0, triggered, firstHalf)) {
            channelOn_[channel] = false;
        }
        // The counter is dealt with first: a write that both expires the
        // length and triggers leaves the channel on, which is why it is the
        // counter that refuses to report an expiry when trigger is set.
        if (triggered) {
            trigger(channel);
        }
    }
}

void Apu::writePulse(u16 address, u8 value) {
    if (address < kPulseFirst || address > kPulseLast) {
        return;
    }
    const int offset = static_cast<int>(address - kPulseFirst);
    PulseChannel& channel = pulse_[static_cast<std::size_t>(offset / kPulseRegisters)];
    switch (offset % kPulseRegisters) {
    case 1: channel.writeDuty(value); break;
    case 2: channel.writeEnvelope(value); break;
    case 3: channel.writeFrequencyLow(value); break;
    case 4: channel.writeFrequencyHigh(value); break;
    default: break; // NRx0 is the sweep, which channel 1 does not own yet
    }
}

// Bit 7 of NRx4. The length counter has already had its share of the write.
// A channel whose DAC is off cannot be switched on, so this is where the
// refusal lives -- and where channels 3 and 4, whose generators are not
// written yet, still get the enable flag every channel has.
void Apu::trigger(std::size_t channel) {
    if (channel < pulse_.size()) {
        pulse_[channel].trigger();
    }
    channelOn_[channel] = dacOn(channel);
}

// Three of the four DACs are off when the top five bits of an envelope
// register are zero. The wave channel has no envelope, so its DAC is a bit of
// its own: NR30 bit 7.
bool Apu::dacOn(std::size_t channel) const {
    switch (channel) {
    case 2: return (nr_[kNr30 - kFirst] & 0x80) != 0;
    case 3: return (nr_[kNr42 - kFirst] & 0xF8) != 0;
    default: return pulse_[channel].dacOn();
    }
}

// Which channel's DAC an address holds, or -1 for every other address.
int Apu::dacChannel(u16 address) {
    switch (address) {
    case 0xFF12: return 0;
    case 0xFF17: return 1;
    case 0xFF1A: return 2;
    case 0xFF21: return 3;
    default: return -1;
    }
}

u8 Apu::stored(u16 address) const {
    if (address >= kWaveFirst) {
        return wave_[address - kWaveFirst];
    }
    if (address > kNr52) {
        return 0x00;
    }
    if (address == kNr52) {
        return static_cast<u8>((powered_ ? 0x80 : 0x00) | channelFlags());
    }
    return nr_[static_cast<std::size_t>(address - kFirst)];
}

// Everything from NR10 to NR51 goes to zero and stays there, and every
// channel reports itself off. Wave RAM is untouched.
void Apu::powerOff() {
    nr_.fill(0x00);
    channelOn_.fill(false);
    for (PulseChannel& channel : pulse_) {
        channel.powerOff();
    }
    // NRx4 goes with the rest, so every length counter loses its enable. The
    // counters themselves are left alone: on DMG they survive the power
    // cycle, which is also why their load bits stay writable while down.
    for (LengthCounter& length : length_) {
        length.powerOff();
    }
    powered_ = false;
}

void Apu::powerOn() {
    powered_ = true;
    step_ = 0;
}

} // namespace fourshades


