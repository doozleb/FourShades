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
