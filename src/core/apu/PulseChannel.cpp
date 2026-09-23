#include "core/apu/PulseChannel.h"

#include <array>
#include <cstddef>

namespace fourshades {

namespace {

// Pan Docs "Audio Registers", NR11/NR21 bits 7-6: the four duty patterns,
// each read left to right as the position walks 0 to 7.
constexpr std::array<std::array<bool, 8>, 4> kDutyPatterns = {{
    {{false, false, false, false, false, false, false, true}},  // 12.5%
    {{true, false, false, false, false, false, false, true}},   // 25%
    {{true, false, false, false, false, true, true, true}},     // 50%
    {{false, true, true, true, true, true, true, false}},       // 75%
}};

} // namespace

void PulseChannel::writeDuty(u8 value) {
    duty_ = (value >> 6) & 0x03;
}

void PulseChannel::writeEnvelope(u8 value) {
    envelope_.write(value);
    dacOn_ = (value & 0xF8) != 0;
}

void PulseChannel::writeFrequencyLow(u8 value) {
    frequency_ = (frequency_ & 0x700) | value;
}

void PulseChannel::writeFrequencyHigh(u8 value) {
    frequency_ = ((value & 0x07) << 8) | (frequency_ & 0x0FF);
}

void PulseChannel::setFrequency(int frequency) {
    frequency_ = frequency & 0x7FF;
}

void PulseChannel::trigger() {
    timer_ = period();
    envelope_.trigger();
}

// A period is never shorter than four T-cycles, so the loop always ends; an
// M-cycle's four T-cycles can cross at most one boundary.
void PulseChannel::tick(int tCycles) {
    timer_ -= tCycles;
    while (timer_ <= 0) {
        timer_ += period();
        position_ = (position_ + 1) & 7;
    }
}

void PulseChannel::powerOff() {
    duty_ = 0;
    frequency_ = 0;
    position_ = 0;
    timer_ = kMaxPeriod;
    dacOn_ = false;
    envelope_ = VolumeEnvelope{};
}

bool PulseChannel::dutyOutput() const {
    return kDutyPatterns[static_cast<std::size_t>(duty_)]
                        [static_cast<std::size_t>(position_)];
}

} // namespace fourshades
