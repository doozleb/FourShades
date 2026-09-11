#include <doctest/doctest.h>

#include "sst/RecordingBus.h"
#include "sst/SstLoader.h"
#include "sst/SstRun.h"
#include "sst_fixtures.h"

#include <string>

using sst_fixtures::kNop;
using sst_fixtures::with;

namespace {
sst::TestOutcome run(const std::string& json) {
    sst::RecordingBus bus;
    return sst::runTest(sst::parseTests(json).at(0), bus);
}
} // namespace

TEST_CASE("a correct result passes") {
    CHECK(run(kNop).status == sst::Status::Pass);
}

// These test the tester: each one breaks the expected result in one place
// and checks the comparator notices and names the right field.
TEST_CASE("a wrong register is caught") {
    const auto outcome = run(with(kNop, R"("pc":257,"sp":65534,"a":1)", R"("pc":257,"sp":65534,"a":9)"));
    REQUIRE(outcome.status == sst::Status::Fail);
    CHECK(outcome.mismatch->field == "a");
    CHECK(outcome.mismatch->expected == "0x09");
    CHECK(outcome.mismatch->actual == "0x01");
}

TEST_CASE("a wrong memory byte is caught") {
    const auto outcome = run(with(kNop, R"("ram":[[256,0]]},"cycles")", R"("ram":[[256,7]]},"cycles")"));
    REQUIRE(outcome.status == sst::Status::Fail);
    CHECK(outcome.mismatch->field == "ram[0x0100]");
}

TEST_CASE("a wrong cycle kind, count or bus value is caught") {
    const auto kind = run(with(kNop, R"([[256,0,"r-m"]])", R"([[256,0,"-wm"]])"));
    REQUIRE(kind.status == sst::Status::Fail);
    CHECK(kind.mismatch->field == "cycle kind");
    CHECK(kind.mismatch->cycle == 0);

    const auto count = run(with(kNop, R"([[256,0,"r-m"]])", R"([[256,0,"r-m"],[256,0,"---"]])"));
    REQUIRE(count.status == sst::Status::Fail);
    CHECK(count.mismatch->field == "cycle count");

    const auto bus = run(with(kNop, R"([[256,0,"r-m"]])", R"([[257,0,"r-m"]])"));
    REQUIRE(bus.status == sst::Status::Fail);
    CHECK(bus.mismatch->field == "cycle bus");
}

TEST_CASE("an expected pending EI is checked") {
    const auto outcome = run(with(kNop, R"("ime":1,"ram":[[256,0]]},"cycles")",
                                  R"("ime":1,"ei":1,"ram":[[256,0]]},"cycles")"));
    REQUIRE(outcome.status == sst::Status::Fail);
    CHECK(outcome.mismatch->field == "ei");
}

TEST_CASE("an unimplemented opcode is reported, not failed") {
    // Uses the CB stub from Task 3. Once Task 13 implements every CB opcode,
    // nothing can reach the unimplemented path, and Task 13 deletes this case.
    const std::string cb =
        R"([{"name":"cb 00 0000",)"
        R"("initial":{"pc":256,"sp":65534,"a":1,"b":2,"c":3,"d":4,"e":5,"f":176,"h":6,"l":7,"ime":1,"ie":0,"ram":[[256,203],[257,0]]},)"
        R"("final":{"pc":258,"sp":65534,"a":1,"b":4,"c":3,"d":4,"e":5,"f":0,"h":6,"l":7,"ime":1,"ram":[[256,203],[257,0]]},)"
        R"("cycles":[[256,203,"r-m"],[257,0,"r-m"]]}])";
    CHECK(run(cb).status == sst::Status::Unimplemented);
}
