#include "sst/SstRun.h"

#include "core/Cpu.h"

namespace sst {

TestOutcome runTest(const SstTest& test, RecordingBus& bus) {
    bus.reset();
    for (const auto& [address, value] : test.initial.ram) {
        bus.poke(address, value);
    }

    fourshades::Cpu cpu(bus);
    fourshades::Registers& r = cpu.regs;
    r.a = test.initial.a;
    r.b = test.initial.b;
    r.c = test.initial.c;
    r.d = test.initial.d;
    r.e = test.initial.e;
    r.setF(test.initial.f);
    r.h = test.initial.h;
    r.l = test.initial.l;
    r.sp = test.initial.sp;
    r.pc = test.initial.pc;
    cpu.ime = test.initial.ime;

    cpu.step();
    // A halted or stopped CPU spends every further M-cycle idle. The HALT and
    // STOP tests record a fixed window of cycles, so keep stepping until the
    // window is full. For every other instruction the CPU is still Running
    // and this loop doesn't run.
    while (cpu.state() != fourshades::Cpu::State::Running && bus.log().size() < test.cycles.size()) {
        cpu.step();
    }

    if (cpu.unimplemented()) {
        return {Status::Unimplemented, std::nullopt};
    }
    if (auto mismatch = compareResult(test, cpu, bus)) {
        return {Status::Fail, std::move(mismatch)};
    }
    return {Status::Pass, std::nullopt};
}

} // namespace sst
