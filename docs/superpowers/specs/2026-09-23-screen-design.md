# Piece 6: the screen — design

**Date:** 2026-09-23
**Status:** approved
**Follows:** piece 5b. The suite stands at **138 / 165**, SST at 499 / 500.

## Why

Every remaining failure in the suite is a picture. Twenty-six in the
`screen` group, plus `boot_hwio` in `boot state`, which piece 5 proved is a
PPU failure wearing a sound failure's clothes.

This piece is the end of the scoreboard.

## What the failures actually are

Scouted before this spec was written, with every hypothesis measured and
reverted rather than guessed. The investigation is at
`.superpowers/sdd/screen-investigation.md`; its grouping, by root cause
rather than by filename:

| group | tests | differing pixels | what it is |
| --- | --- | --- | --- |
| A | 6 | 28,934 | the window re-activates mid-line |
| B | 1 | 22,739 | STOP does not blank the LCD |
| C | 1 | 7,187 | a uniform 12-dot disagreement |
| D | 6 | 4,515 | which dot of a background fetch samples which register |
| E | 2 | 4,330 | the same, for a window fetch |
| F | 2 | 3,508 | the object fetch's dot cost, and OBP |
| G | 4 | 1,404 | LCDC bit 1 or 2 written during an object fetch |
| H | 2 | 612 | the window's start-up cost, and WX below 7 |
| I | 1 | 290 | power-on state |
| J | 1 | 53 | one scanline, undiagnosed |
| K | 1 | — | the PPU's power-on phase (`boot_hwio`) |

**Three of the twenty-seven are not mid-scanline failures at all** — B, I
and K — and they are the cheapest three in the piece.

## The starting point is better than it looks

The natural assumption about a suite failing this way is that the renderer
draws a line at a time and a mid-line register write cannot reach it. That
is not this emulator. `Ppu` and `PixelPipeline` are already a per-dot state
machine and already read LCDC, SCX, SCY, BGP and OBP live at the dot the
fetcher or the pixel needs them — which is why these errors are a tile or
two wide rather than a screen wide.

Four specific things are wrong:

1. **The window is a one-shot latch.** `window_` is set and never cleared,
   and `advanceWindowLine()` runs once per line. Hardware re-checks every
   dot and can start the window more than once on the same scanline.
2. **There is no scanline X counter.** The window trigger is
   `pixelX_ >= wx - 7`, and WX below 7 is handled by a fudge called
   `windowSkip_`.
3. **The background fetcher is four steps over six dots.** Hardware is five
   steps over eight, with an extra push attempt at Get-Tile-Data-High.
4. **An object fetch is a lump penalty**, not a sequence of steps that a
   write to LCDC can cancel part-way through.

## Two plans, not one

The three non-pipeline failures are independent of the other twenty-four,
carry almost no regression risk, and are already measured. They go first, in
their own plan, so the suite is at 141 before anything touches the pipeline.

### Plan 1 — power-on, STOP and the boot state

Three ROMs, and all three fixes have already been measured to work.

- **The PPU's power-on phase.** `boot_hwio` reads LY at M-cycle 1190 and
  wants 0x0A where it gets 0x09, and reads STAT at 1139 where it must still
  be mode 0. Solving both constraints gives an advance of between 253 and
  455 dots, and the advance must be a **multiple of four** — a value that is
  not broke seven unit tests, which is what pins the rule. `Ppu::dot_` set
  to **356** at power-on satisfies both and was measured to pass.
- **STOP blanks the LCD.** `daid/stop_instr` prints its own failure text,
  sets BGP, and enters STOP with no button held. The reference photograph
  from hardware is **entirely white**, and the 301 pixels this emulator
  currently gets right are exactly that white text. Blanking the screen
  while the CPU is stopped was measured to take it to zero differing pixels.
- **Post-boot VRAM.** `ashiepaws/bully` checks DIV first — which the phase
  change fixes, because it reads DIV synchronised to the PPU — and then
  checks that VRAM holds the Nintendo logo the boot ROM unpacks from the
  cartridge header. That is real hardware state this emulator has never
  reproduced, and it is what the ROM is asking for.

Measured together: **140 / 165 from three changed lines**, with every
Mealybug pixel count byte-identical and `ppu timing`, `oam bug` and `sound`
untouched. The third fix takes it to 141.

**One caution, and it is the reason post-boot VRAM is its own task with its
own full run:** seeding VRAM changes the starting state of every screenshot
ROM that does not clear it first. That must be proved across the whole
suite immediately, not at the end of the piece.

### Plan 2 — the window, the fetch dot, and the objects

The remaining twenty-four, in dependence order. The window's X counter comes
first because it is worth six ROMs and 39% of the group's pixel error on its
own, and because groups H and D build on it.

What the hardware does, from Pan Docs' window and pixel-FIFO pages and the
Mealybug Tearoom repository's own PPU notes — **which are not vendored with
the ROMs and must be quoted or vendored by the plan**:

- the window's X counter starts at 0 and gets **seven free increments before
  pixel 0**;
- it compares **equal** to WX, not greater-or-equal;
- it can fire **more than once on the same scanline**;
- the window's row counter increments on **each activation**, not once a
  line.

Then: five-step, eight-dot background fetches with each register pinned to
the stage that samples it; the same for window fetches; and object fetches
as steps that a write to LCDC bit 1 can cancel.

## The ceiling is 164, not 165

`daid/ppu_scanline_bgp` disagrees with its hardware photograph by a
**uniform 12 dots**. That is not a bug in one of the groups above; it is a
disagreement about where the whole scanline sits. Under this project's own
rule — Pan Docs over an emulator-generated test, a hardware-verified test
over a Pan Docs simplification — it may end the piece still failing, with an
entry in `docs/known-divergences.md` explaining the twelve dots and what
would settle them.

**165 is not promised. 164 is the honest target, and the twenty-seventh gets
written up rather than forced.**

## What is not in this piece

STOP resetting DIV. It is unimplemented, it is recorded in `Cpu.cpp`, and it
buys **no test here** — the investigation checked. The single SingleStepTests
failure is a different question entirely: it is about whether STOP is one
byte or two, which this project answers with Pan Docs on purpose. The two
STOP failures are not the same root cause and do not fall together. **499 /
500 is the intended ceiling and stays that way.**

## Testing, and the gate that runs after every task

Unit tests per behaviour, mutation-tested, test written and run before the
implementation.

The pixel counts are the other half of the evidence: a change that reduces a
group's differing pixels without flipping a test to passing is still
progress and gets reported as a number, because a screenshot test is pass or
fail across all 23,040 pixels at once and there is no credit for being
close.

**After every task, before its commit**, four runs that take under three
seconds together against 283 for a full run:

    --only "ppu timing"      12/12 must hold
    --only m3_bgp_change     the only exact Mealybug pass, and the sole
                             evidence for the 7-dot render lag
    --only acid              dmg-acid2
    --only m2_win_en_toggle  the canary for the window work: it is precisely
                             the test that pins "the window row does not
                             advance on hidden lines"

### The risk that fails silently

`dotsRemaining` — mode 3's length. The window rewrite touches it, and it is
the one thing here that can break **without the picture changing**: the
image can be right while STAT's timing is wrong, which takes out the whole
`ppu timing` group and nothing else. It is first in the gate for that
reason.

Behind it: `m3_bgp_change` against the fetcher restructure, and
`intr_2_mode0_timing_sprites` against the object rewrite — the latter
depends on a three-dot rebate for the first object that was fitted, not
derived, in piece 3.
