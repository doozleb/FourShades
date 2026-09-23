#include <doctest/doctest.h>

#include "core/mbc/Rtc.h"

#include <chrono>
#include <cstdint>

using namespace fourshades;

namespace {
// The register numbers a program writes to 4000-5FFF to select a clock
// register, per Pan Docs "MBC3".
constexpr u8 kSeconds = 0x08;
constexpr u8 kMinutes = 0x09;
constexpr u8 kHours = 0x0A;
constexpr u8 kDayLow = 0x0B;
constexpr u8 kDayHigh = 0x0C;

// 4194304 T-cycles per second; GameBoy::tick() is one M-cycle.
constexpr std::uint64_t kTicksPerSecond = 1048576;

void tickTimes(Rtc& rtc, std::uint64_t count) {
    for (std::uint64_t i = 0; i < count; ++i) {
        rtc.tick();
    }
}

// The live registers, which read() deliberately does not show: latch first.
RtcRegisters live(const Rtc& rtc) { return rtc.state().live; }

u16 dayCounter(const RtcRegisters& regs) {
    return static_cast<u16>(regs.dayLow | ((regs.dayHigh & 0x01) << 8));
}
} // namespace

TEST_CASE("RTC a second is 1048576 ticks, and not one fewer") {
    Rtc rtc;
    tickTimes(rtc, kTicksPerSecond - 1);
    CHECK(live(rtc).seconds == 0);
    rtc.tick();
    CHECK(live(rtc).seconds == 1);
}

TEST_CASE("RTC seconds 59 carries a minute") {
    Rtc rtc;
    rtc.write(kSeconds, 59);
    tickTimes(rtc, kTicksPerSecond);
    CHECK(live(rtc).seconds == 0);
    CHECK(live(rtc).minutes == 1);
}

TEST_CASE("RTC 23:59:59 carries into the day counter") {
    Rtc rtc;
    rtc.write(kHours, 23);
    rtc.write(kMinutes, 59);
    rtc.write(kSeconds, 59);
    tickTimes(rtc, kTicksPerSecond);
    const RtcRegisters regs = live(rtc);
    CHECK(regs.seconds == 0);
    CHECK(regs.minutes == 0);
    CHECK(regs.hours == 0);
    CHECK(dayCounter(regs) == 1);
}

TEST_CASE("RTC day 511 wraps to 0 and sets the carry, which then stays set") {
    Rtc rtc;
    rtc.write(kDayLow, 0xFF);
    rtc.write(kDayHigh, 0x01); // day 511
    rtc.advanceSeconds(86400);
    RtcRegisters regs = live(rtc);
    CHECK(dayCounter(regs) == 0);
    CHECK((regs.dayHigh & 0x80) != 0);

    // A further day leaves the carry set: only the program clears it.
    rtc.advanceSeconds(86400);
    regs = live(rtc);
    CHECK(dayCounter(regs) == 1);
    CHECK((regs.dayHigh & 0x80) != 0);
}

TEST_CASE("RTC a program writing dayHigh clears the carry") {
    Rtc rtc;
    rtc.write(kDayLow, 0xFF);
    rtc.write(kDayHigh, 0x01);
    rtc.advanceSeconds(86400);
    REQUIRE((live(rtc).dayHigh & 0x80) != 0);
    rtc.write(kDayHigh, 0x00);
    CHECK((live(rtc).dayHigh & 0x80) == 0);
}

TEST_CASE("RTC bit 6 halts the clock, sub-second accumulator included") {
    Rtc rtc;
    tickTimes(rtc, kTicksPerSecond - 1); // one tick short of a second
    rtc.write(kDayHigh, 0x40);
    CHECK(rtc.halted());
    tickTimes(rtc, kTicksPerSecond);
    CHECK(live(rtc).seconds == 0);
    // And a few more that are deliberately not a whole second: an
    // accumulator that kept counting while halted would be left somewhere
    // else entirely, even though the registers happen to look the same.
    tickTimes(rtc, 7);
    CHECK(live(rtc).seconds == 0);

    rtc.write(kDayHigh, 0x00);
    CHECK_FALSE(rtc.halted());
    rtc.tick(); // the frozen accumulator resumes and completes the second
    CHECK(live(rtc).seconds == 1);
}

TEST_CASE("RTC read returns the latched copy, not the live one") {
    Rtc rtc;
    rtc.advanceSeconds(90);
    CHECK(rtc.read(kSeconds) == 0);
    CHECK(rtc.read(kMinutes) == 0);

    rtc.latch();
    CHECK(rtc.read(kSeconds) == 30);
    CHECK(rtc.read(kMinutes) == 1);

    // The live clock runs on; the latched copy does not.
    rtc.advanceSeconds(1);
    CHECK(rtc.read(kSeconds) == 30);
}

TEST_CASE("RTC latch copies all five registers") {
    Rtc rtc;
    rtc.write(kSeconds, 0x01);
    rtc.write(kMinutes, 0x02);
    rtc.write(kHours, 0x03);
    rtc.write(kDayLow, 0x04);
    rtc.write(kDayHigh, 0xC1);
    rtc.latch();
    CHECK(rtc.read(kSeconds) == 0x01);
    CHECK(rtc.read(kMinutes) == 0x02);
    CHECK(rtc.read(kHours) == 0x03);
    CHECK(rtc.read(kDayLow) == 0x04);
    CHECK(rtc.read(kDayHigh) == 0xC1);
}

TEST_CASE("RTC writing seconds resets the sub-second accumulator") {
    Rtc rtc;
    tickTimes(rtc, kTicksPerSecond - 1);
    rtc.write(kSeconds, 5);
    rtc.tick();
    CHECK(live(rtc).seconds == 5); // a whole second to go, not one tick
}

TEST_CASE("RTC an out-of-range seconds value is kept and counted from") {
    Rtc rtc;
    rtc.write(kSeconds, 0x3F); // 63: above 59, so it counts on up
    rtc.advanceSeconds(1);
    CHECK(live(rtc).seconds == 64);
    CHECK(live(rtc).minutes == 0);

    // 64 -> ... -> 255 -> 0 (no carry) -> ... -> 59 -> carry.
    rtc.advanceSeconds(192); // 64 + 192 = 256, i.e. 0
    CHECK(live(rtc).seconds == 0);
    CHECK(live(rtc).minutes == 0);
    rtc.advanceSeconds(60);
    CHECK(live(rtc).seconds == 0);
    CHECK(live(rtc).minutes == 1);
}

TEST_CASE("RTC advanceSeconds rolls up through minutes") {
    Rtc rtc;
    rtc.advanceSeconds(90);
    const RtcRegisters regs = live(rtc);
    CHECK(regs.hours == 0);
    CHECK(regs.minutes == 1);
    CHECK(regs.seconds == 30);
}

TEST_CASE("RTC advanceSeconds rolls up through days, keeping the time of day") {
    Rtc rtc;
    rtc.write(kHours, 7);
    rtc.write(kMinutes, 15);
    rtc.write(kSeconds, 30);
    rtc.advanceSeconds(86400 * 3);
    const RtcRegisters regs = live(rtc);
    CHECK(dayCounter(regs) == 3);
    CHECK(regs.hours == 7);
    CHECK(regs.minutes == 15);
    CHECK(regs.seconds == 30);
}

TEST_CASE("RTC advanceSeconds does nothing while halted") {
    Rtc rtc;
    rtc.write(kSeconds, 10);
    rtc.write(kDayHigh, 0x40);
    rtc.advanceSeconds(86400 * 2 + 90);
    const RtcRegisters regs = live(rtc);
    CHECK(regs.seconds == 10);
    CHECK(regs.minutes == 0);
    CHECK(regs.hours == 0);
    CHECK(dayCounter(regs) == 0);
}

TEST_CASE("RTC advanceSeconds over a year is arithmetic, not a loop") {
    Rtc rtc;
    const auto start = std::chrono::steady_clock::now();
    rtc.advanceSeconds(31'536'000); // 365 days
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const RtcRegisters regs = live(rtc);
    CHECK(dayCounter(regs) == 365);
    CHECK(regs.hours == 0);
    CHECK(regs.minutes == 0);
    CHECK(regs.seconds == 0);
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 1000);
}

TEST_CASE("RTC advanceSeconds survives an absurd catch-up") {
    // A save file with a broken timestamp. Reaching the checks at all is the
    // point: a per-second loop would never return.
    Rtc rtc;
    rtc.advanceSeconds(1'000'000'000'000'000'000ULL);
    const RtcRegisters regs = live(rtc);
    CHECK(regs.seconds == 40); // 10^18 mod 60
    CHECK((regs.dayHigh & 0x80) != 0);
}

TEST_CASE("RTC state round-trips the live and latched copies independently") {
    Rtc rtc;
    rtc.write(kSeconds, 1);
    rtc.write(kMinutes, 2);
    rtc.write(kHours, 3);
    rtc.write(kDayLow, 4);
    rtc.write(kDayHigh, 0x01);
    rtc.latch();
    rtc.advanceSeconds(3600); // the live copy moves on, the latched does not

    const RtcState saved = rtc.state();
    CHECK(saved.live.hours == 4);
    CHECK(saved.latched.hours == 3);

    Rtc restored;
    restored.setState(saved);
    const RtcState after = restored.state();
    CHECK(after.live.seconds == saved.live.seconds);
    CHECK(after.live.minutes == saved.live.minutes);
    CHECK(after.live.hours == saved.live.hours);
    CHECK(after.live.dayLow == saved.live.dayLow);
    CHECK(after.live.dayHigh == saved.live.dayHigh);
    CHECK(after.latched.seconds == saved.latched.seconds);
    CHECK(after.latched.minutes == saved.latched.minutes);
    CHECK(after.latched.hours == saved.latched.hours);
    CHECK(after.latched.dayLow == saved.latched.dayLow);
    CHECK(after.latched.dayHigh == saved.latched.dayHigh);
    CHECK(restored.read(kHours) == 3); // the restored latched copy is what read() shows
}

TEST_CASE("RTC copies are independent") {
    Rtc rtc;
    rtc.advanceSeconds(5);
    Rtc copy = rtc;
    rtc.advanceSeconds(5);
    CHECK(live(copy).seconds == 5);
    CHECK(live(rtc).seconds == 10);
}
