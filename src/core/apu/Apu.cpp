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

void Apu::tick(const Timer& timer) {
    steppedThisCycle_ = false;
    clockFromCounter(timer);
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
    // Steps 2 and 6 clock the sweep and step 7 the envelope. Neither exists
    // yet: they arrive with the channels that own them.
}

void Apu::clockLengths() {
    for (std::size_t channel = 0; channel < length_.size(); ++channel) {
        if (length_[channel].clock()) {
            channelOn_[channel] = false;
        }
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
    if (const int controls = lengthControlChannel(address); controls >= 0) {
        // Bit 6 enables the length counter, bit 7 triggers the channel. The
        // half of the length period the write lands in decides the two
        // obscure behaviours the counter implements: it is the first half
        // when the step that comes next does not clock length.
        const bool firstHalf = !stepClocksLength(step_);
        const std::size_t channel = static_cast<std::size_t>(controls);
        if (length_[channel].writeControl((value & 0x40) != 0, (value & 0x80) != 0, firstHalf)) {
            channelOn_[channel] = false;
        }
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


