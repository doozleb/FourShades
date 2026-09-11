#include <doctest/doctest.h>

#include "sst/SstLoader.h"
#include "sst_fixtures.h"

#include <stdexcept>
#include <string>

using sst_fixtures::kNop;
using sst_fixtures::with;

TEST_CASE("parseTests reads every field of a test") {
    const auto tests = sst::parseTests(kNop);
    REQUIRE(tests.size() == 1);
    const sst::SstTest& t = tests[0];
    CHECK(t.name == "00 0000");
    CHECK(t.initial.pc == 256);
    CHECK(t.initial.sp == 65534);
    CHECK(t.initial.f == 0xB0);
    CHECK(t.initial.ime);
    REQUIRE(t.initial.ram.size() == 1);
    CHECK(t.initial.ram[0] == std::pair<fourshades::u16, fourshades::u8>{256, 0});
    CHECK(t.final.pc == 257);
    CHECK_FALSE(t.final.imePending.has_value());
    REQUIRE(t.cycles.size() == 1);
    CHECK(t.cycles[0] == sst::Cycle{256, 0, sst::CycleKind::Read});
}

TEST_CASE("parseTests reads the EI 'ei' flag in the final state") {
    const auto tests = sst::parseTests(with(kNop, R"("ime":1,"ram":[[256,0]]},"cycles")",
                                            R"("ime":1,"ei":1,"ram":[[256,0]]},"cycles")"));
    REQUIRE(tests[0].final.imePending.has_value());
    CHECK(*tests[0].final.imePending);
}

TEST_CASE("parseTests rejects anything it doesn't understand") {
    CHECK_THROWS_AS(sst::parseTests(with(kNop, R"("ie":0)", R"("zz":0)")), std::runtime_error);
    CHECK_THROWS_AS(sst::parseTests(with(kNop, R"("a":1,"b")", R"("a":256,"b")")), std::runtime_error);
    CHECK_THROWS_AS(sst::parseTests(with(kNop, R"("r-m")", R"("r--")")), std::runtime_error);
    CHECK_THROWS_AS(sst::parseTests(with(kNop, R"("sp":65534,)", "")), std::runtime_error);
    CHECK_THROWS_AS(sst::parseTests(with(kNop, R"("name":"00 0000",)", R"("name":"00 0000","extra":1,)")),
                    std::runtime_error);
}
