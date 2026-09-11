# FourShades piece 2: the machine and the test-ROM scoreboard. Implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A headless Game Boy (memory map, MBC1 cartridge, timer, interrupts,
serial, OAM DMA, LCD timing skeleton, power-on state) plus a runner that scores
it against the Shootout's 167 original-Game-Boy test ROMs and publishes
"test roms N / 167" with a per-group table.

**Architecture:** `GameBoy` implements the existing `Bus`, so the CPU is
unchanged apart from interrupt handling. Each bus call first advances the timer,
serial, LCD skeleton and DMA by one M-cycle, then does the access. The CPU
checks for interrupts through two cycle-free `Bus` members, so the
SingleStepTests cycle counts can't change. The test-ROM harness (`tools/roms/`)
downloads pinned ROMs, runs each one headless, and decides pass or fail only
from each author's own signal.

**Tech Stack:** C++20, MSVC, CMake + Ninja via `tools\dev.cmd`, doctest,
nlohmann/json (harness only), Python 3.12 standard library.

**Spec:** `docs/superpowers/specs/2026-09-11-machine-test-roms-design.md`

## Global Constraints

- Everything from piece 1's constraints still holds. In particular:
  - Windows only; C++20; build through `.\tools\dev.cmd` from PowerShell in `C:\GameMode`.
  - Stage explicit paths only.
  - Commit trailer `Co-Authored-By:` naming the model that actually wrote the commit.
  - Never hand-edit the scoreboard, manifests or downloaded data.
  - Pan Docs over tests, with divergences recorded in `docs/known-divergences.md`.
- **The SST score must stay exactly 499/500 (only `10` failing, on `pc`) after every task.** Run `.\build\release\tools\sst\sst_runner.exe` before every commit that touches `src/core`.
- The core (`src/core`) must not mention (whole word, case-insensitive): `sst`, `json`, `fopen`, `ifstream`, `ofstream`, `fstream`, `singlesteptests`, `blargg`, `mooneye`, `shootout`, `passed`, `failed`, `fibonacci`. It also must not contain the byte signature `DE B0 61` in any spelling.
- The core gets no test-only hooks. The harness observes through public state only: `Cpu::regs`, `GameBoy::peek`, `GameBoy::serialOutput`, `GameBoy::cycles`.
- Shootout pin: `gbdev/GBEmulatorShootout` commit `38b926bdbc26993d1b4c43e97979ecc66287bf02`. The denominator is exactly **167**.
- One emulated second = 1,048,576 M-cycles (4,194,304 T-cycles ÷ 4).
- Time limit per test = max(2 × Shootout runtime, runtime + 5) emulated seconds, from the formula only and never tuned per test.
- Interrupt bits: VBlank 0x01, LCD 0x02, Timer 0x04, Serial 0x08, Joypad 0x10. Vectors are 0x40 + 8 × bit.

## Hardware facts used (Pan Docs, fetched 2026-09-11)

- **Timer:**
  - The system counter increases 4 T-cycles every M-cycle, and DIV is its high byte.
  - TIMA ticks on a falling edge of `(TAC bit 2) AND (counter bit N)`, where N is 9, 3, 5, 7 for TAC & 3 = 0, 1, 2, 3.
  - Writing DIV zeroes the counter, and can produce that falling edge. Writing TAC can too, including by disabling the timer on DMG.
  - On overflow, TIMA reads 0x00 for one M-cycle ("cycle A"); on the next M-cycle ("cycle B") TIMA = TMA and IF bit 2 is set.
  - Writing TIMA during cycle A cancels the reload and the interrupt. Writing TIMA during cycle B is ignored. Writing TMA during cycle B also lands in TIMA.
- **Interrupt dispatch:** 2 wait M-cycles, push PC (2 M-cycles), set PC (1 M-cycle) — 5 in total. IME is cleared and the IF bit acknowledged. The lowest set bit has the highest priority.
- **HALT:**
  - The CPU wakes when `IE & IF & 0x1F != 0`, whatever IME is.
  - With IME=0 and an interrupt already pending, HALT doesn't halt and the next opcode fetch doesn't increment PC (the HALT bug).
  - EI then a bugged HALT: the interrupt returns to the HALT itself.
- **Serial:** writing SC with bits 7 and 0 set starts an internal-clock transfer at 8192 Hz, one bit per 128 M-cycles, which is a falling edge of system-counter bit 8. The bits shifted in are 1 with no partner. After 8 bits, SC bit 7 clears and IF bit 3 is set.
- **OAM DMA:** writing FF46 copies XX00–XX9F to FE00–FE9F, one byte per M-cycle for 160 M-cycles. On DMG the CPU can only use FF00–FFFF meanwhile. Sources E000 and up read 0x2000 lower.
- **MBC1:**
  - Registers: 0000–1FFF RAM enable (low nibble 0xA); 2000–3FFF 5-bit bank (0 → 1, judged on the full 5 bits); 4000–5FFF 2-bit register; 6000–7FFF mode.
  - Mode 1 applies the 2-bit register to 0000–3FFF (as bits 5–6) and to RAM.
  - The bank is masked to the ROM size.
  - RAM reads give 0xFF while disabled.
- **Power-on (DMG after boot, at PC=0x0100):**
  - CPU: A=01; F=0xB0 if the header checksum byte (0x014D) is non-zero, else 0x80; B=00 C=13 D=00 E=D8 H=01 L=4D SP=FFFE.
  - I/O: P1=CF SB=00 SC=7E DIV=AB (system counter 0xABCC) TIMA=00 TMA=00 TAC=F8 IF=E1 LCDC=91 STAT=85 LY=00 BGP=FC DMA=FF.
- **Memory map:** FEA0–FEFF reads 0x00 on DMG. Unmapped I/O reads 0xFF. Echo E000–FDFF mirrors C000–DDFF.

## The 167 tests (Shootout `testroms/*.py`, active, no `model=CGB`/`SGB`)

| Suite file | DMG tests | Method | Needs (piece) |
|---|---|---|---|
| blargg.py | 38 | blargg | cpu_instrs, instr_timing, mem_timing ×2, halt_bug (2); oam_bug (3); dmg_sound (5) |
| mooneye.py | 95 | mooneye (sprite_priority: screenshot) | mostly 2; ppu (3); mbc2/mbc5 (4) |
| mealybug.py (`dmg(...)` entries) | 24 | screenshot | 3 |
| acid.py | 2 | screenshot | 3 |
| ashiepaws.py | 2 | screenshot | 3 |
| cpp.py | 3 | screenshot | MBC3/RTC, 4 |
| daid.py | 3 | screenshot | 3 |

---

### Task 1: Interrupts in the CPU

**Files:**
- Modify: `src/core/Bus.h`, `src/core/Cpu.h`, `src/core/Cpu.cpp`, `tools/sst/RecordingBus.h`
- Test: `tests/test_cpu_interrupts.cpp`

**Interfaces:**
- Produces: `Bus::pendingInterrupts() -> u8` (IE & IF & 0x1F) and `Bus::acknowledgeInterrupt(int bit)`. Both are pure virtual and neither costs a cycle.
- Produces: `sst::RecordingBus::setPendingInterrupts(u8)`. It defaults to 0, and `reset()` clears it.
- Produces: `Cpu::step()` dispatches interrupts, wakes from HALT, and emulates the HALT bug.

- [ ] **Step 1: Write the failing tests**

`tests/test_cpu_interrupts.cpp`:

```cpp
#include <doctest/doctest.h>

#include "core/Cpu.h"
#include "sst/RecordingBus.h"

#include <initializer_list>

using namespace fourshades;
using sst::Cycle;
using sst::CycleKind;
using sst::RecordingBus;

namespace {
void load(RecordingBus& bus, std::initializer_list<u8> program) {
    u16 address = 0x0100;
    for (const u8 byte : program) {
        bus.poke(address++, byte);
    }
}

Cpu makeCpu(RecordingBus& bus) {
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.regs.sp = 0xFFFE;
    return cpu;
}
} // namespace

TEST_CASE("an enabled interrupt is dispatched in 5 M-cycles") {
    RecordingBus bus;
    load(bus, {0x00});
    Cpu cpu = makeCpu(bus);
    cpu.ime = true;
    bus.setPendingInterrupts(0x04); // timer
    cpu.step();
    CHECK(cpu.regs.pc == 0x0050);
    CHECK(cpu.regs.sp == 0xFFFC);
    CHECK(bus.peek(0xFFFD) == 0x01); // return address high byte
    CHECK(bus.peek(0xFFFC) == 0x00); // low byte
    CHECK_FALSE(cpu.ime);
    CHECK(bus.pendingInterrupts() == 0x00); // acknowledged
    REQUIRE(bus.log().size() == 5);
    CHECK(bus.log()[0].kind == CycleKind::Idle);
    CHECK(bus.log()[1].kind == CycleKind::Idle);
    CHECK(bus.log()[2] == Cycle{0xFFFD, 0x01, CycleKind::Write});
    CHECK(bus.log()[3] == Cycle{0xFFFC, 0x00, CycleKind::Write});
    CHECK(bus.log()[4].kind == CycleKind::Idle);
}

TEST_CASE("the lowest pending bit is serviced first") {
    RecordingBus bus;
    load(bus, {0x00});
    Cpu cpu = makeCpu(bus);
    cpu.ime = true;
    bus.setPendingInterrupts(0x06); // LCD and timer
    cpu.step();
    CHECK(cpu.regs.pc == 0x0048);
    CHECK(bus.pendingInterrupts() == 0x04);
}

TEST_CASE("nothing is dispatched while IME is off") {
    RecordingBus bus;
    load(bus, {0x00});
    Cpu cpu = makeCpu(bus);
    bus.setPendingInterrupts(0x01);
    cpu.step();
    CHECK(cpu.regs.pc == 0x0101);
    CHECK(bus.pendingInterrupts() == 0x01);
}

TEST_CASE("EI lets the instruction after it run before an interrupt") {
    RecordingBus bus;
    load(bus, {0xFB, 0x00, 0x00}); // EI, NOP, NOP
    Cpu cpu = makeCpu(bus);
    bus.setPendingInterrupts(0x01);
    cpu.step(); // EI
    CHECK(cpu.regs.pc == 0x0101);
    cpu.step(); // NOP runs; IME turns on at its end
    CHECK(cpu.regs.pc == 0x0102);
    cpu.step(); // now the interrupt
    CHECK(cpu.regs.pc == 0x0040);
    CHECK(bus.peek(0xFFFD) == 0x01);
    CHECK(bus.peek(0xFFFC) == 0x02);
}

TEST_CASE("HALT wakes on a pending interrupt even with IME off") {
    RecordingBus bus;
    load(bus, {0x76, 0x00}); // HALT, NOP
    Cpu cpu = makeCpu(bus);
    cpu.step();
    CHECK(cpu.state() == Cpu::State::Halted);
    cpu.step();
    CHECK(cpu.state() == Cpu::State::Halted);
    bus.setPendingInterrupts(0x04);
    cpu.step(); // wakes, and with IME off simply runs the NOP
    CHECK(cpu.state() == Cpu::State::Running);
    CHECK(cpu.regs.pc == 0x0102);
    CHECK(bus.pendingInterrupts() == 0x04); // not serviced, so not acknowledged
}

TEST_CASE("the HALT bug reads the next byte twice") {
    RecordingBus bus;
    load(bus, {0x76, 0x3C, 0x00}); // HALT, INC A, NOP
    Cpu cpu = makeCpu(bus);
    bus.setPendingInterrupts(0x04); // pending before HALT, IME off
    cpu.step();
    CHECK(cpu.state() == Cpu::State::Running); // HALT didn't halt
    CHECK(cpu.regs.pc == 0x0101);
    cpu.step(); // INC A, but PC doesn't advance past it
    CHECK(cpu.regs.a == 1);
    CHECK(cpu.regs.pc == 0x0101);
    cpu.step(); // INC A again, normally this time
    CHECK(cpu.regs.a == 2);
    CHECK(cpu.regs.pc == 0x0102);
}

TEST_CASE("EI then a bugged HALT returns to the HALT") {
    RecordingBus bus;
    load(bus, {0xFB, 0x76, 0x00}); // EI, HALT, NOP
    Cpu cpu = makeCpu(bus);
    bus.setPendingInterrupts(0x01);
    cpu.step(); // EI
    cpu.step(); // HALT with IME still 0: halt bug; IME turns on after it
    cpu.step(); // dispatch
    CHECK(cpu.regs.pc == 0x0040);
    CHECK(bus.peek(0xFFFD) == 0x01);
    CHECK(bus.peek(0xFFFC) == 0x01); // returns to the HALT at 0x0101
}
```

- [ ] **Step 2: Run to verify it fails**

```powershell
.\tools\dev.cmd cmake --build --preset release
```

Expected: the build FAILS because `setPendingInterrupts` doesn't exist (and `Cpu` has no interrupt handling).

- [ ] **Step 3: Implement**

Replace the `Bus` class body in `src/core/Bus.h` with:

```cpp
class Bus {
public:
    virtual ~Bus() = default;
    virtual u8 read(u16 address) = 0;
    virtual void write(u16 address, u8 value) = 0;
    // A cycle with no memory access (16-bit arithmetic, a taken branch, ...).
    virtual void idle() = 0;

    // The interrupt lines. Neither costs a cycle: the CPU samples them between
    // M-cycles rather than reading IE and IF over the bus.
    virtual u8 pendingInterrupts() = 0;             // IE & IF & 0x1F
    virtual void acknowledgeInterrupt(int bit) = 0; // clears that IF bit
};
```

In `tools/sst/RecordingBus.h`, add inside the class (public section, after `reset()`):

```cpp
    // Interrupt lines for unit tests. SingleStepTests never set them, so every
    // SST test sees "nothing pending", exactly as before interrupts existed.
    void setPendingInterrupts(u8 bits) { pending_ = bits; }
    u8 pendingInterrupts() override { return pending_; }
    void acknowledgeInterrupt(int bit) override { pending_ = static_cast<u8>(pending_ & ~(1 << bit)); }
```

In `reset()`, add `pending_ = 0;` as its last statement. In the private members, add `u8 pending_ = 0;`.

In `src/core/Cpu.h`, add to the private section, after `bool executeMisc(u8 opcode);`:

```cpp
    void dispatchInterrupt();
```

and add this member after `bool unimplemented_ = false;`:

```cpp
    bool haltBug_ = false; // the next opcode fetch doesn't advance PC
```

In `src/core/Cpu.cpp`, replace `Cpu::step()` and `Cpu::fetch8()` with:

```cpp
void Cpu::step() {
    if (state_ == State::Halted) {
        if (bus_.pendingInterrupts() == 0) {
            bus_.idle();
            return;
        }
        state_ = State::Running; // Pan Docs "halt": wakes whatever IME is
    }
    if (state_ != State::Running) {
        bus_.idle();
        return;
    }
    if (ime && bus_.pendingInterrupts() != 0) {
        dispatchInterrupt();
        return;
    }
    execute(fetch8());
    // EI sets the delay to 2, so IME turns on at the end of the instruction
    // after EI. DI zeroes it, cancelling a pending EI.
    if (imeDelay_ > 0 && --imeDelay_ == 0) {
        ime = true;
    }
}

u8 Cpu::fetch8() {
    const u8 value = bus_.read(regs.pc);
    if (haltBug_) {
        haltBug_ = false; // Pan Docs "halt bug": this fetch doesn't advance PC
    } else {
        regs.pc = static_cast<u16>(regs.pc + 1);
    }
    return value;
}
```

Add after `Cpu::step()`:

```cpp
// Pan Docs "Interrupts": two wait M-cycles, push PC (two), set PC (one).
void Cpu::dispatchInterrupt() {
    ime = false;
    imeDelay_ = 0;
    if (haltBug_) {
        // EI then a bugged HALT: the handler returns to the HALT itself.
        haltBug_ = false;
        regs.pc = static_cast<u16>(regs.pc - 1);
    }
    bus_.idle();
    bus_.idle();
    regs.sp = static_cast<u16>(regs.sp - 1);
    bus_.write(regs.sp, hi(regs.pc));
    // The vector is chosen after the high byte is pushed. If that push
    // overwrote IE (SP was 0x0000), nothing may be pending any more, and the
    // CPU then continues at 0x0000 with nothing acknowledged.
    const u8 pending = bus_.pendingInterrupts();
    regs.sp = static_cast<u16>(regs.sp - 1);
    bus_.write(regs.sp, lo(regs.pc));
    if (pending == 0) {
        regs.pc = 0x0000;
    } else {
        int bit = 0;
        while ((pending & (1 << bit)) == 0) {
            ++bit;
        }
        bus_.acknowledgeInterrupt(bit);
        regs.pc = static_cast<u16>(0x40 + 8 * bit);
    }
    bus_.idle();
}
```

In `Cpu::executeMisc`, replace the HALT case with:

```cpp
    case 0x76: // HALT
        if (!ime && bus_.pendingInterrupts() != 0) {
            haltBug_ = true; // Pan Docs "halt bug": doesn't halt, next fetch repeats
        } else {
            state_ = State::Halted;
        }
        return true;
```

- [ ] **Step 4: Run to verify it passes, and that SST is unchanged**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\build\release\fourshades_tests.exe
.\build\release\tools\sst\sst_runner.exe
python tools/scoreboard.py check build/sst-results.json
python tools/check_core_isolation.py
```

Expected:
- doctest: `test cases: 39 | 39 passed` (32 existing + 7 new);
- the runner: `CPU instructions: 499 / 500 passing` with only `FAIL 10`;
- `scoreboard matches the test results`;
- `core isolation check passed`.

- [ ] **Step 5: Commit**

```powershell
git add src/core/Bus.h src/core/Cpu.h src/core/Cpu.cpp tools/sst/RecordingBus.h tests/test_cpu_interrupts.cpp
git commit -m "feat(cpu): interrupt dispatch, HALT wake-up and the HALT bug" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
```

---

### Task 2: Cartridge (plain ROM and MBC1)

**Files:**
- Create: `src/core/Cartridge.h`, `src/core/Cartridge.cpp`
- Modify: `CMakeLists.txt` (add `src/core/Cartridge.cpp` to `fourshades_core`)
- Test: `tests/test_cartridge.cpp`

**Interfaces:**
- Produces: `class fourshades::Cartridge` with:
  - `static std::optional<Cartridge> load(std::vector<u8> rom, std::string* error)`;
  - `u8 read(u16 address) const` (0000–7FFF, A000–BFFF);
  - `void write(u16 address, u8 value)`;
  - `Kind kind() const` (`Kind::RomOnly`, `Kind::Mbc1`);
  - `bool headerChecksumOk() const`;
  - `u8 headerChecksum() const`.

- [ ] **Step 1: Write the failing tests**

`tests/test_cartridge.cpp`:

```cpp
#include <doctest/doctest.h>

#include "core/Cartridge.h"

#include <string>
#include <vector>

using namespace fourshades;

namespace {
// A ROM of `banks` 16 KiB banks whose every bank starts with its own number,
// with a valid header of the given type, ROM-size and RAM-size codes.
std::vector<u8> makeRom(std::size_t banks, u8 type, u8 romCode, u8 ramCode) {
    std::vector<u8> rom(banks * 0x4000, 0x00);
    for (std::size_t bank = 0; bank < banks; ++bank) {
        rom[bank * 0x4000] = static_cast<u8>(bank);
        rom[bank * 0x4000 + 1] = static_cast<u8>(bank >> 8);
    }
    rom[0x0147] = type;
    rom[0x0148] = romCode;
    rom[0x0149] = ramCode;
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    rom[0x014D] = sum;
    return rom;
}

Cartridge loadOk(std::vector<u8> rom) {
    std::string error;
    auto cart = Cartridge::load(std::move(rom), &error);
    INFO(error);
    REQUIRE(cart.has_value());
    return *cart;
}
} // namespace

TEST_CASE("a plain 32 KiB ROM maps both halves and ignores writes") {
    Cartridge cart = loadOk(makeRom(2, 0x00, 0x00, 0x00));
    CHECK(cart.kind() == Cartridge::Kind::RomOnly);
    CHECK(cart.headerChecksumOk());
    CHECK(cart.read(0x0000) == 0);
    CHECK(cart.read(0x4000) == 1);
    cart.write(0x2000, 0x05);
    CHECK(cart.read(0x4000) == 1);
    CHECK(cart.read(0xA000) == 0xFF); // no cartridge RAM
}

TEST_CASE("loading rejects tiny images and unsupported controllers") {
    std::string error;
    CHECK_FALSE(Cartridge::load(std::vector<u8>(0x100, 0), &error).has_value());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(Cartridge::load(makeRom(2, 0x13, 0x00, 0x00), &error).has_value()); // MBC3
    CHECK(error.find("0x13") != std::string::npos);
}

TEST_CASE("a bad header checksum is reported but not fatal") {
    auto rom = makeRom(2, 0x00, 0x00, 0x00);
    rom[0x014D] ^= 0xFF;
    Cartridge cart = loadOk(rom);
    CHECK_FALSE(cart.headerChecksumOk());
}

TEST_CASE("MBC1 switches ROM banks, and bank 0 selects bank 1") {
    Cartridge cart = loadOk(makeRom(32, 0x01, 0x04, 0x00)); // 512 KiB
    CHECK(cart.kind() == Cartridge::Kind::Mbc1);
    CHECK(cart.read(0x4000) == 1);
    cart.write(0x2000, 0x05);
    CHECK(cart.read(0x4000) == 5);
    cart.write(0x2000, 0x00);
    CHECK(cart.read(0x4000) == 1);
    cart.write(0x2000, 0xE3); // only the low 5 bits count
    CHECK(cart.read(0x4000) == 3);
    CHECK(cart.read(0x0000) == 0);
}

TEST_CASE("MBC1 masks the bank number to the ROM size") {
    Cartridge cart = loadOk(makeRom(8, 0x01, 0x02, 0x00)); // 128 KiB = 8 banks
    cart.write(0x2000, 0x09);
    CHECK(cart.read(0x4000) == 1); // 9 & 7
    cart.write(0x2000, 0x10); // 0x10 & 7 = 0, and 0x10 isn't 0, so bank 0 appears here
    CHECK(cart.read(0x4000) == 0);
}

TEST_CASE("MBC1 upper bits reach banks above 0x1F, and mode 1 remaps 0000-3FFF") {
    Cartridge cart = loadOk(makeRom(128, 0x01, 0x06, 0x00)); // 2 MiB
    cart.write(0x4000, 0x01);
    cart.write(0x2000, 0x02);
    CHECK(cart.read(0x4000) == 0x22);
    CHECK(cart.read(0x0000) == 0); // mode 0: fixed bank 0
    cart.write(0x6000, 0x01);
    CHECK(cart.read(0x0000) == 0x20); // mode 1: bank 0x20
    cart.write(0x2000, 0x00);
    CHECK(cart.read(0x4000) == 0x21); // 0x20 is unreachable here
}

TEST_CASE("MBC1 RAM needs enabling and banks only in mode 1") {
    Cartridge cart = loadOk(makeRom(4, 0x03, 0x01, 0x03)); // 64 KiB ROM, 32 KiB RAM
    CHECK(cart.read(0xA000) == 0xFF); // disabled
    cart.write(0xA000, 0x12);
    cart.write(0x0000, 0x0A); // enable
    CHECK(cart.read(0xA000) == 0x00); // the earlier write was ignored
    cart.write(0xA000, 0x34);
    CHECK(cart.read(0xA000) == 0x34);
    cart.write(0x4000, 0x02); // RAM bank 2, but mode 0 locks bank 0
    CHECK(cart.read(0xA000) == 0x34);
    cart.write(0x6000, 0x01); // mode 1: bank 2 is fresh
    CHECK(cart.read(0xA000) == 0x00);
    cart.write(0xA000, 0x56);
    cart.write(0x4000, 0x00);
    CHECK(cart.read(0xA000) == 0x34);
    cart.write(0x0000, 0x00); // disable
    CHECK(cart.read(0xA000) == 0xFF);
}

TEST_CASE("an MBC1+RAM header claiming no RAM still gets 8 KiB") {
    Cartridge cart = loadOk(makeRom(2, 0x02, 0x00, 0x00));
    cart.write(0x0000, 0x0A);
    cart.write(0xBFFF, 0x77);
    CHECK(cart.read(0xBFFF) == 0x77);
}
```

- [ ] **Step 2: Run to verify it fails**

```powershell
.\tools\dev.cmd cmake --build --preset release
```

Expected: FAIL, `cannot open include file: 'core/Cartridge.h'`.

- [ ] **Step 3: Implement**

`src/core/Cartridge.h`:

```cpp
#pragma once

#include "core/Types.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace fourshades {

// A cartridge: the ROM image plus its memory bank controller. Supports plain
// ROMs (type 0x00) and MBC1 (0x01-0x03), per Pan Docs "MBC1". Other
// controllers arrive in piece 4.
class Cartridge {
public:
    enum class Kind { RomOnly, Mbc1 };

    // Returns nullopt, with a message in *error, for images that are too small
    // or use a controller FourShades doesn't support yet.
    static std::optional<Cartridge> load(std::vector<u8> rom, std::string* error);

    u8 read(u16 address) const;        // 0000-7FFF and A000-BFFF
    void write(u16 address, u8 value); // MBC registers and cartridge RAM

    Kind kind() const { return kind_; }
    bool headerChecksumOk() const { return headerChecksumOk_; }
    u8 headerChecksum() const { return rom_[0x014D]; }

private:
    Cartridge() = default;
    std::size_t romOffset(u16 address) const;
    std::size_t ramOffset(u16 address) const;

    std::vector<u8> rom_;
    std::vector<u8> ram_;
    Kind kind_ = Kind::RomOnly;
    bool headerChecksumOk_ = false;
    std::size_t romBanks_ = 2; // 16 KiB each, always a power of two
    std::size_t ramBanks_ = 0; // 8 KiB each
    bool ramEnabled_ = false;
    u8 bankLow_ = 0;           // 2000-3FFF, 5 bits; 0 acts as 1
    u8 bankHigh_ = 0;          // 4000-5FFF, 2 bits
    bool mode1_ = false;       // 6000-7FFF
};

} // namespace fourshades
```

`src/core/Cartridge.cpp`:

```cpp
#include "core/Cartridge.h"

namespace fourshades {

namespace {

constexpr std::size_t kRomBank = 0x4000;
constexpr std::size_t kRamBank = 0x2000;

std::string hexByte(u8 value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    return std::string("0x") + digits[value >> 4] + digits[value & 0x0F];
}

std::size_t ramBanksFor(u8 code) {
    switch (code) {
    case 0x03: return 4;
    case 0x04: return 16;
    case 0x05: return 8;
    default: return 1; // 0x02 is 8 KiB; 0x00/0x01 on a RAM cartridge also get 8 KiB
    }
}

} // namespace

std::optional<Cartridge> Cartridge::load(std::vector<u8> rom, std::string* error) {
    const auto fail = [&](const std::string& message) -> std::optional<Cartridge> {
        if (error != nullptr) {
            *error = message;
        }
        return std::nullopt;
    };
    if (rom.size() < 0x8000) {
        return fail("ROM image is smaller than 32 KiB");
    }
    Cartridge cart;
    const u8 type = rom[0x0147];
    switch (type) {
    case 0x00: cart.kind_ = Kind::RomOnly; break;
    case 0x01: case 0x02: case 0x03: cart.kind_ = Kind::Mbc1; break;
    default: return fail("unsupported cartridge type " + hexByte(type));
    }
    const u8 sizeCode = rom[0x0148];
    if (sizeCode > 0x08) {
        return fail("unsupported ROM size code " + hexByte(sizeCode));
    }
    // The header's declared size is the truth: pad short dumps, drop excess.
    rom.resize(std::size_t{0x8000} << sizeCode, 0xFF);
    cart.romBanks_ = rom.size() / kRomBank;
    if (type == 0x02 || type == 0x03) {
        cart.ramBanks_ = ramBanksFor(rom[0x0149]);
        cart.ram_.assign(cart.ramBanks_ * kRamBank, 0x00);
    }
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    cart.headerChecksumOk_ = sum == rom[0x014D];
    cart.rom_ = std::move(rom);
    return cart;
}

std::size_t Cartridge::romOffset(u16 address) const {
    std::size_t bank = 0;
    if (kind_ == Kind::Mbc1) {
        if (address < 0x4000) {
            bank = mode1_ ? std::size_t{bankHigh_} << 5 : 0;
        } else {
            bank = (std::size_t{bankHigh_} << 5) | (bankLow_ == 0 ? 1u : bankLow_);
        }
    } else if (address >= 0x4000) {
        bank = 1;
    }
    bank &= romBanks_ - 1;
    return bank * kRomBank + (address & 0x3FFF);
}

std::size_t Cartridge::ramOffset(u16 address) const {
    const std::size_t bank = mode1_ ? (bankHigh_ & (ramBanks_ - 1)) : 0;
    return bank * kRamBank + (address - 0xA000);
}

u8 Cartridge::read(u16 address) const {
    if (address < 0x8000) {
        return rom_[romOffset(address)];
    }
    if (address >= 0xA000 && address < 0xC000 && ramEnabled_ && !ram_.empty()) {
        return ram_[ramOffset(address)];
    }
    return 0xFF;
}

void Cartridge::write(u16 address, u8 value) {
    if (kind_ != Kind::Mbc1) {
        return;
    }
    if (address < 0x2000) {
        ramEnabled_ = (value & 0x0F) == 0x0A;
    } else if (address < 0x4000) {
        bankLow_ = static_cast<u8>(value & 0x1F);
    } else if (address < 0x6000) {
        bankHigh_ = static_cast<u8>(value & 0x03);
    } else if (address < 0x8000) {
        mode1_ = (value & 0x01) != 0;
    } else if (address >= 0xA000 && address < 0xC000 && ramEnabled_ && !ram_.empty()) {
        ram_[ramOffset(address)] = value;
    }
}

} // namespace fourshades
```

In `CMakeLists.txt`, add `src/core/Cartridge.cpp` as a new line inside `add_library(fourshades_core STATIC ...)`, after `src/core/CpuCb.cpp`.

- [ ] **Step 4: Run to verify it passes**

```powershell
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
.\build\release\fourshades_tests.exe
python tools/check_core_isolation.py
```

Expected: doctest `test cases: 47 | 47 passed` (39 + 8), no build warnings, and `core isolation check passed`.

- [ ] **Step 5: Commit**

```powershell
git add src/core/Cartridge.h src/core/Cartridge.cpp CMakeLists.txt tests/test_cartridge.cpp
git commit -m "feat(core): cartridge loading with plain ROM and MBC1" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
```

---

### Task 3: Timer

**Files:**
- Create: `src/core/Timer.h`, `src/core/Timer.cpp`
- Modify: `CMakeLists.txt` (add `src/core/Timer.cpp`)
- Test: `tests/test_timer.cpp`

**Interfaces:**
- Produces: `class fourshades::Timer` with:
  - `bool tick()`: one M-cycle; returns true when this cycle requests the timer interrupt;
  - `u8 read(u16 address) const` and `void write(u16 address, u8 value)` for FF04–FF07;
  - `u16 counter() const` and `void setCounter(u16)`: the 16-bit system counter.

- [ ] **Step 1: Write the failing tests**

`tests/test_timer.cpp`:

```cpp
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
```

- [ ] **Step 2: Run to verify it fails**

```powershell
.\tools\dev.cmd cmake --build --preset release
```

Expected: FAIL, `cannot open include file: 'core/Timer.h'`.

- [ ] **Step 3: Implement**

`src/core/Timer.h`:

```cpp
#pragma once

#include "core/Types.h"

namespace fourshades {

// DIV, TIMA, TMA and TAC, per Pan Docs "Timer and Divider Registers" and
// "Timer obscure behaviour". The 16-bit system counter gains 4 (T-cycles)
// every M-cycle; DIV is its high byte. TIMA ticks on a falling edge of
// (TAC enable AND the counter bit TAC selects), so writes to DIV and TAC can
// tick it too.
class Timer {
public:
    // One M-cycle. Returns true when this cycle requests the timer interrupt.
    bool tick();

    u8 read(u16 address) const;         // FF04-FF07
    void write(u16 address, u8 value);  // FF04-FF07

    u16 counter() const { return counter_; }
    void setCounter(u16 value) { counter_ = value; }

private:
    bool input() const;
    void increment();

    u16 counter_ = 0;
    u8 tima_ = 0;
    u8 tma_ = 0;
    u8 tac_ = 0xF8;
    bool overflowed_ = false; // TIMA overflowed this cycle ("cycle A")
    bool reloading_ = false;  // this cycle is the reload cycle ("cycle B")
};

} // namespace fourshades
```

`src/core/Timer.cpp`:

```cpp
#include "core/Timer.h"

namespace fourshades {

bool Timer::tick() {
    reloading_ = false;
    bool interrupt = false;
    if (overflowed_) {
        overflowed_ = false;
        tima_ = tma_;
        reloading_ = true;
        interrupt = true;
    }
    const bool before = input();
    counter_ = static_cast<u16>(counter_ + 4);
    if (before && !input()) {
        increment();
    }
    return interrupt;
}

u8 Timer::read(u16 address) const {
    switch (address) {
    case 0xFF04: return hi(counter_);
    case 0xFF05: return tima_;
    case 0xFF06: return tma_;
    default: return static_cast<u8>(tac_ | 0xF8);
    }
}

void Timer::write(u16 address, u8 value) {
    switch (address) {
    case 0xFF04: {
        const bool before = input();
        counter_ = 0;
        if (before && !input()) {
            increment();
        }
        break;
    }
    case 0xFF05:
        if (!reloading_) {
            tima_ = value;
            overflowed_ = false; // a write in cycle A cancels the reload
        }
        break;
    case 0xFF06:
        tma_ = value;
        if (reloading_) {
            tima_ = value;
        }
        break;
    default: {
        const bool before = input();
        tac_ = static_cast<u8>(value | 0xF8);
        if (before && !input()) {
            increment();
        }
        break;
    }
    }
}

bool Timer::input() const {
    static constexpr int kBit[4] = {9, 3, 5, 7};
    return (tac_ & 0x04) != 0 && ((counter_ >> kBit[tac_ & 0x03]) & 1) != 0;
}

void Timer::increment() {
    if (tima_ == 0xFF) {
        tima_ = 0x00;
        overflowed_ = true;
    } else {
        ++tima_;
    }
}

} // namespace fourshades
```

In `CMakeLists.txt`, add `src/core/Timer.cpp` to `fourshades_core`, after `src/core/Cartridge.cpp`.

- [ ] **Step 4: Run to verify it passes**

```powershell
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
.\build\release\fourshades_tests.exe
python tools/check_core_isolation.py
```

Expected: doctest `test cases: 55 | 55 passed` (47 + 8), no warnings, and the isolation check passes.

- [ ] **Step 5: Commit**

```powershell
git add src/core/Timer.h src/core/Timer.cpp CMakeLists.txt tests/test_timer.cpp
git commit -m "feat(core): timer with falling-edge increments and the TIMA reload delay" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
```

---

### Task 4: Serial port and the LCD timing skeleton

**Files:**
- Create: `src/core/Serial.h`, `src/core/Serial.cpp`, `src/core/LcdTiming.h`, `src/core/LcdTiming.cpp`
- Modify: `CMakeLists.txt` (add both `.cpp` files)
- Test: `tests/test_serial.cpp`, `tests/test_lcd_timing.cpp`

**Interfaces:**
- Produces: `class fourshades::Serial` with:
  - `bool tick(u16 counterBefore, u16 counterAfter)`: returns true when a transfer completes;
  - `u8 read(u16) const` and `void write(u16, u8)` for FF01–FF02;
  - `const std::vector<u8>& sent() const`: every byte whose transfer was started, in order.
- Produces: `class fourshades::LcdTiming` with:
  - `u8 tick()`: returns the IF bits requested this M-cycle (0 or 0x01);
  - `u8 read(u16) const` and `void write(u16, u8)` for FF40–FF45 and FF47–FF4B.

- [ ] **Step 1: Write the failing tests**

`tests/test_serial.cpp`:

```cpp
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
```

`tests/test_lcd_timing.cpp`:

```cpp
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
```

- [ ] **Step 2: Run to verify it fails**

```powershell
.\tools\dev.cmd cmake --build --preset release
```

Expected: FAIL, `cannot open include file: 'core/Serial.h'`.

- [ ] **Step 3: Implement**

`src/core/Serial.h`:

```cpp
#pragma once

#include "core/Types.h"

#include <vector>

namespace fourshades {

// SB and SC, per Pan Docs "Serial Data Transfer". On DMG the internal clock is
// 8192 Hz: one bit per falling edge of system-counter bit 8. With no link
// partner, the bits shifted in are 1.
class Serial {
public:
    // One M-cycle, given the system counter before and after it advanced.
    // Returns true when a transfer completes (the serial interrupt).
    bool tick(u16 counterBefore, u16 counterAfter);

    u8 read(u16 address) const;         // FF01-FF02
    void write(u16 address, u8 value);  // FF01-FF02

    // Every byte whose transfer was started, in order: what a link-cable
    // partner would have received.
    const std::vector<u8>& sent() const { return sent_; }

private:
    u8 sb_ = 0x00;
    u8 sc_ = 0x7E;
    int bitsLeft_ = 0;
    std::vector<u8> sent_;
};

} // namespace fourshades
```

`src/core/Serial.cpp`:

```cpp
#include "core/Serial.h"

namespace fourshades {

bool Serial::tick(u16 counterBefore, u16 counterAfter) {
    if (bitsLeft_ == 0 || (sc_ & 0x81) != 0x81) {
        return false;
    }
    const bool fallingEdge = (counterBefore & 0x0100) != 0 && (counterAfter & 0x0100) == 0;
    if (!fallingEdge) {
        return false;
    }
    sb_ = static_cast<u8>((sb_ << 1) | 0x01);
    if (--bitsLeft_ == 0) {
        sc_ = static_cast<u8>(sc_ & 0x7F);
        return true;
    }
    return false;
}

u8 Serial::read(u16 address) const {
    return address == 0xFF01 ? sb_ : static_cast<u8>(sc_ | 0x7E);
}

void Serial::write(u16 address, u8 value) {
    if (address == 0xFF01) {
        sb_ = value;
        return;
    }
    sc_ = static_cast<u8>(value | 0x7E);
    if ((value & 0x81) == 0x81) {
        bitsLeft_ = 8;
        sent_.push_back(sb_);
    } else if ((value & 0x80) != 0) {
        bitsLeft_ = 8; // external clock: waits for a partner that never clocks
    } else {
        bitsLeft_ = 0;
    }
}

} // namespace fourshades
```

`src/core/LcdTiming.h`:

```cpp
#pragma once

#include "core/Types.h"

namespace fourshades {

// A placeholder for the PPU, which is piece 3. It keeps only the timing that
// software waits on: LY advances every 114 M-cycles while the LCD is on, wraps
// after line 153, and VBlank is requested on entering line 144. STAT mode bits
// are approximate and there are no STAT interrupts or pixels.
class LcdTiming {
public:
    // One M-cycle. Returns the IF bits requested this cycle.
    u8 tick();

    u8 read(u16 address) const;         // FF40-FF45, FF47-FF4B
    void write(u16 address, u8 value);  // same

private:
    int mode() const;

    u8 lcdc_ = 0x91;
    u8 statSelect_ = 0x00; // STAT bits 3-6
    u8 scy_ = 0x00;
    u8 scx_ = 0x00;
    u8 ly_ = 0x00;
    u8 lyc_ = 0x00;
    u8 bgp_ = 0xFC;
    u8 obp0_ = 0xFF;
    u8 obp1_ = 0xFF;
    u8 wy_ = 0x00;
    u8 wx_ = 0x00;
    int lineCycle_ = 0; // M-cycles into the current line, 0-113
};

} // namespace fourshades
```

`src/core/LcdTiming.cpp`:

```cpp
#include "core/LcdTiming.h"

namespace fourshades {

u8 LcdTiming::tick() {
    if ((lcdc_ & 0x80) == 0) {
        return 0;
    }
    if (++lineCycle_ < 114) {
        return 0;
    }
    lineCycle_ = 0;
    ly_ = static_cast<u8>(ly_ == 153 ? 0 : ly_ + 1);
    return ly_ == 144 ? 0x01 : 0x00;
}

int LcdTiming::mode() const {
    if ((lcdc_ & 0x80) == 0) {
        return 0;
    }
    if (ly_ >= 144) {
        return 1;
    }
    if (lineCycle_ < 20) {
        return 2;
    }
    return lineCycle_ < 63 ? 3 : 0;
}

u8 LcdTiming::read(u16 address) const {
    switch (address) {
    case 0xFF40: return lcdc_;
    case 0xFF41:
        return static_cast<u8>(0x80 | statSelect_ | (ly_ == lyc_ ? 0x04 : 0x00) | mode());
    case 0xFF42: return scy_;
    case 0xFF43: return scx_;
    case 0xFF44: return ly_;
    case 0xFF45: return lyc_;
    case 0xFF47: return bgp_;
    case 0xFF48: return obp0_;
    case 0xFF49: return obp1_;
    case 0xFF4A: return wy_;
    case 0xFF4B: return wx_;
    default: return 0xFF;
    }
}

void LcdTiming::write(u16 address, u8 value) {
    switch (address) {
    case 0xFF40:
        lcdc_ = value;
        if ((value & 0x80) == 0) {
            ly_ = 0;
            lineCycle_ = 0;
        }
        break;
    case 0xFF41: statSelect_ = static_cast<u8>(value & 0x78); break;
    case 0xFF42: scy_ = value; break;
    case 0xFF43: scx_ = value; break;
    case 0xFF45: lyc_ = value; break;
    case 0xFF47: bgp_ = value; break;
    case 0xFF48: obp0_ = value; break;
    case 0xFF49: obp1_ = value; break;
    case 0xFF4A: wy_ = value; break;
    case 0xFF4B: wx_ = value; break;
    default: break; // LY (FF44) is read-only
    }
}

} // namespace fourshades
```

In `CMakeLists.txt`, add `src/core/Serial.cpp` and `src/core/LcdTiming.cpp` to `fourshades_core`.

- [ ] **Step 4: Run to verify it passes**

```powershell
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
.\build\release\fourshades_tests.exe
python tools/check_core_isolation.py
```

Expected: doctest `test cases: 61 | 61 passed` (55 + 6), no warnings, and the isolation check passes.

- [ ] **Step 5: Commit**

```powershell
git add src/core/Serial.h src/core/Serial.cpp src/core/LcdTiming.h src/core/LcdTiming.cpp CMakeLists.txt tests/test_serial.cpp tests/test_lcd_timing.cpp
git commit -m "feat(core): serial port and an LCD timing skeleton" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
```

---

### Task 5: GameBoy: memory map, interrupts, DMA, power-on state

**Files:**
- Create: `src/core/Interrupts.h`, `src/core/GameBoy.h`, `src/core/GameBoy.cpp`
- Modify: `CMakeLists.txt` (add `src/core/GameBoy.cpp`)
- Test: `tests/test_gameboy.cpp`

**Interfaces:**
- Consumes: `Cpu` (Task 1), `Cartridge` (Task 2), `Timer` (Task 3), `Serial` and `LcdTiming` (Task 4).
- Produces: `class fourshades::GameBoy final : public Bus` with:
  - `explicit GameBoy(Cartridge cartridge)`, which applies the DMG power-on state;
  - `Cpu& cpu()`, `const Cpu& cpu() const` and `void step()`;
  - `u8 peek(u16 address) const`: cycle-free, no side effects, ignores DMA blocking;
  - `const std::vector<u8>& serialOutput() const`;
  - `std::uint64_t cycles() const`: M-cycles since power-on;
  - the `Bus` overrides.
- Produces: `namespace fourshades::irq` constants `VBlank=0x01, Lcd=0x02, Timer=0x04, Serial=0x08, Joypad=0x10`.

- [ ] **Step 1: Write the failing tests**

`tests/test_gameboy.cpp`:

```cpp
#include <doctest/doctest.h>

#include "core/GameBoy.h"

#include <memory>
#include <vector>

using namespace fourshades;

namespace {
// A 32 KiB plain ROM with `program` at 0x0100 and a valid header checksum.
std::unique_ptr<GameBoy> makeGameBoy(std::vector<u8> program, u8 type = 0x00, u8 ramCode = 0x00) {
    std::vector<u8> rom(0x8000, 0x00);
    for (std::size_t i = 0; i < program.size(); ++i) {
        rom[0x0100 + i] = program[i];
    }
    rom[0x0147] = type;
    rom[0x0149] = ramCode;
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

TEST_CASE("power-on state matches a DMG after its boot ROM") {
    auto gb = makeGameBoy({0x00});
    const Registers& r = gb->cpu().regs;
    CHECK(r.a == 0x01);
    CHECK(r.f() == 0xB0); // the header checksum byte is non-zero here
    CHECK(r.bc() == 0x0013);
    CHECK(r.de() == 0x00D8);
    CHECK(r.hl() == 0x014D);
    CHECK(r.sp == 0xFFFE);
    CHECK(r.pc == 0x0100);
    CHECK(gb->peek(0xFF00) == 0xCF);
    CHECK(gb->peek(0xFF02) == 0x7E);
    CHECK(gb->peek(0xFF04) == 0xAB);
    CHECK(gb->peek(0xFF07) == 0xF8);
    CHECK(gb->peek(0xFF0F) == 0xE1);
    CHECK(gb->peek(0xFF40) == 0x91);
    CHECK(gb->peek(0xFF46) == 0xFF);
    CHECK(gb->peek(0xFF47) == 0xFC);
}

TEST_CASE("every bus call advances the machine by one M-cycle") {
    auto gb = makeGameBoy({0x00, 0x00});
    gb->step(); // NOP: one fetch
    CHECK(gb->cycles() == 1);
    gb->step();
    CHECK(gb->cycles() == 2);
}

TEST_CASE("memory regions route correctly, and echo RAM mirrors WRAM") {
    auto gb = makeGameBoy({0x00});
    gb->write(0xC123, 0x42);
    CHECK(gb->peek(0xE123) == 0x42);
    gb->write(0xE200, 0x17);
    CHECK(gb->peek(0xC200) == 0x17);
    gb->write(0x8000, 0x11);
    CHECK(gb->peek(0x8000) == 0x11);
    gb->write(0xFE00, 0x22);
    CHECK(gb->peek(0xFE00) == 0x22);
    gb->write(0xFF80, 0x33);
    CHECK(gb->peek(0xFF80) == 0x33);
    CHECK(gb->peek(0xFEA0) == 0x00); // unusable area on DMG
    CHECK(gb->peek(0xFF03) == 0xFF); // unmapped I/O
    CHECK(gb->peek(0x0100) == 0x00); // ROM
    gb->write(0x0100, 0x99);         // ROM writes go to the (absent) MBC
    CHECK(gb->peek(0x0100) == 0x00);
}

TEST_CASE("IF and IE drive the CPU's interrupt lines") {
    auto gb = makeGameBoy({0x00});
    CHECK(gb->pendingInterrupts() == 0x00); // IF=E1 but IE=0
    gb->write(0xFFFF, 0x01);
    CHECK(gb->pendingInterrupts() == 0x01);
    gb->acknowledgeInterrupt(0);
    CHECK(gb->peek(0xFF0F) == 0xE0);
    gb->write(0xFF0F, 0xFF);
    CHECK(gb->peek(0xFF0F) == 0xFF);
    CHECK(gb->pendingInterrupts() == 0x01);
}

TEST_CASE("a timer overflow raises IF bit 2 through the bus") {
    auto gb = makeGameBoy({0x00});
    gb->write(0xFF0F, 0x00);
    gb->write(0xFF06, 0x00);
    gb->write(0xFF05, 0xFF);
    gb->write(0xFF07, 0x05); // every 4 M-cycles
    for (int i = 0; i < 8; ++i) {
        gb->idle();
    }
    CHECK((gb->peek(0xFF0F) & 0x04) != 0);
}

TEST_CASE("the serial port records bytes and interrupts when done") {
    auto gb = makeGameBoy({0x00});
    gb->write(0xFF0F, 0x00);
    gb->write(0xFF01, 'P');
    gb->write(0xFF02, 0x81);
    for (int i = 0; i < 1100; ++i) {
        gb->idle();
    }
    REQUIRE(gb->serialOutput().size() == 1);
    CHECK(gb->serialOutput()[0] == 'P');
    CHECK((gb->peek(0xFF0F) & 0x08) != 0);
}

TEST_CASE("OAM DMA copies 160 bytes and blocks the CPU outside FF00-FFFF meanwhile") {
    auto gb = makeGameBoy({0x00});
    for (int i = 0; i < 0xA0; ++i) {
        gb->write(static_cast<u16>(0xC000 + i), static_cast<u8>(i + 1));
    }
    gb->write(0xFF80, 0x5A);
    gb->write(0xFF46, 0xC0);
    CHECK(gb->peek(0xFF46) == 0xC0);
    gb->idle(); // start-up cycle
    gb->idle(); // first byte copied
    CHECK(gb->read(0xC000) == 0xFF); // blocked
    CHECK(gb->read(0xFF80) == 0x5A); // HRAM still works
    for (int i = 0; i < 170; ++i) {
        gb->idle();
    }
    CHECK(gb->read(0xC000) == 0x01); // no longer blocked
    CHECK(gb->peek(0xFE00) == 0x01);
    CHECK(gb->peek(0xFE9F) == 0xA0);
}

TEST_CASE("the CPU runs a program through the memory map") {
    // LD A,0x12 ; LD (0xC000),A ; LD HL,0xC000 ; INC (HL) ; HALT
    auto gb = makeGameBoy({0x3E, 0x12, 0xEA, 0x00, 0xC0, 0x21, 0x00, 0xC0, 0x34, 0x76});
    for (int i = 0; i < 5; ++i) {
        gb->step();
    }
    CHECK(gb->peek(0xC000) == 0x13);
    CHECK(gb->cpu().state() == Cpu::State::Halted);
}

TEST_CASE("a halted CPU is woken by the VBlank interrupt") {
    // EI ; HALT ; (handler at 0x40 is NOP bytes of a zero-filled ROM)
    auto gb = makeGameBoy({0xFB, 0x76, 0x00});
    gb->write(0xFF0F, 0x00);
    gb->write(0xFFFF, 0x01);
    gb->step(); // EI
    gb->step(); // HALT
    CHECK(gb->cpu().state() == Cpu::State::Halted);
    for (int i = 0; i < 154 * 114 && gb->cpu().regs.pc != 0x0040; ++i) {
        gb->step();
    }
    CHECK(gb->cpu().regs.pc == 0x0040);
}
```

- [ ] **Step 2: Run to verify it fails**

```powershell
.\tools\dev.cmd cmake --build --preset release
```

Expected: FAIL, `cannot open include file: 'core/GameBoy.h'`.

- [ ] **Step 3: Implement**

`src/core/Interrupts.h`:

```cpp
#pragma once

#include "core/Types.h"

namespace fourshades::irq {

// IF/IE bits. The handler for bit n is at 0x40 + 8n.
constexpr u8 VBlank = 0x01;
constexpr u8 Lcd = 0x02;
constexpr u8 Timer = 0x04;
constexpr u8 Serial = 0x08;
constexpr u8 Joypad = 0x10;

} // namespace fourshades::irq
```

`src/core/GameBoy.h`:

```cpp
#pragma once

#include "core/Bus.h"
#include "core/Cartridge.h"
#include "core/Cpu.h"
#include "core/LcdTiming.h"
#include "core/Serial.h"
#include "core/Timer.h"
#include "core/Types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace fourshades {

// The whole machine except the screen. It is the CPU's Bus: every read,
// write or idle first advances the timer, serial port, LCD timing and OAM
// DMA by one M-cycle, then performs the access.
class GameBoy final : public Bus {
public:
    // Starts in the state a DMG (revisions A-C) boot ROM leaves behind, at
    // PC=0x0100 (Pan Docs "Power Up Sequence"). No boot ROM is run.
    explicit GameBoy(Cartridge cartridge);

    GameBoy(const GameBoy&) = delete;
    GameBoy& operator=(const GameBoy&) = delete;

    Cpu& cpu() { return cpu_; }
    const Cpu& cpu() const { return cpu_; }
    void step() { cpu_.step(); }

    // What a debugger would see: no time passes and no DMA blocking applies.
    u8 peek(u16 address) const;
    // Bytes sent over the serial port, in order.
    const std::vector<u8>& serialOutput() const { return serial_.sent(); }
    // M-cycles since power-on.
    std::uint64_t cycles() const { return cycles_; }

    u8 read(u16 address) override;
    void write(u16 address, u8 value) override;
    void idle() override;
    u8 pendingInterrupts() override { return static_cast<u8>(ie_ & if_ & 0x1F); }
    void acknowledgeInterrupt(int bit) override { if_ = static_cast<u8>(if_ & ~(1 << bit)); }

private:
    void tick();
    void tickDma();
    bool dmaBlocks(u16 address) const;
    u8 readIo(u16 address) const;
    void writeIo(u16 address, u8 value);
    void writeMemory(u16 address, u8 value);

    Cartridge cart_;
    Timer timer_;
    Serial serial_;
    LcdTiming lcd_;
    std::array<u8, 0x2000> vram_{};
    std::array<u8, 0x2000> wram_{};
    std::array<u8, 0xA0> oam_{};
    std::array<u8, 0x7F> hram_{};
    u8 ie_ = 0x00;
    u8 if_ = 0x01;          // bits 0-4; reads OR in 0xE0
    u8 joypadSelect_ = 0x00; // P1 bits 4-5
    u8 dmaRegister_ = 0xFF;
    u16 dmaSource_ = 0;
    int dmaStartDelay_ = 0; // M-cycles until a requested DMA begins
    bool dmaActive_ = false;
    int dmaIndex_ = 0;      // next byte to copy, 0-159
    u16 dmaFrom_ = 0;
    std::uint64_t cycles_ = 0;
    Cpu cpu_; // last: it holds a reference to this Bus
};

} // namespace fourshades
```

`src/core/GameBoy.cpp`:

```cpp
#include "core/GameBoy.h"

#include "core/Interrupts.h"

#include <utility>

namespace fourshades {

GameBoy::GameBoy(Cartridge cartridge) : cart_(std::move(cartridge)), cpu_(*this) {
    Registers& r = cpu_.regs;
    r.a = 0x01;
    r.setF(cart_.headerChecksum() != 0 ? 0xB0 : 0x80);
    r.setBc(0x0013);
    r.setDe(0x00D8);
    r.setHl(0x014D);
    r.sp = 0xFFFE;
    r.pc = 0x0100;
    timer_.setCounter(0xABCC); // DIV reads 0xAB
}

void GameBoy::tick() {
    ++cycles_;
    const u16 before = timer_.counter();
    if (timer_.tick()) {
        if_ = static_cast<u8>(if_ | irq::Timer);
    }
    if (serial_.tick(before, timer_.counter())) {
        if_ = static_cast<u8>(if_ | irq::Serial);
    }
    if_ = static_cast<u8>(if_ | lcd_.tick());
    tickDma();
}

void GameBoy::tickDma() {
    if (dmaActive_) {
        u16 from = static_cast<u16>(dmaFrom_ + dmaIndex_);
        if (from >= 0xE000) {
            from = static_cast<u16>(from - 0x2000); // Pan Docs: sources above DFFF
        }
        oam_[static_cast<std::size_t>(dmaIndex_)] = peek(from);
        if (++dmaIndex_ == 0xA0) {
            dmaActive_ = false;
        }
    }
    if (dmaStartDelay_ > 0 && --dmaStartDelay_ == 0) {
        dmaActive_ = true; // a restart replaces a transfer in progress
        dmaIndex_ = 0;
        dmaFrom_ = dmaSource_;
    }
}

bool GameBoy::dmaBlocks(u16 address) const {
    // On DMG the CPU sees only FF00-FFFF while bytes are being copied.
    return dmaActive_ && dmaIndex_ > 0 && address < 0xFF00;
}

u8 GameBoy::read(u16 address) {
    tick();
    return dmaBlocks(address) ? 0xFF : peek(address);
}

void GameBoy::write(u16 address, u8 value) {
    tick();
    if (!dmaBlocks(address)) {
        writeMemory(address, value);
    }
}

void GameBoy::idle() {
    tick();
}

u8 GameBoy::peek(u16 address) const {
    if (address < 0x8000) return cart_.read(address);
    if (address < 0xA000) return vram_[address - 0x8000];
    if (address < 0xC000) return cart_.read(address);
    if (address < 0xE000) return wram_[address - 0xC000];
    if (address < 0xFE00) return wram_[address - 0xE000];
    if (address < 0xFEA0) return oam_[address - 0xFE00];
    if (address < 0xFF00) return 0x00;
    if (address < 0xFF80) return readIo(address);
    if (address < 0xFFFF) return hram_[address - 0xFF80];
    return ie_;
}

void GameBoy::writeMemory(u16 address, u8 value) {
    if (address < 0x8000) {
        cart_.write(address, value);
    } else if (address < 0xA000) {
        vram_[address - 0x8000] = value;
    } else if (address < 0xC000) {
        cart_.write(address, value);
    } else if (address < 0xE000) {
        wram_[address - 0xC000] = value;
    } else if (address < 0xFE00) {
        wram_[address - 0xE000] = value;
    } else if (address < 0xFEA0) {
        oam_[address - 0xFE00] = value;
    } else if (address < 0xFF00) {
        // unusable area: writes are ignored
    } else if (address < 0xFF80) {
        writeIo(address, value);
    } else if (address < 0xFFFF) {
        hram_[address - 0xFF80] = value;
    } else {
        ie_ = value;
    }
}

u8 GameBoy::readIo(u16 address) const {
    switch (address) {
    case 0xFF00: return static_cast<u8>(0xC0 | joypadSelect_ | 0x0F); // no buttons held
    case 0xFF01:
    case 0xFF02: return serial_.read(address);
    case 0xFF04:
    case 0xFF05:
    case 0xFF06:
    case 0xFF07: return timer_.read(address);
    case 0xFF0F: return static_cast<u8>(if_ | 0xE0);
    case 0xFF46: return dmaRegister_;
    default:
        if (address >= 0xFF40 && address <= 0xFF4B) {
            return lcd_.read(address);
        }
        return 0xFF; // not implemented yet (sound is piece 5)
    }
}

void GameBoy::writeIo(u16 address, u8 value) {
    switch (address) {
    case 0xFF00: joypadSelect_ = static_cast<u8>(value & 0x30); break;
    case 0xFF01:
    case 0xFF02: serial_.write(address, value); break;
    case 0xFF04:
    case 0xFF05:
    case 0xFF06:
    case 0xFF07: timer_.write(address, value); break;
    case 0xFF0F: if_ = static_cast<u8>(value & 0x1F); break;
    case 0xFF46:
        dmaRegister_ = value;
        dmaSource_ = static_cast<u16>(value << 8);
        dmaStartDelay_ = 1;
        break;
    default:
        if (address >= 0xFF40 && address <= 0xFF4B) {
            lcd_.write(address, value);
        }
        break;
    }
}

} // namespace fourshades
```

In `CMakeLists.txt`, add `src/core/GameBoy.cpp` to `fourshades_core`.

- [ ] **Step 4: Run to verify it passes, and that SST is unchanged**

```powershell
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
.\build\release\fourshades_tests.exe
.\build\release\tools\sst\sst_runner.exe
python tools/check_core_isolation.py
```

Expected: doctest `test cases: 70 | 70 passed` (61 + 9), no warnings; SST `499 / 500` with only `FAIL 10`; isolation check passed.

If the DMA test's timing assertions fail by a cycle, adjust only `tickDma`/`dmaBlocks`, and write down the chosen timing in a comment. The Mooneye `oam_dma*` tests arbitrate the exact cycles in Task 10, not this unit test.

- [ ] **Step 5: Commit**

```powershell
git add src/core/Interrupts.h src/core/GameBoy.h src/core/GameBoy.cpp CMakeLists.txt tests/test_gameboy.cpp
git commit -m "feat(core): GameBoy memory map with interrupts, DMA and DMG power-on state" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
```

---

### Task 6: The pinned test list and ROM download

**Files:**
- Create: `tools/roms/make_test_list.py`, `tools/roms/fetch_roms.py`
- Create (generated): `tools/roms/tests.json`, `tools/roms/manifest.sha256`
- Modify: `.gitignore` (add `tools/roms/data/`), `.gitattributes` (add `tools/roms/manifest.sha256 text eol=lf`)

**Interfaces:**
- Produces: `tools/roms/tests.json`:
  - `{"shootout_commit": "...", "tests": [...]}`, 167 entries in Shootout order;
  - each entry is `{"name", "rom", "group", "method", "runtime", "limit_seconds", "references": [paths]}`;
  - `method` is `blargg`, `mooneye` or `screenshot`;
  - paths are relative to the Shootout's `testroms/`.
- Produces: `tools/roms/data/<path>` for every ROM and reference image, verified against `tools/roms/manifest.sha256` (`<sha256>  <path>` lines).
- Expected group counts: cpu instructions 11, cpu timing 8, cpu & interrupts 31, timer 13, boot state 3, oam dma 6, serial 1, mbc1 13, mbc2 / mbc5 15, ppu timing 12, oam bug 7, sound 12, mbc3 / rtc 3, screen 32 (total 167). There are 169 reference images named, of which 2 don't exist at the pinned commit and are left out.

- [ ] **Step 1: Write the generator**

`tools/roms/make_test_list.py`:

```python
"""
Build tools/roms/tests.json from the Emulator Shootout's own test definitions,
at a pinned commit, so anyone can regenerate the list and diff it.

    python tools/roms/make_test_list.py          # write tests.json
    python tools/roms/make_test_list.py --check  # exit 1 if tests.json differs

Only the Shootout's active original-Game-Boy (DMG) tests are kept: no
model=CGB or model=SGB. The count must come out at exactly 167.
"""

import ast
import json
import sys
import time
import urllib.request
from pathlib import Path

REPO = "gbdev/GBEmulatorShootout"
COMMIT = "38b926bdbc26993d1b4c43e97979ecc66287bf02"
RAW = f"https://raw.githubusercontent.com/{REPO}/{COMMIT}/"
TREE = f"https://api.github.com/repos/{REPO}/git/trees/{COMMIT}?recursive=1"
SUITES = ["blargg", "mooneye", "mealybug", "acid", "ashiepaws", "cpp", "daid"]
EXPECTED = 167
OUT = Path(__file__).resolve().parent / "tests.json"

# First match wins, so the specific mooneye groups come before the catch-all.
GROUPS = [
    (("blargg/cpu_instrs/",), "cpu instructions"),
    (("blargg/instr_timing", "blargg/mem_timing", "blargg/halt_bug"), "cpu timing"),
    (("blargg/oam_bug/",), "oam bug"),
    (("blargg/dmg_sound/",), "sound"),
    (("mooneye/acceptance/timer/",), "timer"),
    (("mooneye/acceptance/boot_",), "boot state"),
    (("mooneye/acceptance/oam_dma",), "oam dma"),
    (("mooneye/acceptance/serial/",), "serial"),
    (("mooneye/acceptance/ppu/",), "ppu timing"),
    (("mooneye/emulator-only/mbc1/",), "mbc1"),
    (("mooneye/emulator-only/mbc2/", "mooneye/emulator-only/mbc5/"), "mbc2 / mbc5"),
    (("mooneye/acceptance/",), "cpu & interrupts"),
    (("cpp/",), "mbc3 / rtc"),
    (("mooneye/manual-only/", "acid/", "ashiepaws/", "daid/", "mealybug-tearoom-tests/"), "screen"),
]


def get(url: str) -> bytes:
    last_error = None
    for attempt in range(1, 7):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return response.read()
        except OSError as error:
            last_error = error
            time.sleep(2 * attempt)
    raise SystemExit(f"error: {url}: {last_error}")


def group_for(rom: str) -> str:
    for prefixes, group in GROUPS:
        if rom.startswith(prefixes):
            return group
    raise SystemExit(f"error: no group for {rom}")


def method_for(suite: str, rom: str) -> str:
    if suite == "blargg":
        return "blargg"
    if suite == "mooneye" and not rom.startswith("mooneye/manual-only/"):
        return "mooneye"
    return "screenshot"


def const(node):
    return node.value if isinstance(node, ast.Constant) else None


def parse_suite(suite: str, source: str) -> list[dict]:
    calls = [n for n in ast.walk(ast.parse(source)) if isinstance(n, ast.Call) and isinstance(n.func, ast.Name)]
    calls.sort(key=lambda n: (n.lineno, n.col_offset))
    tests = []
    for call in calls:
        kw = {k.arg: k.value for k in call.keywords}
        if call.func.id == "Test":
            # Test(...) calls inside helper functions have a computed name; skip them.
            if not call.args or not isinstance(call.args[0], ast.Constant):
                continue
            model = kw["model"].id if "model" in kw else "DMG"
            if model != "DMG":
                continue
            name = call.args[0].value
            rom = const(kw["rom"]) if "rom" in kw else name
            runtime = float(const(kw["runtime"]))
            if "result" in kw:
                result = kw["result"]
                refs = [const(e) for e in result.elts] if isinstance(result, ast.List) else [const(result)]
            else:
                refs = [rom.rsplit(".", 1)[0] + ".png"]
        elif call.func.id == "dmg":  # mealybug.py's DMG helper
            arg = call.args[0].value
            name = f"mealybug-tearoom-tests/{arg} (DMG)"
            rom = f"mealybug-tearoom-tests/{arg}"
            runtime = 0.5
            refs = ["mealybug-tearoom-tests/" + arg.replace(".gb", "_dmg_blob.png")]
        else:
            continue
        tests.append({"suite": suite, "name": name, "rom": rom, "runtime": runtime, "refs": refs})
    return tests


def build() -> str:
    tree = json.loads(get(TREE))
    if tree.get("truncated"):
        raise SystemExit("error: GitHub truncated the file listing")
    exists = {e["path"][len("testroms/"):] for e in tree["tree"]
              if e["type"] == "blob" and e["path"].startswith("testroms/")}
    entries = []
    for suite in SUITES:
        for t in parse_suite(suite, get(f"{RAW}testroms/{suite}.py").decode("utf-8")):
            if t["rom"] not in exists:
                raise SystemExit(f"error: {t['rom']} is not in the Shootout at {COMMIT}")
            entries.append({
                "name": t["name"],
                "rom": t["rom"],
                "group": group_for(t["rom"]),
                "method": method_for(suite, t["rom"]),
                "runtime": t["runtime"],
                "limit_seconds": max(2 * t["runtime"], t["runtime"] + 5),
                "references": [r for r in t["refs"] if r in exists],
            })
    if len(entries) != EXPECTED:
        raise SystemExit(f"error: expected {EXPECTED} DMG tests, found {len(entries)}")
    return json.dumps({"shootout_commit": COMMIT, "tests": entries}, indent=1) + "\n"


def main() -> int:
    text = build()
    if "--check" in sys.argv[1:]:
        same = OUT.exists() and OUT.read_text(encoding="utf-8") == text
        print("tests.json matches the Shootout" if same else "tests.json differs from the Shootout")
        return 0 if same else 1
    OUT.write_text(text, encoding="utf-8", newline="\n")
    counts: dict[str, int] = {}
    for t in json.loads(text)["tests"]:
        counts[t["group"]] = counts.get(t["group"], 0) + 1
    print(f"wrote {OUT} ({sum(counts.values())} tests)")
    for group, n in counts.items():
        print(f"  {n:3}  {group}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Generate and check the list**

```powershell
python tools/roms/make_test_list.py
python tools/roms/make_test_list.py --check
```

Expected: `wrote ...tests.json (167 tests)` with exactly the group counts listed under Interfaces, then `tests.json matches the Shootout`. If any count differs, stop and report it rather than editing the grouping to force the numbers.

- [ ] **Step 3: Write the fetcher**

`tools/roms/fetch_roms.py`:

```python
"""
Download every test ROM and reference image named in tools/roms/tests.json
from the Emulator Shootout at the pinned commit, and check each against the
committed manifest.

    python tools/roms/fetch_roms.py                    # fetch what's missing or wrong, then verify
    python tools/roms/fetch_roms.py --write-manifest   # one-off: record the hashes

Files come one at a time from raw.githubusercontent.com, each retried on its
own. --write-manifest also checks every file against the git blob hash GitHub
lists for the commit. The ROMs are never committed to this repository.
"""

import hashlib
import http.client
import json
import sys
import time
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
MANIFEST = HERE / "manifest.sha256"
TESTS = json.loads((HERE / "tests.json").read_text(encoding="utf-8"))
COMMIT = TESTS["shootout_commit"]
RAW = f"https://raw.githubusercontent.com/gbdev/GBEmulatorShootout/{COMMIT}/testroms/"
TREE = f"https://api.github.com/repos/gbdev/GBEmulatorShootout/git/trees/{COMMIT}?recursive=1"
ATTEMPTS = 6
WORKERS = 8


def wanted() -> list[str]:
    names = set()
    for test in TESTS["tests"]:
        names.add(test["rom"])
        names.update(test["references"])
    return sorted(names)


def get(url: str) -> bytes:
    last_error = None
    for attempt in range(1, ATTEMPTS + 1):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return response.read()
        except (OSError, http.client.HTTPException) as error:
            last_error = error
            time.sleep(2 * attempt)
    raise SystemExit(f"error: {url}: failed after {ATTEMPTS} attempts: {last_error}")


def git_blob_sha1(content: bytes) -> str:
    return hashlib.sha1(b"blob %d\0" % len(content) + content).hexdigest()


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def download(names: list[str]) -> None:
    def one(name: str) -> None:
        target = DATA / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(get(RAW + urllib.parse.quote(name)))

    with ThreadPoolExecutor(WORKERS) as pool:
        for done, _ in enumerate(pool.map(one, names), 1):
            if done % 50 == 0 or done == len(names):
                print(f"  downloaded {done}/{len(names)}")


def read_manifest() -> dict[str, str]:
    entries = {}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        if len(line) <= 66 or line[64:66] != "  ":
            raise SystemExit(f"error: malformed manifest line: {line!r}")
        entries[line[66:]] = line[:64]
    return entries


def write_manifest() -> int:
    tree = json.loads(get(TREE))
    if tree.get("truncated"):
        raise SystemExit("error: GitHub truncated the file listing")
    blobs = {e["path"][len("testroms/"):]: e["sha"] for e in tree["tree"]
             if e["type"] == "blob" and e["path"].startswith("testroms/")}
    names = wanted()
    unknown = [n for n in names if n not in blobs]
    if unknown:
        raise SystemExit(f"error: not in the Shootout at {COMMIT}: {unknown[:5]}")

    def matches(name: str) -> bool:
        path = DATA / name
        return path.exists() and git_blob_sha1(path.read_bytes()) == blobs[name]

    missing = [n for n in names if not matches(n)]
    if missing:
        print(f"downloading {len(missing)} files")
        download(missing)
    wrong = [n for n in names if not matches(n)]
    if wrong:
        print(f"error: {len(wrong)} files don't match GitHub's git blob hash")
        for name in wrong[:20]:
            print(f"  {name}")
        return 1
    MANIFEST.write_text("".join(f"{sha256_file(DATA / n)}  {n}\n" for n in names),
                        encoding="utf-8", newline="\n")
    print(f"wrote {MANIFEST} ({len(names)} files, each matching GitHub's git blob hash)")
    return 0


def fetch() -> int:
    expected = read_manifest()
    if sorted(expected) != wanted():
        raise SystemExit("error: manifest and tests.json name different files; regenerate the manifest")

    def stale(name: str) -> bool:
        path = DATA / name
        return not path.exists() or sha256_file(path) != expected[name]

    todo = [n for n in sorted(expected) if stale(n)]
    if not todo:
        print(f"test ROMs present and verified ({len(expected)} files)")
        return 0
    print(f"downloading {len(todo)} files")
    download(todo)
    bad = [n for n in todo if stale(n)]
    if bad:
        print(f"error: {len(bad)} downloaded files don't match the manifest")
        for name in bad[:20]:
            print(f"  {name}")
        return 1
    print(f"downloaded {len(todo)} files; all {len(expected)} verified")
    return 0


if __name__ == "__main__":
    sys.exit(write_manifest() if "--write-manifest" in sys.argv[1:] else fetch())
```

Append `tools/roms/data/` to `.gitignore`, and `tools/roms/manifest.sha256 text eol=lf` to `.gitattributes`.

- [ ] **Step 4: Fetch, record and verify**

```powershell
python tools/roms/fetch_roms.py --write-manifest
python tools/roms/fetch_roms.py
(Get-Content tools/roms/manifest.sha256 | Measure-Object -Line).Lines
git status --short
```

Expected:
- `wrote ...manifest.sha256 (334 files, each matching GitHub's git blob hash)` — 167 ROMs + 167 reference images;
- then `test ROMs present and verified (334 files)`;
- `334`;
- and `git status` shows no files under `tools/roms/data/`.

If a download fails, rerun the same command; it resumes. If the file count isn't 334, report the actual count and why (for example, a reference image shared by two tests) rather than forcing it.

- [ ] **Step 5: Commit**

```powershell
git add tools/roms/make_test_list.py tools/roms/fetch_roms.py tools/roms/tests.json tools/roms/manifest.sha256 .gitignore .gitattributes
git commit -m "feat(roms): pin the Shootout's 167 DMG tests and fetch them hash-checked" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
```

---

### Task 7: The test-ROM runner

**Files:**
- Create: `tools/roms/RomTests.h`, `tools/roms/RomTests.cpp`, `tools/roms/Detectors.h`, `tools/roms/Detectors.cpp`, `tools/roms/RomRun.h`, `tools/roms/RomRun.cpp`, `tools/roms/rom_runner.cpp`, `tools/roms/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add `add_subdirectory(tools/roms)` after `add_subdirectory(tools/sst)`; make the tests link `rom_harness`)
- Test: `tests/test_rom_harness.cpp`

**Interfaces:**
- Consumes: `GameBoy`, `Cartridge`, `Registers` (core); `sst::parseManifest`, `sst::readBinaryFile`, `sst::sha256Hex` (piece 1 harness).
- Produces, in namespace `roms`:
  - `enum class Method { Blargg, Mooneye, Screenshot }`;
  - `struct RomTest { name, rom, group; Method method; double runtime, limitSeconds; std::vector<std::string> references; }`;
  - `struct TestList { std::string shootoutCommit; std::vector<RomTest> tests; }` and `TestList parseTestList(std::string_view)`;
  - `enum class Verdict { Running, Pass, Fail }`;
  - `Verdict serialVerdict(std::string_view)`;
  - `Verdict blarggMemoryVerdict(const std::function<u8(u16)>& peek, std::string* text)`;
  - `Verdict mooneyeVerdict(const fourshades::Registers&, u8 nextOpcode)`;
  - `struct RomOutcome { Verdict status; std::string reason; double emulatedSeconds; std::string serial; }` and `RomOutcome runRomTest(const RomTest&, std::vector<u8> romImage)`;
  - `constexpr std::uint64_t kCyclesPerSecond = 1048576`.
- Produces: `build/release/tools/roms/rom_runner.exe [--data DIR] [--manifest FILE] [--tests FILE] [--out FILE] [--only TEXT]`. It writes `build/rom-results.json`:
  - `{"suite": "GBEmulatorShootout (DMG)", "shootout_commit", "partial", "total", "passing", "elapsed_seconds", "tests": [...]}`;
  - each test is `{"name", "group", "method", "status": "pass"|"fail", "reason", "emulated_seconds", "serial"}`;
  - exit 0 means a score was produced; 2 means a harness error.

- [ ] **Step 1: Write the failing tests**

`tests/test_rom_harness.cpp`:

```cpp
#include <doctest/doctest.h>

#include "roms/Detectors.h"
#include "roms/RomRun.h"
#include "roms/RomTests.h"

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fourshades;

namespace {
std::vector<u8> romWith(const std::vector<u8>& program) {
    std::vector<u8> rom(0x8000, 0x00);
    for (std::size_t i = 0; i < program.size(); ++i) {
        rom[0x0100 + i] = program[i];
    }
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    rom[0x014D] = sum;
    return rom;
}

roms::RomTest testOf(roms::Method method, double limit = 1.0) {
    roms::RomTest t;
    t.name = "synthetic";
    t.rom = "synthetic.gb";
    t.group = "cpu & interrupts";
    t.method = method;
    t.runtime = 0.5;
    t.limitSeconds = limit;
    return t;
}

// LD B,3; LD C,5; LD D,8; LD E,13; LD H,21; LD L,<last>; LD B,B; JR -2
std::vector<u8> fibonacciProgram(u8 last) {
    return {0x06, 3, 0x0E, 5, 0x16, 8, 0x1E, 13, 0x26, 21, 0x2E, last, 0x40, 0x18, 0xFE};
}
} // namespace

TEST_CASE("parseTestList reads entries and rejects unknown methods") {
    const std::string ok = R"({"shootout_commit":"abc","tests":[{"name":"t","rom":"a/t.gb","group":"timer",)"
                           R"("method":"mooneye","runtime":1.5,"limit_seconds":6.5,"references":["a/t.png"]}]})";
    const roms::TestList list = roms::parseTestList(ok);
    CHECK(list.shootoutCommit == "abc");
    REQUIRE(list.tests.size() == 1);
    CHECK(list.tests[0].method == roms::Method::Mooneye);
    CHECK(list.tests[0].limitSeconds == 6.5);
    CHECK(list.tests[0].references.size() == 1);
    std::string bad = ok;
    bad.replace(bad.find("mooneye"), 7, "guess");
    CHECK_THROWS_AS(roms::parseTestList(bad), std::runtime_error);
}

TEST_CASE("serial text: Failed beats Passed, and neither means still running") {
    CHECK(roms::serialVerdict("cpu_instrs\n\n01:ok\n\nPassed\n") == roms::Verdict::Pass);
    CHECK(roms::serialVerdict("02-interrupts\n\nFailed #3\n") == roms::Verdict::Fail);
    CHECK(roms::serialVerdict("Passed\nFailed\n") == roms::Verdict::Fail);
    CHECK(roms::serialVerdict("still running") == roms::Verdict::Running);
}

TEST_CASE("Blargg's memory protocol needs the signature, then reads the status") {
    std::map<u16, u8> mem;
    const auto peek = [&](u16 a) { return mem.count(a) ? mem[a] : static_cast<u8>(0xFF); };
    std::string text;
    CHECK(roms::blarggMemoryVerdict(peek, &text) == roms::Verdict::Running); // no signature
    mem[0xA001] = 0xDE;
    mem[0xA002] = 0xB0;
    mem[0xA003] = 0x61;
    mem[0xA000] = 0x80;
    CHECK(roms::blarggMemoryVerdict(peek, &text) == roms::Verdict::Running); // still running
    mem[0xA000] = 0x00;
    mem[0xA004] = 'o';
    mem[0xA005] = 'k';
    mem[0xA006] = 0x00;
    CHECK(roms::blarggMemoryVerdict(peek, &text) == roms::Verdict::Pass);
    CHECK(text == "ok");
    mem[0xA000] = 0x01;
    CHECK(roms::blarggMemoryVerdict(peek, &text) == roms::Verdict::Fail);
}

TEST_CASE("Mooneye verdict fires only on LD B,B with all six registers") {
    Registers r;
    r.b = 3; r.c = 5; r.d = 8; r.e = 13; r.h = 21; r.l = 34;
    CHECK(roms::mooneyeVerdict(r, 0x40) == roms::Verdict::Pass);
    CHECK(roms::mooneyeVerdict(r, 0x00) == roms::Verdict::Running);
    r.l = 35; // five of six
    CHECK(roms::mooneyeVerdict(r, 0x40) == roms::Verdict::Running);
    r.b = r.c = r.d = r.e = r.h = r.l = 0x42;
    CHECK(roms::mooneyeVerdict(r, 0x40) == roms::Verdict::Fail);
}

TEST_CASE("runRomTest passes a program that signals Mooneye success") {
    const auto outcome = roms::runRomTest(testOf(roms::Method::Mooneye), romWith(fibonacciProgram(34)));
    CHECK(outcome.status == roms::Verdict::Pass);
    CHECK(outcome.emulatedSeconds < 0.01);
}

TEST_CASE("runRomTest turns a near-miss into a timeout failure") {
    const auto outcome = roms::runRomTest(testOf(roms::Method::Mooneye, 0.05), romWith(fibonacciProgram(35)));
    CHECK(outcome.status == roms::Verdict::Fail);
    CHECK(outcome.reason.rfind("timeout", 0) == 0);
    CHECK(outcome.emulatedSeconds >= 0.05);
}

TEST_CASE("screenshot tests fail with the reason, without running") {
    const auto outcome = roms::runRomTest(testOf(roms::Method::Screenshot), romWith({0x00}));
    CHECK(outcome.status == roms::Verdict::Fail);
    CHECK(outcome.reason == "needs the PPU (piece 3)");
    CHECK(outcome.emulatedSeconds == 0.0);
}

TEST_CASE("an unsupported cartridge fails with the loader's message") {
    auto rom = romWith({0x00});
    rom[0x0147] = 0x19; // MBC5
    const auto outcome = roms::runRomTest(testOf(roms::Method::Mooneye), rom);
    CHECK(outcome.status == roms::Verdict::Fail);
    CHECK(outcome.reason.find("unsupported cartridge type 0x19") != std::string::npos);
}
```

- [ ] **Step 2: Run to verify it fails**

```powershell
.\tools\dev.cmd cmake --build --preset release
```

Expected: FAIL, `cannot open include file: 'roms/Detectors.h'`.

- [ ] **Step 3: Implement the library**

`tools/roms/RomTests.h`:

```cpp
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace roms {

enum class Method { Blargg, Mooneye, Screenshot };

struct RomTest {
    std::string name;
    std::string rom;   // path relative to the Shootout's testroms/
    std::string group;
    Method method = Method::Screenshot;
    double runtime = 0.0;
    double limitSeconds = 0.0;
    std::vector<std::string> references;
};

struct TestList {
    std::string shootoutCommit;
    std::vector<RomTest> tests;
};

// Parses tools/roms/tests.json. Throws std::runtime_error on anything missing
// or unknown.
TestList parseTestList(std::string_view jsonText);

} // namespace roms
```

`tools/roms/RomTests.cpp`:

```cpp
#include "roms/RomTests.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace roms {

namespace {
using nlohmann::json;

const json& need(const json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) {
        throw std::runtime_error(std::string("tests.json: missing '") + key + "'");
    }
    return object.at(key);
}

Method methodFrom(const std::string& text) {
    if (text == "blargg") return Method::Blargg;
    if (text == "mooneye") return Method::Mooneye;
    if (text == "screenshot") return Method::Screenshot;
    throw std::runtime_error("tests.json: unknown method '" + text + "'");
}
} // namespace

TestList parseTestList(std::string_view jsonText) {
    const json doc = json::parse(jsonText);
    TestList list;
    list.shootoutCommit = need(doc, "shootout_commit").get<std::string>();
    for (const json& item : need(doc, "tests")) {
        RomTest t;
        t.name = need(item, "name").get<std::string>();
        t.rom = need(item, "rom").get<std::string>();
        t.group = need(item, "group").get<std::string>();
        t.method = methodFrom(need(item, "method").get<std::string>());
        t.runtime = need(item, "runtime").get<double>();
        t.limitSeconds = need(item, "limit_seconds").get<double>();
        t.references = need(item, "references").get<std::vector<std::string>>();
        list.tests.push_back(std::move(t));
    }
    return list;
}

} // namespace roms
```

`tools/roms/Detectors.h`:

```cpp
#pragma once

#include "core/Registers.h"
#include "core/Types.h"

#include <functional>
#include <string>
#include <string_view>

namespace roms {

using fourshades::u16;
using fourshades::u8;

enum class Verdict { Running, Pass, Fail };

// Blargg's tests print their result over serial: "Passed" or "Failed".
Verdict serialVerdict(std::string_view serialText);

// Blargg's memory protocol: once A001-A003 hold DE B0 61, A000 is a status
// (0x80 running, 0x00 passed, anything else failed) and A004 holds the result
// text, zero-terminated. `text` receives that text.
Verdict blarggMemoryVerdict(const std::function<u8(u16)>& peek, std::string* text);

// Mooneye's tests finish by executing LD B,B (0x40) with B,C,D,E,H,L holding
// 3,5,8,13,21,34 for a pass, or 0x42 in all six for a failure.
Verdict mooneyeVerdict(const fourshades::Registers& regs, u8 nextOpcode);

} // namespace roms
```

`tools/roms/Detectors.cpp`:

```cpp
#include "roms/Detectors.h"

namespace roms {

Verdict serialVerdict(std::string_view serialText) {
    if (serialText.find("Failed") != std::string_view::npos) return Verdict::Fail;
    if (serialText.find("Passed") != std::string_view::npos) return Verdict::Pass;
    return Verdict::Running;
}

Verdict blarggMemoryVerdict(const std::function<u8(u16)>& peek, std::string* text) {
    if (peek(0xA001) != 0xDE || peek(0xA002) != 0xB0 || peek(0xA003) != 0x61) {
        return Verdict::Running;
    }
    const u8 status = peek(0xA000);
    if (status == 0x80) {
        return Verdict::Running;
    }
    if (text != nullptr) {
        text->clear();
        for (u16 a = 0xA004; a < 0xA004 + 512; ++a) {
            const u8 c = peek(a);
            if (c == 0) break;
            text->push_back(static_cast<char>(c));
        }
    }
    return status == 0x00 ? Verdict::Pass : Verdict::Fail;
}

Verdict mooneyeVerdict(const fourshades::Registers& r, u8 nextOpcode) {
    if (nextOpcode != 0x40) return Verdict::Running;
    if (r.b == 3 && r.c == 5 && r.d == 8 && r.e == 13 && r.h == 21 && r.l == 34) return Verdict::Pass;
    if (r.b == 0x42 && r.c == 0x42 && r.d == 0x42 && r.e == 0x42 && r.h == 0x42 && r.l == 0x42) {
        return Verdict::Fail;
    }
    return Verdict::Running;
}

} // namespace roms
```

`tools/roms/RomRun.h`:

```cpp
#pragma once

#include "core/Types.h"
#include "roms/Detectors.h"
#include "roms/RomTests.h"

#include <cstdint>
#include <string>
#include <vector>

namespace roms {

constexpr std::uint64_t kCyclesPerSecond = 1048576; // M-cycles per emulated second

struct RomOutcome {
    Verdict status = Verdict::Fail;
    std::string reason;
    double emulatedSeconds = 0.0;
    std::string serial; // printable serial output, at most 2 KB
};

// Runs one test ROM headless until its author's pass/fail signal or its time
// limit. Pass/fail is decided here, never by the core.
RomOutcome runRomTest(const RomTest& test, std::vector<u8> romImage);

} // namespace roms
```

`tools/roms/RomRun.cpp`:

```cpp
#include "roms/RomRun.h"

#include "core/Cartridge.h"
#include "core/GameBoy.h"

#include <cstdio>
#include <memory>

namespace roms {

namespace {

std::string printable(const std::vector<u8>& bytes, std::size_t limit) {
    std::string out;
    for (const u8 b : bytes) {
        if (out.size() >= limit) break;
        out.push_back((b >= 0x20 && b < 0x7F) || b == '\n' ? static_cast<char>(b) : '?');
    }
    return out;
}

std::string lastLine(const std::string& text) {
    std::string trimmed = text;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == ' ')) trimmed.pop_back();
    const std::size_t cut = trimmed.find_last_of('\n');
    return cut == std::string::npos ? trimmed : trimmed.substr(cut + 1);
}

} // namespace

RomOutcome runRomTest(const RomTest& test, std::vector<u8> romImage) {
    RomOutcome out;
    if (test.method == Method::Screenshot) {
        out.reason = "needs the PPU (piece 3)";
        return out;
    }
    std::string error;
    auto cart = fourshades::Cartridge::load(std::move(romImage), &error);
    if (!cart) {
        out.reason = "cartridge: " + error;
        return out;
    }
    auto gb = std::make_unique<fourshades::GameBoy>(std::move(*cart));
    const auto limit = static_cast<std::uint64_t>(test.limitSeconds * static_cast<double>(kCyclesPerSecond));
    const auto peek = [&gb](u16 address) { return gb->peek(address); };
    std::uint64_t nextBlarggCheck = 0;

    while (gb->cycles() < limit) {
        const fourshades::Cpu& cpu = gb->cpu();
        if (cpu.state() == fourshades::Cpu::State::Locked) {
            out.reason = "CPU locked on an illegal opcode";
            break;
        }
        if (test.method == Method::Mooneye && cpu.state() == fourshades::Cpu::State::Running) {
            const Verdict v = mooneyeVerdict(cpu.regs, gb->peek(cpu.regs.pc));
            if (v != Verdict::Running) {
                out.status = v;
                out.reason = v == Verdict::Pass ? "Fibonacci registers at LD B,B" : "registers all 0x42";
                break;
            }
        }
        if (test.method == Method::Blargg && gb->cycles() >= nextBlarggCheck) {
            nextBlarggCheck = gb->cycles() + 8192;
            std::string text;
            Verdict v = blarggMemoryVerdict(peek, &text);
            if (v == Verdict::Running) {
                text = printable(gb->serialOutput(), 1 << 20);
                v = serialVerdict(text);
            }
            if (v != Verdict::Running) {
                out.status = v;
                out.reason = v == Verdict::Pass ? "Passed" : "Failed: " + lastLine(text).substr(0, 200);
                break;
            }
        }
        gb->step();
    }
    out.emulatedSeconds = static_cast<double>(gb->cycles()) / static_cast<double>(kCyclesPerSecond);
    out.serial = printable(gb->serialOutput(), 2048);
    if (out.reason.empty()) {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "timeout after %.1f s", out.emulatedSeconds);
        out.reason = buffer;
    }
    return out;
}

} // namespace roms
```

`tools/roms/CMakeLists.txt`:

```cmake
# The test-ROM harness. Never linked into the emulator itself.
add_library(rom_harness STATIC
    RomTests.cpp
    Detectors.cpp
    RomRun.cpp
)
target_link_libraries(rom_harness PUBLIC sst_harness)

add_executable(rom_runner rom_runner.cpp)
target_link_libraries(rom_runner PRIVATE rom_harness)
```

In the root `CMakeLists.txt`, add `add_subdirectory(tools/roms)` on the line after `add_subdirectory(tools/sst)`, and change `target_link_libraries(fourshades_tests PRIVATE sst_harness)` to `target_link_libraries(fourshades_tests PRIVATE sst_harness rom_harness)`.

- [ ] **Step 4: Run the unit tests**

```powershell
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
.\build\release\fourshades_tests.exe
```

Expected: `test cases: 78 | 78 passed` (70 + 8), and no warnings.

- [ ] **Step 5: Write the runner executable**

`tools/roms/rom_runner.cpp`:

```cpp
// Runs the Emulator Shootout's 167 DMG test ROMs against FourShades and writes
// build/rom-results.json for tools/scoreboard.py. Run from the repository root.
//
//   rom_runner                all 167 tests
//   rom_runner --only timer   tests whose name contains "timer" (marked partial)

#include "roms/RomRun.h"
#include "roms/RomTests.h"
#include "sst/Manifest.h"
#include "sst/Sha256.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kExpectedTests = 167;

struct Options {
    std::filesystem::path data = "tools/roms/data";
    std::filesystem::path manifest = "tools/roms/manifest.sha256";
    std::filesystem::path tests = "tools/roms/tests.json";
    std::filesystem::path out = "build/rom-results.json";
    std::string only;
};

int usage() {
    std::cerr << "usage: rom_runner [--data DIR] [--manifest FILE] [--tests FILE] [--out FILE] [--only TEXT]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) return usage();
        if (arg == "--data") options.data = argv[++i];
        else if (arg == "--manifest") options.manifest = argv[++i];
        else if (arg == "--tests") options.tests = argv[++i];
        else if (arg == "--out") options.out = argv[++i];
        else if (arg == "--only") options.only = argv[++i];
        else return usage();
    }

    try {
        const roms::TestList list = roms::parseTestList(sst::readBinaryFile(options.tests));
        if (list.tests.size() != kExpectedTests) {
            throw std::runtime_error("tests.json lists " + std::to_string(list.tests.size()) + " tests, expected 167");
        }

        // Every file the list names must match the committed manifest exactly.
        std::set<std::string> named;
        for (const auto& t : list.tests) {
            named.insert(t.rom);
            named.insert(t.references.begin(), t.references.end());
        }
        const auto entries = sst::parseManifest(sst::readBinaryFile(options.manifest));
        std::set<std::string> listed;
        std::vector<std::string> problems;
        for (const auto& entry : entries) {
            listed.insert(entry.path);
            const std::filesystem::path file = options.data / std::filesystem::path(entry.path);
            if (!std::filesystem::exists(file)) {
                problems.push_back(entry.path + ": missing");
            } else if (sst::sha256Hex(sst::readBinaryFile(file)) != entry.sha256) {
                problems.push_back(entry.path + ": hash does not match the manifest");
            }
        }
        if (listed != named) {
            problems.push_back("manifest and tests.json name different files");
        }
        if (!problems.empty()) {
            for (const auto& p : problems) std::cerr << "data check: " << p << '\n';
            std::cerr << "refusing to run: run python tools/roms/fetch_roms.py\n";
            return 2;
        }

        const auto start = std::chrono::steady_clock::now();
        const bool partial = !options.only.empty();
        nlohmann::json results = nlohmann::json::array();
        std::size_t passing = 0;
        std::size_t total = 0;
        std::vector<std::string> groupOrder;
        std::map<std::string, std::pair<int, int>> groups; // group -> (passing, total)
        std::map<std::string, std::string> firstFailure;

        for (const roms::RomTest& test : list.tests) {
            if (partial && test.name.find(options.only) == std::string::npos) continue;
            ++total;
            const std::string bytes = sst::readBinaryFile(options.data / std::filesystem::path(test.rom));
            const auto outcome = roms::runRomTest(test, std::vector<fourshades::u8>(bytes.begin(), bytes.end()));
            const bool pass = outcome.status == roms::Verdict::Pass;
            if (!groups.count(test.group)) groupOrder.push_back(test.group);
            auto& g = groups[test.group];
            g.second += 1;
            if (pass) {
                ++passing;
                g.first += 1;
            } else if (!firstFailure.count(test.group)) {
                firstFailure[test.group] = test.name + ": " + outcome.reason;
            }
            const char* method = test.method == roms::Method::Blargg ? "blargg"
                               : test.method == roms::Method::Mooneye ? "mooneye" : "screenshot";
            results.push_back({{"name", test.name}, {"group", test.group}, {"method", method},
                               {"status", pass ? "pass" : "fail"}, {"reason", outcome.reason},
                               {"emulated_seconds", outcome.emulatedSeconds}, {"serial", outcome.serial}});
        }
        if (total == 0) throw std::runtime_error("--only matched no tests");

        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const nlohmann::json doc = {
            {"suite", "GBEmulatorShootout (DMG)"}, {"shootout_commit", list.shootoutCommit},
            {"partial", partial}, {"total", total}, {"passing", passing},
            {"elapsed_seconds", seconds}, {"tests", results},
        };
        if (options.out.has_parent_path()) std::filesystem::create_directories(options.out.parent_path());
        std::ofstream out(options.out);
        out << doc.dump(1) << '\n';
        if (!out.good()) {
            std::cerr << "error: could not write " << options.out.string() << '\n';
            return 2;
        }

        for (const auto& name : groupOrder) {
            const auto& g = groups[name];
            std::printf("  %-18s %3d / %-3d  %s\n", name.c_str(), g.first, g.second,
                        firstFailure.count(name) ? firstFailure[name].c_str() : "");
        }
        std::printf("%s%zu / %zu passing  (%.1f s)\nresults: %s\n", partial ? "selected: " : "test roms: ",
                    passing, total, seconds, options.out.string().c_str());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
```

- [ ] **Step 6: Build and take the first real test-ROM score**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\build\release\tools\roms\rom_runner.exe
```

Expected: a per-group table and `test roms: N / 167 passing`, where N is whatever the code honestly scores. Record the full table in the report. Everything in `screen`, `sound`, `oam bug`, `ppu timing`, `mbc2 / mbc5` and `mbc3 / rtc` should fail with a reason. Tasks 9 and 10 raise the score, so this task changes no emulator code to move N.

Also check the data guard: append a byte to one downloaded ROM, confirm the runner exits 2 with `data check: ...`, then restore it with `python tools/roms/fetch_roms.py`.

- [ ] **Step 7: Commit**

```powershell
git add tools/roms/RomTests.h tools/roms/RomTests.cpp tools/roms/Detectors.h tools/roms/Detectors.cpp tools/roms/RomRun.h tools/roms/RomRun.cpp tools/roms/rom_runner.cpp tools/roms/CMakeLists.txt CMakeLists.txt tests/test_rom_harness.cpp
git commit -m "feat(roms): headless test-ROM runner with Blargg and Mooneye detectors" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
```

---

### Task 8: Scoreboard, per-group table, stricter isolation, CI

**Files:**
- Modify (replace whole file): `tools/scoreboard.py`, `tools/test_scoreboard.py`
- Modify: `tools/check_core_isolation.py`, `.github/workflows/ci.yml`, `README.md`, `CLAUDE.md`
- Modify (generated): `scoreboard.json`, the README blocks

**Interfaces:**
- Consumes: `build/sst-results.json` (piece 1) and `build/rom-results.json` (Task 7).
- Produces: `python tools/scoreboard.py update|check <sst-results.json> <rom-results.json>`, and `scoreboard.run(mode, sst_path, rom_path, readme_path, board_path) -> int`.
- Produces: README blocks between `<!-- scoreboard:start -->`/`<!-- scoreboard:end -->` (two lines, as now) and `<!-- groups:start -->`/`<!-- groups:end -->` (a generated per-group table).

- [ ] **Step 1: Write the failing tests**

Replace `tools/test_scoreboard.py` with:

```python
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scoreboard  # noqa: E402


def sst_results(passing, partial=False, total=500):
    files = [{"name": f"{i:03}", "status": "pass" if i < passing else "fail"} for i in range(total)]
    return {"suite": "SingleStepTests/sm83", "commit": "abc", "partial": partial,
            "total_files": total, "passing_files": passing, "files": files}


def rom_results(passing_by_group, partial=False):
    tests = []
    for group, (passing, total) in passing_by_group.items():
        for i in range(total):
            ok = i < passing
            tests.append({"name": f"{group}/t{i}.gb", "group": group, "method": "mooneye",
                          "status": "pass" if ok else "fail",
                          "reason": "Fibonacci registers at LD B,B" if ok else "timeout after 6.5 s",
                          "emulated_seconds": 1.0, "serial": ""})
    n = sum(1 for t in tests if t["status"] == "pass")
    return {"suite": "GBEmulatorShootout (DMG)", "shootout_commit": "def", "partial": partial,
            "total": len(tests), "passing": n, "elapsed_seconds": 1.0, "tests": tests}


FULL = {"timer": (3, 13), "screen": (0, 32), "cpu instructions": (11, 11), "other": (0, 111)}  # 167 in all


class ScoreboardTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.readme = self.dir / "README.md"
        self.board = self.dir / "scoreboard.json"
        self.sst = self.dir / "sst.json"
        self.rom = self.dir / "rom.json"
        self.readme.write_text(
            "intro\n<!-- scoreboard:start -->\nold\n<!-- scoreboard:end -->\nmiddle\n"
            "<!-- groups:start -->\nold\n<!-- groups:end -->\noutro\n", encoding="utf-8")

    def write(self, sst, rom):
        self.sst.write_text(json.dumps(sst), encoding="utf-8")
        self.rom.write_text(json.dumps(rom), encoding="utf-8")

    def run_mode(self, mode):
        return scoreboard.run(mode, self.sst, self.rom, self.readme, self.board)

    def test_line_format(self):
        self.assertEqual(scoreboard.line("cpu instructions", 250, 500),
                         "cpu instructions  " + "█" * 8 + "░" * 8 + "   250 / 500")
        self.assertEqual(scoreboard.line("test roms", 14, 167),
                         "test roms         " + "█" * 1 + "░" * 15 + "    14 / 167")

    def test_update_then_check_passes(self):
        self.write(sst_results(499), rom_results(FULL))
        self.assertEqual(self.run_mode("update"), 0)
        self.assertEqual(self.run_mode("check"), 0)
        text = self.readme.read_text(encoding="utf-8")
        self.assertIn("499 / 500", text)
        self.assertIn("14 / 167", text)
        self.assertIn("| timer | 3 / 13 | `timer/t3.gb`: timeout after 6.5 s |", text)
        self.assertIn("| cpu instructions | 11 / 11 |  |", text)
        self.assertTrue(text.startswith("intro\n") and text.endswith("outro\n"))
        self.assertIn("\nmiddle\n", text)
        board = json.loads(self.board.read_text(encoding="utf-8"))
        self.assertEqual(board["test_roms"]["passing"], 14)
        self.assertEqual(board["test_roms"]["groups"][0]["group"], "timer")

    def test_check_fails_when_the_group_table_is_edited(self):
        self.write(sst_results(499), rom_results(FULL))
        self.run_mode("update")
        text = self.readme.read_text(encoding="utf-8").replace("3 / 13", "13 / 13")
        self.readme.write_text(text, encoding="utf-8")
        self.assertEqual(self.run_mode("check"), 1)

    def test_check_fails_when_the_rom_score_changes(self):
        self.write(sst_results(499), rom_results(FULL))
        self.run_mode("update")
        self.write(sst_results(499), rom_results({**FULL, "timer": (4, 13)}))
        self.assertEqual(self.run_mode("check"), 1)

    def test_partial_or_short_rom_results_are_refused(self):
        self.write(sst_results(499), rom_results(FULL, partial=True))
        with self.assertRaises(SystemExit):
            self.run_mode("update")
        self.write(sst_results(499), rom_results({"timer": (3, 13)}))
        with self.assertRaises(SystemExit):
            self.run_mode("update")

    def test_inconsistent_results_are_refused(self):
        data = sst_results(10)
        data["passing_files"] = 11
        self.write(data, rom_results(FULL))
        with self.assertRaises(SystemExit):
            self.run_mode("update")
        rom = rom_results(FULL)
        rom["passing"] += 1
        self.write(sst_results(499), rom)
        with self.assertRaises(SystemExit):
            self.run_mode("update")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify it fails**

```powershell
python -m unittest discover -s tools -p "test_*.py"
```

Expected: FAIL (errors: `run()` takes 4 positional arguments, and `groups` markers aren't handled).

- [ ] **Step 3: Implement the scoreboard**

Replace `tools/scoreboard.py` with:

```python
"""
Generate the README scoreboard, the per-group table and scoreboard.json from
the SingleStepTests and test-ROM results files.

    python tools/scoreboard.py update build/sst-results.json build/rom-results.json
    python tools/scoreboard.py check  build/sst-results.json build/rom-results.json

`check` exits 1 if the committed README blocks or scoreboard.json differ from
what the results say. CI runs it, so a hand-edited score fails the build,
whether it's too high or too low.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
START, END = "<!-- scoreboard:start -->", "<!-- scoreboard:end -->"
GROUPS_START, GROUPS_END = "<!-- groups:start -->", "<!-- groups:end -->"
BAR = 16
CPU_FILES = 500
ROM_TESTS = 167  # the Shootout's DMG tests; see docs/superpowers/specs/2026-09-11-machine-test-roms-design.md


def load_sst(path):
    results = json.loads(Path(path).read_text(encoding="utf-8"))
    if results.get("partial"):
        raise SystemExit("error: partial SingleStepTests results (--only was used); run the full suite")
    files = results.get("files", [])
    if results.get("total_files") != CPU_FILES or len(files) != CPU_FILES:
        raise SystemExit(f"error: expected SingleStepTests results for {CPU_FILES} files")
    if sum(1 for f in files if f["status"] == "pass") != results.get("passing_files"):
        raise SystemExit("error: SingleStepTests results are inconsistent")
    return results


def load_roms(path):
    results = json.loads(Path(path).read_text(encoding="utf-8"))
    if results.get("partial"):
        raise SystemExit("error: partial test-ROM results (--only was used); run all tests")
    tests = results.get("tests", [])
    if results.get("total") != ROM_TESTS or len(tests) != ROM_TESTS:
        raise SystemExit(f"error: expected test-ROM results for {ROM_TESTS} tests")
    if sum(1 for t in tests if t["status"] == "pass") != results.get("passing"):
        raise SystemExit("error: test-ROM results are inconsistent")
    return results


def groups_of(rom):
    order, groups = [], {}
    for t in rom["tests"]:
        g = groups.get(t["group"])
        if g is None:
            g = groups[t["group"]] = {"group": t["group"], "passing": 0, "total": 0, "first_failure": None}
            order.append(t["group"])
        g["total"] += 1
        if t["status"] == "pass":
            g["passing"] += 1
        elif g["first_failure"] is None:
            g["first_failure"] = {"test": t["name"], "reason": t["reason"]}
    return [groups[name] for name in order]


def scoreboard(sst, rom):
    return {
        "cpu_instructions": {"passing": sst["passing_files"], "total": CPU_FILES},
        "test_roms": {"passing": rom["passing"], "total": ROM_TESTS, "groups": groups_of(rom)},
        "source": {"sst_suite": sst["suite"], "sst_commit": sst["commit"],
                   "rom_suite": rom["suite"], "shootout_commit": rom["shootout_commit"]},
    }


def line(label, passing, total):
    filled = BAR * passing // total
    return f"{label:<18}{'█' * filled}{'░' * (BAR - filled)}  {passing:>4} / {total}"


def score_block(board):
    cpu, roms = board["cpu_instructions"], board["test_roms"]
    return "\n".join([START, "```", line("cpu instructions", cpu["passing"], cpu["total"]),
                      line("test roms", roms["passing"], roms["total"]), "```", END])


def groups_block(board):
    rows = [GROUPS_START, "| group | passing | first failing test |", "|---|---|---|"]
    for g in board["test_roms"]["groups"]:
        failure = g["first_failure"]
        cell = "" if failure is None else f"`{failure['test']}`: {failure['reason']}".replace("|", "/")
        rows.append(f"| {g['group']} | {g['passing']} / {g['total']} | {cell} |")
    rows.append(GROUPS_END)
    return "\n".join(rows)


def replace_block(text, start_marker, end_marker, block):
    start, end = text.find(start_marker), text.find(end_marker)
    if start == -1 or end == -1 or end < start:
        raise SystemExit(f"error: README has no {start_marker} ... {end_marker} markers")
    return text[:start] + block + text[end + len(end_marker):]


def run(mode, sst_path, rom_path, readme_path, board_path):
    board = scoreboard(load_sst(sst_path), load_roms(rom_path))
    board_json = json.dumps(board, indent=2) + "\n"
    readme = Path(readme_path).read_text(encoding="utf-8")
    new_readme = replace_block(readme, START, END, score_block(board))
    new_readme = replace_block(new_readme, GROUPS_START, GROUPS_END, groups_block(board))

    if mode == "update":
        Path(readme_path).write_text(new_readme, encoding="utf-8")
        Path(board_path).write_text(board_json, encoding="utf-8")
        print(f"scoreboard updated: cpu instructions {board['cpu_instructions']['passing']} / {CPU_FILES}, "
              f"test roms {board['test_roms']['passing']} / {ROM_TESTS}")
        return 0

    problems = []
    if new_readme != readme:
        problems.append("README.md scoreboard or group table")
    board_file = Path(board_path)
    if not board_file.exists() or board_file.read_text(encoding="utf-8") != board_json:
        problems.append("scoreboard.json")
    if problems:
        print("scoreboard does not match the test results: " + ", ".join(problems))
        print("run: python tools/scoreboard.py update build/sst-results.json build/rom-results.json")
        return 1
    print("scoreboard matches the test results")
    return 0


def main(argv):
    if len(argv) != 4 or argv[1] not in ("update", "check"):
        raise SystemExit(__doc__)
    return run(argv[1], argv[2], argv[3], ROOT / "README.md", ROOT / "scoreboard.json")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

- [ ] **Step 4: Tighten the isolation check**

In `tools/check_core_isolation.py`, replace the `FORBIDDEN = ...` line with:

```python
FORBIDDEN = re.compile(
    r"\b(sst|json|fopen|ifstream|ofstream|fstream|singlesteptests"
    r"|blargg|mooneye|shootout|passed|failed|fibonacci)\b",
    re.IGNORECASE)
# The byte signature one test suite uses to report results, in any spelling.
SIGNATURE = re.compile(r"(0x)?de[\s,_]*(0x)?b0[\s,_]*(0x)?61", re.IGNORECASE)
```

and change `if FORBIDDEN.search(text):` to `if FORBIDDEN.search(text) or SIGNATURE.search(text):`.

Update the module docstring's last sentence to: `That keeps it impossible, not just discouraged, for core code to look at the tests or to know what a passing result looks like.`

Prove it can fail: temporarily add `// 0xDE, 0xB0, 0x61` to the end of `src/core/Types.h`, run the check (expect `core isolation check failed`), then remove the line (expect `passed`), and confirm `git diff --quiet -- src/core/Types.h`.

- [ ] **Step 5: Put the group-table markers in the README, and update CI and CLAUDE.md**

In `README.md`, directly after the paragraph that ends `starts moving in piece 2.` (before the **Correction** paragraph), insert:

```markdown
The test-ROM line, group by group, with the first test each group fails:

<!-- groups:start -->
<!-- groups:end -->
```

Then replace that same status paragraph's last sentence, `The test-ROM line starts moving in piece 2.`, with `The test-ROM line counts the 167 original-Game-Boy tests that gbdev's Emulator Shootout runs; a test passes only on its author's own pass signal.`

In the README's **Building** code block, add after the `sst_runner.exe` line:

```powershell
python tools/roms/fetch_roms.py            # the test ROMs, pinned and hash-checked
.\build\release\tools\roms\rom_runner.exe  # score the machine against the test ROMs
```

In the **Repository layout** block, add a line `tools/roms/                the test-ROM harness, its pinned test list and manifest` after the `tools/sst/` line.

In `CLAUDE.md`:
- change the scoreboard command to `python tools/scoreboard.py update build/sst-results.json build/rom-results.json`;
- add `python tools/roms/fetch_roms.py` and `.\build\release\tools\roms\rom_runner.exe` to the Building list;
- add these two bullets under **Never**:
  - `- Edit tools/roms/data/, tools/roms/manifest.sha256 or tools/roms/tests.json by hand (regenerate with make_test_list.py).`
  - `- Raise a test's time limit, or special-case a ROM, to make it pass.`

In `.github/workflows/ci.yml`, replace the `Scoreboard matches the run` step and everything after it with:

```yaml
      - uses: actions/cache@v4
        with:
          path: tools/roms/data
          key: roms-38b926bdbc26993d1b4c43e97979ecc66287bf02

      - name: Fetch test ROMs (pinned, hash-checked)
        run: python tools/roms/fetch_roms.py

      - name: Run test ROMs
        run: .\build\release\tools\roms\rom_runner.exe

      - name: Scoreboard matches the runs
        run: python tools/scoreboard.py check build/sst-results.json build/rom-results.json

      - name: Keep the results
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: results
          path: |
            build/sst-results.json
            build/rom-results.json
          if-no-files-found: error
```

- [ ] **Step 6: Run everything and generate the scoreboard**

```powershell
python -m unittest discover -s tools -p "test_*.py"
python tools/check_core_isolation.py
.\tools\dev.cmd cmake --build --preset release
.\build\release\tools\sst\sst_runner.exe
.\build\release\tools\roms\rom_runner.exe
python tools/scoreboard.py update build/sst-results.json build/rom-results.json
python tools/scoreboard.py check build/sst-results.json build/rom-results.json
```

Expected:
- the Python tests: `OK` (6 tests);
- the isolation check passes;
- SST `499 / 500`;
- the ROM runner prints its table;
- `scoreboard updated: cpu instructions 499 / 500, test roms N / 167`;
- `scoreboard matches the test results`.

- [ ] **Step 7: Commit, push, and confirm CI**

```powershell
git add tools/scoreboard.py tools/test_scoreboard.py tools/check_core_isolation.py .github/workflows/ci.yml README.md CLAUDE.md scoreboard.json
git commit -m "feat: test-ROM scoreboard with a per-group table, checked in CI" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
git push origin main
```

Poll `https://api.github.com/repos/doozleb/FourShades/commits/<HEAD sha>/check-runs` in a single PowerShell loop with `Start-Sleep`, for up to about 20 minutes, until the run completes. It must conclude `success`. If it fails, fix the cause (never by removing a check) and push again.

---

### Tasks 9 and 10: Raise the score, test by test

These two tasks can't be written as code in advance, because the failures are
whatever the test ROMs reveal. Each is a disciplined debugging task. It uses
the superpowers:systematic-debugging skill: no fix without a named root cause.

**The rules for both tasks:**
- **Scope:**
  - Task 9's groups: `cpu instructions`, `cpu timing`.
  - Task 10's groups: `timer`, `cpu & interrupts`, `boot state`, `oam dma`, `serial`, `mbc1`.
  - Don't work on other groups. `ppu timing`, `oam bug`, `screen`, `sound`, `mbc2 / mbc5` and `mbc3 / rtc` belong to later pieces.
- **Evidence first, for each failing test:**
  - run it alone: `.\build\release\tools\roms\rom_runner.exe --only "<part of its name>" --out build/rom-partial.json`;
  - read its `reason` and `serial` in `build/rom-partial.json`;
  - read the test's own source or README where available (Blargg's `.s` files ship beside the ROMs in the Shootout; Mooneye's sources are at https://github.com/Gekkio/mooneye-test-suite);
  - read the relevant Pan Docs page.
  - State the root cause in one sentence before changing code.
- **Fix in the core, the way the hardware works.**
  - Never special-case a ROM, a name, an address pattern or a register value.
  - Never raise a time limit.
  - Never touch the detectors, the manifests, `tests.json` or the downloaded data.
  - If the fix contradicts Pan Docs, stop and record a divergence instead.
- **Each fix gets a failing unit test first,** in the matching `tests/test_*.cpp`. The test reproduces the root cause in a few lines, not by running the ROM.
- **After every fix:**
  - the full SST run must still say `499 / 500` with only `10` failing;
  - all unit tests pass;
  - the isolation check passes;
  - rerun the full ROM runner and confirm no previously passing test now fails. Any regression is fixed before anything else.
- **One commit per root cause.** The message names it, e.g. `fix(timer): TIMA write in the reload cycle was not ignored`, and the commit also contains the regenerated `README.md` and `scoreboard.json` (`python tools/scoreboard.py update build/sst-results.json build/rom-results.json`).
- **Push after each commit that raises the score,** and confirm CI is green before the next.
- **Stop condition:**
  - every test in scope passes; or
  - each remaining one has a written explanation in the task report:
    - what it tests;
    - why it fails;
    - what it needs (for example "needs PPU mode timing, piece 3");
    - whether it's a candidate for `docs/known-divergences.md`.
  - A test you couldn't crack in about 90 minutes of evidence-driven work also gets that explanation, and moves on. Don't guess.

### Task 9: CPU groups (Blargg cpu_instrs, instr_timing, mem_timing, mem_timing-2, halt_bug)

**Files:** whatever the root causes require, in `src/core/` and `tests/`, plus the generated `README.md` and `scoreboard.json`.

- [ ] **Step 1:** Run the full ROM runner and list every failing test in `cpu instructions` and `cpu timing`, with its reason.
- [ ] **Step 2:** Fix `cpu instructions` first. All 11 must pass: they are the independent cross-check that the CPU wasn't fitted to SingleStepTests. If one fails because of a CPU bug that SST didn't catch, write it down prominently in the report. That's a finding worth publishing.
- [ ] **Step 3:** Then `cpu timing` (instr_timing, mem_timing ×2 and mem_timing-2 ×3 need the timer to be exact; halt_bug needs the HALT bug).
- [ ] **Step 4:** Report the final group counts, every root cause with its commit, and an explanation for anything left failing.

### Task 10: The machine groups (timer, cpu & interrupts, boot state, oam dma, serial, mbc1)

**Files:** whatever the root causes require, in `src/core/` and `tests/`, plus the generated `README.md` and `scoreboard.json`.

- [ ] **Step 1:** Run the full ROM runner and list every failing test in scope, with its reason.
- [ ] **Step 2:** `timer` first (13 Mooneye tests). These arbitrate the within-M-cycle order (advance, then access). If they show the order must change, change it in `GameBoy::read`/`write`, record the reasoning in the commit message and in a "Timing model" note at the end of `docs/known-divergences.md`, and re-verify SST.
- [ ] **Step 3:** Then `cpu & interrupts` (instruction timing, EI/DI/HALT, interrupt timing, `ie_push`, register bits), then `boot state`, `oam dma`, `serial` and `mbc1`. Some `cpu & interrupts` tests read PPU registers; if one fails only for that reason, explain it and move on. Record `mbc1/multicart_rom_8Mb` as out of scope (MBC1M wiring, piece 4) unless it passes on its own.
- [ ] **Step 4:** Report the final group counts, every root cause with its commit, and an explanation for anything left failing.

---

### Task 11: Close piece 2

**Files:**
- Modify: `README.md` (status paragraph only, never the generated blocks), `docs/known-divergences.md` (if Tasks 9–10 added entries), `docs/superpowers/specs/2026-09-11-machine-test-roms-design.md` (Status line)

- [ ] **Step 1: Check every success criterion from the spec, with actual output**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\build\release\fourshades_tests.exe
python -m unittest discover -s tools -p "test_*.py"
python tools/check_core_isolation.py
.\build\release\tools\sst\sst_runner.exe
.\build\release\tools\roms\rom_runner.exe
python tools/scoreboard.py check build/sst-results.json build/rom-results.json
```

Record:
1. SST 499/500;
2. all 167 tests have a status and reason;
3. whether all 11 cpu_instrs pass;
4. the timer group and instr_timing results;
5. the README shows N / 167 with the table;
6. the ROM runner's `elapsed_seconds`, which must be under 300.

- [ ] **Step 2: Update the README status paragraph.** Say that piece 2 is done, that the test-ROM line is real, which groups are complete, and that the remaining groups wait on the PPU (piece 3), cartridge chips (piece 4) and sound (piece 5). Link `docs/known-divergences.md` if Tasks 9–10 added entries. Keep it factual and short.

- [ ] **Step 3: Mark the spec implemented.** Change its Status line to `implemented 2026-09-11 (test roms N / 167; see README)`, with the real N.

- [ ] **Step 4: Commit, push, confirm CI green**

```powershell
git add README.md docs/superpowers/specs/2026-09-11-machine-test-roms-design.md docs/known-divergences.md
git commit -m "docs: piece 2 complete" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
git push origin main
```

Poll the check-runs API until the run completes; it must be `success`.
