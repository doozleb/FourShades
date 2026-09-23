#include <doctest/doctest.h>

#include "core/GameBoy.h"
#include "core/apu/Apu.h"

#include <utility>
#include <vector>

using namespace fourshades;

namespace {

struct Entry {
    u16 address;
    u8 value; // the read mask, or the power-up read value
};

// Pan Docs "Audio Registers": a read returns the stored byte OR this mask.
// FF15, FF1F and FF27-FF2F are not registers and read 0xFF entirely.
const std::vector<Entry>& readMasks() {
    static const std::vector<Entry> table = {
        {0xFF10, 0x80}, {0xFF11, 0x3F}, {0xFF12, 0x00}, {0xFF13, 0xFF},
        {0xFF14, 0xBF}, {0xFF15, 0xFF}, {0xFF16, 0x3F}, {0xFF17, 0x00},
        {0xFF18, 0xFF}, {0xFF19, 0xBF}, {0xFF1A, 0x7F}, {0xFF1B, 0xFF},
        {0xFF1C, 0x9F}, {0xFF1D, 0xFF}, {0xFF1E, 0xBF}, {0xFF1F, 0xFF},
        {0xFF20, 0xFF}, {0xFF21, 0x00}, {0xFF22, 0x00}, {0xFF23, 0xBF},
        {0xFF24, 0x00}, {0xFF25, 0x00}, {0xFF26, 0x70},
        {0xFF27, 0xFF}, {0xFF28, 0xFF}, {0xFF29, 0xFF}, {0xFF2A, 0xFF},
        {0xFF2B, 0xFF}, {0xFF2C, 0xFF}, {0xFF2D, 0xFF}, {0xFF2E, 0xFF},
        {0xFF2F, 0xFF},
        {0xFF30, 0x00}, {0xFF31, 0x00}, {0xFF32, 0x00}, {0xFF33, 0x00},
        {0xFF34, 0x00}, {0xFF35, 0x00}, {0xFF36, 0x00}, {0xFF37, 0x00},
        {0xFF38, 0x00}, {0xFF39, 0x00}, {0xFF3A, 0x00}, {0xFF3B, 0x00},
        {0xFF3C, 0x00}, {0xFF3D, 0x00}, {0xFF3E, 0x00}, {0xFF3F, 0x00},
    };
    return table;
}

// Pan Docs "Power Up Sequence", the DMG column, checked against the document
// on 2026-09-23. These are read values, so they already include the masks
// above. FF15, FF1F and FF27-FF3F are not in that table.
const std::vector<Entry>& powerUpValues() {
    static const std::vector<Entry> table = {
        {0xFF10, 0x80}, {0xFF11, 0xBF}, {0xFF12, 0xF3}, {0xFF13, 0xFF},
        {0xFF14, 0xBF}, {0xFF16, 0x3F}, {0xFF17, 0x00}, {0xFF18, 0xFF},
        {0xFF19, 0xBF}, {0xFF1A, 0x7F}, {0xFF1B, 0xFF}, {0xFF1C, 0x9F},
        {0xFF1D, 0xFF}, {0xFF1E, 0xBF}, {0xFF20, 0xFF}, {0xFF21, 0x00},
        {0xFF22, 0x00}, {0xFF23, 0xBF}, {0xFF24, 0x77}, {0xFF25, 0xF3},
        {0xFF26, 0xF1},
    };
    return table;
}

void powerOff(Apu& apu) { apu.write(0xFF26, 0x00); }
void powerOn(Apu& apu) { apu.write(0xFF26, 0x80); }

} // namespace

TEST_CASE("every sound register reads its stored value with the unused bits up") {
    for (const Entry& entry : readMasks()) {
        CAPTURE(entry.address);
        Apu apu; // fresh: writing 0x00 to NR52 powers the rest down
        apu.write(entry.address, 0x00);
        CHECK(apu.read(entry.address) == entry.value);
    }
}

TEST_CASE("the gaps in the register block read 0xFF whatever is written") {
    Apu apu;
    std::vector<u16> gaps = {0xFF15, 0xFF1F};
    for (u16 address = 0xFF27; address <= 0xFF2F; ++address) {
        gaps.push_back(address);
    }
    for (u16 address : gaps) {
        CAPTURE(address);
        CHECK(apu.read(address) == 0xFF);
        apu.write(address, 0x5A);
        CHECK(apu.read(address) == 0xFF);
        apu.write(address, 0x00);
        CHECK(apu.read(address) == 0xFF);
        CHECK(apu.stored(address) == 0x00);
    }
}

TEST_CASE("the sound registers power up as Pan Docs' table says") {
    Apu apu;
    for (const Entry& entry : powerUpValues()) {
        CAPTURE(entry.address);
        CHECK(apu.read(entry.address) == entry.value);
    }
}

TEST_CASE("powering off zeroes FF10-FF25 and ignores further writes to them") {
    Apu apu;
    for (u16 address = 0xFF10; address <= 0xFF25; ++address) {
        apu.write(address, 0xFF);
    }
    powerOff(apu);
    CHECK(apu.powered() == false);
    CHECK(apu.read(0xFF26) == 0x70); // power down, no channel on

    for (const Entry& entry : readMasks()) {
        if (entry.address > 0xFF25) continue;
        CAPTURE(entry.address);
        CHECK(apu.read(entry.address) == entry.value);
    }
    // Writes land nowhere while the APU is down. NR11, NR21, NR31 and NR41
    // are the DMG exception and have their own case below.
    for (u16 address = 0xFF10; address <= 0xFF25; ++address) {
        if (address == 0xFF11 || address == 0xFF16 || address == 0xFF1B ||
            address == 0xFF20) {
            continue;
        }
        CAPTURE(address);
        apu.write(address, 0xFF);
        CHECK(apu.stored(address) == 0x00);
    }
}

TEST_CASE("powering off leaves wave RAM alone") {
    Apu apu;
    for (u16 address = 0xFF30; address <= 0xFF3F; ++address) {
        apu.write(address, static_cast<u8>(address & 0xFF));
    }
    powerOff(apu);
    for (u16 address = 0xFF30; address <= 0xFF3F; ++address) {
        CAPTURE(address);
        CHECK(apu.read(address) == static_cast<u8>(address & 0xFF));
    }
    // And it stays writable with the APU down.
    apu.write(0xFF35, 0xC3);
    CHECK(apu.read(0xFF35) == 0xC3);
}

TEST_CASE("on DMG the length load still writes while the APU is down") {
    struct Length {
        u16 address;
        u8 written;
        u8 kept;
    };
    // NR11, NR21 and NR41 load six bits; NR31 loads all eight.
    const std::vector<Length> lengths = {
        {0xFF11, 0xFF, 0x3F}, {0xFF16, 0xB5, 0x35},
        {0xFF1B, 0xA7, 0xA7}, {0xFF20, 0xFF, 0x3F},
    };
    for (const Length& length : lengths) {
        CAPTURE(length.address);
        Apu apu;
        powerOff(apu);
        apu.write(length.address, length.written);
        CHECK(apu.stored(length.address) == length.kept);
    }
}

TEST_CASE("powering on restarts the frame sequencer") {
    // Task 3 brings the sequencer itself; until then the only thing to assert
    // is that the step a power-on leaves behind is 0.
    Apu apu;
    powerOff(apu);
    powerOn(apu);
    CHECK(apu.powered() == true);
    CHECK(apu.sequencerStep() == 0);
    CHECK(apu.read(0xFF26) == 0xF0); // on, and no channel came back with it
}

TEST_CASE("NR52's low four bits are read only") {
    Apu apu;
    const u8 before = apu.read(0xFF26);
    apu.write(0xFF26, 0xFF);
    CHECK(apu.read(0xFF26) == before);
    apu.write(0xFF26, 0x8F);
    CHECK(apu.read(0xFF26) == before);
    // Bit 7 clear still powers down, whatever the low bits say.
    apu.write(0xFF26, 0x0F);
    CHECK(apu.read(0xFF26) == 0x70);
}

TEST_CASE("the machine routes FF10-FF3F to the APU") {
    auto cart = Cartridge::load(std::vector<u8>(0x8000, 0x00), nullptr);
    REQUIRE(cart.has_value());
    GameBoy gb{std::move(*cart)};
    for (const Entry& entry : powerUpValues()) {
        CAPTURE(entry.address);
        CHECK(gb.peek(entry.address) == entry.value);
    }
    gb.write(0xFF30, 0x5A);
    CHECK(gb.peek(0xFF30) == 0x5A);
    gb.write(0xFF12, 0x00);
    CHECK(gb.peek(0xFF12) == 0x00);
    // And the gaps answer through the machine too, where 0xFF used to be the
    // answer for the whole block.
    CHECK(gb.peek(0xFF1F) == 0xFF);
    CHECK(gb.peek(0xFF26) == 0xF1);
}
