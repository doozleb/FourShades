#include <doctest/doctest.h>

#include "core/Cpu.h"
#include "sst/RecordingBus.h"

#include <initializer_list>

using namespace fourshades;
using sst::CycleKind;
using sst::RecordingBus;

namespace {
// Places `program` at 0x0100, where each test points PC.
void load(RecordingBus& bus, std::initializer_list<u8> program) {
    u16 address = 0x0100;
    for (const u8 byte : program) {
        bus.poke(address++, byte);
    }
}
} // namespace

TEST_CASE("NOP is one read cycle and advances PC") {
    RecordingBus bus;
    load(bus, {0x00});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.step();
    CHECK(cpu.regs.pc == 0x0101);
    REQUIRE(bus.log().size() == 1);
    CHECK(bus.log()[0].kind == CycleKind::Read);
    CHECK(bus.log()[0].address == 0x0100);
}

TEST_CASE("EI enables interrupts only after the next instruction") {
    RecordingBus bus;
    load(bus, {0xFB, 0x00});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.step();
    CHECK_FALSE(cpu.ime);
    CHECK(cpu.imePending());
    cpu.step();
    CHECK(cpu.ime);
    CHECK_FALSE(cpu.imePending());
}

TEST_CASE("DI straight after EI leaves interrupts disabled") {
    RecordingBus bus;
    load(bus, {0xFB, 0xF3, 0x00});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.step();
    cpu.step();
    cpu.step();
    CHECK_FALSE(cpu.ime);
    CHECK_FALSE(cpu.imePending());
}

TEST_CASE("DI disables interrupts immediately") {
    RecordingBus bus;
    load(bus, {0xF3});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.ime = true;
    cpu.step();
    CHECK_FALSE(cpu.ime);
}

TEST_CASE("HALT stops fetching; each later step is one idle cycle") {
    RecordingBus bus;
    load(bus, {0x76, 0x00});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.step();
    CHECK(cpu.state() == Cpu::State::Halted);
    CHECK(cpu.regs.pc == 0x0101);
    cpu.step();
    REQUIRE(bus.log().size() == 2);
    CHECK(bus.log()[1].kind == CycleKind::Idle);
    CHECK(cpu.regs.pc == 0x0101);
}

TEST_CASE("an illegal opcode locks the CPU") {
    RecordingBus bus;
    load(bus, {0xD3});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.step();
    CHECK(cpu.state() == Cpu::State::Locked);
    cpu.step();
    CHECK(cpu.regs.pc == 0x0101);
    CHECK(bus.log().back().kind == CycleKind::Idle);
}
