#include <doctest/doctest.h>

#include "core/Cpu.h"
#include "sst/RecordingBus.h"

#include <array>
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
    // The first wait M-cycle is the opcode fetch the dispatch replaces.
    CHECK(bus.log()[0] == Cycle{0x0100, 0x00, CycleKind::Read});
    CHECK(bus.log()[1].kind == CycleKind::Idle);
    CHECK(bus.log()[2] == Cycle{0xFFFD, 0x01, CycleKind::Write});
    CHECK(bus.log()[3] == Cycle{0xFFFC, 0x00, CycleKind::Write});
    CHECK(bus.log()[4].kind == CycleKind::Idle);
}

namespace {
// A bus whose interrupt line rises during a chosen M-cycle (1-based), the way
// the timer's reload cycle raises IF at the start of an M-cycle.
class RaisingBus final : public Bus {
public:
    RaisingBus(int raiseCycle, u8 bits) : raiseCycle_(raiseCycle), bits_(bits) {}
    u8 read(u16 address) override { cycle(); return memory[address]; }
    void write(u16 address, u8 value) override { cycle(); memory[address] = value; }
    void idle() override { cycle(); }
    std::optional<u8> haltedCycle(u16 address) override {
        cycle();
        if (pending_ == 0) {
            return std::nullopt;
        }
        return memory[address];
    }
    u8 pendingInterrupts() override { return pending_; }
    void acknowledgeInterrupt(int bit) override { pending_ = static_cast<u8>(pending_ & ~(1 << bit)); }
    std::array<u8, 0x10000> memory{};
    int cycles = 0;

private:
    void cycle() {
        if (++cycles == raiseCycle_) {
            pending_ = static_cast<u8>(pending_ | bits_);
        }
    }
    int raiseCycle_;
    u8 bits_;
    u8 pending_ = 0;
};
} // namespace

// The CPU decides whether to dispatch at the end of the opcode-fetch M-cycle,
// so a request raised during that fetch replaces the fetched instruction; the
// fetch counts as the first of the dispatch's five M-cycles.
TEST_CASE("an interrupt raised during an opcode fetch is dispatched instead of that opcode") {
    RaisingBus bus(1, 0x04); // timer, raised in the first M-cycle: the fetch of INC A
    bus.memory[0x0100] = 0x3C; // INC A
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.regs.sp = 0xFFFE;
    cpu.ime = true;
    cpu.step();
    CHECK(cpu.regs.a == 0x00); // INC A never ran
    CHECK(cpu.regs.pc == 0x0050);
    CHECK(bus.memory[0xFFFD] == 0x01);
    CHECK(bus.memory[0xFFFC] == 0x00); // returns to the INC A
    CHECK(bus.cycles == 5);
}

TEST_CASE("an interrupt raised after the fetch waits for the next instruction") {
    RaisingBus bus(2, 0x04); // raised in INC BC's internal M-cycle
    bus.memory[0x0100] = 0x03; // INC BC (2 M-cycles)
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.regs.sp = 0xFFFE;
    cpu.ime = true;
    cpu.step();
    CHECK(cpu.regs.bc() == 0x0001);
    CHECK(cpu.regs.pc == 0x0101);
    cpu.step();
    CHECK(cpu.regs.pc == 0x0050);
    CHECK(bus.memory[0xFFFC] == 0x01); // returns to the byte after INC BC
    CHECK(bus.cycles == 2 + 5);
}

// A halted CPU samples at the same point of each M-cycle as a running one, so
// HALT services an interrupt with the same latency as a run of NOPs would.
TEST_CASE("a halted CPU dispatches an interrupt raised during a halted M-cycle at once") {
    RaisingBus bus(3, 0x01); // raised in the second halted M-cycle
    bus.memory[0x0100] = 0x76; // HALT
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.regs.sp = 0xFFFE;
    cpu.ime = true;
    cpu.step(); // HALT: M-cycle 1
    cpu.step(); // halted: M-cycle 2
    CHECK(cpu.state() == Cpu::State::Halted);
    cpu.step(); // M-cycle 3 sees the request; it is the dispatch's first M-cycle
    CHECK(cpu.regs.pc == 0x0040);
    CHECK(bus.memory[0xFFFC] == 0x01); // returns to the byte after HALT
    CHECK(bus.cycles == 2 + 5);
}

TEST_CASE("with IME off, the M-cycle that wakes a halted CPU fetches the next opcode") {
    RaisingBus bus(3, 0x01);
    bus.memory[0x0100] = 0x76; // HALT
    bus.memory[0x0101] = 0x3C; // INC A
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.regs.sp = 0xFFFE;
    cpu.step(); // HALT
    cpu.step(); // halted
    cpu.step(); // wakes in M-cycle 3 and runs INC A, fetched in that M-cycle
    CHECK(cpu.state() == Cpu::State::Running);
    CHECK(cpu.regs.a == 0x01);
    CHECK(cpu.regs.pc == 0x0102);
    CHECK(bus.cycles == 3);
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
