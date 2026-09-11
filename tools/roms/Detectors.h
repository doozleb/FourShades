#pragma once

#include "core/Registers.h"
#include "core/Types.h"

#include <functional>
#include <string>
#include <string_view>

namespace roms {

using fourshades::u16;
using fourshades::u8;

enum class Verdict { Running, Pass, Fail };

// Blargg's tests print their result over serial: "Passed" or "Failed".
Verdict serialVerdict(std::string_view serialText);

// Blargg's memory protocol: once A001-A003 hold DE B0 61, A000 is a status
// (0x80 running, 0x00 passed, anything else failed) and A004 holds the result
// text, zero-terminated. `text` receives that text.
Verdict blarggMemoryVerdict(const std::function<u8(u16)>& peek, std::string* text);

// Mooneye's tests finish by executing LD B,B (0x40) with B,C,D,E,H,L holding
// 3,5,8,13,21,34 for a pass, or 0x42 in all six for a failure.
Verdict mooneyeVerdict(const fourshades::Registers& regs, u8 nextOpcode);

} // namespace roms
