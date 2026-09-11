#include <doctest/doctest.h>

#include "core/LcdTiming.h"

using namespace fourshades;

TEST_CASE("LY advances every 114 M-cycles and VBlank starts at line 144") {
    LcdTiming lcd; // power-on: LCD on, LY 0
    for (int i = 0; i < 113; ++i) {
        CHECK(lcd.tick() == 0);
    }
    CHECK(lcd.read(0xFF44) == 0);
    lcd.tick();
    CHECK(lcd.read(0xFF44) == 1);
    u8 requested = 0;
    for (int i = 0; i < 143 * 114; ++i) {
        requested = static_cast<u8>(requested | lcd.tick());
    }
    CHECK(lcd.read(0xFF44) == 144);
    CHECK(requested == 0x01);
    for (int i = 0; i < 10 * 114; ++i) {
        lcd.tick();
    }
    CHECK(lcd.read(0xFF44) == 0); // 154 lines, then back to 0
}

TEST_CASE("turning the LCD off holds LY at 0") {
    LcdTiming lcd;
    for (int i = 0; i < 500; ++i) {
        lcd.tick();
    }
    lcd.write(0xFF40, 0x11);
    CHECK(lcd.read(0xFF44) == 0);
    for (int i = 0; i < 500; ++i) {
        CHECK(lcd.tick() == 0);
    }
    CHECK(lcd.read(0xFF44) == 0);
}

TEST_CASE("STAT reports LY=LYC and keeps bit 7 set") {
    LcdTiming lcd;
    lcd.write(0xFF45, 0x00);
    CHECK((lcd.read(0xFF41) & 0x84) == 0x84);
    lcd.write(0xFF45, 0x05);
    CHECK((lcd.read(0xFF41) & 0x04) == 0);
    lcd.write(0xFF44, 0x33); // LY is read-only
    CHECK(lcd.read(0xFF44) == 0);
}
