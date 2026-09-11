#include "roms/RomRun.h"

#include "core/Cartridge.h"
#include "core/GameBoy.h"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace roms {

namespace {

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
    bool sawBlarggRunning = false;       // per-test state for blarggMemoryVerdict
    std::size_t lastMooneyeSerialSize = 0;
    bool memoryEvidence = false;         // a Blargg verdict came from the memory protocol
    std::string memoryText;              // ...and this is its (already sanitized) text

    while (gb->cycles() < limit) {
        const fourshades::Cpu& cpu = gb->cpu();
        if (cpu.state() == fourshades::Cpu::State::Locked) {
            out.reason = "CPU locked on an illegal opcode";
            break;
        }
        if (test.method == Method::Mooneye && cpu.state() == fourshades::Cpu::State::Running) {
            Verdict decided = mooneyeVerdict(cpu.regs, gb->peek(cpu.regs.pc));
            std::string reason;
            if (decided == Verdict::Pass) {
                reason = "Fibonacci registers at LD B,B";
            } else if (decided == Verdict::Fail) {
                reason = "registers all 0x42";
            } else if (gb->serialOutput().size() != lastMooneyeSerialSize) {
                // Cheap: only re-scan the serial stream when it has grown.
                lastMooneyeSerialSize = gb->serialOutput().size();
                const Verdict serial = mooneyeSerialVerdict(gb->serialOutput());
                if (serial == Verdict::Pass) {
                    decided = Verdict::Pass;
                    reason = "Fibonacci bytes over serial";
                } else if (serial == Verdict::Fail) {
                    decided = Verdict::Fail;
                    reason = "failure bytes (0x42) over serial";
                }
            }
            if (decided != Verdict::Running) {
                out.status = decided;
                out.reason = reason;
                break;
            }
        }
        if (test.method == Method::Blargg && gb->cycles() >= nextBlarggCheck) {
            nextBlarggCheck = gb->cycles() + 8192;
            std::string text;
            Verdict v = blarggMemoryVerdict(peek, sawBlarggRunning, &text);
            const bool fromMemory = v != Verdict::Running;
            if (v == Verdict::Running) {
                text = printable(gb->serialOutput(), 1 << 20);
                v = serialVerdict(text);
            }
            if (v != Verdict::Running) {
                out.status = v;
                out.reason = v == Verdict::Pass ? "Passed" : "Failed: " + lastLine(text).substr(0, 200);
                if (fromMemory) {
                    memoryEvidence = true;
                    memoryText = text;
                }
                break;
            }
        }
        gb->step();
    }
    out.emulatedSeconds = static_cast<double>(gb->cycles()) / static_cast<double>(kCyclesPerSecond);
    out.serial = printable(gb->serialOutput(), 2048);
    if (memoryEvidence && out.serial.empty()) {
        // The result came from cartridge RAM, not the link port: carry the
        // author's own text as evidence instead of leaving `serial` empty.
        out.serial = memoryText.substr(0, std::min<std::size_t>(memoryText.size(), 2048));
    }
    if (out.reason.empty()) {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "timeout after %.1f s", out.emulatedSeconds);
        out.reason = buffer;
    }
    return out;
}

} // namespace roms
