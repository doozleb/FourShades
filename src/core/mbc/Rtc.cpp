#include "core/mbc/Rtc.h"

namespace fourshades {

namespace {
constexpr u8 kRegSeconds = 0x08;
constexpr u8 kRegMinutes = 0x09;
constexpr u8 kRegHours = 0x0A;
constexpr u8 kRegDayLow = 0x0B;
constexpr u8 kRegDayHigh = 0x0C;

constexpr u8 kHaltBit = 0x40;
constexpr u8 kCarryBit = 0x80;

// How wide each counter is. Pan Docs gives the range a running clock keeps to
// (0-59, 0-59, 0-23) but not how many bits the registers have, and the two are
// not the same thing: a program can write any byte, and only the width decides
// what comes back. Six bits for seconds and minutes, five for hours, and three
// for the day-high register — bits 0, 6 and 7, the only ones Pan Docs names.
// See docs/known-divergences.md, "MBC3's clock: the register widths and the
// latch, where Pan Docs is silent".
constexpr u8 kSecondsMask = 0x3F;
constexpr u8 kMinutesMask = 0x3F;
constexpr u8 kHoursMask = 0x1F;
constexpr u8 kDayHighMask = 0xC1;

constexpr std::uint64_t kDayCounterPeriod = 512; // 9 bits

// Advances one counter by `count` steps and returns how many times it
// carried into the next one.
//
// `limit` is the last in-range value: the counter carries when it steps off
// it, so a value of `limit` plus one step is 0 plus a carry. A value above
// `limit` is not a fault to be corrected — the hardware keeps whatever the
// program wrote and counts on from it, with no carry, until the register
// wraps back into range. `span` is how many values the register can hold, so
// writing 63 to the six-bit seconds counts 63, 0, 1, ... 59, and only then
// carries a minute.
std::uint64_t advanceField(u8& value, u8 limit, std::uint64_t span, std::uint64_t count) {
    if (value > limit) {
        const std::uint64_t toWrap = span - value; // steps to reach 0
        if (count < toWrap) {
            value = static_cast<u8>(value + count);
            return 0;
        }
        count -= toWrap;
        value = 0;
    }

    // In range. Split the step count so nothing overflows even when a save
    // file hands us a nonsense number of seconds.
    const std::uint64_t period = static_cast<std::uint64_t>(limit) + 1;
    const std::uint64_t whole = count / period;
    const std::uint64_t part = value + count % period;
    value = static_cast<u8>(part % period);
    return whole + part / period;
}

// A saved state restored into registers only as wide as the chip's. Bits the
// hardware cannot hold must not come back through a save file either.
RtcRegisters narrowed(const RtcRegisters& regs) {
    RtcRegisters out = regs;
    out.seconds = static_cast<u8>(out.seconds & kSecondsMask);
    out.minutes = static_cast<u8>(out.minutes & kMinutesMask);
    out.hours = static_cast<u8>(out.hours & kHoursMask);
    out.dayHigh = static_cast<u8>(out.dayHigh & kDayHighMask);
    return out;
}
} // namespace

void Rtc::addDays(std::uint64_t days) {
    const std::uint64_t day = live_.dayLow | ((live_.dayHigh & 0x01u) << 8);
    const std::uint64_t total = day + days;
    live_.dayLow = static_cast<u8>(total & 0xFF);
    live_.dayHigh = static_cast<u8>((live_.dayHigh & ~0x01u) | ((total >> 8) & 0x01u));
    if (total >= kDayCounterPeriod) {
        // Pan Docs: bit 7 is set by a day-counter overflow and cleared only
        // by the program. Never cleared here, however many days pass.
        live_.dayHigh = static_cast<u8>(live_.dayHigh | kCarryBit);
    }
}

void Rtc::advanceSeconds(std::uint64_t seconds) {
    if (halted()) {
        return;
    }
    std::uint64_t carry = advanceField(live_.seconds, 59, kSecondsMask + 1, seconds);
    carry = advanceField(live_.minutes, 59, kMinutesMask + 1, carry);
    carry = advanceField(live_.hours, 23, kHoursMask + 1, carry);
    addDays(carry);
}

void Rtc::tick() {
    if (halted()) {
        // Everything freezes, the sub-second accumulator included: the clock
        // picks up mid-second where it stopped.
        return;
    }
    if (++ticks_ >= kTicksPerSecond) {
        ticks_ -= kTicksPerSecond;
        advanceSeconds(1);
    }
}

void Rtc::latch() { latched_ = live_; }

u8 Rtc::read(u8 reg) const {
    switch (reg) {
    case kRegSeconds:
        return latched_.seconds;
    case kRegMinutes:
        return latched_.minutes;
    case kRegHours:
        return latched_.hours;
    case kRegDayLow:
        return latched_.dayLow;
    case kRegDayHigh:
        // Three bits wide, so bits 1-5 are already gone: see the masks above.
        return latched_.dayHigh;
    default:
        // Not a clock register; the caller decodes 0x08-0x0C before getting
        // here, so this is the open-bus answer and nothing more.
        return 0xFF;
    }
}

void Rtc::write(u8 reg, u8 value) {
    switch (reg) {
    case kRegSeconds:
        live_.seconds = static_cast<u8>(value & kSecondsMask);
        // Hardware restarts the sub-second divider on a seconds write, so a
        // program that sets the seconds gets a whole second before the next
        // increment instead of however much of one was left.
        ticks_ = 0;
        break;
    case kRegMinutes:
        live_.minutes = static_cast<u8>(value & kMinutesMask);
        break;
    case kRegHours:
        live_.hours = static_cast<u8>(value & kHoursMask);
        break;
    case kRegDayLow:
        live_.dayLow = value;
        break;
    case kRegDayHigh:
        // The three bits that exist, and this is the only way the halt bit is
        // set or cleared and the only way the day-overflow carry is cleared.
        live_.dayHigh = static_cast<u8>(value & kDayHighMask);
        break;
    default:
        break;
    }
}

bool Rtc::halted() const { return (live_.dayHigh & kHaltBit) != 0; }

RtcState Rtc::state() const { return RtcState{live_, latched_}; }

void Rtc::setState(const RtcState& state) {
    live_ = narrowed(state.live);
    latched_ = narrowed(state.latched);
    // The saved state is whole seconds, so a restored clock starts a fresh
    // one rather than inheriting whatever this object had accumulated.
    ticks_ = 0;
}

} // namespace fourshades
