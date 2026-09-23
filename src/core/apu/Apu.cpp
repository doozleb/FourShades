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
    // NR52 is never indexed here: read() builds it out of the power bit and
    // the four channel flags rather than out of a stored byte, and returns
    // before it reaches this table. The entry stays so the table still covers
    // the whole range it claims to.
    0x70, // FF26 NR52
};

bool isGap(u16 address) { return address == 0xFF15 || address == 0xFF1F; }

// One M-cycle is four T-cycles, and the frequency timers count T-cycles.
constexpr int kTicksPerMCycle = 4;

// A digital level runs 0 to 15 and its DAC's output runs +1 to -1, so half
// the digital range is what one unit of analog output costs.
constexpr float kHalfScale = 7.5f;

// How many channels the mix is divided by, so that a side of the pair stays
// within the same [-1, +1] one channel is within.
constexpr float kChannels = 4.0f;

// How far NR51's left nibble is from its right one.
constexpr int kLeftShift = 4;

// The two pulse channels own five consecutive registers each, FF10-FF14 and
// FF15-FF19. FF15 is the hole where channel 2's sweep register would be.
constexpr u16 kPulseFirst = 0xFF10;
constexpr u16 kPulseLast = 0xFF19;
constexpr int kPulseRegisters = 5;

// The wave channel's five, FF1A-FF1E.
constexpr u16 kWaveFirstRegister = 0xFF1A;
constexpr u16 kWaveLastRegister = 0xFF1E;

// The noise channel's four, FF20-FF23.
constexpr u16 kNoiseFirstRegister = 0xFF20;
constexpr u16 kNoiseLastRegister = 0xFF23;

// The sequencer's eight steps. Steps 0, 2, 4 and 6 clock the length counters
// (256 Hz), steps 2 and 6 the sweep (128 Hz) and step 7 the envelope (64 Hz);
// those rates are what the table produces, not three separate timers.
bool stepClocksLength(int step) { return (step & 1) == 0; }

// Steps 2 and 6, which are also length steps: the sweep runs at half the
// length counter's rate off the same sequence.
bool stepClocksSweep(int step) { return step == 2 || step == 6; }

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
    for (u16 address = kWaveFirstRegister; address <= kWaveLastRegister; ++address) {
        writeWave(address, nr_[static_cast<std::size_t>(address - kFirst)]);
    }
    for (u16 address = kNoiseFirstRegister; address <= kNoiseLastRegister; ++address) {
        writeNoise(address, nr_[static_cast<std::size_t>(address - kFirst)]);
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
    if (stepClocksSweep(step)) {
        clockSweep();
    }
    if (step == 7) {
        clockEnvelopes();
    }
}

void Apu::clockLengths() {
    for (std::size_t channel = 0; channel < length_.size(); ++channel) {
        if (length_[channel].clock()) {
            channelOn_[channel] = false;
        }
    }
}

// Channel 1's sweep, and the two ways it reaches the rest of the APU: it
// switches the channel off through the same flag every other channel is
// switched off by, and its new frequency is a write to NR13 and NR14, so the
// stored bytes follow the channel's frequency.
void Apu::clockSweep() {
    if (sweep_.clock(pulse_[0])) {
        channelOn_[0] = false;
    }
    const int frequency = pulse_[0].frequency();
    nr_[kNr13 - kFirst] = static_cast<u8>(frequency & 0xFF);
    nr_[kNr14 - kFirst] =
        static_cast<u8>((nr_[kNr14 - kFirst] & 0xF8) | ((frequency >> 8) & 0x07));
}

void Apu::clockEnvelopes() {
    for (PulseChannel& channel : pulse_) {
        channel.clockEnvelope();
    }
    noise4_.clockEnvelope();
}

// The frequency timers run only while the APU has power.
void Apu::tickChannels() {
    if (!powered_) {
        return;
    }
    for (PulseChannel& channel : pulse_) {
        channel.tick(kTicksPerMCycle);
    }
    // Channel 3 is the one generator whose running is visible from outside
    // it: a channel that is switched off does not read wave RAM, which is
    // what leaves the last sample read standing in the buffer and leaves the
    // sixteen bytes reachable. The other three keep their timers running with
    // the channel off, because nothing outside them can tell.
    wave3_.tick(kTicksPerMCycle, wave_, channelOn_[2]);
    noise4_.tick(kTicksPerMCycle);
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
        // While channel 3 plays, the address the CPU asked for is not the one
        // it gets: the channel has the sixteen bytes, and hands over the one
        // it is reading, on the one T-cycle it reads it. Every other T-cycle
        // the CPU sees nothing at all.
        if (channelOn_[2]) {
            return waveRamReachable() ? wave_[wave3_.readIndex()] : 0xFF;
        }
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
    // contents when the APU goes down nor stops answering. A playing channel 3
    // is the one thing that comes between it and the CPU, and it does so for
    // writes exactly as it does for reads.
    if (address >= kWaveFirst) {
        if (channelOn_[2]) {
            if (waveRamReachable()) {
                wave_[wave3_.readIndex()] = value;
            }
            return;
        }
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
    writeWave(address, value);
    writeNoise(address, value);
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

// The CPU reaches wave RAM on the T-cycle channel 3 reads a byte out of it,
// and on no other. The channel reads on the last T-cycle of the M-cycles it
// reads in, which is the one the CPU's own access falls on.
bool Apu::waveRamReachable() const {
    return wave3_.readingNow();
}

// Retriggering channel 3 while it is about to read a sample byte, on a
// monochrome console, rewrites the front of wave RAM with the bytes that read
// was going to come from: the first byte alone if it was one of the first
// four, and otherwise the whole aligned group of four it was inside.
void Apu::corruptWaveRam() {
    const std::size_t index = wave3_.nextReadIndex();
    if (index < 4) {
        wave_[0] = wave_[index];
        return;
    }
    const std::size_t base = index & ~std::size_t{3};
    for (std::size_t offset = 0; offset < 4; ++offset) {
        wave_[offset] = wave_[base + offset];
    }
}

void Apu::writeWave(u16 address, u8 value) {
    if (address < kWaveFirstRegister || address > kWaveLastRegister) {
        return;
    }
    switch (address) {
    case 0xFF1C: wave3_.writeLevel(value); break;          // NR32
    case 0xFF1D: wave3_.writeFrequencyLow(value); break;   // NR33
    case 0xFF1E: wave3_.writeFrequencyHigh(value); break;  // NR34
    default:
        // NR30, the DAC bit, which the APU reads out of the stored byte for
        // all four channels alike, and NR31, the length load, which the
        // length counter has already taken.
        break;
    }
}

void Apu::writeNoise(u16 address, u8 value) {
    if (address < kNoiseFirstRegister || address > kNoiseLastRegister) {
        return;
    }
    switch (address) {
    case 0xFF21: noise4_.writeEnvelope(value); break; // NR42
    case 0xFF22: noise4_.writeControl(value); break;  // NR43
    default:
        // NR41, the length load, which the length counter has already taken,
        // and NR44, whose trigger and length enable belong to the APU.
        break;
    }
}

// What a channel is handing its DAC: a digital level, 0 to 15. The two pulse
// channels and the noise channel play their envelope's volume or nothing, as
// their generator's output bit says; the wave channel plays its sample buffer
// shifted by NR32.
u8 Apu::channelLevel(std::size_t channel) const {
    switch (channel) {
    case 2: return wave3_.output();
    case 3: return noise4_.output() ? noise4_.volume() : 0;
    default:
        return pulse_[channel].dutyOutput() ? pulse_[channel].volume() : 0;
    }
}

// The mixer. Pan Docs "Audio Details": a DAC that is on turns a digital 0 into
// an analog +1 and a digital 15 into an analog -1 -- the slope is negative --
// and one that is off "fades to an analog value of 0, which corresponds to
// 'digital 7.5'". So only a DAC decides whether a channel reaches the mix.
//
// A channel that is switched off is not the same thing as a DAC that is off:
// "a disabled channel outputs 0, which an enabled DAC will dutifully convert
// into 'analog 1'". It holds its side at +1, not at silence. Switching a
// channel off is audible because of that step to +1 and the high-pass
// filter's decay back down from it, not because the contribution vanishes.
//
// NR51 then routes each channel to the left, the right, both or neither, and
// NR50's two three-bit volumes scale each side by (volume + 1) / 8.
//
// The sum of four channels within [-1, +1] is divided by four, so a side of
// this pair is within [-1, +1] too: that is this emulator's unit for a sample
// and not something the hardware does. Nothing else happens here -- the DMG's
// high-pass capacitor, which is what pulls the mix back to zero, belongs to
// the output stage.
Apu::Sample Apu::sample() const {
    if (!powered_) {
        return Sample{0.0f, 0.0f};
    }
    float left = 0.0f;
    float right = 0.0f;
    const u8 routing = nr_[kNr51 - kFirst];
    for (std::size_t channel = 0; channel < channelOn_.size(); ++channel) {
        if (!dacOn(channel)) {
            continue;
        }
        // A channel that is switched off hands its DAC a digital 0, which the
        // mapping below turns into the +1 that DAC is still driving.
        const u8 level = channelOn_[channel] ? channelLevel(channel) : 0;
        const float analog = 1.0f - static_cast<float>(level) / kHalfScale;
        const unsigned bit = 1u << channel;
        if ((routing & (bit << kLeftShift)) != 0) {
            left += analog;
        }
        if ((routing & bit) != 0) {
            right += analog;
        }
    }
    // NR50 bit 7 and bit 3 are the Vin mixers: the cartridge's own sound pin
    // routed to a side. No cartridge this emulator runs drives that pin, so
    // there is nothing on it to add -- the bits are read and stored, and mix
    // in silence.
    const u8 volumes = nr_[kNr50 - kFirst];
    const float leftVolume = static_cast<float>(((volumes >> 4) & 0x07) + 1) / 8.0f;
    const float rightVolume = static_cast<float>((volumes & 0x07) + 1) / 8.0f;
    return Sample{left * leftVolume / kChannels, right * rightVolume / kChannels};
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
    default:
        // NR10, the sweep, which only channel 1 has. FF15 -- where channel
        // 2's would be -- lands in this same case, because it is five along
        // from NR10, and both callers filter it out two frames further up.
        // The address is checked here as well rather than relying on that:
        // a gap reaching the sweep would rewrite channel 1's shadow frequency
        // and could switch channel 1 off.
        if (offset == 0 && sweep_.write(value)) {
            channelOn_[0] = false;
        }
        break;
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
    if (channel == 2) {
        // Before the trigger, because the corruption is of the byte the
        // channel was about to read and the trigger throws that index away.
        if (channelOn_[2] && wave3_.aboutToRead()) {
            corruptWaveRam();
        }
        wave3_.trigger();
    }
    if (channel == 3) {
        noise4_.trigger();
    }
    channelOn_[channel] = dacOn(channel);
    // Channel 1's sweep takes its copy of the frequency here, and with a
    // non-zero shift runs its overflow check at once -- so the write that
    // triggered the channel can be the write that switches it back off.
    if (channel == 0 && sweep_.trigger(pulse_[0])) {
        channelOn_[0] = false;
    }
}

// Three of the four DACs are off when the top five bits of an envelope
// register are zero. The wave channel has no envelope, so its DAC is a bit of
// its own: NR30 bit 7. All four are read out of the stored byte and nowhere
// else, so there is no second copy of a DAC bit to fall out of step with the
// register -- clockSweep already writes nr_ without going through a channel.
bool Apu::dacOn(std::size_t channel) const {
    switch (channel) {
    case 0: return (nr_[kNr12 - kFirst] & 0xF8) != 0;
    case 1: return (nr_[kNr22 - kFirst] & 0xF8) != 0;
    case 2: return (nr_[kNr30 - kFirst] & 0x80) != 0;
    default: return (nr_[kNr42 - kFirst] & 0xF8) != 0;
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
    // NR10 goes to zero with the rest, and the unit behind it -- shadow
    // register, timer, enabled flag and the negate latch -- goes with it.
    sweep_.powerOff();
    wave3_.powerOff();
    noise4_.powerOff();
    powered_ = false;
}

void Apu::powerOn() {
    powered_ = true;
    step_ = 0;
    // Channel 3's sample buffer is cleared by the power coming back, so a
    // freshly powered APU emits a digital zero until the channel reads.
    wave3_.powerOn();
}

} // namespace fourshades


