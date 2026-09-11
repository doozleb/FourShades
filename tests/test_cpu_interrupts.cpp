#include <doctest/doctest.h>

#include "core/Cpu.h"
#include "sst/RecordingBus.h"

#include <initializer_list>

using namespace fourshades;
using sst::Cycle;
using sst::CycleKind;
using sst::RecordingBus;

namespace {
void load(RecordingBus& bus, std::initializer_list<u8> program) {
    u16 address = 0x0100;
    for (const u8 byte : program) {
        bus.poke(address++, byte);
    }
}

Cpu makeCpu(RecordingBus& bus) {
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.regs.sp = 0xFFFE;
    return cpu;
}
} // namespace

TEST_CASE("an enabled interrupt is dispatched in 5 M-cycles") {
    RecordingBus bus;
    load(bus, {0x00});
    Cpu cpu = makeCpu(bus);
    cpu.ime = true;
    bus.setPendingInterrupts(0x04); // timer
    cpu.step();
    CHECK(cpu.regs.pc == 0x0050);
    CHECK(cpu.regs.sp == 0xFFFC);
    CHECK(bus.peek(0xFFFD) == 0x01); // return address high byte
    CHECK(bus.peek(0xFFFC) == 0x00); // low byte
    CHECK_FALSE(cpu.ime);
    CHECK(bus.pendingInterrupts() == 0x00); // acknowledged
    REQUIRE(bus.log().size() == 5);
    CHECK(bus.log()[0].kind == CycleKind::Idle);
    CHECK(bus.log()[1].kind == CycleKind::Idle);
    CHECK(bus.log()[2] == Cycle{0xFFFD, 0x01, CycleKind::Write});
    CHECK(bus.log()[3] == Cycle{0xFFFC, 0x00, CycleKind::Write});
    CHECK(bus.log()[4].kind == CycleKind::Idle);
}

TEST_CASE("the lowest pending bit is serviced first") {
    RecordingBus bus;
    load(bus, {0x00});
    Cpu cpu = makeCpu(bus);
    cpu.ime = true;
    bus.setPendingInterrupts(0x06); // LCD and timer
    cpu.step();
    CHECK(cpu.regs.pc == 0x0048);
    CHECK(bus.pendingInterrupts() == 0x04);
}

TEST_CASE("nothing is dispatched while IME is off") {
    RecordingBus bus;
    load(bus, {0x00});
    Cpu cpu = makeCpu(bus);
    bus.setPendingInterrupts(0x01);
    cpu.step();
    CHECK(cpu.regs.pc == 0x0101);
    CHECK(bus.pendingInterrupts() == 0x01);
}

TEST_CASE("EI lets the instruction after it run before an interrupt") {
    RecordingBus bus;
    load(bus, {0xFB, 0x00, 0x00}); // EI, NOP, NOP
    Cpu cpu = makeCpu(bus);
    bus.setPendingInterrupts(0x01);
    cpu.step(); // EI
    CHECK(cpu.regs.pc == 0x0101);
    cpu.step(); // NOP runs; IME turns on at its end
    CHECK(cpu.regs.pc == 0x0102);
    cpu.step(); // now the interrupt
    CHECK(cpu.regs.pc == 0x0040);
    CHECK(bus.peek(0xFFFD) == 0x01);
    CHECK(bus.peek(0xFFFC) == 0x02);
}

TEST_CASE("HALT wakes on a pending interrupt even with IME off") {
    RecordingBus bus;
    load(bus, {0x76, 0x00}); // HALT, NOP
    Cpu cpu = makeCpu(bus);
    cpu.step();
    CHECK(cpu.state() == Cpu::State::Halted);
    cpu.step();
    CHECK(cpu.state() == Cpu::State::Halted);
    bus.setPendingInterrupts(0x04);
    cpu.step(); // wakes, and with IME off simply runs the NOP
    CHECK(cpu.state() == Cpu::State::Running);
    CHECK(cpu.regs.pc == 0x0102);
    CHECK(bus.pendingInterrupts() == 0x04); // not serviced, so not acknowledged
}

TEST_CASE("the HALT bug reads the next byte twice") {
    RecordingBus bus;
    load(bus, {0x76, 0x3C, 0x00}); // HALT, INC A, NOP
    Cpu cpu = makeCpu(bus);
    bus.setPendingInterrupts(0x04); // pending before HALT, IME off
    cpu.step();
    CHECK(cpu.state() == Cpu::State::Running); // HALT didn't halt
    CHECK(cpu.regs.pc == 0x0101);
    cpu.step(); // INC A, but PC doesn't advance past it
    CHECK(cpu.regs.a == 1);
    CHECK(cpu.regs.pc == 0x0101);
    cpu.step(); // INC A again, normally this time
    CHECK(cpu.regs.a == 2);
    CHECK(cpu.regs.pc == 0x0102);
}

TEST_CASE("EI then a bugged HALT returns to the HALT") {
    RecordingBus bus;
    load(bus, {0xFB, 0x76, 0x00}); // EI, HALT, NOP
    Cpu cpu = makeCpu(bus);
    bus.setPendingInterrupts(0x01);
    cpu.step(); // EI
    cpu.step(); // HALT with IME still 0: halt bug; IME turns on after it
    cpu.step(); // dispatch
    CHECK(cpu.regs.pc == 0x0040);
    CHECK(bus.peek(0xFFFD) == 0x01);
    CHECK(bus.peek(0xFFFC) == 0x01); // returns to the HALT at 0x0101
}
