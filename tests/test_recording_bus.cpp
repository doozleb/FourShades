#include <doctest/doctest.h>

#include "sst/RecordingBus.h"

using sst::Cycle;
using sst::CycleKind;
using sst::RecordingBus;

TEST_CASE("reads, writes and idle cycles are logged in order") {
    RecordingBus bus;
    bus.poke(0x1234, 0xAB);
    CHECK(bus.log().empty());

    CHECK(bus.read(0x1234) == 0xAB);
    bus.write(0x2000, 0x55);
    bus.idle();

    REQUIRE(bus.log().size() == 3);
    CHECK(bus.log()[0] == Cycle{0x1234, 0xAB, CycleKind::Read});
    CHECK(bus.log()[1] == Cycle{0x2000, 0x55, CycleKind::Write});
    CHECK(bus.log()[2].kind == CycleKind::Idle);
    CHECK(bus.peek(0x2000) == 0x55);
}

TEST_CASE("reset clears touched memory and the log") {
    RecordingBus bus;
    bus.poke(0x0010, 1);
    bus.write(0x0020, 2);
    bus.reset();
    CHECK(bus.peek(0x0010) == 0);
    CHECK(bus.peek(0x0020) == 0);
    CHECK(bus.log().empty());
}
