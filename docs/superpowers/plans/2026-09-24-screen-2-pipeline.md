# Piece 6, Plan 2: The Window, the Fetch Dot, and the Objects

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Claim the twenty-four remaining `screen` failures, taking the suite
from **141/165** toward **164/165**.

**Architecture:** Four changes to the pixel pipeline, in dependence order —
a real scanline X counter for the window, a five-step eight-dot background
fetch with each register pinned to the stage that samples it, the same for
window fetches, and object fetches as cancellable steps.

**Tech Stack:** C++20, MSVC, CMake + Ninja, doctest. Windows only; every
build command goes through `tools\dev.cmd`.

**Spec:** `docs/superpowers/specs/2026-09-23-screen-design.md`.
**Evidence:** `.superpowers/sdd/screen-investigation.md` — read the section
for your group before starting. Its groupings and pixel counts were measured
by running the ROMs, not inferred.

## Where this starts

Plan 1 closed at 141/165. The `screen` group is 6/30 and its failures now
total 47,378 differing pixels. The groups from the investigation, with what
remains:

| group | tests | px | what it is |
| --- | --- | --- | --- |
| A | 6 | ~28,900 | the window re-activates mid-line |
| D | 6 | ~2,500 | which dot of a background fetch samples which register |
| E | 2 | ~3,550 | the same, for a window fetch |
| F | 2 | ~2,200 | the object fetch's dot cost, and OBP |
| G | 4 | ~1,300 | LCDC bit 1 or 2 written during an object fetch |
| H | 2 | 612 | the window's start-up cost, and WX below 7 |
| C | 1 | 7,186 | a uniform 12-dot disagreement — may end the piece unfixed |
| J | 1 | 53 | one scanline, undiagnosed |

## What the hardware does — quote this, do not paraphrase it

From Pan Docs' window and pixel-FIFO pages, and the Mealybug Tearoom
repository's own PPU notes. **Those notes are not vendored with the ROMs.**
Task 1 fetches them and quotes the relevant passages into
`docs/known-divergences.md` or a comment, so that later tasks are not each
re-fetching a web page and so the evidence survives the page changing.

- The window has an **X counter** that starts at 0 and gets **seven free
  increments before pixel 0**.
- It compares **equal** to WX, not greater-or-equal.
- It can fire **more than once on the same scanline**.
- The window's **row counter increments on each activation**, not once a
  line.
- A background fetch is **five steps over eight dots**, with an extra push
  attempt at Get-Tile-Data-High.

## Global Constraints

- Follow Pan Docs over an emulator-generated test. A hardware-verified test
  outranks a Pan Docs simplification. Mealybug's ROMs are photographs of
  real hardware. Either way the decision goes in `docs/known-divergences.md`
  with the evidence — never silently.
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
  with that full run and update.**
- **SST stays at 499/500** and the CPU is not touched by this plan.
- Stage explicit paths in every commit. Never `git add -A`.

## The gate, after every task, before its commit

```
.\build\release\tools\roms\rom_runner.exe --only "ppu timing"     --out build\gate.json
.\build\release\tools\roms\rom_runner.exe --only m3_bgp_change    --out build\gate.json
.\build\release\tools\roms\rom_runner.exe --only acid             --out build\gate.json
.\build\release\tools\roms\rom_runner.exe --only m2_win_en_toggle --out build\gate.json
```

Two seconds together, against 283 for a full run. Never write a partial
result to `build/rom-results.json`.

**`ppu timing` is first because it is the one that fails silently.** The
window work touches `dotsRemaining` — mode 3's length — and a wrong answer
there leaves the picture correct while STAT's timing is wrong. That takes
out all twelve of those tests and nothing else, and it is invisible in any
screenshot.

**`m2_win_en_toggle` is the canary for Task 3.** It is precisely the test
that pins "the window's row does not advance on lines where the window is
hidden", and Task 3 makes that conditional.

## Report pixels, not just verdicts

A screenshot test is pass or fail across all 23,040 pixels at once, so a
change can be a large improvement and flip nothing. **Every task reports the
differing-pixel count for each ROM in its group, before and after.** A group
that halves its error without flipping a test has made real progress and the
report must show it; a group whose error grows has regressed even if no
verdict moved.

---

### Task 1: The window's X counter, behaviour-neutrally

**Files:** `src/core/PixelPipeline.h/.cpp`, `src/core/Ppu.cpp`, `tests/test_pixel_pipeline.cpp`

No ROM moves. This task introduces the counter and makes the existing
behaviour run **through** it, so that Tasks 2–5 are small.

Today the window trigger is `pixelX_ >= wx - 7` and WX below 7 is handled by
a fudge called `windowSkip_`. Replace the arithmetic with a real counter —
starts at 0, seven free increments before pixel 0, compares **equal** to WX
— arranged so that, for every input the current code handles, the new code
produces **the identical picture and the identical mode-3 length**.

- [ ] **Step 1: Fetch the Mealybug PPU notes and quote the window passages** into the code or `docs/known-divergences.md`, per the section above. Later tasks depend on them.
- [ ] **Step 2: Write characterisation tests first** — capture what the current pipeline produces for a spread of WX values including 0, 6, 7, 8, 166 and 167, and for a window that never triggers. Run them against the current code and watch them **pass**; they are the net, not the target.
- [ ] **Step 3: Implement the counter** and route the existing behaviour through it.
- [ ] **Step 4: Run the characterisation tests.** Every one must still pass. Any difference is a behaviour change this task is not allowed to make.
- [ ] **Step 5: Whole unit suite, then the gate.**
- [ ] **Step 6: Run `--only screen`** and confirm the pixel counts are **identical**, ROM for ROM, to the table in your brief. Report them.
- [ ] **Step 7: Mutation-test.** Apply each, rebuild, confirm a red, revert: six free increments instead of seven; compare greater-or-equal instead of equal.
- [ ] **Step 8: Commit.** No scoreboard change, no push.

---

### Task 2: Turning the window off mid-line

**Files:** `src/core/PixelPipeline.h/.cpp`, `tests/test_pixel_pipeline.cpp`

The window is a one-shot latch: `window_` is set and never cleared. Clearing
LCDC bit 5 part-way along a line must stop the window there and return the
fetcher to the background.

- [ ] **Step 1: Write the failing test**, then run it.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Unit suite, then the gate.**
- [ ] **Step 4: `--only m3_lcdc_win_en_change_multiple` and `--only m3_lcdc_win_en_change_multiple_wx`.** Report the pixel counts before and after — these two carry about 14,000 of the group's error and are unlikely to flip on this task alone.
- [ ] **Step 5: Mutation-test:** ignore a clear of bit 5 until the next line.
- [ ] **Step 6: Full run if anything flipped, scoreboard, commit, push. Otherwise commit with the pixel counts in the message.**

---

### Task 3: Re-activation, and the row counter

**Files:** `src/core/PixelPipeline.h/.cpp`, `src/core/Ppu.cpp`, `tests/test_pixel_pipeline.cpp`

The window can start **more than once on the same scanline**, and its row
counter increments **on each activation**. This is the heart of group A.

**`m2_win_en_toggle` is the canary.** It passes today because the row does
not advance on a line where the window is hidden, and this task makes that
rule conditional. If it breaks, the condition is wrong — do not adjust the
test.

- [ ] **Step 1: Write the failing tests** — two activations on one line advance the row twice; a line with no activation does not advance it; a line where the window is enabled but never reaches WX does not advance it.
- [ ] **Step 2: Run them and watch them fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Unit suite, then the gate — and read `m2_win_en_toggle`'s result before anything else.**
- [ ] **Step 5: `--only screen`.** Report every pixel count before and after. Group A's six are the target.
- [ ] **Step 6: Mutation-test:** advance the row once a line regardless of activations; allow only one activation a line.
- [ ] **Step 7: Full run, scoreboard, commit, push.**

---

### Task 4: The pixel a WX change leaves behind

**Files:** `src/core/PixelPipeline.cpp`, `tests/test_pixel_pipeline.cpp`

`m3_wx_4_change`, `m3_wx_5_change`, `m3_wx_6_change` and
`m3_wx_4_change_sprites` change WX mid-line. The investigation found a
colour-0 pixel is produced at the change. Read its group A section for what
it measured before deciding what to implement, and check it against the
Mealybug notes quoted in Task 1.

- [ ] **Step 1: Write the failing test, run it.**
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Unit suite, then the gate.**
- [ ] **Step 4: `--only m3_wx_`.** Report all four counts before and after.
- [ ] **Step 5: Mutation-test:** emit the fetched pixel instead of colour 0.
- [ ] **Step 6: Full run if anything flipped, scoreboard, commit, push.**

---

### Task 5: WX below 7, from the counter rather than the fudge

**Files:** `src/core/PixelPipeline.h/.cpp`, `tests/test_pixel_pipeline.cpp`

Delete `windowSkip_`. With the counter from Task 1, WX values 0 to 6 fall
out of the seven free increments rather than needing a clip. Group H —
`m3_window_timing` and `m3_window_timing_wx_0` — is the target.

- [ ] **Step 1: Write the failing tests for WX = 0 through 6, run them.**
- [ ] **Step 2: Implement, and remove `windowSkip_` entirely.** If it cannot be removed, that means the counter is not yet doing its job and the report should say which case still needs it.
- [ ] **Step 3: Unit suite, then the gate.**
- [ ] **Step 4: `--only m3_window_timing` and `--only screen`.** Report the counts.
- [ ] **Step 5: Mutation-test:** clamp WX below 7 to 7.
- [ ] **Step 6: Full run, scoreboard, commit, push.**

---

### Task 6: Mode 3's length, re-derived

**Files:** `src/core/Ppu.cpp`, `src/core/PixelPipeline.cpp`, `tests/test_ppu.cpp`

`dotsRemaining` was written against the old window rule. After Tasks 1–5 it
must be derived from what the pipeline now actually does, not patched to
match.

**This is the silent one.** `ppu timing` 12/12 is the only thing that
measures it, the picture can be perfect while it is wrong, and it is the
reason that group leads the gate.

- [ ] **Step 1: Read `ppu timing`'s twelve ROMs and what each measures.**
- [ ] **Step 2: Write tests asserting mode 3's length for: no window; a window from pixel 0; a window starting mid-line; two activations on one line; WX below 7; and each SCX value 0–7.**
- [ ] **Step 3: Run them and record which fail.** Some should already pass.
- [ ] **Step 4: Re-derive the calculation.**
- [ ] **Step 5: Unit suite, then the gate.** `ppu timing` must be 12/12.
- [ ] **Step 6: Mutation-test:** drop the window's start-up cost; drop SCX's penalty.
- [ ] **Step 7: Full run, scoreboard, commit, push.**

---

### Task 7: Five steps, eight dots

**Files:** `src/core/PixelPipeline.h/.cpp`, `tests/test_pixel_pipeline.cpp`

The background fetcher is four steps over six dots; hardware is five steps
over eight, with an extra push attempt at Get-Tile-Data-High. This is what
makes Tasks 8 and 9 possible — a register can only be pinned to a stage once
the stages are right.

**`m3_bgp_change` is the risk.** It is the only exact Mealybug pass and the
sole evidence for the seven-dot render lag measured in piece 3. If it breaks,
the lag and the new fetch length are disagreeing and the report must say so
rather than either being adjusted to fit.

- [ ] **Step 1: Write the failing tests** for the step sequence and the dot cost, run them.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Unit suite, then the gate — read `m3_bgp_change` first.**
- [ ] **Step 4: `--only screen`.** Report every count; expect group D and E to move.
- [ ] **Step 5: Mutation-test:** six dots instead of eight; drop the extra push.
- [ ] **Step 6: Full run, scoreboard, commit, push.**

---

### Task 8: Each register pinned to the stage that samples it

**Files:** `src/core/PixelPipeline.cpp`, `tests/test_pixel_pipeline.cpp`

Group D, six ROMs. The investigation measured which stage samples which
register — LCDC's tile-select at steps 0 and 1, SCY at the step before the
fetch and at 0 and 1, and so on. Read its group D section and the Mealybug
notes from Task 1, and pin each one.

- [ ] **Step 1: Write one failing test per register**, each writing it at a different stage of a fetch and asserting which fetch it affects. Run them.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Unit suite, then the gate.**
- [ ] **Step 4: `--only screen`.** Group D's six are the target: `m3_scy_change`, `m3_lcdc_tile_sel_change`, `m3_lcdc_bg_en_change`, `m3_lcdc_bg_map_change`, `m3_scx_low_3_bits`, `m3_scx_high_5_bits`.
- [ ] **Step 5: Mutation-test:** sample each pinned register one stage late.
- [ ] **Step 6: Full run, scoreboard, commit, push.**

---

### Task 9: The same, for window fetches

**Files:** `src/core/PixelPipeline.cpp`, `tests/test_pixel_pipeline.cpp`

Group E: `m3_lcdc_tile_sel_win_change` and `m3_lcdc_win_map_change`.

- [ ] **Step 1: Write the failing tests, run them.**
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Unit suite, then the gate.**
- [ ] **Step 4: `--only screen`, counts before and after.**
- [ ] **Step 5: Mutation-test:** use the background's map bit for a window fetch.
- [ ] **Step 6: Full run, scoreboard, commit, push.**

---

### Task 10: Objects, as steps that can be cancelled

**Files:** `src/core/PixelPipeline.h/.cpp`, `tests/test_pixel_pipeline.cpp`

Groups F and G, six ROMs. An object fetch is a lump penalty today; on
hardware it is a sequence a write to LCDC bit 1 can cancel part-way through,
and bit 2 changes the object's height mid-fetch.

**`intr_2_mode0_timing_sprites` is the risk here**, and it depends on a
three-dot rebate for the first object that was **fitted, not derived**, in
piece 3. If the stepped fetch makes that rebate unnecessary, say so and
delete it with the evidence; if it makes it wrong, that is a finding.

- [ ] **Step 1: Write the failing tests, run them.**
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Unit suite, then the gate, plus `--only intr_2_mode0_timing_sprites`.**
- [ ] **Step 4: `--only screen`, counts before and after.**
- [ ] **Step 5: Mutation-test:** make the fetch uncancellable; sample the height once at the start.
- [ ] **Step 6: Full run, scoreboard, commit, push.**

---

### Task 11: The last two, and the piece closed

Two ROMs are left and they are not the same kind of problem.

**`ashiepaws/strikethrough`** — 53 differing pixels on **one scanline**, row
68, x 63–142, undiagnosed. Diagnose it the way plan 1's ROMs were
diagnosed: trace what it does, find what it expects. Fix it if the fix is
principled; write it up if it is not.

**`daid/ppu_scanline_bgp`** — 7,186 pixels, and the investigation measured
the disagreement as a **uniform 12 dots** across the whole scanline. That is
not one of the groups above. Decide it under the project's own rule and
**write the entry either way**: what the ROM measures, what this emulator
does, what the twelve dots are, and what would settle it. **Leaving it
failing with a written reason is an acceptable outcome and was predicted in
the spec.**

- [ ] **Step 1: Diagnose `strikethrough`.** Report what it is before deciding what to do.
- [ ] **Step 2: Act on it** — fix with a test, or write the entry.
- [ ] **Step 3: Diagnose `ppu_scanline_bgp`'s twelve dots** against `m3_bgp_change`, which passes exactly and pins the seven-dot lag. If the two disagree, that disagreement is the finding.
- [ ] **Step 4: Act on it**, and write the `docs/known-divergences.md` entry regardless of which way it goes.
- [ ] **Step 5: Clean configure and build; whole unit suite; `python tools/check_core_isolation.py`.**
- [ ] **Step 6: Both full suites, then the scoreboard.** Compare group by group against 141/165. SST must still be 499/500.
- [ ] **Step 7: Re-read `docs/known-divergences.md` whole.** This piece adds several entries and edits others; check none of them overclaim and none are stale.
- [ ] **Step 8: Commit and push.**
- [ ] **Step 9: Verify CI** through the GitHub API and **read the response**.

## Self-review

**Spec coverage.** Groups A (Tasks 2–5), H (5), D (8), E (9), F and G (10),
C and J (11). Group B, I and K were plan 1. Task 1 is the enabling
refactor and Task 6 is the safety net for the silent risk the spec names.

**Placeholders.** None. Every task names its ROMs, its gate and its
mutations. Task 4 and Task 8 deliberately send the implementer to the
investigation's measurements rather than restating numbers this plan has not
verified — that is the piece-3 lesson about writing remembered values into
a plan.

**Type consistency.** No new types are promised. `windowSkip_` is named as
the thing Task 5 deletes, and `dotsRemaining` as the thing Task 6
re-derives; both are existing names in `src/core/`.
