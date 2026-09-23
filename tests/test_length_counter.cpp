#include <doctest/doctest.h>

#include "core/apu/LengthCounter.h"

using namespace fourshades;

namespace {

// The three shapes of NRx4 write, spelled out so the cases below read as the
// hardware does: enable/disable length, trigger, and which half of the length
// period the write lands in.
constexpr bool kFirstHalf = true;  // the next sequencer step does not clock length
constexpr bool kSecondHalf = false;

LengthCounter shortCounter() { return LengthCounter{LengthCounter::kShort}; }

} // namespace

TEST_CASE("a counter loaded with 1 and clocked once switches its channel off") {
    LengthCounter length = shortCounter();
    length.load(63); // 64 - 63
    CHECK(length.value() == 1);
    CHECK_FALSE(length.writeControl(true, false, kSecondHalf));
    CHECK(length.enabled());
    CHECK(length.clock()); // reaching zero is what disables the channel
    CHECK(length.value() == 0);
    // And it stays there: a counter at zero neither wraps nor fires again.
    CHECK_FALSE(length.clock());
    CHECK(length.value() == 0);
}

TEST_CASE("a load of zero means the whole length, not an expired counter") {
    LengthCounter length = shortCounter();
    length.load(0);
    CHECK(length.value() == 64);
    length.load(1);
    CHECK(length.value() == 63);
}

TEST_CASE("clocking does nothing while length is disabled") {
    LengthCounter length = shortCounter();
    length.load(63); // one clock from expiry
    CHECK_FALSE(length.enabled());
    for (int i = 0; i < 10; ++i) {
        CHECK_FALSE(length.clock());
    }
    CHECK(length.value() == 1);
}

TEST_CASE("enabling length in the first half of the period clocks it once") {
    LengthCounter length = shortCounter();
    length.load(60); // 4 left
    CHECK_FALSE(length.writeControl(true, false, kFirstHalf));
    CHECK(length.value() == 3);
}

TEST_CASE("enabling length in the second half of the period does not clock it") {
    LengthCounter length = shortCounter();
    length.load(60);
    CHECK_FALSE(length.writeControl(true, false, kSecondHalf));
    CHECK(length.value() == 4);
}

TEST_CASE("the extra clock needs length to have been off and to be going on") {
    LengthCounter length = shortCounter();
    length.load(60);
    CHECK_FALSE(length.writeControl(true, false, kSecondHalf));
    CHECK(length.value() == 4);
    // Already enabled: writing bit 6 again is not the 0 -> 1 edge.
    CHECK_FALSE(length.writeControl(true, false, kFirstHalf));
    CHECK(length.value() == 4);
    // Disabling clocks nothing either.
    CHECK_FALSE(length.writeControl(false, false, kFirstHalf));
    CHECK(length.value() == 4);
}

TEST_CASE("an extra clock that reaches zero switches the channel off") {
    LengthCounter length = shortCounter();
    length.load(63); // 1 left
    CHECK(length.writeControl(true, false, kFirstHalf));
    CHECK(length.value() == 0);
}

TEST_CASE("an extra clock that reaches zero leaves a triggered channel alone") {
    // The same write triggers, so the counter reloads and nothing is disabled.
    LengthCounter length = shortCounter();
    length.load(63);
    CHECK_FALSE(length.writeControl(true, true, kFirstHalf));
    // Zero, then reloaded to the maximum and clocked once by the same write.
    CHECK(length.value() == 63);
    CHECK(length.enabled());
}

TEST_CASE("a trigger with a zero counter reloads it to the maximum") {
    LengthCounter length = shortCounter();
    length.load(63);
    CHECK_FALSE(length.writeControl(true, false, kSecondHalf));
    CHECK(length.clock());
    CHECK(length.value() == 0);
    CHECK_FALSE(length.writeControl(true, true, kSecondHalf));
    CHECK(length.value() == 64);
}

TEST_CASE("a trigger reloading in the first half lands on the maximum minus one") {
    LengthCounter length = shortCounter();
    length.load(63);
    CHECK_FALSE(length.writeControl(true, false, kSecondHalf));
    CHECK(length.clock());
    CHECK(length.value() == 0);
    CHECK_FALSE(length.writeControl(true, true, kFirstHalf));
    CHECK(length.value() == 63);
}

TEST_CASE("a trigger in the first half with length off reloads the whole maximum") {
    // The extra clock on reload needs length enabled by this write, the same
    // way the extra clock on an enable does.
    LengthCounter length = shortCounter();
    length.load(63);
    CHECK_FALSE(length.writeControl(true, false, kSecondHalf));
    CHECK(length.clock());
    CHECK_FALSE(length.writeControl(false, true, kFirstHalf));
    CHECK(length.value() == 64);
}

TEST_CASE("a trigger leaves a counter that has not expired alone") {
    LengthCounter length = shortCounter();
    length.load(60);
    CHECK_FALSE(length.writeControl(false, true, kFirstHalf));
    CHECK(length.value() == 4);
}

TEST_CASE("channel 3 counts from 256, not 64") {
    LengthCounter length{LengthCounter::kWave};
    CHECK(length.maximum() == 256);
    length.load(0);
    CHECK(length.value() == 256);
    length.load(200);
    CHECK(length.value() == 56);
    length.load(255);
    CHECK(length.value() == 1);
    CHECK_FALSE(length.writeControl(true, false, kSecondHalf));
    CHECK(length.clock());
    CHECK_FALSE(length.writeControl(true, true, kSecondHalf));
    CHECK(length.value() == 256);
}

TEST_CASE("powering the APU down stops the counter without clearing it") {
    // On DMG a length counter keeps its value across a power cycle; only the
    // enable, which lives in NRx4, goes away with the register.
    LengthCounter length = shortCounter();
    length.load(60);
    CHECK_FALSE(length.writeControl(true, false, kSecondHalf));
    length.powerOff();
    CHECK_FALSE(length.enabled());
    CHECK(length.value() == 4);
    CHECK_FALSE(length.clock());
    CHECK(length.value() == 4);
}
