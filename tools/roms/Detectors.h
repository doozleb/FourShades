#pragma once

#include "core/Registers.h"
#include "core/Types.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace roms {

using fourshades::u16;
using fourshades::u8;

enum class Verdict { Running, Pass, Fail };

// Replaces any byte outside printable ASCII (and '\n') with '?', and stops
// once `limit` output bytes have been produced. Shared by every detector and
// by the runner so text that reaches JSON output is always safe to encode.
std::string printable(const std::vector<u8>& bytes, std::size_t limit);

// Blargg's tests print their result over serial: "Passed" or "Failed".
Verdict serialVerdict(std::string_view serialText);

// Blargg's memory protocol: once A001-A003 hold DE B0 61, A000 is a status
// (0x80 running, 0x00 passed, anything else failed) and A004 holds the result
// text, zero-terminated.
//
// Cartridge RAM starts at all zero bytes, so immediately after the shell
// writes the DE B0 61 signature (before it has written A000 = 0x80 "running"
// at all), A000 still reads 0x00 -- indistinguishable from a genuine pass.
// A final status (Pass/Fail) is therefore only trusted once a *previous*
// call has observed the signature together with A000 == 0x80; `sawRunning`
// carries that fact across calls for one test (start each test with it
// false). Until it is true, every read -- including a 0x00 or a "failing"
// byte -- reports Running.
//
// When a final status is returned, `text` (if non-null) receives the result
// text at A004: bytes are read until a NUL or address 0xBFFF, whichever
// comes first, then sanitized through `printable()`.
Verdict blarggMemoryVerdict(const std::function<u8(u16)>& peek, bool& sawRunning, std::string* text);

// Mooneye's tests finish by executing LD B,B (0x40) with B,C,D,E,H,L holding
// 3,5,8,13,21,34 for a pass. Failure has been signaled two different ways
// across Mooneye builds: some set all six of those registers to 0x42 before
// LD B,B (checked here); others send the failure bytes over serial instead
// and leave the registers untouched (see `mooneyeSerialVerdict`) -- a build
// using the serial form will never satisfy this function's Fail branch, only
// Running, so both detectors must be checked.
Verdict mooneyeVerdict(const fourshades::Registers& regs, u8 nextOpcode);

// Mooneye's serial-based signal, used by builds that report over the link
// port instead of (or as well as) registers: Pass once the serial byte
// stream contains the Fibonacci sequence 03 05 08 0D 15 22 in order, Fail
// once it contains six 0x42 bytes in a row, else Running.
Verdict mooneyeSerialVerdict(const std::vector<u8>& serial);

} // namespace roms
