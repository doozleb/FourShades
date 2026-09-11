#include "roms/Detectors.h"

#include <algorithm>

namespace roms {

std::string printable(const std::vector<u8>& bytes, std::size_t limit) {
    std::string out;
    for (const u8 b : bytes) {
        if (out.size() >= limit) break;
        out.push_back((b >= 0x20 && b < 0x7F) || b == '\n' ? static_cast<char>(b) : '?');
    }
    return out;
}

Verdict serialVerdict(std::string_view serialText) {
    if (serialText.find("Failed") != std::string_view::npos) return Verdict::Fail;
    if (serialText.find("Passed") != std::string_view::npos) return Verdict::Pass;
    return Verdict::Running;
}

Verdict blarggMemoryVerdict(const std::function<u8(u16)>& peek, bool& sawRunning, std::string* text) {
    if (peek(0xA001) != 0xDE || peek(0xA002) != 0xB0 || peek(0xA003) != 0x61) {
        return Verdict::Running;
    }
    const u8 status = peek(0xA000);
    if (status == 0x80) {
        sawRunning = true;
        return Verdict::Running;
    }
    if (!sawRunning) {
        // Signature present but we have never seen the shell actually mark
        // itself running: this is cartridge RAM's zeroed initial state (or a
        // stale signature from before the test started), not a real result.
        return Verdict::Running;
    }
    if (text != nullptr) {
        std::vector<u8> raw;
        for (u16 a = 0xA004; a <= 0xBFFF; ++a) {
            const u8 c = peek(a);
            if (c == 0) break;
            raw.push_back(c);
        }
        *text = printable(raw, raw.size());
    }
    return status == 0x00 ? Verdict::Pass : Verdict::Fail;
}

Verdict mooneyeVerdict(const fourshades::Registers& r, u8 nextOpcode) {
    if (nextOpcode != 0x40) return Verdict::Running;
    if (r.b == 3 && r.c == 5 && r.d == 8 && r.e == 13 && r.h == 21 && r.l == 34) return Verdict::Pass;
    if (r.b == 0x42 && r.c == 0x42 && r.d == 0x42 && r.e == 0x42 && r.h == 0x42 && r.l == 0x42) {
        return Verdict::Fail;
    }
    return Verdict::Running;
}

Verdict mooneyeSerialVerdict(const std::vector<u8>& serial) {
    static const std::vector<u8> kPass = {3, 5, 8, 13, 21, 34};
    static const std::vector<u8> kFail = {0x42, 0x42, 0x42, 0x42, 0x42, 0x42};
    const auto contains = [&](const std::vector<u8>& sequence) {
        return std::search(serial.begin(), serial.end(), sequence.begin(), sequence.end()) != serial.end();
    };
    if (contains(kPass)) return Verdict::Pass;
    if (contains(kFail)) return Verdict::Fail;
    return Verdict::Running;
}

} // namespace roms
