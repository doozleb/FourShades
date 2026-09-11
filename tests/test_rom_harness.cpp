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

TEST_CASE("parseTestList reads entries and rejects unknown methods") {
    const std::string ok = R"({"shootout_commit":"abc","tests":[{"name":"t","rom":"a/t.gb","group":"timer",)"
                           R"("method":"mooneye","runtime":1.5,"limit_seconds":6.5,"references":["a/t.png"]}]})";
    const roms::TestList list = roms::parseTestList(ok);
    CHECK(list.shootoutCommit == "abc");
    REQUIRE(list.tests.size() == 1);
    CHECK(list.tests[0].method == roms::Method::Mooneye);
    CHECK(list.tests[0].limitSeconds == 6.5);
    CHECK(list.tests[0].references.size() == 1);
    std::string bad = ok;
    bad.replace(bad.find("mooneye"), 7, "guess");
    CHECK_THROWS_AS(roms::parseTestList(bad), std::runtime_error);
}

TEST_CASE("serial text: Failed beats Passed, and neither means still running") {
    CHECK(roms::serialVerdict("cpu_instrs\n\n01:ok\n\nPassed\n") == roms::Verdict::Pass);
    CHECK(roms::serialVerdict("02-interrupts\n\nFailed #3\n") == roms::Verdict::Fail);
    CHECK(roms::serialVerdict("Passed\nFailed\n") == roms::Verdict::Fail);
    CHECK(roms::serialVerdict("still running") == roms::Verdict::Running);
}

TEST_CASE("Blargg's memory protocol needs the signature, then reads the status") {
    std::map<u16, u8> mem;
    const auto peek = [&](u16 a) { return mem.count(a) ? mem[a] : static_cast<u8>(0xFF); };
    std::string text;
    CHECK(roms::blarggMemoryVerdict(peek, &text) == roms::Verdict::Running); // no signature
    mem[0xA001] = 0xDE;
    mem[0xA002] = 0xB0;
    mem[0xA003] = 0x61;
    mem[0xA000] = 0x80;
    CHECK(roms::blarggMemoryVerdict(peek, &text) == roms::Verdict::Running); // still running
    mem[0xA000] = 0x00;
    mem[0xA004] = 'o';
    mem[0xA005] = 'k';
    mem[0xA006] = 0x00;
    CHECK(roms::blarggMemoryVerdict(peek, &text) == roms::Verdict::Pass);
    CHECK(text == "ok");
    mem[0xA000] = 0x01;
    CHECK(roms::blarggMemoryVerdict(peek, &text) == roms::Verdict::Fail);
}

TEST_CASE("Mooneye verdict fires only on LD B,B with all six registers") {
    Registers r;
    r.b = 3; r.c = 5; r.d = 8; r.e = 13; r.h = 21; r.l = 34;
    CHECK(roms::mooneyeVerdict(r, 0x40) == roms::Verdict::Pass);
    CHECK(roms::mooneyeVerdict(r, 0x00) == roms::Verdict::Running);
    r.l = 35; // five of six
    CHECK(roms::mooneyeVerdict(r, 0x40) == roms::Verdict::Running);
    r.b = r.c = r.d = r.e = r.h = r.l = 0x42;
    CHECK(roms::mooneyeVerdict(r, 0x40) == roms::Verdict::Fail);
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

TEST_CASE("screenshot tests fail with the reason, without running") {
    const auto outcome = roms::runRomTest(testOf(roms::Method::Screenshot), romWith({0x00}));
    CHECK(outcome.status == roms::Verdict::Fail);
    CHECK(outcome.reason == "needs the PPU (piece 3)");
    CHECK(outcome.emulatedSeconds == 0.0);
}

TEST_CASE("an unsupported cartridge fails with the loader's message") {
    auto rom = romWith({0x00});
    rom[0x0147] = 0x19; // MBC5
    const auto outcome = roms::runRomTest(testOf(roms::Method::Mooneye), rom);
    CHECK(outcome.status == roms::Verdict::Fail);
    CHECK(outcome.reason.find("unsupported cartridge type 0x19") != std::string::npos);
}
