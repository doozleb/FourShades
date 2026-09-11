#include "roms/RomRun.h"

#include "core/Cartridge.h"
#include "core/GameBoy.h"

#include <cstdio>
#include <memory>

namespace roms {

namespace {

std::string printable(const std::vector<u8>& bytes, std::size_t limit) {
    std::string out;
    for (const u8 b : bytes) {
        if (out.size() >= limit) break;
        out.push_back((b >= 0x20 && b < 0x7F) || b == '\n' ? static_cast<char>(b) : '?');
    }
    return out;
}

std::string lastLine(const std::string& text) {
    std::string trimmed = text;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == ' ')) trimmed.pop_back();
    const std::size_t cut = trimmed.find_last_of('\n');
    return cut == std::string::npos ? trimmed : trimmed.substr(cut + 1);
}

} // namespace

RomOutcome runRomTest(const RomTest& test, std::vector<u8> romImage) {
    RomOutcome out;
    if (test.method == Method::Screenshot) {
        out.reason = "needs the PPU (piece 3)";
        return out;
    }
    std::string error;
    auto cart = fourshades::Cartridge::load(std::move(romImage), &error);
    if (!cart) {
        out.reason = "cartridge: " + error;
        return out;
    }
    auto gb = std::make_unique<fourshades::GameBoy>(std::move(*cart));
    const auto limit = static_cast<std::uint64_t>(test.limitSeconds * static_cast<double>(kCyclesPerSecond));
    const auto peek = [&gb](u16 address) { return gb->peek(address); };
    std::uint64_t nextBlarggCheck = 0;

    while (gb->cycles() < limit) {
        const fourshades::Cpu& cpu = gb->cpu();
        if (cpu.state() == fourshades::Cpu::State::Locked) {
            out.reason = "CPU locked on an illegal opcode";
            break;
        }
        if (test.method == Method::Mooneye && cpu.state() == fourshades::Cpu::State::Running) {
            const Verdict v = mooneyeVerdict(cpu.regs, gb->peek(cpu.regs.pc));
            if (v != Verdict::Running) {
                out.status = v;
                out.reason = v == Verdict::Pass ? "Fibonacci registers at LD B,B" : "registers all 0x42";
                break;
            }
        }
        if (test.method == Method::Blargg && gb->cycles() >= nextBlarggCheck) {
            nextBlarggCheck = gb->cycles() + 8192;
            std::string text;
            Verdict v = blarggMemoryVerdict(peek, &text);
            if (v == Verdict::Running) {
                text = printable(gb->serialOutput(), 1 << 20);
                v = serialVerdict(text);
            }
            if (v != Verdict::Running) {
                out.status = v;
                out.reason = v == Verdict::Pass ? "Passed" : "Failed: " + lastLine(text).substr(0, 200);
                break;
            }
        }
        gb->step();
    }
    out.emulatedSeconds = static_cast<double>(gb->cycles()) / static_cast<double>(kCyclesPerSecond);
    out.serial = printable(gb->serialOutput(), 2048);
    if (out.reason.empty()) {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "timeout after %.1f s", out.emulatedSeconds);
        out.reason = buffer;
    }
    return out;
}

} // namespace roms
