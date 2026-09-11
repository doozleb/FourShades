#include "core/Cpu.h"

namespace fourshades {

bool Cpu::executeWide(u8 opcode) {
    const int p = (opcode >> 4) & 3;

    if (opcode < 0x40) {
        switch (opcode & 0x0F) {
        case 0x1: // LD rr, nn
            writeRp(p, fetch16());
            return true;
        case 0x3: // INC rr: the 16-bit increment costs an internal cycle
            writeRp(p, static_cast<u16>(readRp(p) + 1));
            bus_.idle();
            return true;
        case 0xB: // DEC rr
            writeRp(p, static_cast<u16>(readRp(p) - 1));
            bus_.idle();
            return true;
        case 0x9: // ADD HL, rr
            addHl(readRp(p));
            bus_.idle();
            return true;
        default:
            break;
        }
    }
    if (opcode >= 0xC0) {
        switch (opcode & 0x0F) {
        case 0x1: // POP rr (C1 D1 E1 F1; F1 is POP AF, which masks F's low nibble)
            writeRp2(p, pop16());
            return true;
        case 0x5: // PUSH rr
            push16(readRp2(p));
            return true;
        default:
            break;
        }
    }

    switch (opcode) {
    case 0x08: { // LD (nn), SP
        const u16 address = fetch16();
        bus_.write(address, lo(regs.sp));
        bus_.write(static_cast<u16>(address + 1), hi(regs.sp));
        return true;
    }
    case 0xF9: // LD SP, HL
        regs.sp = regs.hl();
        bus_.idle();
        return true;
    case 0xE8: { // ADD SP, e
        const u16 result = addSpOffset(fetch8());
        bus_.idle();
        bus_.idle();
        regs.sp = result;
        return true;
    }
    case 0xF8: { // LD HL, SP+e
        const u16 result = addSpOffset(fetch8());
        bus_.idle();
        regs.setHl(result);
        return true;
    }
    default:
        return false;
    }
}

void Cpu::addHl(u16 value) {
    const int hl = regs.hl();
    const int r = hl + value;
    setFlags(regs.flag(Registers::FlagZ), false, ((hl & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF, r > 0xFFFF);
    regs.setHl(static_cast<u16>(r));
}

// SP plus a signed offset. The flags come from the unsigned low-byte addition,
// even though the offset itself is signed; Z and N are always cleared.
u16 Cpu::addSpOffset(u8 offset) {
    const int sp = regs.sp;
    setFlags(false, false, ((sp & 0x0F) + (offset & 0x0F)) > 0x0F, ((sp & 0xFF) + offset) > 0xFF);
    return static_cast<u16>(sp + static_cast<i8>(offset));
}

} // namespace fourshades
