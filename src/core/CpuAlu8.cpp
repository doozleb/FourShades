#include "core/Cpu.h"

namespace fourshades {

bool Cpu::executeAlu8(u8 opcode) {
    const int y = (opcode >> 3) & 7;
    const int z = opcode & 7;

    if (opcode >= 0x80 && opcode <= 0xBF) { // ALU A, r
        alu8(y, readR8(z));
        return true;
    }
    if (opcode >= 0xC0 && z == 6) { // ALU A, n (C6 CE D6 DE E6 EE F6 FE)
        alu8(y, fetch8());
        return true;
    }
    if (opcode < 0x40 && z == 4) { // INC r
        writeR8(y, inc8(readR8(y)));
        return true;
    }
    if (opcode < 0x40 && z == 5) { // DEC r
        writeR8(y, dec8(readR8(y)));
        return true;
    }

    const bool carry = regs.flag(Registers::FlagC);
    switch (opcode) {
    case 0x07: { // RLCA. The A-register rotates always clear Z.
        const bool out = (regs.a & 0x80) != 0;
        regs.a = static_cast<u8>((regs.a << 1) | (out ? 0x01 : 0));
        setFlags(false, false, false, out);
        return true;
    }
    case 0x0F: { // RRCA
        const bool out = (regs.a & 0x01) != 0;
        regs.a = static_cast<u8>((regs.a >> 1) | (out ? 0x80 : 0));
        setFlags(false, false, false, out);
        return true;
    }
    case 0x17: { // RLA
        const bool out = (regs.a & 0x80) != 0;
        regs.a = static_cast<u8>((regs.a << 1) | (carry ? 0x01 : 0));
        setFlags(false, false, false, out);
        return true;
    }
    case 0x1F: { // RRA
        const bool out = (regs.a & 0x01) != 0;
        regs.a = static_cast<u8>((regs.a >> 1) | (carry ? 0x80 : 0));
        setFlags(false, false, false, out);
        return true;
    }
    case 0x27: daa(); return true;
    case 0x2F: // CPL
        regs.a = static_cast<u8>(~regs.a);
        setFlags(regs.flag(Registers::FlagZ), true, true, carry);
        return true;
    case 0x37: setFlags(regs.flag(Registers::FlagZ), false, false, true); return true;   // SCF
    case 0x3F: setFlags(regs.flag(Registers::FlagZ), false, false, !carry); return true; // CCF
    default: return false;
    }
}

void Cpu::alu8(int operation, u8 value) {
    const int a = regs.a;
    const int v = value;
    const int carry = regs.flag(Registers::FlagC) ? 1 : 0;
    switch (operation) {
    case 0: { // ADD
        const int r = a + v;
        regs.a = static_cast<u8>(r);
        setFlags(regs.a == 0, false, ((a & 0xF) + (v & 0xF)) > 0xF, r > 0xFF);
        break;
    }
    case 1: { // ADC
        const int r = a + v + carry;
        regs.a = static_cast<u8>(r);
        setFlags(regs.a == 0, false, ((a & 0xF) + (v & 0xF) + carry) > 0xF, r > 0xFF);
        break;
    }
    case 2: { // SUB
        const int r = a - v;
        regs.a = static_cast<u8>(r);
        setFlags(regs.a == 0, true, (a & 0xF) < (v & 0xF), r < 0);
        break;
    }
    case 3: { // SBC
        const int r = a - v - carry;
        regs.a = static_cast<u8>(r);
        setFlags(regs.a == 0, true, ((a & 0xF) - (v & 0xF) - carry) < 0, r < 0);
        break;
    }
    case 4: // AND
        regs.a = static_cast<u8>(a & v);
        setFlags(regs.a == 0, false, true, false);
        break;
    case 5: // XOR
        regs.a = static_cast<u8>(a ^ v);
        setFlags(regs.a == 0, false, false, false);
        break;
    case 6: // OR
        regs.a = static_cast<u8>(a | v);
        setFlags(regs.a == 0, false, false, false);
        break;
    default: { // CP: SUB without storing the result
        const int r = a - v;
        setFlags(static_cast<u8>(r) == 0, true, (a & 0xF) < (v & 0xF), r < 0);
        break;
    }
    }
}

u8 Cpu::inc8(u8 value) {
    const u8 r = static_cast<u8>(value + 1);
    setFlags(r == 0, false, (value & 0xF) == 0xF, regs.flag(Registers::FlagC));
    return r;
}

u8 Cpu::dec8(u8 value) {
    const u8 r = static_cast<u8>(value - 1);
    setFlags(r == 0, true, (value & 0xF) == 0, regs.flag(Registers::FlagC));
    return r;
}

// Adjusts A to binary-coded decimal after an addition (N clear) or a
// subtraction (N set), using the H and C flags that operation left.
void Cpu::daa() {
    u8 a = regs.a;
    bool carry = regs.flag(Registers::FlagC);
    const bool half = regs.flag(Registers::FlagH);
    const bool subtract = regs.flag(Registers::FlagN);
    if (!subtract) {
        if (carry || a > 0x99) {
            a = static_cast<u8>(a + 0x60);
            carry = true;
        }
        if (half || (a & 0x0F) > 0x09) {
            a = static_cast<u8>(a + 0x06);
        }
    } else {
        if (carry) {
            a = static_cast<u8>(a - 0x60);
        }
        if (half) {
            a = static_cast<u8>(a - 0x06);
        }
    }
    regs.a = a;
    setFlags(a == 0, subtract, false, carry);
}

} // namespace fourshades
