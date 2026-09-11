#include "sst/SstCompare.h"

#include <cstdio>

namespace sst {

namespace {

std::string hex(unsigned value, int digits) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%0*X", digits, value);
    return buffer;
}

const char* kindName(CycleKind kind) {
    switch (kind) {
    case CycleKind::Read: return "r-m";
    case CycleKind::Write: return "-wm";
    default: return "---";
    }
}

} // namespace

std::optional<Mismatch> compareResult(const SstTest& test, const fourshades::Cpu& cpu, const RecordingBus& bus) {
    const CpuSnapshot& want = test.final;
    const fourshades::Registers& r = cpu.regs;

    const struct {
        const char* name;
        unsigned expected;
        unsigned actual;
        int digits;
    } registers[] = {
        {"a", want.a, r.a, 2},   {"b", want.b, r.b, 2},   {"c", want.c, r.c, 2},
        {"d", want.d, r.d, 2},   {"e", want.e, r.e, 2},   {"f", want.f, r.f(), 2},
        {"h", want.h, r.h, 2},   {"l", want.l, r.l, 2},   {"sp", want.sp, r.sp, 4},
        {"pc", want.pc, r.pc, 4},
    };
    for (const auto& reg : registers) {
        if (reg.expected != reg.actual) {
            return Mismatch{reg.name, hex(reg.expected, reg.digits), hex(reg.actual, reg.digits)};
        }
    }
    if (want.ime != cpu.ime) {
        return Mismatch{"ime", want.ime ? "1" : "0", cpu.ime ? "1" : "0"};
    }
    const bool pending = want.imePending.value_or(false);
    if (pending != cpu.imePending()) {
        return Mismatch{"ei", pending ? "1" : "0", cpu.imePending() ? "1" : "0"};
    }

    for (const auto& [address, value] : want.ram) {
        if (bus.peek(address) != value) {
            return Mismatch{"ram[" + hex(address, 4) + "]", hex(value, 2), hex(bus.peek(address), 2)};
        }
    }

    const auto& got = bus.log();
    if (got.size() != test.cycles.size()) {
        return Mismatch{"cycle count", std::to_string(test.cycles.size()), std::to_string(got.size())};
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        const Cycle& expected = test.cycles[i];
        const Cycle& actual = got[i];
        if (expected.kind != actual.kind) {
            return Mismatch{"cycle kind", kindName(expected.kind), kindName(actual.kind), static_cast<int>(i)};
        }
        if (expected.kind != CycleKind::Idle &&
            (expected.address != actual.address || expected.value != actual.value)) {
            return Mismatch{"cycle bus", hex(expected.address, 4) + "=" + hex(expected.value, 2),
                            hex(actual.address, 4) + "=" + hex(actual.value, 2), static_cast<int>(i)};
        }
    }
    return std::nullopt;
}

} // namespace sst
