#include "core/Cpu.h"

namespace fourshades {

void Cpu::step() {
    if (state_ != State::Running) {
        bus_.idle();
        return;
    }
    execute(fetch8());
    // EI sets the delay to 2, so IME turns on at the end of the instruction
    // after EI. DI zeroes it, cancelling a pending EI.
    if (imeDelay_ > 0 && --imeDelay_ == 0) {
        ime = true;
    }
}

u8 Cpu::fetch8() {
    const u8 value = bus_.read(regs.pc);
    regs.pc = static_cast<u16>(regs.pc + 1);
    return value;
}

u16 Cpu::fetch16() {
    const u8 low = fetch8();
    const u8 high = fetch8();
    return make16(high, low);
}

u8 Cpu::readR8(int index) {
    switch (index) {
    case 0: return regs.b;
    case 1: return regs.c;
    case 2: return regs.d;
    case 3: return regs.e;
    case 4: return regs.h;
    case 5: return regs.l;
    case 6: return bus_.read(regs.hl());
    default: return regs.a;
    }
}

void Cpu::writeR8(int index, u8 value) {
    switch (index) {
    case 0: regs.b = value; break;
    case 1: regs.c = value; break;
    case 2: regs.d = value; break;
    case 3: regs.e = value; break;
    case 4: regs.h = value; break;
    case 5: regs.l = value; break;
    case 6: bus_.write(regs.hl(), value); break;
    default: regs.a = value; break;
    }
}

u16 Cpu::readRp(int index) const {
    switch (index) {
    case 0: return regs.bc();
    case 1: return regs.de();
    case 2: return regs.hl();
    default: return regs.sp;
    }
}

void Cpu::writeRp(int index, u16 value) {
    switch (index) {
    case 0: regs.setBc(value); break;
    case 1: regs.setDe(value); break;
    case 2: regs.setHl(value); break;
    default: regs.sp = value; break;
    }
}

u16 Cpu::readRp2(int index) const {
    return index == 3 ? regs.af() : readRp(index);
}

void Cpu::writeRp2(int index, u16 value) {
    if (index == 3) {
        regs.setAf(value);
    } else {
        writeRp(index, value);
    }
}

void Cpu::push16(u16 value) {
    bus_.idle(); // SP is decremented before the first write, costing a cycle
    regs.sp = static_cast<u16>(regs.sp - 1);
    bus_.write(regs.sp, hi(value));
    regs.sp = static_cast<u16>(regs.sp - 1);
    bus_.write(regs.sp, lo(value));
}

u16 Cpu::pop16() {
    const u8 low = bus_.read(regs.sp);
    regs.sp = static_cast<u16>(regs.sp + 1);
    const u8 high = bus_.read(regs.sp);
    regs.sp = static_cast<u16>(regs.sp + 1);
    return make16(high, low);
}

bool Cpu::condition(int index) const {
    switch (index & 3) {
    case 0: return !regs.flag(Registers::FlagZ);
    case 1: return regs.flag(Registers::FlagZ);
    case 2: return !regs.flag(Registers::FlagC);
    default: return regs.flag(Registers::FlagC);
    }
}

void Cpu::setFlags(bool z, bool n, bool h, bool c) {
    regs.setF(static_cast<u8>((z ? Registers::FlagZ : 0) | (n ? Registers::FlagN : 0) |
                              (h ? Registers::FlagH : 0) | (c ? Registers::FlagC : 0)));
}

void Cpu::execute(u8 opcode) {
    if (executeMisc(opcode) || executeLoads8(opcode) || executeAlu8(opcode) ||
        executeWide(opcode) || executeControl(opcode)) {
        return;
    }
    unimplemented_ = true;
}

bool Cpu::executeMisc(u8 opcode) {
    switch (opcode) {
    case 0x00: // NOP
        return true;
    case 0x10: // STOP
        // Pan Docs' STOP flowchart (Reducing Power Consumption): with no button
        // held, no speed switch requested (always the case on a DMG) and no
        // interrupt pending, "STOP is a 2-byte opcode, STOP mode is entered".
        // Pan Docs is silent on whether the second byte is read with a bus
        // cycle, so the test's observed no-read bus pattern is kept. The
        // button, interrupt and DIV-reset cases need the joypad, interrupts
        // and timer (piece 2). See docs/known-divergences.md.
        regs.pc = static_cast<u16>(regs.pc + 1);
        state_ = State::Stopped;
        return true;
    case 0x76: // HALT
        state_ = State::Halted;
        return true;
    case 0xF3: // DI
        ime = false;
        imeDelay_ = 0;
        return true;
    case 0xFB: // EI
        // A second EI while one is already pending must not restart the
        // delay: IME turns on after the instruction following the FIRST EI.
        if (imeDelay_ == 0) {
            imeDelay_ = 2;
        }
        return true;
    case 0xCB:
        executeCb();
        return true;
    case 0xD3: case 0xDB: case 0xDD: case 0xE3: case 0xE4: case 0xEB:
    case 0xEC: case 0xED: case 0xF4: case 0xFC: case 0xFD:
        state_ = State::Locked; // illegal opcode: the real CPU hangs
        return true;
    default:
        return false;
    }
}

} // namespace fourshades
