#pragma once

#include "core/Types.h"

#include <cstdint>

namespace fourshades {

// The five registers, live or latched. Days are 9 bits: `dayLow` plus bit 0
// of `dayHigh`. Bit 6 of `dayHigh` halts the clock; bit 7 is the carry that
// a day-counter overflow sets and only the program clears.
struct RtcRegisters {
    u8 seconds = 0;
    u8 minutes = 0;
    u8 hours = 0;
    u8 dayLow = 0;
    u8 dayHigh = 0;
};

// Everything needed to save and restore a clock.
struct RtcState {
    RtcRegisters live;
    RtcRegisters latched;
};

// The MBC3 cartridge's real-time clock, per Pan Docs "MBC3". It is not an
// `Mbc`: it is a chip beside the banking, which Mbc3 owns and drives, so it
// is built and tested on its own.
//
// It counts *emulated* time and never host time: the core is not allowed to
// read a wall clock, so the only inputs are `tick()` (one M-cycle, 1048576
// of which make a second) and `advanceSeconds()`, which the app layer calls
// once at load with the real seconds elapsed since the save was written.
// That catch-up can be tens of millions of seconds, so it is arithmetic
// rather than a loop.
//
// There are two copies of the registers. The live one counts; the latched
// one is what the program reads, and only a 6000-7FFF latch sequence copies
// live over latched. That is the whole point of the latch: a program reading
// five registers one at a time would otherwise see a clock that ticked
// between two of them.
//
// Deliberately plain data: Mbc3 is copied whenever a Cartridge is, and the
// compiler's own copy constructor has to do the right thing.
class Rtc {
public:
    void tick();  // one M-cycle
    void latch(); // live -> latched

    // `reg` is 0x08-0x0C as written to 4000-5FFF. Any other value is not
    // a clock register and must not reach these.
    u8 read(u8 reg) const;         // from the latched copy
    void write(u8 reg, u8 value);  // to the live copy

    bool halted() const; // bit 6 of live dayHigh

    RtcState state() const;
    void setState(const RtcState& state);
    void advanceSeconds(std::uint64_t seconds); // catch-up; no-op while halted

private:
    // 4194304 T-cycles per second; GameBoy::tick() is one M-cycle, so
    // 1048576 ticks make a second.
    static constexpr std::uint64_t kTicksPerSecond = 1048576;

    void addDays(std::uint64_t days);

    RtcRegisters live_;
    RtcRegisters latched_;
    std::uint64_t ticks_ = 0; // sub-second accumulator, frozen while halted
};

} // namespace fourshades
