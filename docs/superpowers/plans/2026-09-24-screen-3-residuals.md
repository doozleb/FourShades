# Piece 6, Plan 3: The Residuals

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Take the suite from **149/165** as far as the evidence reaches.
Realistically **154**, possibly **158**. The ceiling is **163**, and 165 is
not available.

**Ordered by evidence, not by pixel count.** Plan 2's closing triage
measured what each remaining failure needs; two of them need something this
repository does not contain, and they are carried as questions rather than
scheduled as deliverables.

**Spec:** `docs/superpowers/specs/2026-09-23-screen-design.md`.
**Evidence:** `.superpowers/sdd/task-11-report.md` — its triage is the
reason this plan is ordered the way it is. Read it before Task 1.

## Where this stands

All sixteen remaining failures are in `screen`; **every other group in the
suite is whole**, and SST is at its intended 499/500. The group's error is
11,399 differing pixels, and **7,239 of them — 64% — are two ROMs already
diagnosed and deliberately left failing**:

- `daid/ppu_scanline_bgp` (7,186 px). Its twelve dots sit in *LY becomes 0 →
  LYC STAT request → leaving `halt` → dispatch*, and they contradict a
  hardware photograph **and** at least five hardware-verified Mooneye ROMs
  this emulator passes. Written up.
- `ashiepaws/strikethrough` (53 px). An OAM-DMA-versus-object-scan race. No
  monotone race selects the one object the reference draws; it needs a
  suppression mechanism, and implementing the candidate gives 7 px and still
  fails. Written up.

**Neither is in this plan.** Do not reopen them without new evidence from
outside this repository.

## Global Constraints

- Follow Pan Docs over an emulator-generated test; a hardware-verified test
  outranks a Pan Docs simplification; either way the decision goes in
  `docs/known-divergences.md` with the evidence.
- **Never special-case a test, a test name, a ROM, or an address pattern
  only a test uses.** This plan is where that rule bites hardest: several
  remaining ROMs are the *only* evidence for the thing they measure, so a
  rule derived from one of them and nothing else is a fit. Where that is the
  case a task says so, and the honest outcome may be to leave it.
- `src/core/` must not depend on the tests: no test names, no file or JSON
  access, no includes from `tools/`, no host clock.
- Never loosen the comparator, the detectors, the runner or any image
  comparator; never raise a time limit or special-case a ROM.
- Never edit `tools/sst/data/`, `tools/roms/data/`, either `manifest.sha256`,
  or `tools/roms/tests.json` by hand.
- The README scoreboard block and `scoreboard.json` are written only by
  `python tools/scoreboard.py update build/sst-results.json build/rom-results.json`
  from a full unfiltered run. Every score-moving task ends with that.
- **SST stays at 499/500.** No task here touches the CPU.
- Stage explicit paths. Never `git add -A`.

## The gate, after every task, before its commit

```
.\build\release\tools\roms\rom_runner.exe --only "ppu timing"                --out build\gate.json
.\build\release\tools\roms\rom_runner.exe --only intr_2_mode0_timing_sprites --out build\gate.json
.\build\release\tools\roms\rom_runner.exe --only m3_bgp_change               --out build\gate.json
.\build\release\tools\roms\rom_runner.exe --only m3_scx_low_3_bits           --out build\gate.json
.\build\release\tools\roms\rom_runner.exe --only acid                        --out build\gate.json
.\build\release\tools\roms\rom_runner.exe --only "oam bug"                   --out build\gate.json
```

A full run is about 21 seconds and the unit suite 1.6 seconds, so run both
at the end of every task regardless.

**Hazards, learned in plan 2.** Restoring a mutated file with PowerShell's
`Copy-Item` preserves its timestamp, so Ninja keeps stale objects and a
green build can be running mutant code — stamp the mtime and verify
behaviourally, not only by hash. A timing case measured on line 0 measures
less than it appears to; use line 1. A unit case cannot separate a two-dot
stage on an undisturbed line, because both grids are M-cycle-aligned.

---

### Task 1: Re-diff group D's four frames

**ROMs:** `m3_lcdc_tile_sel_change` (410 px), `m3_scy_change` (259),
`m3_lcdc_bg_map_change` (124), `m3_scx_high_5_bits` (12) — 805 together.

**Why first: the first step costs nothing and has the best record in the
piece.** The cause is genuinely unknown — the divergence file says it "has
not been traced to anything" — but the residual has a shape, one tile per
line, and the last two times anyone diffed these frames line by line they
found mechanisms worth 2,778 and 1,472 pixels.

- [ ] **Step 1: Diff all four frames against their references, line by line**, and describe the shape of what is left: which columns, which lines, and what the reference draws there instead. Report that before proposing anything.
- [ ] **Step 2: Form a hypothesis and test it against all four**, not one. A mechanism that explains one frame and not the others is the wrong mechanism.
- [ ] **Step 3: If a mechanism explains all four**, write the failing test, watch it fail, implement, mutation-test.
- [ ] **Step 4: If nothing explains all four**, say so, record the shape in `docs/known-divergences.md`, and stop. **That is a complete task** — the diff is the deliverable, and a wrong mechanism costs more than none.
- [ ] **Step 5: Gate, unit suite, full run. If a verdict flipped: scoreboard, commit, push.** Otherwise commit with the pixel counts.

---

### Task 2: LCDC bit 0, which is not a fetch input

**ROM:** `m3_lcdc_bg_en_change` (376 px), alone.

**What Task 8 of plan 2 found:** LCDC bit 0 is read at **pixel emission**,
not during a fetch. It is not a fetch input, Mealybug's notes say nothing
about BG_EN, and that is why it never moved a pixel through any of the fetch
work — and never could have.

**What to try:** a one-dot sweep at emission, and the separate question of
whether clearing bit 0 blanks pixels already in the FIFO or only those
fetched after.

**The honesty requirement.** This ROM is the only evidence for whatever rule
you land on. A one-dot sweep that finds the value making this ROM pass is a
fit unless you can state the mechanism in hardware terms and show it follows
from something other than this ROM's pixels. **If you cannot, leave it
failing and write the sweep's results into `docs/known-divergences.md`** —
the table of what each dot costs is genuinely useful to the next person, and
a fitted constant is not.

- [ ] **Step 1: Sweep the emission dot and record what each value costs**, in pixels, for this ROM and for every other `screen` ROM.
- [ ] **Step 2: Test the FIFO question separately** — clearing bit 0 mid-line, with pixels already queued.
- [ ] **Step 3: Decide, on the standard above.** Implement with a test if the mechanism is articulable; record the sweep and leave it if not.
- [ ] **Step 4: Gate, unit suite, full run, scoreboard if anything flipped, commit, push.**

---

### Task 3: When LCDC bit 5 is sampled inside a fetch

**ROMs:** `m3_lcdc_win_en_change_multiple` (468 px),
`m3_lcdc_win_en_change_multiple_wx` (85) — 553 together.

All four of Mealybug's WIN_EN sentences are implemented. What is left is the
sub-fetch sample dot: the 468 pixels are **one tile wide, columns 49–56** —
the tile the window hands back on.

This is the same shape as plan 2's Task 8, which swept the three background
fetch stages and found each independently prefers its first dot. Do the same
sweep for the dot at which a fetch in flight notices bit 5 going low.

- [ ] **Step 1: Sweep the sample dot within the fetch**, recording every `screen` ROM's pixel count at each position.
- [ ] **Step 2: Check the result against Mealybug's quoted WIN_EN sentences** in `docs/known-divergences.md`. If the best-scoring dot contradicts them, follow the document and report the contradiction.
- [ ] **Step 3: Write the failing test, watch it fail, implement, mutation-test.**
- [ ] **Step 4: Gate, unit suite, full run, scoreboard if anything flipped, commit, push.**

---

### Task 4: LCDC bits 1 and 2 during an object fetch

**ROMs:** `m3_lcdc_obj_en_change` (56 px), `..._variant` (152),
`m3_lcdc_obj_size_change` (310), `..._scx` (190) — 708 together.

**Last, because it is the riskiest.** The gate here is
`intr_2_mode0_timing_sprites`, which is hardware-verified, and plan 2's Task
10 has just finished rearranging object timing around it — including moving
a three-dot constant out of the object penalty and into mode 3's length,
with the reason still unexplained.

Three things are known and worth having in hand: bit 2's read dot is already
pinned from both sides; the LCDC bit 1 cancel is implemented per Pan Docs
and **nothing in either suite measures it**; and the 60-pixel divergence
between the two `obj_size` variants is unexplained.

- [ ] **Step 1: Diff all four frames** and say what the residual's shape is in each.
- [ ] **Step 2: Derive the cancel and height dots**, per register, the way plan 2's Task 8 derived the fetch stages. Pan Docs gives the direction, not the dots.
- [ ] **Step 3: Explain the 60-pixel divergence between the two `obj_size` variants**, or say plainly that you could not.
- [ ] **Step 4: Write the failing tests, watch them fail, implement, mutation-test.**
- [ ] **Step 5: Gate — read `intr_2_mode0_timing_sprites` first** — unit suite, full run, scoreboard if anything flipped, commit, push.

---

### Task 5: The piece closed, for real this time

- [ ] **Step 1: Clean configure and build; whole unit suite; `python tools/check_core_isolation.py`.**
- [ ] **Step 2: Both full suites, then the scoreboard.** Compare group by group against 149/165 and against piece 6's start at 138/165. SST must be 499/500.
- [ ] **Step 3: Re-read `docs/known-divergences.md` whole.** Three tasks have just added to it. Check nothing overclaims, nothing is stale, and the failing-screenshot table matches a fresh run to the pixel.
- [ ] **Step 4: Update `README.md`'s prose outside the scoreboard block** — the roadmap's piece numbering is inconsistent with how the work actually ran, and the closing paragraph describes a state two plans out of date. Do not touch the generated block.
- [ ] **Step 5: Write the piece's closing summary** into `docs/known-divergences.md` or the README as appropriate: what the screen group cost, what it bought, and the honest statement of what is left and why — 165 was never available, 163 is the ceiling, and the two biggest remaining ROMs contradict evidence this project trusts more than it trusts a fix.
- [ ] **Step 6: Commit, push, and verify CI through the GitHub API, reading the response.**

## Self-review

**Spec coverage.** The spec predicted 164 as the honest target and said one
ROM might end the piece written up rather than forced. Two did, for measured
reasons, and this plan schedules only what still has evidence behind it.

**Placeholders.** None. Every task names its ROMs and its current pixel
count, and Tasks 1 and 2 explicitly permit — and define — the outcome where
the answer is "not without evidence".

**Type consistency.** No new types. Every ROM named here appears in the
failing-screenshot table in `docs/known-divergences.md` with the same count.
