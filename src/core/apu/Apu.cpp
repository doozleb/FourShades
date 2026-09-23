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

// Nothing is clocked here yet. The frame sequencer arrives with the length
// counter and hangs off a falling edge of a system-counter bit, not off this.
void Apu::tick() {}

u8 Apu::read(u16 address) const {
    if (address >= kWaveFirst) {
        return wave_[address - kWaveFirst];
    }
    if (address > kNr52) {
        return 0xFF; // FF27-FF2F: nothing is there
    }
    if (address == kNr52) {
        return static_cast<u8>((powered_ ? 0x80 : 0x00) | 0x70 | channelsOn_);
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
        // rest of the byte (a duty, say) does not.
        nr_[index] = static_cast<u8>(value & lengthLoadMask(address));
        return;
    }
    nr_[index] = value;
}

u8 Apu::stored(u16 address) const {
    if (address >= kWaveFirst) {
        return wave_[address - kWaveFirst];
    }
    if (address > kNr52) {
        return 0x00;
    }
    if (address == kNr52) {
        return static_cast<u8>((powered_ ? 0x80 : 0x00) | channelsOn_);
    }
    return nr_[static_cast<std::size_t>(address - kFirst)];
}

// Everything from NR10 to NR51 goes to zero and stays there, and every
// channel reports itself off. Wave RAM is untouched.
void Apu::powerOff() {
    nr_.fill(0x00);
    channelsOn_ = 0x00;
    powered_ = false;
}

void Apu::powerOn() {
    powered_ = true;
    step_ = 0;
}

} // namespace fourshades


