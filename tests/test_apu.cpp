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

TEST_CASE("every channel's enable flag follows its own DAC and trigger") {
    // The two pulse channels have their generators; channels 3 and 4 do not
    // yet. The enable flag, the length counter and the DAC belong to all
    // four all the same, which is what this checks.
    struct Channel {
        u8 flag;
        u16 dacRegister;
        u8 dacOn;   // a value whose DAC bits are set
        u16 control; // NRx4
    };
    // Channel 3's DAC is NR30 bit 7; the other three are the top five bits of
    // an envelope register.
    const std::vector<Channel> channels = {
        {0x01, 0xFF12, 0xF0, 0xFF14},
        {0x02, 0xFF17, 0xF0, 0xFF19},
        {0x04, 0xFF1A, 0x80, 0xFF1E},
        {0x08, 0xFF21, 0xF0, 0xFF23},
    };
    for (const Channel& channel : channels) {
        CAPTURE(channel.flag);
        Apu apu;
        apu.write(channel.dacRegister, 0x00); // DAC off, so the channel is off
        REQUIRE((apu.read(0xFF26) & channel.flag) == 0);
        apu.write(channel.control, 0x80); // a trigger the DAC refuses
        CHECK((apu.read(0xFF26) & channel.flag) == 0);
        apu.write(channel.dacRegister, channel.dacOn);
        CHECK((apu.read(0xFF26) & channel.flag) == 0); // a DAC does not enable
        apu.write(channel.control, 0x80);
        CHECK((apu.read(0xFF26) & channel.flag) != 0); // a trigger does
        apu.write(channel.dacRegister, 0x00);
        CHECK((apu.read(0xFF26) & channel.flag) == 0); // and a DAC disables
    }
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
    // 0xF1 at power-up, but that NR12 write above turned channel 1's DAC off
    // and took the channel with it, so its flag is gone.
    CHECK(gb.peek(0xFF26) == 0xF0);
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

// ---------------------------------------------------------------------------
// The mixer: NR50, NR51 and what Apu::sample() hands the output stage.
// ---------------------------------------------------------------------------

namespace {

constexpr u16 kNr50 = 0xFF24;
constexpr u16 kNr51 = 0xFF25;

// Pan Docs "Audio Details -- DACs": "a digital value of $0 is transformed
// into an analog +1 and a value of $F into an analog -1", linearly between.
float dac(int level) { return 1.0f - static_cast<float>(level) / 7.5f; }

// The mix divides by the four channels, so one channel alone, with both sides
// at full master volume, comes to this.
float quarter(int level) { return dac(level) / 4.0f; }

// Every channel off, both sides at full volume, every channel routed to both.
// The state each mixer case switches exactly what it is about back on from:
// the boot ROM leaves channel 1 playing, so its DAC has to go first.
void hush(Apu& apu) {
    apu.write(0xFF12, 0x00); // NR12: channel 1's DAC, and the channel with it
    apu.write(0xFF17, 0x00); // NR22
    apu.write(0xFF1A, 0x00); // NR30
    apu.write(0xFF21, 0x00); // NR42
    apu.write(kNr50, 0x77);
    apu.write(kNr51, 0xFF);
}

// Channel 1 or 2 at a known digital level. Duty 1 is high at position 0 and
// duty 0 is low there, and a fresh APU that has not been ticked is at
// position 0, so `high` picks between a level of `volume` and a level of 0.
void startPulse(Apu& apu, int channel, u8 volume, bool high) {
    const u16 base = channel == 0 ? 0xFF10 : 0xFF15;
    apu.write(static_cast<u16>(base + 1), high ? 0x40 : 0x00);
    apu.write(static_cast<u16>(base + 2), static_cast<u8>(volume << 4));
    apu.write(static_cast<u16>(base + 4), 0x80);
}

// Channel 3 with 0xF in its sample buffer: wave RAM all ones, the shortest
// period, and four M-cycles for it to read one.
void startWave(Apu& apu, Timer& timer, u8 outputLevel) {
    for (u16 address = 0xFF30; address <= 0xFF3F; ++address) {
        apu.write(address, 0xFF);
    }
    apu.write(0xFF1A, 0x80);                              // NR30: the DAC
    apu.write(0xFF1C, static_cast<u8>(outputLevel << 5)); // NR32
    apu.write(0xFF1D, 0xFF);                              // NR33
    apu.write(0xFF1E, 0x87);                              // NR34: frequency 2047, trigger
    for (int mCycle = 0; mCycle < 4; ++mCycle) {
        cycle(timer, apu);
    }
}

// Channel 4 after `steps` LFSR steps. NR43 zero is a period of eight
// T-cycles, which is two M-cycles a step.
void startNoise(Apu& apu, Timer& timer, u8 volume, int steps) {
    apu.write(0xFF22, 0x00);                         // NR43
    apu.write(0xFF21, static_cast<u8>(volume << 4)); // NR42
    apu.write(0xFF23, 0x80);                         // NR44: trigger
    for (int mCycle = 0; mCycle < steps * 2; ++mCycle) {
        cycle(timer, apu);
    }
}

} // namespace

TEST_CASE("each channel reaches the mix through its own DAC") {
    // One channel at a time, at a digital level only it can be producing.
    SUBCASE("channel 1") {
        Apu apu;
        hush(apu);
        startPulse(apu, 0, 15, true); // duty high, volume 15
        CHECK(apu.sample().left == doctest::Approx(quarter(15)));
        CHECK(apu.sample().right == doctest::Approx(quarter(15)));
    }
    SUBCASE("channel 2") {
        Apu apu;
        hush(apu);
        startPulse(apu, 1, 9, true);
        CHECK(apu.sample().left == doctest::Approx(quarter(9)));
    }
    SUBCASE("channel 3") {
        Timer timer;
        Apu apu;
        hush(apu);
        startWave(apu, timer, 2); // output level 2: the sample shifted right by 1
        REQUIRE(apu.wave().sample() == 0x0F);
        CHECK(apu.sample().left == doctest::Approx(quarter(7)));
    }
    SUBCASE("channel 4") {
        Timer timer;
        Apu apu;
        hush(apu);
        // Fifteen steps is the first one that leaves bit 0 of the LFSR clear,
        // and the output is the inverted bit 0.
        startNoise(apu, timer, 12, 15);
        REQUIRE(apu.noise().output());
        CHECK(apu.sample().left == doctest::Approx(quarter(12)));
    }
    SUBCASE("all four at once") {
        Apu apu;
        hush(apu);
        startPulse(apu, 0, 15, false); // duty low: a digital zero
        startPulse(apu, 1, 15, false);
        apu.write(0xFF1A, 0x80); // channel 3's DAC ...
        apu.write(0xFF1E, 0x80); // ... and a trigger, with an empty buffer
        apu.write(0xFF21, 0xF0); // channel 4's DAC ...
        apu.write(0xFF23, 0x80); // ... and a trigger, with the LFSR all ones
        REQUIRE(apu.read(0xFF26) == 0xFF);
        CHECK(apu.sample().left == doctest::Approx(4.0f * quarter(0)));
        CHECK(apu.sample().right == doctest::Approx(4.0f * quarter(0)));
    }
}

TEST_CASE("NR51 routes each channel to the left, the right, both or neither") {
    // Pan Docs, NR51: bits 0-3 are the four channels on the right and bits
    // 4-7 the same four on the left.
    struct Case {
        int channel;
        u8 flag; // the channel's bit within a nibble
    };
    const std::vector<Case> channels = {{0, 0x01}, {1, 0x02}, {2, 0x04}, {3, 0x08}};
    for (const Case& entry : channels) {
        CAPTURE(entry.channel);
        Timer timer;
        Apu apu;
        hush(apu);
        switch (entry.channel) {
        case 0: startPulse(apu, 0, 15, true); break;
        case 1: startPulse(apu, 1, 15, true); break;
        case 2: startWave(apu, timer, 1); break; // the sample, unshifted
        default: startNoise(apu, timer, 15, 15); break;
        }
        const float one = quarter(15);

        apu.write(kNr51, static_cast<u8>(entry.flag << 4)); // left alone
        CHECK(apu.sample().left == doctest::Approx(one));
        CHECK(apu.sample().right == doctest::Approx(0.0f));

        apu.write(kNr51, entry.flag); // right alone
        CHECK(apu.sample().left == doctest::Approx(0.0f));
        CHECK(apu.sample().right == doctest::Approx(one));

        apu.write(kNr51, static_cast<u8>((entry.flag << 4) | entry.flag));
        CHECK(apu.sample().left == doctest::Approx(one));
        CHECK(apu.sample().right == doctest::Approx(one));

        apu.write(kNr51, static_cast<u8>(~((entry.flag << 4) | entry.flag)));
        CHECK(apu.sample().left == doctest::Approx(0.0f));
        CHECK(apu.sample().right == doctest::Approx(0.0f));
    }
}

TEST_CASE("NR50 scales each side by (volume + 1) / 8") {
    // Pan Docs, NR50: bits 6-4 are the left master volume and bits 2-0 the
    // right. "A value of 0 is treated as a volume of 1 (very quiet) and a
    // value of 7 is treated as a volume of 8 (no volume reduction)."
    Apu apu;
    hush(apu);
    startPulse(apu, 0, 15, true);
    const float one = quarter(15);
    for (int left = 0; left < 8; ++left) {
        for (int right = 0; right < 8; ++right) {
            CAPTURE(left);
            CAPTURE(right);
            apu.write(kNr50, static_cast<u8>((left << 4) | right));
            CHECK(apu.sample().left ==
                  doctest::Approx(one * static_cast<float>(left + 1) / 8.0f));
            CHECK(apu.sample().right ==
                  doctest::Approx(one * static_cast<float>(right + 1) / 8.0f));
        }
    }
    // Bit 7 and bit 3 are the Vin mixers. Nothing in the cartridges this
    // emulator runs drives that pin, so setting them changes nothing.
    apu.write(kNr50, 0x77);
    const Apu::Sample without = apu.sample();
    apu.write(kNr50, 0xFF);
    CHECK(apu.sample().left == doctest::Approx(without.left));
    CHECK(apu.sample().right == doctest::Approx(without.right));
}

TEST_CASE("a channel that is off, or whose DAC is off, contributes nothing") {
    SUBCASE("a DAC that goes off takes its channel's contribution with it") {
        Apu apu;
        hush(apu);
        startPulse(apu, 0, 15, true);
        REQUIRE(apu.sample().left == doctest::Approx(quarter(15)));
        apu.write(0xFF12, 0x00);
        CHECK(apu.sample().left == doctest::Approx(0.0f));
        CHECK(apu.sample().right == doctest::Approx(0.0f));
    }
    SUBCASE("a DAC on with no trigger behind it is still silence") {
        Apu apu;
        hush(apu);
        apu.write(0xFF16, 0x40); // NR21: duty 1, high at position 0
        apu.write(0xFF17, 0xF0); // NR22: volume 15, DAC on -- but no trigger
        REQUIRE((apu.read(0xFF26) & 0x02) == 0);
        CHECK(apu.sample().left == doctest::Approx(0.0f));
    }
    SUBCASE("a length counter running out silences the channel") {
        Timer timer;
        Apu apu;
        hush(apu);
        startPulse(apu, 0, 15, true);
        apu.write(0xFF11, 0x7F); // NR11: duty 1 still, one length step left
        apu.write(0xFF14, 0xC0); // length enabled, and triggered again
        REQUIRE(apu.sample().left == doctest::Approx(quarter(15)));
        cycleTo(timer, apu, 0x2000);
        REQUIRE((apu.read(0xFF26) & 0x01) == 0);
        CHECK(apu.sample().left == doctest::Approx(0.0f));
    }
    SUBCASE("a powered-down APU is silent on both sides") {
        Apu apu;
        hush(apu);
        startPulse(apu, 0, 15, true);
        REQUIRE(apu.sample().left == doctest::Approx(quarter(15)));
        powerOff(apu);
        CHECK(apu.sample().left == doctest::Approx(0.0f));
        CHECK(apu.sample().right == doctest::Approx(0.0f));
    }
}
