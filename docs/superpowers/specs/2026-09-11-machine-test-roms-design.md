# FourShades piece 2: the machine and the test-ROM scoreboard

**Status:** design, approved by the owner 2026-09-11
**Date:** 2026-09-11
**Piece:** 2 of 6 (foundation + CPU ✓ → **machine + test-ROM scoreboard** →
graphics + window → cartridge chips → sound → browser build)

## Goal

Turn the CPU into a Game Boy that can run real test ROMs without a screen, and
make the second scoreboard line real. **"test roms N / 167"** counts the
original-Game-Boy (DMG) tests that gbdev's Emulator Shootout runs, at Shootout
commit `38b926bdbc26993d1b4c43e97979ecc66287bf02` (2026-07-13).

All 167 are in the runner from the first day. A test that needs hardware
FourShades doesn't have yet simply fails, with the reason recorded. The number
can only rise because of real work.

## Where the 167 come from

These are the Shootout's active tests with no `model=CGB`/`model=SGB`, taken
from its `testroms/*.py` definitions at the pinned commit:

| Source | DMG tests |
|---|---|
| blargg | 38 |
| mooneye | 95 |
| mealybug-tearoom-tests (the `dmg(...)` entries) | 24 |
| acid (dmg-acid2 and friends) | 2 |
| ashiepaws (bully, strikethrough) | 2 |
| cpp | 3 |
| daid | 3 |
| **total** | **167** |

The earlier public figure of 1,300 was an uncounted estimate, and was corrected
on 2026-09-11 (FourShades 45a9a8b, doozleb.com2 265ff05).

## Scope

**In (piece 2):**
- `Cartridge`: loads a ROM, parses and validates its header, and supports plain
  ROM (type 0x00) and MBC1 (0x01–0x03), including RAM banking. MBC1 is needed
  now because every Blargg test ROM uses it.
- `GameBoy`: the memory map, implementing the existing `Bus`. It covers ROM
  0000–7FFF, VRAM 8000–9FFF (storage only), cartridge RAM A000–BFFF, WRAM
  C000–DFFF, echo E000–FDFF, OAM FE00–FE9F (storage), the unusable area
  FEA0–FEFF, I/O FF00–FF7F, HRAM FF80–FFFE, and IE FFFF.
- `Timer`: the 16-bit internal counter (DIV is its high byte) and TIMA/TMA/TAC,
  with falling-edge increments, DIV-write resets, and the one-M-cycle
  TIMA-overflow reload delay. These follow Pan Docs "Timer and Divider
  Registers" and "Timer obscure behaviour".
- Interrupts:
  - IF/IE, with delivery at the start of `Cpu::step()`.
  - Dispatch: idle, idle, push high byte, push low byte, jump to the vector —
    5 M-cycles in all, with IME cleared and the IF bit acknowledged.
  - HALT wakes whenever `IE & IF & 0x1F` is non-zero, even with IME=0.
  - The HALT bug: HALT with IME=0 and an interrupt pending makes the next
    opcode byte be read twice.
- `Serial`: SB/SC, internal-clock transfers at the DMG rate, and the serial
  interrupt. With no link partner, incoming bits are 1. Every byte written out
  is captured for the harness.
- `Joypad`: P1 with no buttons pressed.
- OAM DMA (FF46): 160 byte copies, one per M-cycle, after a start delay. While
  it runs, the CPU can only see HRAM, per Pan Docs "OAM DMA Transfer".
- LCD timing skeleton:
  - LY advances every 114 M-cycles while the LCD is on and wraps after 153.
  - VBlank requests IF bit 0 when LY reaches 144.
  - LY=LYC sets the STAT coincidence bit.
  - There is no pixel output, and the STAT mode bits are approximate. This is
    a placeholder that piece 3 replaces. It exists because many test ROMs wait
    for LY before they start.
- Power-on state: the CPU registers and I/O values a DMG (revisions A/B/C) boot
  ROM leaves behind, from Pan Docs "Power Up Sequence". FourShades starts at
  PC=0x0100 and never runs a boot ROM.
- The test-ROM harness, scoreboard, CI and cheat guards (below).

**Out:** pixel output and PPU accuracy (piece 3); MBC2, MBC3 and MBC5 (piece 4);
sound (piece 5); Game Boy Color features; joypad input from a user; a window.

## Interfaces that change

`Bus` gains two members that cost **no** cycles:

```cpp
virtual u8 pendingInterrupts() = 0;        // IE & IF & 0x1F
virtual void acknowledgeInterrupt(int bit) = 0; // clear IF bit
```

The CPU checks for interrupts through these, so the check never logs an
M-cycle. `RecordingBus` answers "nothing pending", which keeps every one of the
500,000 SingleStepTests and their cycle counts unchanged. **The SST score must
stay 499/500 through the whole of piece 2; CI already enforces that.**

**Timing model within an M-cycle.** Each `GameBoy::read`/`write`/`idle` first
advances the timer, serial, DMA and LCD skeleton by one M-cycle (4 T-cycles),
then performs the access. The Mooneye timer tests arbitrate this. If they show
the access must come first, the order changes and the change is recorded in
`docs/known-divergences.md`'s history section.

## The test-ROM harness (`tools/roms/`, never linked into the core)

- **Pinned data.** `fetch_roms.py` downloads each ROM the list needs, plus its
  Shootout reference PNG (for piece 3), from `raw.githubusercontent.com` at the
  pinned Shootout commit, one file at a time with per-file retries. Each file
  is checked against GitHub's git blob hash and recorded in a committed SHA-256
  manifest, the same design as `tools/sst/`. The files go into
  `tools/roms/data/`, which is gitignored.
- **The test list.** `tools/roms/tests.json` is committed and generated by
  `make_test_list.py` from the Shootout's own `testroms/*.py` at the pinned
  commit, so anyone can regenerate and diff it. Each entry has:
  - `name`: the Shootout test name;
  - `rom`: its path;
  - `group` (see below);
  - `method`: `blargg`, `mooneye` or `screenshot`;
  - `limit_seconds`: emulated seconds, max(2 × Shootout runtime, runtime + 5).
- **How pass/fail is decided** — only by the harness, only from each author's
  own signal:
  - `blargg` (Blargg's serial or memory result):
    - Serial output containing `Passed` is a pass; `Failed` is a fail.
    - For ROMs that write their result to cartridge RAM: once A001–A003 hold
      `DE B0 61` and A000 ≠ 0x80, A000 = 0x00 is a pass and anything else is a
      fail. The text at A004 is recorded as the failure detail.
  - `mooneye` (Fibonacci registers):
    - When the CPU is about to execute `LD B,B` (0x40), the harness checks
      B,C,D,E,H,L. The values 3,5,8,13,21,34 are a pass; 0x42 in all six is a
      fail.
    - The harness reads the opcode at PC through a cycle-free peek before each
      step. The core has no test hook.
  - `screenshot`: always "fail: needs the PPU (piece 3)" in piece 2.
  - Hitting the time limit with no signal is a fail ("timeout", with the
    serial text so far).
- **Groups** are assigned by path in `make_test_list.py`. The script fails if
  any test falls in no group:
  - cpu instructions (blargg cpu_instrs);
  - cpu timing (blargg instr_timing, mem_timing, mem_timing-2, halt_bug);
  - cpu & interrupts (mooneye acceptance root, `bits/`, `instr/`,
    `interrupts/`, excluding the groups below);
  - timer (mooneye `acceptance/timer`);
  - boot state (mooneye `boot_*`);
  - oam dma (mooneye `oam_dma*`);
  - serial (mooneye `acceptance/serial`);
  - mbc1 (mooneye `emulator-only/mbc1`);
  - mbc2/mbc5 (mooneye `emulator-only/mbc2`, `mbc5`);
  - ppu timing (mooneye `acceptance/ppu`);
  - oam bug (blargg `oam_bug`);
  - sound (blargg `dmg_sound`);
  - screen (acid, ashiepaws, cpp, daid, mealybug, mooneye `manual-only`,
    `misc`).
  The plan lists the exact per-group counts it generates.
- **Runner.** `rom_runner.exe` runs every test headless, as fast as the host
  allows. It stops each test at its pass/fail signal or its time limit, and
  writes `build/rom-results.json`: per test, the group, status, reason,
  emulated time used, and serial text (truncated to 2 KB). It also prints a
  summary with the first failing test in each group. It exits non-zero only on
  harness errors, never for a low score.

## Scoreboard

`scoreboard.py` reads both results files. The README block keeps its two lines,
and the test-ROM line now comes from `rom-results.json`. Below the block goes a
generated per-group table (group, passing / total, first failing test and its
reason), between its own markers. `check` covers both. `scoreboard.json` gains
the per-group numbers. CI fetches the ROMs (cached by Shootout commit), runs
the ROM runner after the SST runner, and uploads `rom-results.json` as an
artifact.

The site's scoreboard (doozleb.com2 `src/data/projects.ts`) stays hand-updated
at each post, as today.

## Keeping it honest

- The ROMs and reference images are pinned by commit and hash-checked, and
  the runner refuses mismatched data.
- `check_core_isolation.py` also forbids, in `src/core`:
  - the words `blargg`, `mooneye`, `shootout`, `passed`, `failed` and
    `fibonacci`;
  - the byte sequence `0xDE, 0xB0, 0x61` in any spelling (as three hex bytes
    or as `0xDEB061`).
  The core may not know what a test is.
- The core gets no test-only hooks. The harness observes through public state
  (registers, a cycle-free memory peek, and the serial byte log).
- Blargg's `cpu_instrs` is written by someone other than SingleStepTests'
  author. If the CPU passes SST but fails `cpu_instrs`, it has been fitted to
  one suite, and that gets published.
- A test that hangs, or that passes only because its time limit was raised,
  doesn't count. Limits come from the formula above, never per test by hand.

**Decision (2026-09-11):** the rule above was Pan Docs over every test. It's
now refined: Pan Docs still outranks a test that only another emulator
generated (SingleStepTests), but a hardware-verified test — one run and
checked against real DMG, MGB, SGB, SGB2, CGB, AGB or AGS hardware — outranks
a Pan Docs sentence that turns out to be a simplification. The first case was
OAM DMA: Pan Docs says DMG's CPU "can access only HRAM" during OAM DMA, but
nine hardware-verified tests show only OAM and the bus the DMA reads from are
actually blocked; per-bus blocking passed all nine with no regression (76 →
85 of 167). See `docs/known-divergences.md` ("OAM DMA bus conflicts") for the
evidence and every such decision going forward.

## Testing

Unit tests (doctest), each written to fail first:
- Cartridge: header parsing, a bad checksum warning (not fatal), MBC1 bank
  switching (including bank 0→1 mapping and the 5-bit/2-bit register split),
  and RAM enable.
- Memory map: every region routes to the right place, echo mirrors WRAM, and
  reads of unused areas give the documented values.
- Timer:
  - DIV increments;
  - TIMA at each TAC rate;
  - a DIV write causing a falling-edge increment;
  - overflow → one cycle reading 0 → TMA reload plus IF;
  - a TIMA write during the reload cycle.
- Interrupts: dispatch cycle count and order, priority (lowest bit first),
  HALT wake with IME=0, the HALT bug, EI then an interrupt, and RETI.
- Serial: a transfer completes after the right number of cycles, captures the
  byte, and raises the interrupt.
- DMA: copies 160 bytes, takes the right number of cycles, and blocks non-HRAM
  reads.
- The harness itself:
  - the pass/fail detectors on synthetic states: Blargg serial text, Blargg
    memory signature, Mooneye registers;
  - time limits;
  - that a detector can't fire on a near-miss, such as five of the six
    Fibonacci registers matching.

The SST suite keeps running unchanged, as the CPU regression guard.

## Success criteria

1. The SST score is still 499/500, and CI still enforces it.
2. All 167 tests run in CI, each with a recorded status and reason.
3. All 11 Blargg `cpu_instrs` pass. That is the independent CPU cross-check.
4. The Mooneye timer group and the Blargg `instr_timing` test pass, or each
   remaining failure has a documented reason.
5. The README shows "test roms N / 167" plus the per-group table, generated
   and CI-checked. N is whatever the code honestly scores. The design
   estimates 60–80, but the target is honesty, not a number.
6. A full ROM run takes under 5 minutes in CI.

## Risks

| Risk | Response |
|---|---|
| Interrupt checks disturb the SST cycle counts | They cost no cycles by interface design, and CI's 499/500 check catches any slip |
| The order of access and advance within an M-cycle is wrong | The Mooneye timer tests arbitrate; the change is documented |
| Tests hang on the LCD-timing skeleton | Time limits turn a hang into a recorded failure; piece 3 replaces the skeleton |
| Test ROM licences | ROMs are downloaded at run time from the Shootout (MIT repository), never committed |
| The Shootout's own list changes | It's pinned by commit, and any update is a deliberate, published change of denominator |
| This machine's network drops downloads | Per-file fetching with retries, which resumes (proven in piece 1) |
