#pragma once

#include "core/Types.h"

namespace fourshades {

// DIV, TIMA, TMA and TAC, per Pan Docs "Timer and Divider Registers" and
// "Timer obscure behaviour". The 16-bit system counter gains 4 (T-cycles)
// every M-cycle; DIV is its high byte. TIMA ticks on a falling edge of
// (TAC enable AND the counter bit TAC selects), so writes to DIV and TAC can
// tick it too.
//
// The counter lives here and other parts of the machine hang off its bits --
// the serial clock off bit 8, the sound frame sequencer off bit 12 -- so this
// is also the one place that decides when a counter bit falls.
class Timer {
public:
    // One M-cycle. Returns true when this cycle requests the timer interrupt.
    bool tick();

    u8 read(u16 address) const;         // FF04-FF07
    void write(u16 address, u8 value);  // FF04-FF07

    u16 counter() const { return counter_; }
    void setCounter(u16 value) { counter_ = value; }

    // True when bit `bit` (0-15) of the system counter went 1 -> 0 during the
    // M-cycle in progress: either the increment in tick(), or a write to DIV
    // within that cycle clearing a bit the increment had left set. Each tick()
    // starts a fresh cycle, so an edge is reported once and only once.
    //
    // Reading this does not consume it: it stays true for the whole M-cycle,
    // so every subsystem that cares can ask. But a subsystem that asks only
    // from the machine's own per-cycle tick never sees the second kind of
    // edge, because the machine advances time first and performs the access
    // second: a DIV write lands after the asking is over, and the next cycle
    // wipes the edge. Whatever wants those has to ask again after the write.
    bool counterBitFell(int bit) const { return ((fell_ >> bit) & 1) != 0; }

    // The same question about a counter pair held elsewhere.
    static bool counterBitFell(u16 before, u16 after, int bit) {
        const u16 mask = static_cast<u16>(1u << bit);
        return (before & mask) != 0 && (after & mask) == 0;
    }

private:
    bool input() const;
    void increment();
    // Records every bit that `before` held and the new counter_ does not.
    void noteFalls(u16 before) { fell_ = static_cast<u16>(fell_ | (before & ~counter_)); }

    u16 counter_ = 0;
    u16 fell_ = 0; // counter bits that fell during the M-cycle in progress
    u8 tima_ = 0;
    u8 tma_ = 0;
    u8 tac_ = 0xF8;
    bool overflowed_ = false; // TIMA overflowed this cycle ("cycle A")
    bool reloading_ = false;  // this cycle is the reload cycle ("cycle B")
};

} // namespace fourshades
