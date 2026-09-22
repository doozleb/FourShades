#include <doctest/doctest.h>

#include "app/Input.h"
#include "core/GameBoy.h"
#include "core/Joypad.h"

#include <array>
#include <memory>
#include <vector>

using namespace fourshades;

namespace {

// The four values that can reach P1's select bits, written as a program
// would: bit 4 clear selects the d-pad, bit 5 clear selects the action
// buttons, and 0x30 selects neither.
constexpr u8 kSelectBoth = 0x00;
constexpr u8 kSelectDpadOnly = 0x20;
constexpr u8 kSelectActionOnly = 0x10;
constexpr u8 kSelectNeither = 0x30;

// What the low nibble should be, worked out line by line from Pan Docs
// rather than the way Joypad works it out: for each of the four lines, ask
// whether a selected button sitting on it is held, and if so pull it to 0.
// Line n carries the d-pad's bit n and the action group's bit n.
u8 expectedLines(u8 select, u8 pressed) {
    u8 value = 0;
    for (int line = 0; line < 4; ++line) {
        const bool dpadHeld = (select & 0x10) == 0 && (pressed & (1 << line)) != 0;
        const bool actionHeld = (select & 0x20) == 0 && (pressed & (1 << (line + 4))) != 0;
        if (!dpadHeld && !actionHeld) {
            value = static_cast<u8>(value | (1 << line)); // nothing pulls it down
        }
    }
    return value;
}

// A 32 KiB plain ROM with `program` at 0x0100 and a valid header checksum.
// Mirrors tests/test_gameboy.cpp's helper.
std::unique_ptr<GameBoy> makeGameBoy(std::vector<u8> program) {
    std::vector<u8> rom(0x8000, 0x00);
    for (std::size_t i = 0; i < program.size(); ++i) {
        rom[0x0100 + i] = program[i];
    }
    rom[0x0147] = 0x00;
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    rom[0x014D] = sum;
    auto cart = Cartridge::load(std::move(rom), nullptr);
    REQUIRE(cart.has_value());
    return std::make_unique<GameBoy>(std::move(*cart));
}

} // namespace

TEST_CASE("P1 powers on with both groups selected and nothing held") {
    Joypad pad;
    CHECK(pad.read() == 0xCF); // Pan Docs' power-up value for FF00
}

TEST_CASE("bits 7 and 6 read 1 whatever is selected and whatever is held") {
    for (u8 select : {kSelectBoth, kSelectDpadOnly, kSelectActionOnly, kSelectNeither}) {
        for (int pressed = 0; pressed <= 0xFF; ++pressed) {
            Joypad pad;
            pad.write(select);
            pad.setButtons(static_cast<u8>(pressed));
            CAPTURE(select);
            CAPTURE(pressed);
            CHECK((pad.read() & 0xC0) == 0xC0);
        }
    }
}

TEST_CASE("every select bit combination reports its own group's buttons, active low") {
    for (u8 select : {kSelectBoth, kSelectDpadOnly, kSelectActionOnly, kSelectNeither}) {
        for (int pressed = 0; pressed <= 0xFF; ++pressed) {
            Joypad pad;
            pad.write(select);
            pad.setButtons(static_cast<u8>(pressed));
            CAPTURE(select);
            CAPTURE(pressed);
            CHECK((pad.read() & 0x0F) == expectedLines(select, static_cast<u8>(pressed)));
            CHECK((pad.read() & 0x30) == select); // the select bits read back
        }
    }
}

// The cases the table above covers, spelled out, so a wrong nibble says which
// button it got wrong rather than only which mask.
TEST_CASE("the named buttons land on the lines Pan Docs puts them on") {
    Joypad pad;
    pad.write(kSelectDpadOnly);
    pad.setButtons(button::Right);
    CHECK(pad.read() == 0xEE); // line 0 low
    pad.setButtons(button::Left);
    CHECK(pad.read() == 0xED); // line 1
    pad.setButtons(button::Up);
    CHECK(pad.read() == 0xEB); // line 2
    pad.setButtons(button::Down);
    CHECK(pad.read() == 0xE7); // line 3
    pad.setButtons(static_cast<u8>(button::A | button::B | button::Select | button::Start));
    CHECK(pad.read() == 0xEF); // the action group is not selected: nothing

    pad.write(kSelectActionOnly);
    pad.setButtons(button::A);
    CHECK(pad.read() == 0xDE);
    pad.setButtons(button::B);
    CHECK(pad.read() == 0xDD);
    pad.setButtons(button::Select);
    CHECK(pad.read() == 0xDB);
    pad.setButtons(button::Start);
    CHECK(pad.read() == 0xD7);
    pad.setButtons(static_cast<u8>(button::Right | button::Left | button::Up | button::Down));
    CHECK(pad.read() == 0xDF); // the d-pad is not selected
}

TEST_CASE("with neither group selected the low nibble reads 0xF however much is held") {
    Joypad pad;
    pad.write(kSelectNeither);
    CHECK(pad.read() == 0xFF);
    pad.setButtons(0xFF); // every button on the machine
    CHECK(pad.read() == 0xFF);
}

// Pan Docs, INT $60: "If both are selected and, for example, a bit is already
// held Low by an action button, pressing the corresponding direction button
// would make no difference." The two groups share the four lines.
TEST_CASE("with both groups selected the two nibbles share the lines") {
    Joypad pad;
    pad.write(kSelectBoth);
    CHECK(pad.read() == 0xCF);
    pad.setButtons(button::Right);
    CHECK(pad.read() == 0xCE);
    pad.setButtons(button::A); // the action button on the same line
    CHECK(pad.read() == 0xCE);
    pad.setButtons(static_cast<u8>(button::Right | button::A));
    CHECK(pad.read() == 0xCE); // together: no difference, as Pan Docs says
    pad.setButtons(static_cast<u8>(button::Right | button::Start));
    CHECK(pad.read() == 0xC6); // different lines: both low
    pad.setButtons(0xFF);
    CHECK(pad.read() == 0xC0);
}

TEST_CASE("only bits 4 and 5 of a write land") {
    Joypad pad;
    pad.setButtons(button::Down);
    pad.write(0xCF); // the power-up value: select bits clear, everything else set
    CHECK(pad.read() == 0xC7);
    pad.write(0xFF);
    CHECK(pad.read() == 0xFF); // neither group selected now
    pad.write(0x0F); // the low nibble is read-only: this selects both groups
    CHECK(pad.read() == 0xC7);
}

TEST_CASE("the interrupt follows a line falling, not a button moving") {
    SUBCASE("a press on a selected line requests it") {
        Joypad pad;
        pad.write(kSelectDpadOnly);
        CHECK(pad.setButtons(button::Down) == true);
    }
    SUBCASE("a press on a group that is not selected requests nothing") {
        Joypad pad;
        pad.write(kSelectDpadOnly);
        CHECK(pad.setButtons(button::Start) == false);
        CHECK(pad.setButtons(static_cast<u8>(button::Start | button::A | button::B)) == false);
    }
    SUBCASE("a press with neither group selected requests nothing") {
        Joypad pad;
        pad.write(kSelectNeither);
        CHECK(pad.setButtons(0xFF) == false);
    }
    SUBCASE("a second button on a line already low requests nothing") {
        Joypad pad;
        pad.write(kSelectBoth);
        CHECK(pad.setButtons(button::Right) == true);
        CHECK(pad.setButtons(static_cast<u8>(button::Right | button::A)) == false);
    }
    SUBCASE("a press on a different line while one is held requests it") {
        Joypad pad;
        pad.write(kSelectBoth);
        CHECK(pad.setButtons(button::Right) == true);
        CHECK(pad.setButtons(static_cast<u8>(button::Right | button::Down)) == true);
    }
    SUBCASE("a release requests nothing") {
        Joypad pad;
        pad.write(kSelectBoth);
        CHECK(pad.setButtons(button::Right) == true);
        CHECK(pad.setButtons(0x00) == false);
    }
    SUBCASE("selecting a group that already holds a button drops its line") {
        Joypad pad;
        pad.write(kSelectDpadOnly);
        CHECK(pad.setButtons(button::Start) == false); // not selected, no line moved
        CHECK(pad.write(kSelectActionOnly) == true);   // now it is: the line falls
        CHECK(pad.write(kSelectNeither) == false);     // rising again requests nothing
    }
}

TEST_CASE("the machine shows the buttons at FF00 and requests the interrupt in IF") {
    auto gb = makeGameBoy({0x00});
    CHECK(gb->peek(0xFF00) == 0xCF);
    CHECK((gb->peek(0xFF0F) & 0x10) == 0x00); // IF bit 4: the joypad

    gb->setButtons(button::Start);
    CHECK(gb->peek(0xFF00) == 0xC7); // both groups are selected at power-up
    CHECK((gb->peek(0xFF0F) & 0x10) == 0x10);
}

TEST_CASE("a press on a group the program did not select leaves IF alone") {
    // LD A,0x20 / LDH (00),A: select the d-pad only.
    auto gb = makeGameBoy({0x3E, 0x20, 0xE0, 0x00});
    gb->step();
    gb->step();
    REQUIRE(gb->peek(0xFF00) == 0xEF);
    gb->setButtons(button::Start);
    CHECK(gb->peek(0xFF00) == 0xEF);
    CHECK((gb->peek(0xFF0F) & 0x10) == 0x00);
    gb->setButtons(static_cast<u8>(button::Start | button::Down));
    CHECK(gb->peek(0xFF00) == 0xE7);
    CHECK((gb->peek(0xFF0F) & 0x10) == 0x10);
}

TEST_CASE("a program reading FF00 over the bus sees the buttons") {
    // LD A,0x10 / LDH (00),A / LDH A,(00): select the action buttons and read.
    auto gb = makeGameBoy({0x3E, 0x10, 0xE0, 0x00, 0xF0, 0x00});
    gb->setButtons(static_cast<u8>(button::A | button::Down));
    gb->step();
    gb->step();
    gb->step();
    CHECK(gb->cpu().regs.a == 0xDE); // line 0 low for A; Down is not selected
}

TEST_CASE("selecting a group with a button already held requests the interrupt") {
    // LD A,0x20 / LDH (00),A then LD A,0x10 / LDH (00),A, with Start held
    // between the two writes: the second write itself drops line 3.
    auto gb = makeGameBoy({0x3E, 0x20, 0xE0, 0x00, 0x3E, 0x10, 0xE0, 0x00});
    gb->step();
    gb->step();
    gb->setButtons(button::Start);
    REQUIRE((gb->peek(0xFF0F) & 0x10) == 0x00);
    gb->step();
    gb->step();
    CHECK((gb->peek(0xFF0F) & 0x10) == 0x10);
}

namespace {
// Runs `gb` until the CPU is stopped, or gives up. The programs below reach
// STOP well inside the budget.
void runToStop(GameBoy& gb) {
    for (int i = 0; i < 100; ++i) {
        if (gb.cpu().state() == Cpu::State::Stopped) {
            return;
        }
        gb.step();
    }
    FAIL("the CPU never stopped");
}
} // namespace

// Pan Docs, Reducing Power Consumption: "STOP is terminated by one of the P10
// to P13 lines going low."
TEST_CASE("STOP wakes on a press of a selected button") {
    // LD A,0x20 / LDH (00),A / STOP / INC A
    auto gb = makeGameBoy({0x3E, 0x20, 0xE0, 0x00, 0x10, 0x00, 0x3C});
    runToStop(*gb);
    const u16 stoppedAt = gb->cpu().regs.pc;
    CHECK(stoppedAt == 0x0106); // STOP is two bytes

    for (int i = 0; i < 20; ++i) {
        gb->step();
        REQUIRE(gb->cpu().state() == Cpu::State::Stopped);
        REQUIRE(gb->cpu().regs.pc == stoppedAt);
    }

    gb->setButtons(button::Down);
    gb->step();
    CHECK(gb->cpu().state() == Cpu::State::Running);
    CHECK(gb->cpu().regs.pc == stoppedAt); // it resumes where it stopped
    gb->step();
    CHECK(gb->cpu().regs.a == 0x21); // the INC A after STOP ran
}

TEST_CASE("STOP ignores a press on a group the program did not select") {
    // LD A,0x20 / LDH (00),A / STOP: only the d-pad can end this one.
    auto gb = makeGameBoy({0x3E, 0x20, 0xE0, 0x00, 0x10, 0x00, 0x3C});
    runToStop(*gb);
    gb->setButtons(static_cast<u8>(button::Start | button::Select | button::A | button::B));
    for (int i = 0; i < 20; ++i) {
        gb->step();
        REQUIRE(gb->cpu().state() == Cpu::State::Stopped);
    }
    CHECK(gb->cpu().regs.a == 0x20); // the INC A after STOP never ran
}

TEST_CASE("STOP with neither group selected is never ended by a button") {
    // LD A,0x30 / LDH (00),A / STOP: no line can fall at all.
    auto gb = makeGameBoy({0x3E, 0x30, 0xE0, 0x00, 0x10, 0x00, 0x3C});
    runToStop(*gb);
    gb->setButtons(0xFF);
    for (int i = 0; i < 20; ++i) {
        gb->step();
        REQUIRE(gb->cpu().state() == Cpu::State::Stopped);
    }
}

TEST_CASE("STOP wakes with IME off and IE clear: the line is not an interrupt") {
    auto gb = makeGameBoy({0x3E, 0x20, 0xE0, 0x00, 0x10, 0x00, 0x3C});
    runToStop(*gb);
    REQUIRE(gb->cpu().ime == false);
    REQUIRE((gb->peek(0xFFFF) & 0x10) == 0x00); // IE bit 4 clear
    gb->setButtons(button::Up);
    gb->step();
    CHECK(gb->cpu().state() == Cpu::State::Running);
}

// The key mapping: app-side, and the only place in FourShades a keyboard is
// named at all.
namespace {
// A stand-in for SDL_GetKeyboardState's array.
struct Keyboard {
    std::array<bool, SDL_SCANCODE_COUNT> keys{};
    void hold(SDL_Scancode key) { keys[key] = true; }
    u8 mask() const { return app::buttonMask(keys.data(), static_cast<int>(keys.size())); }
};
} // namespace

TEST_CASE("the keyboard maps to the buttons the design asks for") {
    struct Case {
        SDL_Scancode key;
        u8 button;
    };
    const Case cases[] = {
        {SDL_SCANCODE_RIGHT, button::Right},  {SDL_SCANCODE_LEFT, button::Left},
        {SDL_SCANCODE_UP, button::Up},        {SDL_SCANCODE_DOWN, button::Down},
        {SDL_SCANCODE_Z, button::A},          {SDL_SCANCODE_X, button::B},
        {SDL_SCANCODE_RETURN, button::Start}, {SDL_SCANCODE_BACKSPACE, button::Select},
    };
    for (const Case& c : cases) {
        Keyboard keyboard;
        keyboard.hold(c.key);
        CAPTURE(static_cast<int>(c.key));
        CHECK(keyboard.mask() == c.button);
    }
}

TEST_CASE("an unbound key presses nothing, and held keys combine") {
    Keyboard keyboard;
    CHECK(keyboard.mask() == 0x00);
    keyboard.hold(SDL_SCANCODE_Q);
    CHECK(keyboard.mask() == 0x00);
    keyboard.hold(SDL_SCANCODE_LEFT);
    keyboard.hold(SDL_SCANCODE_Z);
    CHECK(keyboard.mask() == (button::Left | button::A));
}

TEST_CASE("a missing or short keyboard array holds nothing") {
    CHECK(app::buttonMask(nullptr, 0) == 0x00);
    std::array<bool, SDL_SCANCODE_COUNT> keys{};
    keys[SDL_SCANCODE_RIGHT] = true;
    CHECK(app::buttonMask(keys.data(), 0) == 0x00);
}
