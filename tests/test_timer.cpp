#include <doctest/doctest.h>

#include "core/Timer.h"

using namespace fourshades;

namespace {
// Ticks until TIMA changes (or a limit), returning the number of M-cycles.
int cyclesUntilTimaChanges(Timer& timer, int limit = 2000) {
    const u8 start = timer.read(0xFF05);
    for (int i = 1; i <= limit; ++i) {
        timer.tick();
        if (timer.read(0xFF05) != start) {
            return i;
        }
    }
    return -1;
}
} // namespace

TEST_CASE("DIV is the high byte of a counter that gains 4 each M-cycle") {
    Timer timer;
    timer.setCounter(0x00FC);
    CHECK(timer.read(0xFF04) == 0x00);
    timer.tick();
    CHECK(timer.counter() == 0x0100);
    CHECK(timer.read(0xFF04) == 0x01);
    timer.write(0xFF04, 0x55); // any write resets it
    CHECK(timer.counter() == 0);
}

TEST_CASE("TIMA counts at each TAC rate") {
    const int expected[4] = {256, 4, 16, 64}; // M-cycles per increment
    for (int rate = 0; rate < 4; ++rate) {
        Timer timer;
        timer.write(0xFF07, static_cast<u8>(0x04 | rate));
        cyclesUntilTimaChanges(timer); // align to an edge
        INFO("rate " << rate);
        CHECK(cyclesUntilTimaChanges(timer) == expected[rate]);
    }
}

TEST_CASE("TAC reads with its unused bits set, and a disabled timer doesn't count") {
    Timer timer;
    timer.write(0xFF07, 0x01);
    CHECK(timer.read(0xFF07) == 0xF9);
    CHECK(cyclesUntilTimaChanges(timer, 1000) == -1);
}

TEST_CASE("writing DIV while the selected bit is set ticks TIMA once") {
    Timer timer;
    timer.write(0xFF07, 0x05);  // bit 3 selected
    timer.setCounter(0x0008);   // bit 3 set
    timer.write(0xFF04, 0x00);  // falling edge
    CHECK(timer.read(0xFF05) == 1);
}

TEST_CASE("disabling the timer while the selected bit is set ticks TIMA once (DMG)") {
    Timer timer;
    timer.write(0xFF07, 0x05);
    timer.setCounter(0x0008);
    timer.write(0xFF07, 0x01); // enable off: the AND gate output falls
    CHECK(timer.read(0xFF05) == 1);
}

TEST_CASE("overflow: TIMA reads 0 for one cycle, then TMA and the interrupt") {
    Timer timer;
    timer.write(0xFF06, 0x23);  // TMA
    timer.write(0xFF05, 0xFF);  // TIMA
    timer.write(0xFF07, 0x05);  // every 4 M-cycles
    timer.setCounter(0x000C);   // bit 3 set: the next tick is a falling edge
    CHECK_FALSE(timer.tick());  // cycle A: overflow
    CHECK(timer.read(0xFF05) == 0x00);
    CHECK(timer.tick());        // cycle B: reload and interrupt
    CHECK(timer.read(0xFF05) == 0x23);
    CHECK_FALSE(timer.tick());
}

TEST_CASE("writing TIMA during cycle A cancels the reload and the interrupt") {
    Timer timer;
    timer.write(0xFF06, 0x23);
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF07, 0x05);
    timer.setCounter(0x000C);
    timer.tick();              // cycle A
    timer.write(0xFF05, 0x42);
    CHECK_FALSE(timer.tick()); // no interrupt
    CHECK(timer.read(0xFF05) == 0x42);
}

TEST_CASE("during cycle B a TIMA write is ignored and a TMA write lands in TIMA") {
    Timer timer;
    timer.write(0xFF06, 0x23);
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF07, 0x05);
    timer.setCounter(0x000C);
    timer.tick();              // cycle A
    CHECK(timer.tick());       // cycle B
    timer.write(0xFF05, 0x42); // ignored
    CHECK(timer.read(0xFF05) == 0x23);
    timer.write(0xFF06, 0x77); // lands in TIMA too
    CHECK(timer.read(0xFF05) == 0x77);
}
