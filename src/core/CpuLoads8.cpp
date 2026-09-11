#include "core/Cpu.h"

namespace fourshades {

// 8-bit loads. (HL) as an operand goes through readR8/writeR8 index 6, which
// costs the memory cycle at the right point.
bool Cpu::executeLoads8(u8 opcode) {
    const int y = (opcode >> 3) & 7;
    const int z = opcode & 7;

    if (opcode >= 0x40 && opcode <= 0x7F && opcode != 0x76) { // LD r, r'
        writeR8(y, readR8(z));
        return true;
    }
    if (opcode < 0x40 && z == 6) { // LD r, n (06 0E 16 1E 26 2E 36 3E)
        writeR8(y, fetch8());
        return true;
    }

    switch (opcode) {
    case 0x02: bus_.write(regs.bc(), regs.a); return true;
    case 0x12: bus_.write(regs.de(), regs.a); return true;
    case 0x22: // LD (HL+), A
        bus_.write(regs.hl(), regs.a);
        regs.setHl(static_cast<u16>(regs.hl() + 1));
        return true;
    case 0x32: // LD (HL-), A
        bus_.write(regs.hl(), regs.a);
        regs.setHl(static_cast<u16>(regs.hl() - 1));
        return true;
    case 0x0A: regs.a = bus_.read(regs.bc()); return true;
    case 0x1A: regs.a = bus_.read(regs.de()); return true;
    case 0x2A: // LD A, (HL+)
        regs.a = bus_.read(regs.hl());
        regs.setHl(static_cast<u16>(regs.hl() + 1));
        return true;
    case 0x3A: // LD A, (HL-)
        regs.a = bus_.read(regs.hl());
        regs.setHl(static_cast<u16>(regs.hl() - 1));
        return true;
    case 0xE0: { // LDH (n), A
        const u8 n = fetch8();
        bus_.write(static_cast<u16>(0xFF00 + n), regs.a);
        return true;
    }
    case 0xF0: { // LDH A, (n)
        const u8 n = fetch8();
        regs.a = bus_.read(static_cast<u16>(0xFF00 + n));
        return true;
    }
    case 0xE2: bus_.write(static_cast<u16>(0xFF00 + regs.c), regs.a); return true; // LD (C), A
    case 0xF2: regs.a = bus_.read(static_cast<u16>(0xFF00 + regs.c)); return true; // LD A, (C)
    case 0xEA: bus_.write(fetch16(), regs.a); return true;                       // LD (nn), A
    case 0xFA: regs.a = bus_.read(fetch16()); return true;                       // LD A, (nn)
    default: return false;
    }
}

} // namespace fourshades
