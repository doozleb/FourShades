#pragma once

#include "core/Types.h"

namespace fourshades {

// The SM83 register file. F is private because its low four bits don't exist
// on the real chip: they always read as zero, whatever is written.
struct Registers {
    static constexpr u8 FlagZ = 0x80;
    static constexpr u8 FlagN = 0x40;
    static constexpr u8 FlagH = 0x20;
    static constexpr u8 FlagC = 0x10;

    u8 a = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    u16 sp = 0, pc = 0;

    u8 f() const { return f_; }
    void setF(u8 value) { f_ = static_cast<u8>(value & 0xF0); }
    bool flag(u8 mask) const { return (f_ & mask) != 0; }

    u16 af() const { return make16(a, f_); }
    u16 bc() const { return make16(b, c); }
    u16 de() const { return make16(d, e); }
    u16 hl() const { return make16(h, l); }
    void setAf(u16 value) { a = hi(value); setF(lo(value)); }
    void setBc(u16 value) { b = hi(value); c = lo(value); }
    void setDe(u16 value) { d = hi(value); e = lo(value); }
    void setHl(u16 value) { h = hi(value); l = lo(value); }

private:
    u8 f_ = 0;
};

} // namespace fourshades
