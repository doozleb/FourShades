#include "core/Cpu.h"

namespace fourshades {

// CB xx: x = 0 rotate/shift/swap, 1 BIT, 2 RES, 3 SET; y = operation or bit;
// z = register (6 is (HL), which reads memory and, except for BIT, writes it back).
void Cpu::executeCb() {
    // The CB byte is read here by fetch8(), on its own M-cycle, and - like
    // every other byte fetch8() reads - it reports no IDU write for the PC
    // increment that goes with it. That is a gap against Pan Docs, recorded
    // in docs/known-divergences.md, "Still unimplemented: the PC increment
    // on an operand byte, the CB byte included"; it is not closed here.
    //
    // What used to stand in this place was a bus_.iduCycle(regs.pc) call.
    // step() advances PC before calling execute(), so regs.pc is the CB
    // byte's address, not the prefix's, and no tick has run since the prefix
    // was fetched: the call reported the wrong address against an M-cycle
    // that had already reported the right one. Every other call site reports
    // the address the unit stepped *from*, and for a prefix at $FDFF this
    // one reported $FE00 instead - inside OAM, corrupting a row for an
    // M-cycle whose own address was outside it. It was deleted rather than
    // moved: there is nothing for it to report that step() has not already.
    const u8 opcode = fetch8();
    const int x = opcode >> 6;
    const int y = (opcode >> 3) & 7;
    const int z = opcode & 7;
    const u8 value = readR8(z);

    switch (x) {
    case 0:
        writeR8(z, rotateShift(y, value));
        break;
    case 1: // BIT y, r
        setFlags((value & (1 << y)) == 0, false, true, regs.flag(Registers::FlagC));
        break;
    case 2: // RES y, r
        writeR8(z, static_cast<u8>(value & ~(1 << y)));
        break;
    default: // SET y, r
        writeR8(z, static_cast<u8>(value | (1 << y)));
        break;
    }
}

u8 Cpu::rotateShift(int operation, u8 value) {
    const bool oldCarry = regs.flag(Registers::FlagC);
    u8 r = 0;
    bool carry = false;
    switch (operation) {
    case 0: // RLC
        carry = (value & 0x80) != 0;
        r = static_cast<u8>((value << 1) | (carry ? 0x01 : 0));
        break;
    case 1: // RRC
        carry = (value & 0x01) != 0;
        r = static_cast<u8>((value >> 1) | (carry ? 0x80 : 0));
        break;
    case 2: // RL
        carry = (value & 0x80) != 0;
        r = static_cast<u8>((value << 1) | (oldCarry ? 0x01 : 0));
        break;
    case 3: // RR
        carry = (value & 0x01) != 0;
        r = static_cast<u8>((value >> 1) | (oldCarry ? 0x80 : 0));
        break;
    case 4: // SLA
        carry = (value & 0x80) != 0;
        r = static_cast<u8>(value << 1);
        break;
    case 5: // SRA: keeps bit 7
        carry = (value & 0x01) != 0;
        r = static_cast<u8>((value >> 1) | (value & 0x80));
        break;
    case 6: // SWAP
        r = static_cast<u8>((value << 4) | (value >> 4));
        break;
    default: // SRL
        carry = (value & 0x01) != 0;
        r = static_cast<u8>(value >> 1);
        break;
    }
    setFlags(r == 0, false, false, carry);
    return r;
}

} // namespace fourshades
