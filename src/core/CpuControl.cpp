#include "core/Cpu.h"

namespace fourshades {

bool Cpu::executeControl(u8 opcode) {
    const int y = (opcode >> 3) & 7;

    switch (opcode) {
    case 0x18: { // JR e
        const u8 offset = fetch8();
        jumpRelative(offset);
        return true;
    }
    case 0x20: case 0x28: case 0x30: case 0x38: { // JR cc, e (cc = y - 4)
        const u8 offset = fetch8();
        if (condition(y - 4)) {
            jumpRelative(offset);
        }
        return true;
    }
    case 0xC3: { // JP nn
        const u16 target = fetch16();
        regs.pc = target;
        bus_.idle();
        return true;
    }
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: { // JP cc, nn
        const u16 target = fetch16();
        if (condition(y)) {
            regs.pc = target;
            bus_.idle();
        }
        return true;
    }
    case 0xE9: // JP HL: no extra cycle
        regs.pc = regs.hl();
        return true;
    case 0xCD: { // CALL nn
        const u16 target = fetch16();
        push16(regs.pc);
        regs.pc = target;
        return true;
    }
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: { // CALL cc, nn
        const u16 target = fetch16();
        if (condition(y)) {
            push16(regs.pc);
            regs.pc = target;
        }
        return true;
    }
    case 0xC9: // RET
        regs.pc = pop16();
        bus_.idle();
        return true;
    case 0xD9: // RETI: like RET, and enables interrupts with no delay
        regs.pc = pop16();
        bus_.idle();
        ime = true;
        return true;
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: // RET cc: checking cc costs a cycle
        bus_.idle();
        if (condition(y)) {
            regs.pc = pop16();
            bus_.idle();
        }
        return true;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF: // RST
        push16(regs.pc);
        regs.pc = static_cast<u16>(y * 8);
        return true;
    default:
        return false;
    }
}

void Cpu::jumpRelative(u8 offset) {
    regs.pc = static_cast<u16>(regs.pc + static_cast<i8>(offset));
    bus_.idle();
}

} // namespace fourshades
