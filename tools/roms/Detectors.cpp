#include "roms/Detectors.h"

namespace roms {

Verdict serialVerdict(std::string_view serialText) {
    if (serialText.find("Failed") != std::string_view::npos) return Verdict::Fail;
    if (serialText.find("Passed") != std::string_view::npos) return Verdict::Pass;
    return Verdict::Running;
}

Verdict blarggMemoryVerdict(const std::function<u8(u16)>& peek, std::string* text) {
    if (peek(0xA001) != 0xDE || peek(0xA002) != 0xB0 || peek(0xA003) != 0x61) {
        return Verdict::Running;
    }
    const u8 status = peek(0xA000);
    if (status == 0x80) {
        return Verdict::Running;
    }
    if (text != nullptr) {
        text->clear();
        for (u16 a = 0xA004; a < 0xA004 + 512; ++a) {
            const u8 c = peek(a);
            if (c == 0) break;
            text->push_back(static_cast<char>(c));
        }
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

} // namespace roms
