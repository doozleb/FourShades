#pragma once

#include "core/Bus.h"
#include "core/Registers.h"
#include "core/Types.h"

namespace fourshades {

// The Game Boy's SM83 CPU. It reaches the outside world only through Bus,
// one M-cycle per call, so every cycle it spends is visible on the bus.
class Cpu {
public:
    enum class State { Running, Halted, Stopped, Locked };

    explicit Cpu(Bus& bus) : bus_(bus) {}

    // Runs one whole instruction, opcode fetch included. While halted,
    // stopped or locked it spends one idle M-cycle instead.
    void step();

    State state() const { return state_; }

    // True after EI, until the instruction after it has run (Pan Docs: EI).
    bool imePending() const { return imeDelay_ > 0; }

    // Set when step() met an opcode with no implementation yet. The test
    // runner reports those instructions as unimplemented rather than failing.
    bool unimplemented() const { return unimplemented_; }

    Registers regs;
    bool ime = false;

private:
    // Cpu.cpp: fetch, register and stack helpers, dispatch, misc opcodes.
    u8 fetch8();
    u16 fetch16();
    u8 readR8(int index);               // 0-7: B C D E H L (HL) A
    void writeR8(int index, u8 value);
    u16 readRp(int index) const;        // 0-3: BC DE HL SP
    void writeRp(int index, u16 value);
    u16 readRp2(int index) const;       // 0-3: BC DE HL AF
    void writeRp2(int index, u16 value);
    void push16(u16 value);
    u16 pop16();
    bool condition(int index) const;    // 0-3: NZ Z NC C
    void setFlags(bool z, bool n, bool h, bool c);
    void execute(u8 opcode);
    bool executeMisc(u8 opcode);

    // CpuLoads8.cpp
    bool executeLoads8(u8 opcode);

    // CpuAlu8.cpp
    bool executeAlu8(u8 opcode);
    void alu8(int operation, u8 value); // 0-7: ADD ADC SUB SBC AND XOR OR CP
    u8 inc8(u8 value);
    u8 dec8(u8 value);
    void daa();

    // CpuWide.cpp
    bool executeWide(u8 opcode);
    void addHl(u16 value);
    u16 addSpOffset(u8 offset);

    // CpuControl.cpp
    bool executeControl(u8 opcode);
    void jumpRelative(u8 offset);

    // CpuCb.cpp
    void executeCb();
    u8 rotateShift(int operation, u8 value); // 0-7: RLC RRC RL RR SLA SRA SWAP SRL

    Bus& bus_;
    State state_ = State::Running;
    int imeDelay_ = 0;
    bool unimplemented_ = false;
};

} // namespace fourshades
