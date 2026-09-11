#pragma once

#include "core/Types.h"

namespace fourshades {

// DIV, TIMA, TMA and TAC, per Pan Docs "Timer and Divider Registers" and
// "Timer obscure behaviour". The 16-bit system counter gains 4 (T-cycles)
// every M-cycle; DIV is its high byte. TIMA ticks on a falling edge of
// (TAC enable AND the counter bit TAC selects), so writes to DIV and TAC can
// tick it too.
class Timer {
public:
    // One M-cycle. Returns true when this cycle requests the timer interrupt.
    bool tick();

    u8 read(u16 address) const;         // FF04-FF07
    void write(u16 address, u8 value);  // FF04-FF07

    u16 counter() const { return counter_; }
    void setCounter(u16 value) { counter_ = value; }

private:
    bool input() const;
    void increment();

    u16 counter_ = 0;
    u8 tima_ = 0;
    u8 tma_ = 0;
    u8 tac_ = 0xF8;
    bool overflowed_ = false; // TIMA overflowed this cycle ("cycle A")
    bool reloading_ = false;  // this cycle is the reload cycle ("cycle B")
};

} // namespace fourshades
