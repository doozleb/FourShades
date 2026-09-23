#include <doctest/doctest.h>

#include "core/GameBoy.h"
#include "core/Timer.h"
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

// One M-cycle of the pair, in the order the machine runs them: the counter
// moves first, then the APU asks it what fell.
void cycle(Timer& timer, Apu& apu) {
    timer.tick();
    apu.tick(timer);
}

// Puts the counter four T-cycles below `target` and runs the cycle that
// reaches it, so the APU sees whatever that crossing does to bit 12.
void cycleTo(Timer& timer, Apu& apu, u16 target) {
    timer.setCounter(static_cast<u16>(target - 4));
    cycle(timer, apu);
    REQUIRE(timer.counter() == target);
}

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
    Apu apu;
    Timer timer;
    cycleTo(timer, apu, 0x2000); // one step in, so 0 below means something
    REQUIRE(apu.sequencerStep() == 1);
    powerOff(apu);
    powerOn(apu);
    CHECK(apu.powered() == true);
    CHECK(apu.sequencerStep() == 0);
    CHECK(apu.read(0xFF26) == 0xF0); // on, and no channel came back with it
}

TEST_CASE("the frame sequencer steps on a falling edge of counter bit 12") {
    Apu apu;
    Timer timer;
    CHECK(apu.sequencerStep() == 0);

    cycleTo(timer, apu, 0x1000); // bit 12 rises: not an edge the sequencer wants
    CHECK(apu.sequencerStep() == 0);
    cycle(timer, apu);           // 0x1004, nothing crosses
    CHECK(apu.sequencerStep() == 0);

    cycleTo(timer, apu, 0x2000); // bit 12 falls
    CHECK(apu.sequencerStep() == 1);
    cycle(timer, apu);
    CHECK(apu.sequencerStep() == 1); // the edge belongs to one M-cycle only

    cycleTo(timer, apu, 0x2200); // bit 8 falls; bit 12 is clear either side
    CHECK(apu.sequencerStep() == 1);
    cycleTo(timer, apu, 0x3000); // bit 12 rises again
    CHECK(apu.sequencerStep() == 1);
}

TEST_CASE("the frame sequencer wraps after eight steps") {
    Apu apu;
    Timer timer;
    for (int i = 1; i <= 8; ++i) {
        CAPTURE(i);
        cycleTo(timer, apu, 0x2000);
        CHECK(apu.sequencerStep() == (i & 7));
    }
}

TEST_CASE("a DIV write that drops counter bit 12 steps the frame sequencer") {
    // The machine advances time first and performs the access second, so the
    // edge a DIV write makes lands after the APU's per-cycle poll. It has to
    // be asked again from the write itself, or this edge is lost.
    auto cart = Cartridge::load(std::vector<u8>(0x8000, 0x00), nullptr);
    REQUIRE(cart.has_value());
    GameBoy gb{std::move(*cart)};
    // Wait for DIV bit 4 -- counter bit 12 -- to be set, and for the cycle the
    // write itself spends not to be the one that carries the counter past it.
    for (int guard = 0; guard < 4096; ++guard) {
        const u8 div = gb.peek(0xFF04);
        if ((div & 0x10) != 0 && (div & 0x0F) != 0x0F) break;
        gb.idle();
    }
    REQUIRE((gb.peek(0xFF04) & 0x10) != 0);
    const int before = gb.apu().sequencerStep();
    gb.write(0xFF04, 0x00);
    CHECK(gb.peek(0xFF04) == 0x00);
    CHECK(gb.apu().sequencerStep() == (before + 1) % 8);
}

TEST_CASE("one falling edge is one step, however many times it is asked about") {
    // The edge query is a level that stands for the whole M-cycle, so the
    // cycle that both crosses bit 12 and has the counter written must not
    // step the sequencer twice.
    Apu apu;
    Timer timer;
    timer.setCounter(0x1FFC);
    timer.tick(); // 0x2000: bit 12 falls on the increment itself
    apu.tick(timer);
    CHECK(apu.sequencerStep() == 1);
    timer.write(0xFF04, 0x00); // the same M-cycle's write, on a counter already past it
    apu.counterWritten(timer);
    CHECK(apu.sequencerStep() == 1);
}

TEST_CASE("a DIV write that drops nothing leaves the frame sequencer alone") {
    auto cart = Cartridge::load(std::vector<u8>(0x8000, 0x00), nullptr);
    REQUIRE(cart.has_value());
    GameBoy gb{std::move(*cart)};
    for (int guard = 0; guard < 4096; ++guard) {
        const u8 div = gb.peek(0xFF04);
        if ((div & 0x10) == 0 && (div & 0x0F) != 0x0F) break;
        gb.idle();
    }
    REQUIRE((gb.peek(0xFF04) & 0x10) == 0);
    const int before = gb.apu().sequencerStep();
    gb.write(0xFF04, 0x00);
    CHECK(gb.apu().sequencerStep() == before);
}

TEST_CASE("a length counter that runs out switches its channel off in NR52") {
    Apu apu;
    Timer timer;
    // Channel 1 is the one the boot ROM leaves running, so it is the one
    // whose flag can be watched going out.
    REQUIRE((apu.read(0xFF26) & 0x01) != 0);
    apu.write(0xFF11, 0x3F); // one length step left
    apu.write(0xFF14, 0x40); // length enabled, no trigger
    // Step 0 clocks length, and the counter reaches zero on it.
    cycleTo(timer, apu, 0x2000);
    CHECK(apu.sequencerStep() == 1);
    CHECK((apu.read(0xFF26) & 0x01) == 0);
    CHECK(apu.read(0xFF26) == 0xF0);
}

TEST_CASE("the frame sequencer clocks length on four of its eight steps") {
    Apu apu;
    Timer timer;
    apu.write(0xFF11, 0x3B); // 64 - 59: five length steps left
    apu.write(0xFF14, 0x40); // length enabled, no trigger
    REQUIRE(apu.sequencerStep() == 0);
    // Five length clocks are eight sequencer steps and one more: steps 0, 2,
    // 4 and 6 of the first round, then step 0 of the second. A sequencer that
    // clocked length every step would have run out on the fifth.
    for (int step = 1; step <= 8; ++step) {
        CAPTURE(step);
        cycleTo(timer, apu, 0x2000);
        CHECK((apu.read(0xFF26) & 0x01) != 0);
    }
    CHECK(apu.sequencerStep() == 0); // eight steps: back to the start
    cycleTo(timer, apu, 0x2000);
    CHECK((apu.read(0xFF26) & 0x01) == 0);
}
