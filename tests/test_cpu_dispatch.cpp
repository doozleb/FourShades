// A sweep over every opcode, checked only for which "bucket" step() lands it
// in (locked / halted / stopped / running-and-implemented). SingleStepTests
// checks exact behaviour; this just makes sure dispatch never falls through
// to "unimplemented" for an opcode that should be handled, and that the CPU
// doesn't lock, halt or stop unless the opcode is one of the ones documented
// to do that.
#include <doctest/doctest.h>

#include "core/Cpu.h"
#include "sst/RecordingBus.h"

using namespace fourshades;
using sst::RecordingBus;

namespace {
bool isIllegal(u8 opcode) {
    switch (opcode) {
    case 0xD3: case 0xDB: case 0xDD: case 0xE3: case 0xE4: case 0xEB:
    case 0xEC: case 0xED: case 0xF4: case 0xFC: case 0xFD:
        return true;
    default:
        return false;
    }
}
} // namespace

TEST_CASE("every base opcode except CB dispatches to a defined state") {
    for (int op = 0x00; op <= 0xFF; ++op) {
        if (op == 0xCB) {
            continue;
        }
        const u8 opcode = static_cast<u8>(op);
        CAPTURE(op);

        RecordingBus bus;
        bus.poke(0x0100, opcode);
        bus.poke(0x0101, 0x00);
        bus.poke(0x0102, 0x00);
        Cpu cpu(bus);
        cpu.regs.pc = 0x0100;
        cpu.step();

        if (isIllegal(opcode)) {
            CHECK(cpu.state() == Cpu::State::Locked);
        } else if (opcode == 0x76) {
            CHECK(cpu.state() == Cpu::State::Halted);
        } else if (opcode == 0x10) {
            CHECK(cpu.state() == Cpu::State::Stopped);
        } else {
            CHECK(cpu.state() == Cpu::State::Running);
            CHECK_FALSE(cpu.unimplemented());
        }
    }
}

TEST_CASE("every CB opcode dispatches to Running and implemented") {
    for (int op = 0x00; op <= 0xFF; ++op) {
        const u8 opcode = static_cast<u8>(op);
        CAPTURE(op);

        RecordingBus bus;
        bus.poke(0x0100, 0xCB);
        bus.poke(0x0101, opcode);
        Cpu cpu(bus);
        cpu.regs.pc = 0x0100;
        cpu.step();

        CHECK(cpu.state() == Cpu::State::Running);
        CHECK_FALSE(cpu.unimplemented());
    }
}
