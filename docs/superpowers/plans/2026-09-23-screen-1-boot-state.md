# Piece 6, Plan 1: Power-on, STOP and the Boot State

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Claim the three remaining failures that are not mid-scanline
rendering problems — `boot_hwio`, `daid/stop_instr` and `ashiepaws/bully` —
taking the suite from 138/165 to **141/165** before anything touches the
pixel pipeline.

**Architecture:** Three small, independent changes to power-on state and to
STOP. No change to the fetcher, the FIFO or the window.

**Tech Stack:** C++20, MSVC (Visual Studio 2026), CMake + Ninja, doctest.
Windows only; every build command goes through `tools\dev.cmd`.

**Spec:** `docs/superpowers/specs/2026-09-23-screen-design.md`.
**Evidence:** `.superpowers/sdd/screen-investigation.md` — every fix in this
plan was measured there and then reverted. Read the relevant section before
the task that uses it.

## Global Constraints

- Follow Pan Docs over an emulator-generated test. A hardware-verified test
  outranks a Pan Docs simplification. Either way the decision goes in
  `docs/known-divergences.md` with the evidence — never silently.
- Never special-case a test, a test name, a ROM, or an address pattern only
  a test uses.
- `src/core/` must not depend on the tests: no test names, no file or JSON
  access, no includes from `tools/`, no host clock.
- Never loosen the comparator, the loader, the detectors, the runner or any
  image comparator; never raise a time limit or special-case a ROM.
- Never edit `tools/sst/data/`, `tools/roms/data/`, either `manifest.sha256`,
  or `tools/roms/tests.json` by hand.
- The README scoreboard block and `scoreboard.json` are written only by
  `python tools/scoreboard.py update build/sst-results.json build/rom-results.json`
  from a full run with no `--only` filter. **Every score-moving task ends
  with that full run and update** — CI fails when the checked-in scoreboard
  disagrees with a fresh run.
- **SST must stay at 499/500.** The one failure is STOP's length, a
  deliberate Pan-Docs-over-SingleStepTests divergence. This plan touches
  STOP; if SST moves in either direction, stop and report.
- Stage explicit paths in every commit. Never `git add -A`.

## The gate, after every task, before its commit

```
.\build\release\tools\roms\rom_runner.exe --only "ppu timing"       --out build\gate.json
.\build\release\tools\roms\rom_runner.exe --only m3_bgp_change      --out build\gate.json
.\build\release\tools\roms\rom_runner.exe --only acid               --out build\gate.json
.\build\release\tools\roms\rom_runner.exe --only m2_win_en_toggle   --out build\gate.json
```

12/12, pass, pass, pass. Under three seconds together, against 283 for a
full run. Never write a partial result to `build/rom-results.json`.

---

### Task 1: The PPU's power-on phase

**Files:**
- Modify: `src/core/Ppu.cpp` or `src/core/Ppu.h` (wherever `dot_` is initialised), `tests/test_gameboy.cpp`
- Possibly: `docs/known-divergences.md`

**The measurement, from the investigation — use it verbatim.**
`boot_hwio-dmgABCmgb` reads `$FF44` (LY) at M-cycle 1190 and wants **0x0A**
where this emulator gives 0x09, and reads `$FF41` (STAT) at M-cycle 1139
where it must still report **mode 0**. Solving both constraints:

> the power-on PPU phase must advance by **between 253 and 455 dots**, and
> the advance must be a **multiple of 4**.

`Ppu::dot_` initialised to **356** satisfies both and was measured to make
`boot_hwio` pass. A value that is not a multiple of four — 354 was tried —
breaks seven unit tests, which is what pins the mod-4 rule rather than
anyone's preference.

**One unit test inherits the old phase and will fail:** `tests/test_gameboy.cpp`
around line 187. It is not a regression — it asserts the power-on phase
directly. Update it to the new value and say in your report what it was
asserting and why the new value is right.

- [ ] **Step 1: Read the investigation's `boot_hwio` section**, then find where `dot_` is initialised.
- [ ] **Step 2: Write a failing test** in `tests/test_ppu.cpp` (or wherever the power-on state is tested) asserting the new power-on phase, and one asserting LY and the mode a fixed number of M-cycles in — pick the two the ROM checks, at 1139 and 1190 M-cycles, so the test pins the same thing the ROM does without naming it.
- [ ] **Step 3: Run it and watch it fail.**
- [ ] **Step 4: Set the phase.**
- [ ] **Step 5: Fix `tests/test_gameboy.cpp`'s inherited assertion.**
- [ ] **Step 6: Run the whole unit suite.** Expected: everything passes. If any test other than that one needed changing, stop — the mod-4 rule or the range is wrong and the investigation's arithmetic should be re-derived rather than the test adjusted.
- [ ] **Step 7: Run the gate.**
- [ ] **Step 8: Run `--only boot_hwio`.** Expected: passes.
- [ ] **Step 9: Mutation-test.** Apply each, rebuild, confirm a red, revert: set the phase to 354 (not a multiple of four); set it to 260, the bottom of the solved range; set it back to 0.
- [ ] **Step 10: Full run, scoreboard update, commit, push.** Expected: **139/165**, SST 499/500, and every other group unchanged.
- [ ] **Step 11: Record it.** Pan Docs does not give the PPU's power-on phase. Add a `docs/known-divergences.md` entry: what the two constraints were, how the range and the mod-4 rule were derived, that 356 is one of 49 values inside the solved range and the ROMs cannot distinguish them, and what would settle it.

---

### Task 2: STOP blanks the screen

**Files:**
- Modify: `src/core/Cpu.cpp` (the STOP path), `src/core/Ppu.h` or `Ppu.cpp`, `src/core/GameBoy.cpp` as the wiring requires
- Test: `tests/test_ppu.cpp` or `tests/test_cpu.cpp`

**What the ROM shows.** `daid/stop_instr` prints its own status text, sets
BGP to 0x0F, and enters STOP with no button held. Its reference photograph
from hardware is **entirely white**, and the 301 pixels this emulator
already matches are exactly that white text — everything else differs
because this emulator keeps drawing. Blanking the screen while the CPU is
stopped was measured to take it to **zero** differing pixels.

**Pan Docs** describes STOP as putting the machine in a very low power state
where the LCD is off. Implement what the document says; the ROM is the
check, not the source.

**What this must not do:** change how STOP is decoded, how long it is, or
when it wakes. The SingleStepTests failure at 499/500 is about STOP's
length, is a deliberate divergence, and must stay exactly as it is.

- [ ] **Step 1: Read the investigation's `stop_instr` section and Pan Docs on STOP.**
- [ ] **Step 2: Write the failing test:** entering STOP blanks the screen; leaving STOP restores it; and a machine that never enters STOP is unaffected.
- [ ] **Step 3: Run it and watch it fail.**
- [ ] **Step 4: Implement.**
- [ ] **Step 5: Run the whole unit suite, then the gate.**
- [ ] **Step 6: Run `.\build\release\tools\sst\sst_runner.exe`.** Expected: **499/500**, unchanged, and the one failure still the STOP length. This is the check that matters for this task.
- [ ] **Step 7: Run `--only stop_instr`.** Expected: passes, 0 differing pixels.
- [ ] **Step 8: Mutation-test.** Apply each, rebuild, confirm a red, revert: blank on the wrong edge, so the screen stays blank after STOP ends; blank the framebuffer but keep the PPU advancing; blank on HALT as well as STOP.
- [ ] **Step 9: Full run, scoreboard update, commit, push.** Expected: **140/165**.

---

### Task 3: The VRAM the boot ROM leaves behind

**Files:**
- Modify: `src/core/GameBoy.cpp` (power-on state)
- Test: `tests/test_gameboy.cpp`

**What the ROM is asking for.** `ashiepaws/bully` checks DIV first — which
Task 1 fixes, because it reads DIV synchronised to the PPU — and then checks
that VRAM contains the **Nintendo logo the boot ROM unpacks from the
cartridge header**. The investigation found the expected bytes in the ROM at
`$0BD6` and confirmed the failure message moves from "Invalid initial DIV"
to "Invalid initial tile data" once Task 1 lands.

This is real hardware state. A DMG's boot ROM reads the 48 compressed bytes
at `0x0104`-`0x0133`, expands each nibble into a 2×2 block of pixels, and
writes the result into VRAM as tiles before handing control to the
cartridge. FourShades starts with VRAM zeroed and has never reproduced it.

**Derive the unpacking from the boot ROM's documented behaviour** — Pan Docs
describes it under the boot sequence — and from the header bytes of the
cartridge actually loaded. **Do not hard-code the logo**: it must come from
the cartridge's own header, because that is where the hardware reads it
from, and a cartridge with a corrupt logo must produce the corrupt result.

- [ ] **Step 1: Read Pan Docs on the boot ROM's logo routine**, and the investigation's `bully` section.
- [ ] **Step 2: Write the failing test:** after power-on, VRAM holds the tiles derived from the loaded cartridge's header logo, at the addresses the boot ROM writes them to; and a cartridge whose header logo differs produces different VRAM.
- [ ] **Step 3: Run it and watch it fail.**
- [ ] **Step 4: Implement.**
- [ ] **Step 5: Run the whole unit suite, then the gate.**
- [ ] **Step 6: Run the FULL suite immediately — do not defer this.** Seeding VRAM changes the starting state of every screenshot ROM that does not clear it first, so this is the one task in the plan whose blast radius is the entire suite. Expected: **141/165**, with `bully` passing and **nothing else moving in either direction**. If any other test moves, that is the finding: report it with the test and the pixel count before deciding anything.
- [ ] **Step 7: Mutation-test.** Apply each, rebuild, confirm a red, revert: hard-code the standard logo instead of reading the header; write the tiles one tile off; expand the nibbles in the wrong order.
- [ ] **Step 8: Scoreboard update, commit, push.**

---

### Task 4: Plan 1 closed

- [ ] **Step 1: Clean configure and build.**
- [ ] **Step 2: Whole unit suite.** All pass, no skips.
- [ ] **Step 3: `python tools/check_core_isolation.py`.** Passes.
- [ ] **Step 4: Both full suites, then the scoreboard.** Expected: SST **499/500** and test ROMs **141/165**. Compare group by group against the plan's start — cpu instructions 11/11, cpu timing 8/8, oam bug 7/7, sound 12/12, cpu & interrupts 31/31, boot state **3/3**, oam dma 6/6, ppu timing 12/12, serial 1/1, timer 13/13, mbc1 13/13, mbc2 / mbc5 15/15, mbc3 / rtc 3/3, screen **6/30**. Any difference in either direction goes in the report.
- [ ] **Step 5: Check `docs/known-divergences.md`** carries the power-on phase entry from Task 1, and anything else this plan decided against a document.
- [ ] **Step 6: Commit and push.**
- [ ] **Step 7: Verify CI** for the pushed commit through the GitHub API and **read the response**. Never write "CI green" without looking.

## Self-review

**Spec coverage.** Groups K, B and I of the spec's table are Tasks 1, 2 and
3. Group C, the 12-dot disagreement, is explicitly left to plan 2 and may
end the piece unfixed. Nothing here touches the fetcher, the FIFO or the
window.

**Placeholders.** None. The phase, its range, the mod-4 rule, the two
M-cycle timestamps and the header offsets are all exact and all measured.

**Type consistency.** No new types. The only interface question is where the
STOP-blanking flag lives, which Task 2's implementer decides and reports.
