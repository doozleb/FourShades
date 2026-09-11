#include <doctest/doctest.h>

#include "roms/Detectors.h"
#include "roms/RomRun.h"
#include "roms/RomTests.h"

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fourshades;

namespace {
std::vector<u8> romWith(const std::vector<u8>& program) {
    std::vector<u8> rom(0x8000, 0x00);
    for (std::size_t i = 0; i < program.size(); ++i) {
        rom[0x0100 + i] = program[i];
    }
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    rom[0x014D] = sum;
    return rom;
}

roms::RomTest testOf(roms::Method method, double limit = 1.0) {
    roms::RomTest t;
    t.name = "synthetic";
    t.rom = "synthetic.gb";
    t.group = "cpu & interrupts";
    t.method = method;
    t.runtime = 0.5;
    t.limitSeconds = limit;
    return t;
}

// LD B,3; LD C,5; LD D,8; LD E,13; LD H,21; LD L,<last>; LD B,B; JR -2
std::vector<u8> fibonacciProgram(u8 last) {
    return {0x06, 3, 0x0E, 5, 0x16, 8, 0x1E, 13, 0x26, 21, 0x2E, last, 0x40, 0x18, 0xFE};
}
} // namespace

namespace {
const std::string kOneTest =
    R"({"shootout_commit":"abc","tests":[{"name":"t","rom":"mooneye/t.gb","group":"timer",)"
    R"("method":"mooneye","informational":false,"runtime":1.5,"limit_seconds":6.5,"references":["a/t.png"]}]})";

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}
} // namespace

TEST_CASE("parseTestList reads entries and rejects unknown methods") {
    const roms::TestList list = roms::parseTestList(kOneTest);
    CHECK(list.shootoutCommit == "abc");
    REQUIRE(list.tests.size() == 1);
    CHECK(list.tests[0].method == roms::Method::Mooneye);
    CHECK(list.tests[0].limitSeconds == 6.5);
    CHECK(list.tests[0].references.size() == 1);
    CHECK_FALSE(list.tests[0].informational);
    CHECK_THROWS_AS(roms::parseTestList(replaced(kOneTest, "\"mooneye\",", "\"guess\",")), std::runtime_error);
}

TEST_CASE("parseTestList requires a boolean 'informational' on every entry") {
    const std::string informational = replaced(
        replaced(replaced(kOneTest, "\"informational\":false", "\"informational\":true"),
                 "\"method\":\"mooneye\"", "\"method\":\"screenshot\""),
        "mooneye/t.gb", "daid/t.gb");
    CHECK(roms::parseTestList(informational).tests[0].informational);
    CHECK_THROWS_AS(roms::parseTestList(replaced(kOneTest, "\"informational\":false,", "")), std::runtime_error);
    CHECK_THROWS_AS(roms::parseTestList(replaced(kOneTest, "\"informational\":false", "\"informational\":0")),
                    std::runtime_error);
}

TEST_CASE("parseTestList rejects a time limit that isn't max(2 x runtime, runtime + 5)") {
    CHECK_THROWS_AS(roms::parseTestList(replaced(kOneTest, "\"limit_seconds\":6.5", "\"limit_seconds\":7.0")),
                    std::runtime_error);
    CHECK_THROWS_AS(roms::parseTestList(replaced(kOneTest, "\"limit_seconds\":6.5", "\"limit_seconds\":6.4")),
                    std::runtime_error);
    // runtime 10 -> 2 x runtime wins.
    const std::string longer =
        replaced(replaced(kOneTest, "\"runtime\":1.5", "\"runtime\":10"), "\"limit_seconds\":6.5", "\"limit_seconds\":20");
    CHECK(roms::parseTestList(longer).tests[0].limitSeconds == 20.0);
    CHECK_THROWS_AS(roms::parseTestList(replaced(longer, "\"limit_seconds\":20", "\"limit_seconds\":15")),
                    std::runtime_error);
    // Floating-point rounding (0.1 + 5 written as 5.1) is not a mismatch.
    CHECK_NOTHROW(roms::parseTestList(
        replaced(replaced(kOneTest, "\"runtime\":1.5", "\"runtime\":0.1"), "\"limit_seconds\":6.5", "\"limit_seconds\":5.1")));
}

TEST_CASE("parseTestList rejects a method that doesn't follow from the ROM's path") {
    // blargg/ -> blargg
    CHECK_THROWS_AS(roms::parseTestList(replaced(kOneTest, "mooneye/t.gb", "blargg/t.gb")), std::runtime_error);
    CHECK_NOTHROW(roms::parseTestList(
        replaced(replaced(kOneTest, "mooneye/t.gb", "blargg/t.gb"), "\"method\":\"mooneye\"", "\"method\":\"blargg\"")));
    // mooneye/manual-only/ -> screenshot, not mooneye
    CHECK_THROWS_AS(roms::parseTestList(replaced(kOneTest, "mooneye/t.gb", "mooneye/manual-only/t.gb")),
                    std::runtime_error);
    CHECK_NOTHROW(roms::parseTestList(replaced(replaced(kOneTest, "mooneye/t.gb", "mooneye/manual-only/t.gb"),
                                               "\"method\":\"mooneye\"", "\"method\":\"screenshot\"")));
    // anything else -> screenshot
    CHECK_THROWS_AS(roms::parseTestList(replaced(kOneTest, "mooneye/t.gb", "daid/t.gb")), std::runtime_error);
    CHECK_THROWS_AS(roms::parseTestList(replaced(kOneTest, "\"method\":\"mooneye\"", "\"method\":\"screenshot\"")),
                    std::runtime_error);
}

TEST_CASE("serial text: Failed beats Passed, and neither means still running") {
    CHECK(roms::serialVerdict("cpu_instrs\n\n01:ok\n\nPassed\n") == roms::Verdict::Pass);
    CHECK(roms::serialVerdict("02-interrupts\n\nFailed #3\n") == roms::Verdict::Fail);
    CHECK(roms::serialVerdict("Passed\nFailed\n") == roms::Verdict::Fail);
    CHECK(roms::serialVerdict("still running") == roms::Verdict::Running);
}

TEST_CASE("Blargg's memory protocol needs the signature AND a prior 'running' status before trusting a final read") {
    std::map<u16, u8> mem;
    const auto peek = [&](u16 a) { return mem.count(a) ? mem[a] : static_cast<u8>(0xFF); };
    std::string text;
    bool sawRunning = false;
    CHECK(roms::blarggMemoryVerdict(peek, sawRunning, &text) == roms::Verdict::Running); // no signature
    CHECK_FALSE(sawRunning);

    mem[0xA001] = 0xDE;
    mem[0xA002] = 0xB0;
    mem[0xA003] = 0x61;
    // Cartridge RAM starts zeroed: signature just landed and A000 already
    // reads 0x00, which looks exactly like a pass. Must not be trusted yet.
    mem[0xA000] = 0x00;
    CHECK(roms::blarggMemoryVerdict(peek, sawRunning, &text) == roms::Verdict::Running);
    CHECK_FALSE(sawRunning);

    mem[0xA000] = 0x80; // now genuinely running
    CHECK(roms::blarggMemoryVerdict(peek, sawRunning, &text) == roms::Verdict::Running);
    CHECK(sawRunning);

    mem[0xA000] = 0x00;
    mem[0xA004] = 'o';
    mem[0xA005] = 'k';
    mem[0xA006] = 0x00;
    CHECK(roms::blarggMemoryVerdict(peek, sawRunning, &text) == roms::Verdict::Pass);
    CHECK(text == "ok");
}

TEST_CASE("Blargg's memory protocol reports Fail once running has genuinely been seen") {
    std::map<u16, u8> mem;
    const auto peek = [&](u16 a) { return mem.count(a) ? mem[a] : static_cast<u8>(0xFF); };
    std::string text;
    bool sawRunning = false;
    mem[0xA001] = 0xDE;
    mem[0xA002] = 0xB0;
    mem[0xA003] = 0x61;
    mem[0xA000] = 0x80;
    CHECK(roms::blarggMemoryVerdict(peek, sawRunning, &text) == roms::Verdict::Running);
    CHECK(sawRunning);
    mem[0xA000] = 0x01;
    CHECK(roms::blarggMemoryVerdict(peek, sawRunning, &text) == roms::Verdict::Fail);
}

TEST_CASE("Blargg's memory protocol text stops at 0xBFFF, the end of cartridge RAM") {
    std::map<u16, u8> mem;
    const auto peek = [&](u16 a) { return mem.count(a) ? mem[a] : static_cast<u8>(0xFF); };
    std::string text;
    bool sawRunning = true;
    mem[0xA001] = 0xDE;
    mem[0xA002] = 0xB0;
    mem[0xA003] = 0x61;
    mem[0xA000] = 0x00;
    for (unsigned a = 0xA004; a <= 0xBFFF; ++a) mem[static_cast<u16>(a)] = 'a'; // no NUL anywhere
    // 0xC000 (WRAM, past the end) reads non-zero too, so only the cap can stop the scan.
    CHECK(roms::blarggMemoryVerdict(peek, sawRunning, &text) == roms::Verdict::Pass);
    CHECK(text.size() == 0xBFFF - 0xA004 + 1);
}

TEST_CASE("Blargg's memory protocol text is NUL-bounded and sanitized") {
    std::map<u16, u8> mem;
    const auto peek = [&](u16 a) { return mem.count(a) ? mem[a] : static_cast<u8>(0xFF); };
    std::string text;
    bool sawRunning = true; // already past "running" for this check
    mem[0xA001] = 0xDE;
    mem[0xA002] = 0xB0;
    mem[0xA003] = 0x61;
    mem[0xA000] = 0x00;
    mem[0xA004] = 0x01; // non-printable control byte: must come through as '?'
    mem[0xA005] = 'k';
    mem[0xA006] = 0x00; // NUL terminates the scan
    mem[0xA007] = 'X';  // must never appear: scan stopped at the NUL above
    CHECK(roms::blarggMemoryVerdict(peek, sawRunning, &text) == roms::Verdict::Pass);
    CHECK(text == "?k");
}

TEST_CASE("Mooneye register verdict fires only on LD B,B with all six registers") {
    Registers r;
    r.b = 3; r.c = 5; r.d = 8; r.e = 13; r.h = 21; r.l = 34;
    CHECK(roms::mooneyeVerdict(r, 0x40) == roms::Verdict::Pass);
    CHECK(roms::mooneyeVerdict(r, 0x00) == roms::Verdict::Running);
    r.l = 35; // five of six
    CHECK(roms::mooneyeVerdict(r, 0x40) == roms::Verdict::Running);
    r.b = r.c = r.d = r.e = r.h = r.l = 0x42;
    CHECK(roms::mooneyeVerdict(r, 0x40) == roms::Verdict::Fail);
}

TEST_CASE("mooneyeSerialVerdict reads the Fibonacci pass/fail byte sequences over serial") {
    CHECK(roms::mooneyeSerialVerdict({}) == roms::Verdict::Running);
    CHECK(roms::mooneyeSerialVerdict({1, 2, 3, 5, 8}) == roms::Verdict::Running); // partial sequence
    CHECK(roms::mooneyeSerialVerdict({0x01, 0x02, 3, 5, 8, 13, 21, 34}) == roms::Verdict::Pass);
    CHECK(roms::mooneyeSerialVerdict({0x42, 0x42, 0x42, 0x42, 0x42, 0x42}) == roms::Verdict::Fail);
}

TEST_CASE("runRomTest passes a program that signals Mooneye success") {
    const auto outcome = roms::runRomTest(testOf(roms::Method::Mooneye), romWith(fibonacciProgram(34)));
    CHECK(outcome.status == roms::Verdict::Pass);
    CHECK(outcome.emulatedSeconds < 0.01);
}

TEST_CASE("runRomTest turns a near-miss into a timeout failure") {
    const auto outcome = roms::runRomTest(testOf(roms::Method::Mooneye, 0.05), romWith(fibonacciProgram(35)));
    CHECK(outcome.status == roms::Verdict::Fail);
    CHECK(outcome.reason.rfind("timeout", 0) == 0);
    CHECK(outcome.emulatedSeconds >= 0.05);
}

namespace {
// LD A,b; LDH (SB),A; LD A,0x81; LDH (SC),A -- one internal-clock serial send.
std::vector<u8> serialSend(u8 b) {
    return {0x3E, b, 0xE0, 0x01, 0x3E, 0x81, 0xE0, 0x02};
}

// Sends `bytes` over serial one at a time, then executes LD B,B with every
// register left at 0 (neither the pass nor the fail register pattern), then
// loops forever. Models a Mooneye build that only signals over serial.
std::vector<u8> serialOnlyProgram(std::initializer_list<u8> bytes) {
    std::vector<u8> program;
    for (const u8 b : bytes) {
        const auto send = serialSend(b);
        program.insert(program.end(), send.begin(), send.end());
    }
    const std::vector<u8> zeroRegsAndHalt = {
        0x06, 0, 0x0E, 0, 0x16, 0, 0x1E, 0, 0x26, 0, 0x2E, 0, // LD B/C/D/E/H/L, 0
        0x40,                                                  // LD B,B
        0x18, 0xFE,                                            // JR -2
    };
    program.insert(program.end(), zeroRegsAndHalt.begin(), zeroRegsAndHalt.end());
    return program;
}
} // namespace

TEST_CASE("runRomTest catches a Mooneye pass signaled only over serial, registers never set") {
    const auto outcome = roms::runRomTest(testOf(roms::Method::Mooneye),
                                           romWith(serialOnlyProgram({3, 5, 8, 13, 21, 34})));
    CHECK(outcome.status == roms::Verdict::Pass);
    CHECK(outcome.reason == "Fibonacci bytes over serial");
}

TEST_CASE("runRomTest catches a Mooneye failure signaled only over serial, registers never set to 0x42") {
    const auto outcome = roms::runRomTest(
        testOf(roms::Method::Mooneye), romWith(serialOnlyProgram({0x42, 0x42, 0x42, 0x42, 0x42, 0x42})));
    CHECK(outcome.status == roms::Verdict::Fail);
    CHECK(outcome.reason == "failure bytes (0x42) over serial");
}

TEST_CASE("screenshot tests fail with the reason, without running") {
    const auto outcome = roms::runRomTest(testOf(roms::Method::Screenshot), romWith({0x00}));
    CHECK(outcome.status == roms::Verdict::Fail);
    CHECK(outcome.reason == "needs the PPU (piece 3)");
    CHECK(outcome.emulatedSeconds == 0.0);
}

TEST_CASE("blarggFailureReason doesn't repeat 'Failed' when Blargg's own last line already starts with it") {
    CHECK(roms::blarggFailureReason("cpu_instrs\n\n01:ok\n\nFailed #3\n") == "Failed #3");
    CHECK(roms::blarggFailureReason("02-interrupts\n\nFailed\n") == "Failed");
}

TEST_CASE("blarggFailureReason still prefixes 'Failed: ' when the last line doesn't say so itself") {
    CHECK(roms::blarggFailureReason("some test\n\nsee above\n") == "Failed: see above");
    CHECK(roms::blarggFailureReason("") == "Failed: ");
}

TEST_CASE("an unsupported cartridge fails with the loader's message") {
    auto rom = romWith({0x00});
    rom[0x0147] = 0x19; // MBC5
    const auto outcome = roms::runRomTest(testOf(roms::Method::Mooneye), rom);
    CHECK(outcome.status == roms::Verdict::Fail);
    CHECK(outcome.reason.find("unsupported cartridge type 0x19") != std::string::npos);
}
