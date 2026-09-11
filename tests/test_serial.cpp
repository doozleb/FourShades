#include <doctest/doctest.h>

#include "core/Serial.h"

using namespace fourshades;

namespace {
// Drives the serial port with a system counter that gains 4 per M-cycle,
// the way GameBoy does. Returns the M-cycles until the transfer completes.
int runUntilDone(Serial& serial, u16& counter, int limit = 5000) {
    for (int i = 1; i <= limit; ++i) {
        const u16 before = counter;
        counter = static_cast<u16>(counter + 4);
        if (serial.tick(before, counter)) {
            return i;
        }
    }
    return -1;
}
} // namespace

TEST_CASE("an internal-clock transfer takes 8 bits of 128 M-cycles") {
    Serial serial;
    u16 counter = 0;
    serial.write(0xFF01, 0x41);
    serial.write(0xFF02, 0x81);
    CHECK((serial.read(0xFF02) & 0x80) != 0);
    const int cycles = runUntilDone(serial, counter);
    // The first bit waits for the next falling edge of counter bit 8, so the
    // transfer takes between 7 and 8 full bit periods.
    CHECK(cycles > 7 * 128);
    CHECK(cycles <= 8 * 128);
    CHECK((serial.read(0xFF02) & 0x80) == 0);
    CHECK(serial.read(0xFF01) == 0xFF); // no partner: ones shifted in
    REQUIRE(serial.sent().size() == 1);
    CHECK(serial.sent()[0] == 0x41);
}

TEST_CASE("an external-clock transfer never completes without a partner") {
    Serial serial;
    u16 counter = 0;
    serial.write(0xFF01, 0x12);
    serial.write(0xFF02, 0x80);
    CHECK(runUntilDone(serial, counter, 3000) == -1);
    CHECK(serial.sent().empty());
}

TEST_CASE("SC reads with its unused bits set") {
    Serial serial;
    serial.write(0xFF02, 0x00);
    CHECK(serial.read(0xFF02) == 0x7E);
}

TEST_CASE("restarting a transfer mid-way records both bytes and runs the new one to completion") {
    Serial serial;
    u16 counter = 0;
    serial.write(0xFF01, 'A');
    serial.write(0xFF02, 0x81);
    for (int i = 0; i < 300; ++i) { // a couple of bits of 'A' go out
        const u16 before = counter;
        counter = static_cast<u16>(counter + 4);
        CHECK_FALSE(serial.tick(before, counter));
    }
    serial.write(0xFF01, 'B');
    serial.write(0xFF02, 0x81); // restart
    REQUIRE(serial.sent().size() == 2);
    CHECK(serial.sent()[0] == 'A');
    CHECK(serial.sent()[1] == 'B');
    const int cycles = runUntilDone(serial, counter);
    CHECK(cycles > 0);
    CHECK(cycles <= 8 * 128); // the restart begins a fresh 8-bit transfer
    CHECK(serial.read(0xFF01) == 0xFF);
}
