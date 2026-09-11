#include <doctest/doctest.h>

#include "core/Registers.h"

using namespace fourshades;

TEST_CASE("F keeps its low nibble at zero whatever is written") {
    Registers r;
    r.setF(0xFF);
    CHECK(r.f() == 0xF0);
    r.setAf(0x12FF);
    CHECK(r.a == 0x12);
    CHECK(r.f() == 0xF0);
    CHECK(r.af() == 0x12F0);
}

TEST_CASE("register pairs read and write both halves") {
    Registers r;
    r.setBc(0x1234);
    CHECK(r.b == 0x12);
    CHECK(r.c == 0x34);
    CHECK(r.bc() == 0x1234);
    r.setDe(0xABCD);
    CHECK(r.de() == 0xABCD);
    r.setHl(0x0102);
    CHECK(r.h == 0x01);
    CHECK(r.l == 0x02);
    CHECK(r.hl() == 0x0102);
}

TEST_CASE("flag reads individual bits of F") {
    Registers r;
    r.setF(Registers::FlagZ | Registers::FlagC);
    CHECK(r.flag(Registers::FlagZ));
    CHECK_FALSE(r.flag(Registers::FlagN));
    CHECK_FALSE(r.flag(Registers::FlagH));
    CHECK(r.flag(Registers::FlagC));
}
