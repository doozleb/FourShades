#include <doctest/doctest.h>

#include "core/Cpu.h"
#include "sst/RecordingBus.h"

#include <initializer_list>
#include <vector>

using namespace fourshades;
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

TEST_CASE("every opcode fetch reports the PC the 16-bit unit stepped") {
    RecordingBus bus;
    load(bus, {0x00, 0x00});
    Cpu cpu = makeCpu(bus);
    cpu.step();
    cpu.step();
    CHECK(bus.iduAddresses() == std::vector<u16>{0x0100, 0x0101});
}

TEST_CASE("INC rr and DEC rr report the value before the step") {
    RecordingBus bus;
    load(bus, {0x23, 0x2B}); // INC HL ; DEC HL
    Cpu cpu = makeCpu(bus);
    cpu.regs.setHl(0xFE10);
    cpu.step();
    cpu.step();
    CHECK(cpu.regs.hl() == 0xFE10);
    // two fetches, then the two stepped values
    CHECK(bus.iduAddresses() == std::vector<u16>{0x0100, 0xFE10, 0x0101, 0xFE11});
}

TEST_CASE("LD A,(HL+) reports HL in the same M-cycle as the read") {
    RecordingBus bus;
    load(bus, {0x2A}); // LD A,(HL+)
    Cpu cpu = makeCpu(bus);
    cpu.regs.setHl(0xFE20);
    cpu.step();
    CHECK(bus.iduAddresses() == std::vector<u16>{0x0100, 0xFE20});
    CHECK(bus.log().size() == 2); // still two M-cycles: fetch and read
}

TEST_CASE("PUSH reports the stack pointer before each decrement") {
    RecordingBus bus;
    load(bus, {0xC5}); // PUSH BC
    Cpu cpu = makeCpu(bus);
    cpu.step();
    CHECK(bus.iduAddresses() == std::vector<u16>{0x0100, 0xFFFE, 0xFFFD});
    CHECK(bus.log().size() == 4); // fetch, idle, write, write
}

TEST_CASE("POP reports the stack pointer before each increment") {
    RecordingBus bus;
    load(bus, {0xC1}); // POP BC
    Cpu cpu = makeCpu(bus);
    cpu.step();
    CHECK(bus.iduAddresses() == std::vector<u16>{0x0100, 0xFFFE, 0xFFFF});
    CHECK(bus.log().size() == 3); // fetch and two reads
}

TEST_CASE("interrupt dispatch reports the fetch and both stack decrements") {
    RecordingBus bus;
    load(bus, {0x00}); // NOP: fetched then dropped by the dispatch
    Cpu cpu = makeCpu(bus);
    cpu.regs.sp = 0xFE20;
    cpu.ime = true;
    bus.setPendingInterrupts(0x04); // timer
    cpu.step();
    CHECK(cpu.regs.pc == 0x0050);
    CHECK(bus.iduAddresses() == std::vector<u16>{0x0100, 0xFE20, 0xFE1F});
    // Same 5 logged M-cycles as before iduCycle existed: the hook is free.
    CHECK(bus.log().size() == 5);
}

TEST_CASE("a CB-prefixed instruction reports the prefix fetch only") {
    // The prefix is an opcode fetch, so step() reports the PC the 16-bit
    // unit stepped. The CB byte itself is read by fetch8(), which reports no
    // IDU write - the same gap every operand byte has, recorded in
    // docs/known-divergences.md, "Still unimplemented: the PC increment on
    // an operand byte, the CB byte included". Closing it means changing
    // fetch8() for every operand, not carving out a special case here.
    RecordingBus bus;
    load(bus, {0xCB, 0x30}); // SWAP B
    Cpu cpu = makeCpu(bus);
    cpu.regs.b = 0x12;
    cpu.step();
    CHECK(cpu.regs.b == 0x21); // nibbles swapped: confirms the CB byte ran
    CHECK(bus.iduAddresses() == std::vector<u16>{0x0100});
    CHECK(bus.log().size() == 2); // two M-cycles: the prefix and the CB byte
}

TEST_CASE("a CB prefix at the top of echo RAM does not corrupt OAM") {
    // The deleted call reported regs.pc, which step() has already advanced
    // past the prefix: for a prefix at $FDFF that is $FE00, inside OAM, on
    // an M-cycle whose own address is outside it. Every IDU report here
    // names the address the unit stepped from, so this instruction's single
    // report is $FDFF and nothing inside OAM is named at all.
    RecordingBus bus;
    bus.poke(0xFDFF, 0xCB);
    bus.poke(0xFE00, 0x30); // SWAP B
    Cpu cpu = makeCpu(bus);
    cpu.regs.pc = 0xFDFF;
    cpu.regs.b = 0x12;
    cpu.step();
    CHECK(cpu.regs.b == 0x21);
    CHECK(bus.iduAddresses() == std::vector<u16>{0xFDFF});
}
