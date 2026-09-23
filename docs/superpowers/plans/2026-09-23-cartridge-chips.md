# Piece 4: Cartridge Chips Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Support MBC2, MBC3 (with its real-time clock), MBC5 and MBC1
multicart cartridges, so that 20 more test ROMs pass and Pokémon loads.

**Architecture:** `Cartridge` keeps header parsing, ROM/RAM storage and the
save interface, and delegates banking to an `Mbc` interface with one small
implementation per chip under `src/core/mbc/`. The real-time clock is its
own class, ticked from `GameBoy::tick()` in emulated M-cycles; real elapsed
time enters only through the save file, in `app/`.

**Tech Stack:** C++20, MSVC (Visual Studio 2026), CMake + Ninja, doctest.
Windows only. `cmake` and `cl` are not on PATH — every build command goes
through `tools\dev.cmd`.

**Spec:** `docs/superpowers/specs/2026-09-23-cartridge-chips-design.md`.
Read it before Task 1; it is the authority on behaviour and this plan is the
authority on sequence.

## A note on this plan's form

This plan gives exact register layouts, exact masks, exact test cases and
exact expected values, but it does **not** contain large blocks of finished
implementation code.

That is deliberate and it is the piece-3 lesson: implementation code written
into that plan from memory was wrong in five of its eleven tasks — a palette
sequence, a dot count, a flip axis, a hook's position and a register
clobber — and each wrong block cost a correction round after an implementer
had faithfully typed it in. The piece-3b plan carried values and signatures
without invented code, and needed no corrections.

So: where this plan states a number, a mask, a signature or an expected
result, **use it verbatim — it has been checked against the ROMs and the
documentation**. Where it describes behaviour, write the code yourself from
Pan Docs and the spec.

## Global Constraints

- Follow Pan Docs (https://gbdev.io/pandocs/) over an emulator-generated
  test. A hardware-verified test (Mooneye marks these) outranks a Pan Docs
  sentence that turns out to be a simplification. Either way the decision
  goes in `docs/known-divergences.md` with the evidence — never silently.
- `src/core/` must not depend on the tests: no test names, no file or JSON
  access, no includes from `tools/`. `tools/check_core_isolation.py`
  enforces it and runs in CI.
- Never special-case a test, a test name, a ROM or an address pattern only a
  test uses.
- Never loosen `tools/sst/SstCompare.cpp`, `tools/roms/Detectors.cpp`,
  `tools/roms/RomRun.cpp`, any image comparator, or a test's time limit to
  make something pass.
- Never edit `tools/sst/data/`, `tools/sst/manifest.sha256`,
  `tools/roms/data/`, `tools/roms/manifest.sha256` or
  `tools/roms/tests.json` by hand.
- Never edit the README scoreboard block or `scoreboard.json` by hand. After
  a full run: `python tools/scoreboard.py update build/sst-results.json build/rom-results.json`.
- Stage explicit paths in every commit. Never `git add -A` or `git add .`.
- New `src/core/**` files must be added to the explicit source list in
  `CMakeLists.txt` (lines 20–33). Test files are globbed and need no edit.
- The core never reads a wall clock, the filesystem or the environment.
  `advanceRtcSeconds(std::uint64_t)` takes a number from its caller.

## Build and test commands

```
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
.\build\release\fourshades_tests.exe
.\build\release\fourshades_tests.exe -ts="<test case name substring>"
.\build\release\tools\roms\rom_runner.exe --only emulator-only/mbc5 --out build\rom-scratch.json
python tools/check_core_isolation.py
```

`rom_runner` with `--only TEXT` runs the tests whose name or group contains
TEXT — a case-sensitive substring of either — and marks the result partial.
A full run is `rom_runner.exe` with no arguments, writing
`build/rom-results.json`; a partial run writes somewhere else, because
`build/rom-results.json` is what the scoreboard reads and a partial result
must never reach it.

## File structure

| File | Responsibility |
| --- | --- |
| `src/core/mbc/Mbc.h` | the interface: `romBank`, `readRam`, `writeRam`, `writeControl`, `tick` |
| `src/core/mbc/MbcNone.h/.cpp` | type 0x00: bank 1 fixed, writes ignored |
| `src/core/mbc/Mbc1.h/.cpp` | MBC1, later gaining the multicart bank width |
| `src/core/mbc/Mbc2.h/.cpp` | MBC2 and its 512 × 4-bit built-in RAM |
| `src/core/mbc/Mbc3.h/.cpp` | MBC3, owning an `Rtc` when the type has a timer |
| `src/core/mbc/Mbc5.h/.cpp` | MBC5 |
| `src/core/mbc/Rtc.h/.cpp` | the clock: registers, ticking, latching, halt, day carry |
| `src/core/Cartridge.h/.cpp` | header parsing, storage, the save interface, dispatch to the `Mbc` |
| `app/Save.h/.cpp` | the RTC footer on disk and the catch-up on load |
| `tests/test_mbc*.cpp`, `tests/test_rtc.cpp` | one file per chip |

---

### Task 1: The Mbc interface, with MBC1 and RomOnly behind it

Pure refactor. **No behaviour changes at all.** Its whole value is that the
existing tests and the 12 passing MBC1 ROMs prove the new shape before any
new chip exists.

**Files:**
- Create: `src/core/mbc/Mbc.h`, `src/core/mbc/MbcNone.h`, `src/core/mbc/MbcNone.cpp`, `src/core/mbc/Mbc1.h`, `src/core/mbc/Mbc1.cpp`
- Modify: `src/core/Cartridge.h`, `src/core/Cartridge.cpp`, `CMakeLists.txt`
- Test: `tests/test_cartridge.cpp` (unchanged — it must still pass as written)

**Interfaces — produces:**

```cpp
namespace fourshades {

class Mbc {
public:
    virtual ~Mbc() = default;
    virtual std::size_t romBank(u16 address) const = 0;
    virtual std::optional<u8> readRam(u16 address) const = 0;
    virtual void writeRam(u16 address, u8 value) = 0;
    virtual void writeControl(u16 address, u8 value) = 0;
    virtual void tick() {}
};

} // namespace fourshades
```

Rules the interface carries, which every later chip depends on:

- `romBank(address)` is called for addresses below 0x8000 and returns an
  **unmasked** bank number. `Cartridge` masks it with `bank & (romBanks_ - 1)`
  — bank counts are always powers of two. Keeping the mask in one place is
  why MBC5's 9-bit register and MBC2's 4-bit one need no special handling
  in the caller.
- `readRam(address)` is called for 0xA000–0xBFFF only. `std::nullopt` means
  open bus, and `Cartridge::read` turns that into 0xFF.
- `writeControl(address, value)` is called for addresses below 0x8000 only.
- `tick()` is one M-cycle. The base implementation does nothing, so only
  MBC3 overrides it.

`Cartridge` holds `std::unique_ptr<Mbc> mbc_`. That makes `Cartridge`
move-only, which it already effectively is — check every existing use
compiles, including `GameBoy(Cartridge)` and the ROM runner.

`Cartridge::Kind` keeps its existing values and gains none in this task.

- [ ] **Step 1: Read the spec's Architecture section, then the current `src/core/Cartridge.cpp` in full.** It is 121 lines. Note that `romOffset`, `ramOffset`, `read` and `write` between them hold all MBC1 state: `ramEnabled_`, `bankLow_`, `bankHigh_`, `mode1_`.
- [ ] **Step 2: Create `src/core/mbc/Mbc.h`** with the interface exactly as given above.
- [ ] **Step 3: Create `MbcNone`** — `romBank` returns 0 below 0x4000 and 1 at or above it; `readRam` returns `std::nullopt`; `writeRam` and `writeControl` do nothing.
- [ ] **Step 4: Create `Mbc1`**, moving the existing logic across unchanged. It needs the RAM bank count to mask its RAM bank, and a reference to the cartridge's RAM vector to read and write it. Keep the existing mode-1 behaviour exactly: bank high bits reach 0000–3FFF only in mode 1, and RAM banks switch only in mode 1.
- [ ] **Step 5: Rewrite `Cartridge` to dispatch.** `read`/`write` become: below 0x8000 → `rom_[romBank(address) * 0x4000 + (address & 0x3FFF)]` for reads and `mbc_->writeControl` for writes; 0xA000–0xBFFF → `mbc_->readRam` / `mbc_->writeRam`. Construction picks the implementation from the header type, exactly the same set as today (0x00; 0x01–0x03).
- [ ] **Step 6: Add the four new .cpp files to the `fourshades_core` source list in `CMakeLists.txt`.**
- [ ] **Step 7: Build and run the whole unit suite.**

```
.\tools\dev.cmd cmake --build --preset release
.\build\release\fourshades_tests.exe
```

Expected: the same pass count as before the task, with zero test files
edited. If a test needed editing, the refactor changed behaviour — find out
why rather than editing the test.

- [ ] **Step 8: Run the MBC1 ROMs.**

```
.\build\release\tools\roms\rom_runner.exe --only mbc1 --out build\rom-mbc1.json
```

Expected: 12 of 13, `multicart_rom_8Mb` the only failure — identical to
before.

- [ ] **Step 9: Run `python tools/check_core_isolation.py`.** Expected: passes.
- [ ] **Step 10: Commit.** Stage `src/core/mbc/*`, `src/core/Cartridge.*`, `CMakeLists.txt`.

---

### Task 2: MBC5

**Files:**
- Create: `src/core/mbc/Mbc5.h`, `src/core/mbc/Mbc5.cpp`, `tests/test_mbc5.cpp`
- Modify: `src/core/Cartridge.cpp` (accept types 0x19–0x1E), `src/core/Cartridge.h` (`Kind::Mbc5`), `CMakeLists.txt`

**Interfaces — consumes:** the `Mbc` interface from Task 1.

**Behaviour — every value here is checked, use it verbatim:**

| range | effect |
| --- | --- |
| 0000–1FFF | RAM enable: enabled when `(value & 0x0F) == 0x0A`, disabled otherwise |
| 2000–2FFF | ROM bank bits 0–7 (all 8 bits of the value) |
| 3000–3FFF | ROM bank bit 8 (`value & 0x01`) |
| 4000–5FFF | RAM bank, `value & 0x0F`; on rumble types `value & 0x07` |
| 6000–7FFF | nothing |

- **Bank 0 is selectable.** No 0 → 1 remap. This is the difference from
  MBC1 that the ROM tests check first.
- 0000–3FFF always reads bank 0.
- Header types: 0x19 (MBC5), 0x1A (+RAM), 0x1B (+RAM+BATTERY), 0x1C
  (RUMBLE), 0x1D (RUMBLE+RAM), 0x1E (RUMBLE+RAM+BATTERY).
- RAM exists for 0x1A, 0x1B, 0x1D, 0x1E, sized from the header's RAM code
  by the existing `ramBanksFor`. Battery: 0x1B and 0x1E.
- Rumble types are 0x1C–0x1E: bit 3 of the RAM bank register is the motor,
  drives nothing here, and must not reach the bank number.

- [ ] **Step 1: Write `tests/test_mbc5.cpp` with these cases, and run it to watch them fail.** Use the `makeRom(banks, type, romCode, ramCode)` helper pattern from `tests/test_cartridge.cpp` — each bank begins with its own 16-bit number, so `read(0x4000)` tells you which bank is mapped.
  - a 512-bank ROM (romCode 0x08, type 0x19): write 0xFF to 0x2000 and 0x01 to 0x3000, then `read(0x4000)` maps bank 511
  - writing 0x00 to 0x2000 maps **bank 0** at 0x4000 (not bank 1)
  - the two halves of the bank number are independent: set bit 8, then change the low byte, and only the low bits move
  - the bank wraps to the ROM size: on a 4-bank ROM, selecting bank 5 reads bank 1
  - 0000–3FFF stays bank 0 whatever is selected
  - RAM is unreadable (0xFF) until `(value & 0x0F) == 0x0A` is written to 0x0000, and unreadable again after 0x00
  - on type 0x1B with ramCode 0x03 (4 banks), each of banks 0–3 holds its own byte
  - on type 0x1E, writing 0x0B to 0x4000 selects RAM bank 3, not bank 11 —
    the motor bit is masked away
- [ ] **Step 2: Implement `Mbc5`.**
- [ ] **Step 3: Accept the new types in `Cartridge::load`** — `Kind::Mbc5`, RAM for 0x1A/0x1B/0x1D/0x1E, battery for 0x1B/0x1E. `tests/test_cartridge.cpp` asserts that type 0x13 is *rejected*; leave that assertion alone, it is MBC3 and stays unsupported until Task 4.
- [ ] **Step 4: Build, run the unit suite.** Expected: all pass.
- [ ] **Step 5: Mutation-test the new tests.** Apply each of these, rebuild, confirm at least one test fails, then revert:
  1. remap bank 0 to bank 1
  2. drop the 3000–3FFF bit-8 register (ignore the write)
  3. mask the RAM bank with `& 0x07` on non-rumble types
  4. accept any non-zero value as RAM enable
  Four mutations, four expected reds. Record the result in the task report.
- [ ] **Step 6: Run the ROM groups.**

```
.\build\release\tools\roms\rom_runner.exe --only mbc5 --out build\rom-mbc5.json
.\build\release\tools\roms\rom_runner.exe --only oam_dma --out build\rom-oamdma.json
```

Expected: mbc5 8 of 8; oam dma 6 of 6, including `sources-GS.gb`.

**If `sources-GS.gb` still fails, do not adjust the cartridge to suit it.**
It is the test that was predicted to confirm `GameBoy.cpp`'s inferred
mapping of DMA sources at or above 0xE000 (see the comment in
`GameBoy::tickDma`). Report the failure with its detail; the fix belongs in
the DMA source mapping, and either way it gets an entry in
`docs/known-divergences.md`.

- [ ] **Step 7: Commit and push.** Stage `src/core/mbc/Mbc5.*`, `src/core/Cartridge.*`, `CMakeLists.txt`, `tests/test_mbc5.cpp`.

---

### Task 3: MBC2

**Files:**
- Create: `src/core/mbc/Mbc2.h`, `src/core/mbc/Mbc2.cpp`, `tests/test_mbc2.cpp`
- Modify: `src/core/Cartridge.cpp`, `src/core/Cartridge.h` (`Kind::Mbc2`), `CMakeLists.txt`

**Behaviour — every value here is checked, use it verbatim:**

- Header types 0x05 (MBC2) and 0x06 (MBC2+BATTERY). 0x06 has a battery.
- **The header's RAM size byte is 0x00 on every MBC2 cartridge and must be
  ignored.** The controller has 512 × 4 bits of its own. Allocate 512 bytes
  regardless of the header, one nibble per byte in the low four bits.
- Writes below 0x4000 are decoded by **bit 8 of the address**
  (`address & 0x0100`):
  - clear → RAM enable, enabled when `(value & 0x0F) == 0x0A`
  - set → ROM bank, `value & 0x0F`, and **0 becomes 1**
- Maximum 16 banks (256 KiB). The bank is masked to the ROM size as usual.
- RAM occupies A000–A1FF and is mirrored through BFFF: the offset is
  `(address - 0xA000) & 0x01FF`.
- A read returns `0xF0 | (stored & 0x0F)`. A write stores `value & 0x0F`.
- Writes to 0x4000–0x7FFF do nothing.

- [ ] **Step 1: Write `tests/test_mbc2.cpp` with these cases, and run it to watch them fail.**
  - writing 0x0A to 0x0000 enables RAM; writing 0x0A to 0x0100 does **not**
    (that address has bit 8 set, so it is a bank write)
  - writing 0x01 to 0x2100 selects bank 1; writing 0x01 to 0x2000 does not
    change the bank — it is a RAM-enable write
  - bank 0 selects bank 1
  - only the low 4 bits of the value reach the bank: writing 0x1F selects
    bank 15 on a 16-bank ROM
  - a value written to 0xA000 reads back as `0xF0 | value`: write 0x05, read
    0xF5; write 0xFF, read 0xFF; write 0x00, read 0xF0
  - the mirror: a byte written at 0xA000 reads back at 0xA200, 0xA400 and
    0xBE00; a byte written at 0xB1FF is the same cell as 0xA1FF
  - with RAM disabled, reads return 0xFF and writes are dropped —
    re-enabling shows the old value, not the dropped one
  - `cart.ram().size() == 512` on a type 0x06 cartridge whose header RAM
    code is 0x00
- [ ] **Step 2: Implement `Mbc2`.**
- [ ] **Step 3: Accept types 0x05 and 0x06 in `Cartridge::load`,** allocating 512 bytes of RAM for both and setting the battery for 0x06 only.
- [ ] **Step 4: Build, run the unit suite.** Expected: all pass.
- [ ] **Step 5: Mutation-test.** Apply each, rebuild, confirm a red, revert:
  1. decode the control writes by address range (below 0x2000 = RAM enable) instead of by bit 8
  2. return the stored byte without the `0xF0` fill
  3. mask the RAM address with 0x1FFF instead of 0x01FF
  4. keep the full byte on a RAM write instead of the low nibble
- [ ] **Step 6: Run the ROMs.**

```
.\build\release\tools\roms\rom_runner.exe --only mbc2 --out build\rom-mbc2.json
```

Expected: 7 of 7. `bits_unused` is the one that checks the read-back fill;
if it fails, the answer is in Pan Docs' MBC2 section, not in the test.

- [ ] **Step 7: Commit and push.**

---

### Task 4: MBC3 without the clock

The clock arrives in Tasks 5 and 6. This task makes MBC3 cartridges load
and bank, which is what `ramg-mbc3-test` and Pokémon Red/Blue need.

**Files:**
- Create: `src/core/mbc/Mbc3.h`, `src/core/mbc/Mbc3.cpp`, `tests/test_mbc3.cpp`
- Modify: `src/core/Cartridge.cpp`, `src/core/Cartridge.h` (`Kind::Mbc3`), `tests/test_cartridge.cpp`, `CMakeLists.txt`

**Behaviour:**

| range | effect |
| --- | --- |
| 0000–1FFF | RAM (and timer) enable, `(value & 0x0F) == 0x0A` |
| 2000–3FFF | ROM bank, `value & 0x7F`, 0 → 1 |
| 4000–5FFF | `0x00`–`0x03` select a RAM bank; `0x08`–`0x0C` select a clock register; `0x0D`–`0x0F` are invalid |
| 6000–7FFF | latch (Task 6; ignore for now) |

- Header types: 0x0F (TIMER+BATTERY), 0x10 (TIMER+RAM+BATTERY), 0x11
  (MBC3), 0x12 (+RAM), 0x13 (+RAM+BATTERY).
- RAM exists for 0x10, 0x12, 0x13, sized by the header. **Type 0x0F has a
  battery and a timer but no RAM.**
- Batteries: 0x0F, 0x10, 0x13.
- With a bank number of 0x08 or above selected and no clock yet, the RAM
  window reads open bus (0xFF) and ignores writes. Task 6 replaces that
  with the clock registers.
- The RAM-enable rule: Pan Docs says `(value & 0x0F) == 0x0A`. If
  `ramg-mbc3-test` disagrees, follow the test — it is a hardware test —
  and add the entry to `docs/known-divergences.md` with the evidence.
  **Do not change the rule without running the ROM first.**

- [ ] **Step 1: Write `tests/test_mbc3.cpp`, and run it to watch it fail.**
  - a 128-bank ROM: writing 0x7F to 0x2000 maps bank 127 at 0x4000
  - only 7 bits reach the bank: writing 0xFF also maps bank 127
  - bank 0 selects bank 1
  - RAM banks 0–3 on a ramCode 0x03 cartridge each hold their own byte, and
    switching back finds the earlier value
  - RAM reads 0xFF until enabled
  - selecting bank 0x08 makes the RAM window read 0xFF, and a write there
    does not disturb RAM bank 0
  - a type 0x0F cartridge loads, reports `hasBattery()`, and has no RAM
- [ ] **Step 2: Implement `Mbc3`.**
- [ ] **Step 3: Accept types 0x0F–0x13 in `Cartridge::load`.**
- [ ] **Step 4: Update the one stale assertion in `tests/test_cartridge.cpp`** — the "loading rejects tiny images and unsupported controllers" case uses type 0x13 as its example of an unsupported controller and asserts the error mentions `0x13`. Replace that example with a type that is still unsupported after this piece: **0x20 (MBC6)**, asserting the error mentions `0x20`. Do not delete the case.
- [ ] **Step 5: Build, run the unit suite.** Expected: all pass.
- [ ] **Step 6: Mutation-test.** Apply each, rebuild, confirm a red, revert:
  1. mask the ROM bank with `& 0x1F` (MBC1's width)
  2. drop the 0 → 1 remap
  3. let bank numbers 0x08+ address RAM (mask with `& 0x03`)
  4. enable RAM on any value
- [ ] **Step 7: Run the ROM group.**

```
.\build\release\tools\roms\rom_runner.exe --only mbc3 --out build\rom-mbc3.json
```

Expected: `ramg-mbc3-test` passes. The other two need the clock and are
expected to fail here — say so in the report rather than chasing them.

- [ ] **Step 8: Commit and push.**

---

### Task 5: The clock itself

`Rtc` on its own, with no MBC3 wiring. Unit tests only — no ROM moves this
task.

**Files:**
- Create: `src/core/mbc/Rtc.h`, `src/core/mbc/Rtc.cpp`, `tests/test_rtc.cpp`
- Modify: `CMakeLists.txt`

**Interfaces — produces:**

```cpp
namespace fourshades {

// The five registers, live or latched. Days are 9 bits: `dayLow` plus bit 0
// of `dayHigh`. Bit 6 of `dayHigh` halts the clock; bit 7 is the carry that
// a day-counter overflow sets and only the program clears.
struct RtcRegisters {
    u8 seconds = 0;
    u8 minutes = 0;
    u8 hours = 0;
    u8 dayLow = 0;
    u8 dayHigh = 0;
};

// Everything needed to save and restore a clock.
struct RtcState {
    RtcRegisters live;
    RtcRegisters latched;
};

class Rtc {
public:
    void tick();                                  // one M-cycle
    void latch();                                 // live -> latched

    // `reg` is 0x08-0x0C as written to 4000-5FFF. Any other value is not
    // a clock register and must not reach these.
    u8 read(u8 reg) const;                        // from the latched copy
    void write(u8 reg, u8 value);                 // to the live copy

    bool halted() const;                          // bit 6 of live dayHigh

    RtcState state() const;
    void setState(const RtcState& state);
    void advanceSeconds(std::uint64_t seconds);   // catch-up; no-op while halted

private:
    // 4194304 T-cycles per second; GameBoy::tick() is one M-cycle, so
    // 1048576 ticks make a second.
};

} // namespace fourshades
```

**Behaviour — exact:**

- `kTicksPerSecond = 1048576`. Accumulate ticks; on reaching it, subtract it
  and add a second.
- Rollover: seconds 59 → 0 carries a minute; minutes 59 → 0 carries an hour;
  hours 23 → 0 carries a day.
- The day counter is `dayLow | ((dayHigh & 0x01) << 8)`, 0–511. Incrementing
  past 511 wraps to 0 and sets bit 7 of `dayHigh`. **Bit 7 is never cleared
  by the clock** — only by a program writing `dayHigh`.
- While bit 6 of the live `dayHigh` is set, `tick()` changes nothing —
  including the sub-second accumulator, which freezes where it is.
- `write` stores the value as given and, for the seconds register (0x08),
  **resets the sub-second accumulator to zero**, so a program that sets the
  seconds gets a whole second before the next increment.
- Values out of range are kept and counted from. Writing 0x3B (59) to
  seconds means the next increment carries a minute; writing 0x3F (63)
  means it counts 63 → 64 → … → 59 → carry, which is what the hardware
  does and what a program that reads it back must see.
- `read` returns the latched copy. Pan Docs documents only bits 0, 6 and 7
  of `dayHigh`; what bits 1–5 read back as is not written down anywhere
  this plan trusts. Implement them as reading back whatever was written,
  and let `latch-rtc-test` in Task 6 settle it — if that ROM disagrees,
  follow the ROM and record the decision in `docs/known-divergences.md`.
  Do not guess a fill pattern here.
- `advanceSeconds(n)` adds n seconds through the same rollover path and
  does nothing at all while halted. It must be O(1)-ish, not a loop over n:
  a save left for a year is 31 million seconds.

- [ ] **Step 1: Write `tests/test_rtc.cpp`, and run it to watch it fail.**
  - 1048575 ticks leave seconds at 0; the 1048576th makes it 1
  - seconds 59 + 1 second → seconds 0, minutes 1
  - 23:59:59 + 1 second → 00:00:00 with the day counter at 1
  - day 511 + 1 day → day 0 with `dayHigh` bit 7 set, and a further day
    leaves bit 7 set
  - a program writing `dayHigh` with bit 7 clear clears the carry
  - with bit 6 set, a full second of ticks changes nothing; clearing it
    resumes
  - `read` before any `latch()` returns zeros while the live clock has
    advanced; after `latch()` it returns the advanced values
  - writing seconds resets the accumulator: tick 1048575 times, write
    seconds = 5, tick once, seconds is still 5
  - `advanceSeconds(90)` on a zeroed clock gives 00:01:30
  - `advanceSeconds(86400 * 3)` gives day 3, same time of day
  - `advanceSeconds` while halted changes nothing
  - `advanceSeconds(31'536'000)` (a year) completes promptly and gives day
    365 — this is the test that catches a per-second loop
  - `state()` / `setState()` round trip live and latched independently
- [ ] **Step 2: Implement `Rtc`.**
- [ ] **Step 3: Build, run the unit suite.** Expected: all pass.
- [ ] **Step 4: Mutation-test.** Apply each, rebuild, confirm a red, revert:
  1. make `read` return the live registers instead of the latched ones
  2. clear the day-carry bit on the next increment
  3. let `tick()` run while halted
  4. drop the accumulator reset on a seconds write
  5. make `advanceSeconds` ignore the day rollover
- [ ] **Step 5: Commit.** No ROM run — nothing is wired up yet.

---

### Task 6: MBC3 with the clock

**Files:**
- Modify: `src/core/mbc/Mbc3.h`, `src/core/mbc/Mbc3.cpp`, `src/core/Cartridge.h`, `src/core/Cartridge.cpp`, `src/core/GameBoy.cpp`, `tests/test_mbc3.cpp`

**Interfaces — consumes:** `Rtc` from Task 5. **Produces**, on `Cartridge`:

```cpp
bool hasTimer() const;                            // header type 0x0F or 0x10
RtcState rtcState() const;                        // zeroed when there is no timer
bool setRtcState(const RtcState& state);          // false when there is no timer
void advanceRtcSeconds(std::uint64_t seconds);    // no-op when there is no timer
void tick();                                      // one M-cycle, forwarded to the Mbc
```

**Behaviour:**

- `Mbc3` owns an `Rtc` when the header type is 0x0F or 0x10.
- 4000–5FFF values 0x08–0x0C select a clock register; the RAM window then
  reads and writes that register instead of RAM.
- 6000–7FFF: writing 0x00 and then 0x01 latches. Any other sequence does
  not. The rule is a 0x00 write followed by a 0x01 write; a second 0x01
  without an intervening 0x00 does nothing.
- Bank numbers 0x0D–0x0F: what the hardware does is what
  `rtc-invalid-banks-test` shows. Start from open bus (read 0xFF, ignore
  writes), run the ROM, and if it disagrees, follow the ROM and write the
  entry in `docs/known-divergences.md`.
- On a cartridge with no timer, 0x08–0x0C stay open bus as in Task 4.
- `GameBoy::tick()` calls `cart_.tick()`. Put the call **after** the
  existing timer, serial and PPU ticks and before `tickDma()`; the clock is
  independent of all of them, and this keeps the diff to one line in a
  function whose ordering has already been tuned against ROM tests.

- [ ] **Step 1: Add the clock cases to `tests/test_mbc3.cpp`, and run them to watch them fail.**
  - selecting register 0x08 and reading 0xA000 returns the latched seconds,
    not RAM
  - writing 0xA000 with 0x08 selected sets the clock's seconds; reading it
    back needs a latch first
  - latch: advance the clock, read (old value), write 0x00 then 0x01 to
    0x6000, read again (new value)
  - 0x01 alone, with no preceding 0x00, does not latch
  - RAM and the clock are separate: write RAM bank 0, select 0x08, write a
    clock value, go back to RAM bank 0 — the RAM byte is unchanged
  - a `GameBoy` running a ROM for 1048576 M-cycles advances the cartridge
    clock by one second (this is the test that proves the tick hook exists)
  - with no timer (type 0x13), selecting 0x08 reads 0xFF and
    `hasTimer()` is false
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Build, run the unit suite.** Expected: all pass.
- [ ] **Step 4: Mutation-test.** Apply each, rebuild, confirm a red, revert:
  1. remove the `cart_.tick()` call from `GameBoy::tick()`
  2. latch on any write to 6000–7FFF
  3. route register 0x0C to RAM instead of the clock
  4. make `latch()` copy only the seconds register
- [ ] **Step 5: Run the ROM group.**

```
.\build\release\tools\roms\rom_runner.exe --only rtc --out build\rom-rtc.json
.\build\release\tools\roms\rom_runner.exe --only mbc3 --out build\rom-mbc3.json
```

Expected: `latch-rtc-test` and `rtc-invalid-banks-test` pass, giving 3 of 3
for the group. These are screenshot tests: a failure reports the number of
differing pixels, and the reference images are in `tools/roms/data/cpp/`.

- [ ] **Step 6: Run `python tools/check_core_isolation.py`.** Expected: passes — the clock reads no host time.
- [ ] **Step 7: Commit and push.**

---

### Task 7: The clock on disk

**Files:**
- Modify: `app/Save.h`, `app/Save.cpp`, `app/AppController.cpp`, `tests/test_save.cpp`

**Interfaces — consumes:** `Cartridge::hasTimer()`, `rtcState()`,
`setRtcState()`, `advanceRtcSeconds()` from Task 6.

**The footer — exact, and it is the BGB/VBA layout, so do not reorder it:**

| offset | size | contents |
| --- | --- | --- |
| 0 | 4 × 5 | live seconds, minutes, hours, dayLow, dayHigh |
| 20 | 4 × 5 | latched seconds, minutes, hours, dayLow, dayHigh |
| 40 | 8 | Unix seconds when the save was written |

Each register is a **u32 little-endian** holding a byte's value; the
timestamp is **u64 little-endian**. Total 48 bytes, appended after the
cartridge RAM.

**Rules:**

- A cartridge with a timer saves RAM + 48 bytes. Type 0x0F has no RAM, so
  its save is 48 bytes of footer alone.
- `loadSave` accepts exactly `ram().size()` as now, and additionally
  `ram().size() + 48` **only when the cartridge has a timer**. Every other
  length is still `Refused`, and a refused file is left on disk untouched:
  RAM + 48 on a cartridge with no timer, RAM + 47, RAM + 49.
- After restoring the registers, compute `now - timestamp` in seconds. If
  it is positive, call `advanceRtcSeconds` with it. **If it is zero or
  negative — a host clock that moved backwards — advance by nothing.**
  Never pass a negative number through an unsigned conversion.
- The host time is read in `app/`, with `std::chrono::system_clock`, and
  passed to the core as a number.
- Saving always writes a fresh timestamp.

- [ ] **Step 1: Add the cases to `tests/test_save.cpp`, and run them to watch them fail.** Follow the existing tests' pattern for building a cartridge and a temporary directory.
  - a timer cartridge's save is `ram().size() + 48` bytes
  - a type 0x0F cartridge's save is exactly 48 bytes
  - round trip: set a clock, save, zero the clock, load with the same
    timestamp, and the registers come back — live and latched separately
  - catch-up: write a footer whose timestamp is 3600 seconds ago, load, and
    the clock reads one hour later
  - a halted clock does not catch up
  - a timestamp 100 seconds in the future advances nothing (and does not
    wrap to an enormous number)
  - lengths `ram().size() + 47`, `+ 49`, and `+ 48` on a non-timer
    cartridge are all `Refused`, and the file is still on disk afterwards
    with its bytes unchanged
  - a non-timer cartridge still saves exactly `ram().size()` bytes, so
    saves stay interchangeable with other emulators
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Build, run the unit suite.** Expected: all pass.
- [ ] **Step 4: Mutation-test.** Apply each, rebuild, confirm a red, revert:
  1. write the footer's registers as bytes instead of u32
  2. accept RAM + 48 on a cartridge with no timer
  3. apply the catch-up while halted
  4. use `now - timestamp` unsigned without the backwards check
- [ ] **Step 5: Play-test once, by hand.** Launch `fourshades_app.exe` with an MBC3+RTC ROM if one is to hand, confirm it loads and saves. If no such ROM is available, say so in the report — do not invent one under `tools/roms/data/`, which is never edited.
- [ ] **Step 6: Commit and push.**

---

### Task 8: MBC1 multicart

**Files:**
- Modify: `src/core/mbc/Mbc1.h`, `src/core/mbc/Mbc1.cpp`, `src/core/Cartridge.cpp`, `docs/known-divergences.md`
- Create: `tests/test_mbc1_multicart.cpp`

**Detection — exact:**

> The cartridge type is 0x01, 0x02 or 0x03; the ROM is exactly 1 MiB
> (0x100000 bytes); and the 48-byte Nintendo logo that a header carries at
> 0x0104–0x0133 appears at three or more of the offsets 0x00104, 0x40104,
> 0x80104 and 0xC0104.

Three rather than four because the outer menu's own header is one of them
and some cartridges leave a slot blank.

**Behaviour when detected:** the 2000–3FFF register is **4 bits wide**
(`value & 0x0F`, still 0 → 1), and 4000–5FFF supplies bits 4–5, so the bank
is `(high << 4) | low`. Everything else, including mode 1, is unchanged.

- [ ] **Step 1: Write `tests/test_mbc1_multicart.cpp`, and run it to watch it fail.** Build the fixture ROMs with the `makeRom` helper, then copy the 48 logo bytes into place at the boundary offsets.
  - a 1 MiB ROM with the logo at all four boundaries is detected
  - the same ROM with three logos is detected
  - with two logos it is **not** detected, and banks as an ordinary MBC1
  - a 512 KiB ROM with logos at both its boundaries is not detected (wrong
    size)
  - a detected cartridge: write 0x0F to 0x2000 and 0x03 to 0x4000 → bank
    0x3F; the same writes on an undetected one → bank 0x6F
  - on a detected cartridge, writing 0x1F to 0x2000 selects bank 0x0F, not
    0x1F — the fifth bit is not wired
- [ ] **Step 2: Implement the detection in `Cartridge::load` and the bank width in `Mbc1`.**
- [ ] **Step 3: Build, run the unit suite.** Expected: all pass.
- [ ] **Step 4: Mutation-test.** Apply each, rebuild, confirm a red, revert:
  1. require four logos instead of three
  2. drop the ROM-size condition
  3. keep the 5-bit low register when detected
- [ ] **Step 5: Run the ROM group.**

```
.\build\release\tools\roms\rom_runner.exe --only mbc1 --out build\rom-mbc1.json
```

Expected: 13 of 13.

- [ ] **Step 6: Add the `docs/known-divergences.md` entry** — that multicart cartridges are detected by counting logos because no header byte declares them, what the threshold is, and what would overturn it (a hardware-verified test, or a real multicart that this misidentifies).
- [ ] **Step 7: Commit and push.**

---

### Task 9: The full run, the scoreboard, and the piece closed

**Files:**
- Modify: `scoreboard.json` and the README scoreboard block — **only via the tool**, never by hand
- Modify: `docs/known-divergences.md` if any task left a decision to record

- [ ] **Step 1: Clean release build from scratch.**

```
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
```

- [ ] **Step 2: Run the whole unit suite.** `.\build\release\fourshades_tests.exe` — expected: all pass, no skips.
- [ ] **Step 3: Run `python tools/check_core_isolation.py`.** Expected: passes.
- [ ] **Step 4: Run both full suites.**

```
python tools/sst/fetch_sst.py
.\build\release\tools\sst\sst_runner.exe
python tools/roms/fetch_roms.py
.\build\release\tools\roms\rom_runner.exe
```

Expected: SST 499 / 500 unchanged — **if it moved, something in this piece
broke the CPU and that is the finding**; test ROMs 126 / 165, with
mbc1 13/13, mbc2 / mbc5 15/15, mbc3 / rtc 3/3, oam dma 6/6.

- [ ] **Step 5: Update the scoreboard.**

```
python tools/scoreboard.py update build/sst-results.json build/rom-results.json
```

- [ ] **Step 6: Check the group totals in `scoreboard.json` against Step 4's output.** Any group that moved unexpectedly — up or down — goes in the report.
- [ ] **Step 7: Commit and push.** Stage `scoreboard.json`, `README.md`, `docs/known-divergences.md`.
- [ ] **Step 8: Verify CI.** Check the run for the pushed commit through the GitHub API and read the response. **Never write "CI green" without having looked at it** — in piece 3 that claim was made while CI had been red for four commits.

---

## Self-review

**Spec coverage.** Every section of the spec maps to a task: the interface
and the extraction (1), MBC5 (2), MBC2 (3), MBC3 (4), the clock (5), the
clock wired up (6), persistence and catch-up (7), multicart (8), the
scoreboard and the divergence entries (9). The spec's "prediction on the
record" about DMA sources is discharged in Task 2, Step 6. The spec's error
handling — unsupported types, refused save lengths, a backwards host clock —
is in Tasks 4, 7 and the constraint block.

**Placeholders.** None: every step names its file, its command or its exact
expected value, and the behaviour tables carry the masks rather than
describing them.

**Type consistency.** `RtcRegisters` / `RtcState` are defined once in Task 5
and used under those names in Tasks 6 and 7. `hasTimer`, `rtcState`,
`setRtcState`, `advanceRtcSeconds` and `tick` are spelled identically in
Tasks 6 and 7 and in the spec. The `Mbc` interface's five methods are
spelled identically in Task 1 and every chip task.

**One deliberate gap.** No task adds a test ROM, a reference image or a
manifest entry; `tools/roms/data/`, `tools/roms/manifest.sha256` and
`tools/roms/tests.json` are never edited by hand, and every ROM this piece
needs is already vendored and already counted in the 165.
