# FourShades piece 3: the PPU

**Status:** design, approved by the owner 2026-09-14
**Date:** 2026-09-14
**Piece:** 3 of 6 (foundation + CPU ✓ → machine + test-ROM scoreboard ✓ →
**PPU** → a window (3b) → cartridge chips → sound → browser build)

## Goal

Replace the placeholder LCD line counter with a real picture-processing unit,
modelled dot by dot, and score it with the test ROMs' own reference
screenshots. Piece 3 is headless: it produces a 160×144 frame in memory. The
window that displays it is piece 3b.

Today's score is 85 / 165. The 47 tests in scope are:

| Group | Tests | What they check |
|---|---|---|
| ppu timing | 12 | the exact dot each mode starts and each interrupt fires |
| screen | 30 scored | the finished picture, against a reference image |
| oam bug | 5 still failing | OAM corruption caused by the CPU's 16-bit address unit |

A perfect piece 3 reads 132 / 165. The honest expectation is lower: the 24
Mealybug tests change registers mid-line and are the hardest thing in the
project so far.

## Approach

The PPU is modelled the way the hardware works: a fetcher that walks its steps
two dots at a time, a background queue and a sprite queue, and one pixel pushed
out per dot during mode 3 (Pan Docs "Pixel FIFO"). The alternative — drawing a
whole line at once with a separate timing model — cannot represent a register
change part-way along a line, which is exactly what Mealybug tests, and it
would need a second hand-tuned timing model beside the renderer.

## Scope

**In:**
- `Ppu`: mode state machine (2 → 3 → 0 per line, 1 for lines 144–153), LY,
  LYC, STAT, LCDC, SCX/SCY, WX/WY, BGP/OBP0/OBP1, the VBlank and STAT
  interrupt lines, and ownership of VRAM and OAM.
- The fetcher and the two pixel queues: background and window tiles, sprite
  fetches, priority and palettes, producing a 160×144 frame of shade indices
  0–3.
- Mode 3 length: 172 dots plus the SCX%8 scroll penalty, the 6-dot window
  start-up, and the 6–11 dot per-sprite penalty (Pan Docs "Rendering").
- The quirks the tests measure: line 153 reporting as line 0, the first frame
  after the LCD is switched on, the dot LY=LYC updates, and the DMG "writing
  STAT acts as if 0xFF were written for one M-cycle" quirk.
- Access blocking by mode: VRAM unreadable in mode 3, OAM in modes 2 and 3,
  reads returning 0xFF and writes ignored; FEA0–FEFF reads 0xFF while OAM is
  blocked and 0x00 otherwise. `peek()` is never blocked.
- The OAM corruption bug, including the `Bus::idle(u16 address)` change that
  lets the CPU's 16-bit increment/decrement unit expose the address it puts on
  the bus.
- Power-on phase chosen so STAT reads 0x85 at PC=0x0100, closing the
  divergence recorded in piece 2.
- The harness's screenshot comparator and the failure artefacts it writes.

**Out:** a window, input and sound (3b, 5); CGB features; the sprite "10 per
line" hardware limit is *in* scope (it affects the picture), but CGB priority
rules are not; MBC2/3/5 (piece 4).

## Architecture

```
src/core/Ppu.h/.cpp        modes, dots, registers, interrupt lines, blocking rules
src/core/PixelPipeline.h/.cpp  fetcher steps, background and sprite queues, pixel output
src/core/Ppu takes over    vram_ and oam_ from GameBoy; GameBoy keeps routing
src/core/LcdTiming.*       deleted, along with its tests
```

- `GameBoy::tick()` advances the PPU by one M-cycle (4 dots) and ORs the IF
  bits it returns, as it already does for the timer and serial.
- The PPU exposes: `read(u16)`, `write(u16, u8)` for FF40–FF4B; `vramRead`,
  `vramWrite`, `oamRead`, `oamWrite` used by `GameBoy` with the blocking rules
  applied; `peekVram`, `peekOam` for the debugger view; `frame()` returning the
  160×144 shade indices; `frameCount()`; and `mode()`.
- OAM DMA writes go straight to OAM through a path that ignores blocking (the
  DMA owns the bus), and the PPU can see a DMA is in progress, which piece 4's
  sprite-fetch behaviour will need.
- **Bus change:** `virtual void idle(u16 address)` replaces `idle()`. The CPU
  passes the address its 16-bit unit is driving (INC/DEC rr, PUSH, LD A,(HL±),
  and so on) and 0 where no address is driven. `RecordingBus` records it but
  the SingleStepTests comparator keeps ignoring addresses on idle cycles, so
  the CPU score cannot move. **SST must stay 499/500 throughout.**

## Timing model

- A line is 456 dots; a frame is 154 lines; 4 dots pass per M-cycle.
- Mode 2 (OAM scan) is 80 dots and selects up to 10 sprites for the line.
- Mode 3 starts at 172 dots and grows: `SCX % 8` discarded pixels, 6 dots when
  the window starts on this line, and 6–11 dots per sprite drawn, per Pan Docs'
  object-penalty algorithm.
- Mode 0 fills the rest of the line, so every line is exactly 456 dots.
- Lines 144–153 are mode 1; VBlank's interrupt fires on entering line 144.
- The CPU's access point within an M-cycle stays where piece 2 put it (advance,
  then access). The PPU is stepped dot by dot inside that advance, so the
  access point can be moved within the M-cycle later without touching the
  renderer if the timing tests demand it. Any such change gets recorded.

## The STAT interrupt line

STAT's interrupt is a level line: the OR of the selected conditions (LYC match,
mode 0, mode 1, mode 2). An interrupt is requested only when that line goes
from low to high, which is what `stat_irq_blocking` checks. The DMG quirk where
a write to STAT behaves as if 0xFF were written for one M-cycle is included,
because it can raise a spurious interrupt.

## The OAM corruption bug

Per Pan Docs "OAM Corruption Bug", on DMG a 16-bit register stepped while
holding an address in FE00–FEFF, during mode 2, corrupts OAM: rows of 8 bytes,
first row exempt, with the documented write, read and read-during-step
patterns. FourShades implements all three patterns. The trigger comes from the
address on idle cycles (the new `Bus::idle(u16)`) plus ordinary OAM accesses
during mode 2. Blargg's `oam_bug` ROMs arbitrate; `3-non_causes` and
`6-timing_no_bug` already pass and must keep passing.

## Scoring: the screenshot comparator (harness only)

- A new `roms::Screenshot` decodes the Shootout's reference PNGs. Two formats
  appear at the pinned commit: 8-bit truecolour using the shades 255, 170, 85
  and 0, and 2-bit greyscale. Both map to shade indices 0–3. Anything else is
  a harness error, not a failing test.
- `runRomTest` gains the screenshot method: run to the Shootout's own `runtime`
  for that test, then compare the frame with each of the test's references.
  A match against any reference is a pass — the same criterion the Shootout
  applies to every emulator in its table.
- A failing screenshot test records the number of differing pixels and writes
  the produced frame as a PNG under `build/frames/`, so a difference can be
  looked at rather than guessed at.
- The comparator is exact: no tolerance, no fuzz. Loosening it is forbidden by
  `CLAUDE.md`.

## Testing

Unit tests (doctest), each written to fail first:
- mode sequence and lengths for a plain line, and that every line is 456 dots;
- mode 3 stretching: SCX%8, window start, sprite penalties (a line with 0, 1
  and 10 sprites);
- LY/LYC timing, the line-153 report, and the first frame after the LCD is
  switched on;
- the STAT line: no second interrupt while the line stays high, one on each
  rising edge, and the DMG write quirk;
- blocking: VRAM and OAM per mode, FEA0–FEFF, DMA's unblocked path, `peek`
  unaffected;
- the fetcher: a known tile map and tile data produce a known row of pixels,
  with scroll, window, flips, palettes and sprite priority;
- the 10-sprites-per-line limit and the "X=0 still counts" rule;
- OAM corruption: the three documented patterns on synthetic OAM;
- the screenshot comparator: both PNG formats decoded, an exact match passes,
  a one-pixel difference fails and reports 1.

The SingleStepTests suite keeps running as the CPU regression guard, and the
ROM runner covers the rest.

## Success criteria

1. SST still 499 / 500, only STOP failing.
2. All 12 `ppu timing` tests pass, or each failure has a written explanation.
3. dmg-acid2 passes. It is the single best-known correctness picture for a DMG
   PPU.
4. The score is published honestly, whatever it is, with the per-group table
   regenerated and CI green.
5. The power-on STAT divergence entry is closed, or restated with evidence.
6. A full ROM run stays under 5 minutes in CI.

## Risks

| Risk | Response |
|---|---|
| Mealybug's mid-line tests need dot-exact fetcher behaviour | They are the last group tackled; each failure gets a written explanation rather than a fudge |
| The `Bus::idle` change disturbs the CPU score | The comparator ignores idle addresses; CI enforces 499/500 |
| The OAM bug's patterns are subtle | Implement per Pan Docs, arbitrated by Blargg's `oam_bug`; the two currently passing tests are a regression guard |
| Dot-by-dot stepping is slow | Measure: the ROM suite must stay under 5 minutes in CI; optimise only with a measurement |
| PPU work leaks into the harness or vice versa | The isolation check already forbids it; the comparator lives in `tools/roms/` |
