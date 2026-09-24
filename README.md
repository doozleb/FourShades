# FourShades

A Game Boy emulator written in C++20, built in public with AI coding agents —
and measured, honestly, against the public test ROMs.

<!-- scoreboard:start -->
```
cpu instructions  ███████████████░   499 / 500
test roms         ██████████████░░   149 / 165
```
<!-- scoreboard:end -->

**Status: it plays, with every cartridge chip and a sound unit (piece 5 of 6).**
FourShades is a Game Boy that runs real test ROMs headless - memory map, the
MBC1, MBC2, MBC3-with-clock and MBC5 cartridge controllers, timer, interrupts,
serial, OAM DMA, and a dot-by-dot picture-processing unit that draws
background, window and objects into a 160x144 frame in memory. It passes
dmg-acid2. That frame goes to an SDL3 window at the DMG's own 59.727 Hz, the
keyboard reaches the joypad register, and a cartridge with a battery keeps its
save. The sound hardware is emulated as well - all four channels, the frame
sequencer, the envelopes, the sweep and the DMG's wave RAM window - and passes
12 of the 12 sound test ROMs. It is not audible through a speaker yet: that is
piece 5b, and it is in progress.

The two lines above mean:

- **cpu instructions** — SM83 instructions passing all 1,000 of their
  [SingleStepTests](https://github.com/SingleStepTests/sm83), which check every
  register, every byte of memory and every bus cycle. The one failure is STOP,
  where the tests and the hardware documentation disagree and FourShades
  follows the documentation.
- **test roms** — the original-Game-Boy tests that gbdev's
  [Emulator Shootout](https://gbdev.io/GBEmulatorShootout/) runs and that have
  a pass condition. A test counts only on its author's own pass signal: Blargg's
  result text, or Mooneye's registers and serial bytes.

Every group that depends only on the CPU, the machine or the PPU's timing is
complete: `cpu instructions` (including Blargg's `cpu_instrs` — written by
someone other than SingleStepTests' author, so it is the independent check
that the CPU wasn't fitted to one suite), `cpu timing`, `cpu & interrupts`,
`serial`, `timer`, `oam bug` and `ppu timing`.

What still fails is two separate things:

- **`screen`, 4 of 30.** The picture is drawn, and dmg-acid2 — the best-known
  single correctness image for a DMG — passes. The 26 that remain are 22
  Mealybug Tearoom tests, which change LCDC, the palettes, the scroll or WX
  part-way through a scanline and measure the result pixel by pixel, plus
  `bully`, `strikethrough`, `ppu_scanline_bgp` and `stop_instr`. Each one's
  pixel difference is listed, test by test, in the divergences document,
  along with the decisions behind the drawing; the group table below has one
  row per group, not per test.
- **`boot state`, 2 of 3.** `boot_hwio-dmgABCmgb` reads every hardware
  register at power-on. It used to stop at $FF10, the first sound register;
  with the APU in, it gets past all of those and stops at $FF44 — LY —
  reading 09 where it wants 0A. That is a PPU power-on phase error of about
  63 M-cycles that the missing sound block had been hiding. Correcting it
  means moving a phase that `ppu timing` 12 / 12 and the `screen` group
  currently pin, so this ROM belongs with the picture, not with the sound.

Where a test and the hardware documentation disagree, the decision and its
evidence are in [docs/known-divergences.md](docs/known-divergences.md), along
with the rule for resolving them.

The test-ROM line, group by group, with the first test each group fails:

<!-- groups:start -->
| group | passing | first failing test |
|---|---|---|
| cpu instructions | 11 / 11 |  |
| cpu timing | 8 / 8 |  |
| oam bug | 7 / 7 |  |
| sound | 12 / 12 |  |
| cpu & interrupts | 31 / 31 |  |
| boot state | 3 / 3 |  |
| oam dma | 6 / 6 |  |
| ppu timing | 12 / 12 |  |
| serial | 1 / 1 |  |
| timer | 13 / 13 |  |
| mbc1 | 13 / 13 |  |
| mbc2 / mbc5 | 15 / 15 |  |
| screen | 14 / 30 | `mealybug-tearoom-tests/ppu/m3_lcdc_bg_en_change.gb (DMG)`: differs from the reference in 376 pixels |
| mbc3 / rtc | 3 / 3 |  |

Not counted (informational in the Shootout, no pass condition): `acid/which.gb (DMG)`, `daid/rom_and_ram.gb`.
<!-- groups:end -->

**Correction (11 September 2026):** the test-ROM line has been corrected
twice. It first read 0 / 1300; that total was an estimate from early planning
that I never checked. The real list is the 167 original-Game-Boy tests that
gbdev's Emulator Shootout runs (143 from its suites plus 24 Mealybug, at
Shootout commit `38b926b`), and the line then read N / 167. But 2 of those 167
have no pass condition: they have no reference image, and the Shootout itself
reports them as informational, so no emulator can pass them. The line now
counts the 165 that do (the two informational ones are listed under the group
table). Both lines are generated from a real test run, and CI fails any
commit whose scoreboard doesn't match what the code actually scores.

**Correction (22 September 2026):** the scoreboard committed with `e3066d6`,
"score screenshot tests against the Shootout's references", moves the total
from 85 to 93 and the `ppu timing` group from 0 / 12 to 6 / 12. Reading the
history, that looks like the screenshot comparator's doing. It was not. The
scoreboard had not been regenerated since `932f73c`, so the twenty-two
commits in between — including the whole first half of the PPU, from the mode
machine to objects — were still scored as if they did not exist. `e3066d6` earned the
two `screen` passes in that diff; the six `ppu timing` passes were already
there and had simply never been published. The total was never wrong, only
its attribution to a commit. Nothing is being rewritten; the correction lives
here.

**Correction (22 September 2026):** the commit message for `7ab9eae`, "put
rendering where the hardware images measure it", gives the screenshot group's
before-total as 120,110 differing pixels. That figure was wrong: the 27
failing tests in the group summed to 118,088 before that commit and 73,628
after it, so the fall is 38%, not the 39% the message claims. The commit is
pushed and is not being rewritten, so the correction lives here, in the same
spirit as the one above it.

---

## Why this exists

On the [GSO benchmark][gso], coding agents resolve around **21% of Python
tasks** and around **4% once C or C++ is involved**. Five times worse, on the
same models, for the same class of problem.

That gap is not mysterious once you have worked in it:

| | |
|---|---|
| **Hostile output** | A template instantiation error runs to hundreds of lines, almost all noise. Compilers were written for humans who can skim; an agent reads every token and burns its context on it. |
| **Broken loop** | Change a file in web work and see the result instantly. Here it compiles for minutes — and then still cannot press play and notice the sprite is one pixel out. |
| **No prior art** | Vastly more public code is Python and JavaScript than cycle-accurate C++. The model has read far less of what you are asking it to write. |

Almost everything written about agentic development covers React components and
Python scripts. The place these tools measurably struggle most is the place
almost nobody is documenting.

This repository is an attempt to document it properly.

## Why a Game Boy emulator

Because it is a **solved problem**, and that is the point.

There are hundreds of Game Boy emulators and several are far better than this
one will be. Nobody needs another. What the Game Boy provides is a *yardstick*:

- **The answers already exist.** The 165 original-Game-Boy test ROMs with a
  pass condition in gbdev's
  [Emulator Shootout](https://gbdev.io/GBEmulatorShootout/) define exactly what
  correct behaviour is, and other emulators' results against them are public.
  There is no arguing with a failing test.
- **The difficulty is respected.** Cycle-accurate timing, PPU behaviour and
  interrupt edge cases are genuinely hard, and C++ developers know it.
- **The result is a number.** The pass count is not an opinion, a vibe, or a
  screenshot of a chat window. It goes up, or it does not.

An emulator that boots a commercial game is a claim anyone can check.

## How it is built

Every feature follows the same loop, and the artefacts are committed:

1. **A written spec** — what it does, what it does not do, and how we would know
   it failed.
2. **A plan** — broken into tasks small enough that a reviewer can reject one
   without rejecting the rest.
3. **A failing test, observed failing.** Not written and assumed. Agents produce
   code that looks right and does not work, and reading it will not reliably
   tell you which is which.
4. **Implementation**, then the test green, then a commit.
5. **Review** by something other than whatever wrote it, pointing at file and
   line — with its findings treated as claims to verify, not as facts.

Specs and plans live in `docs/superpowers/` and are published rather than
hidden. They are the receipts.

## What gets published

Every claim ships with its artefact: the spec, the diff, the commit, the test
output. **If the number goes down, it gets published going down.**

The failures are the useful part. Anyone can write a post about an agent
one-shotting a feature; far fewer will show you the three hours it spent
confidently fixing the wrong file.

Write-ups are at **[doozleb.com/posts](https://doozleb.com/posts/)**, and the
project page is **[doozleb.com/projects/fourshades](https://doozleb.com/projects/fourshades/)**.

## Repository layout

```
src/core/                 the emulator core: CPU, memory map, timer, serial, cartridge, PPU (no window, no files)
app/                      the SDL3 window: presentation, keyboard, save files, and the state machine behind them
tests/                    unit tests (doctest)
tools/sst/                the SingleStepTests harness and pinned-data manifest
tools/roms/               the test-ROM harness, its pinned test list and manifest
tools/scoreboard.py       turns a test run into the scoreboard above
third_party/              vendored doctest, nlohmann/json and SDL3, hash-pinned
docs/superpowers/specs/   the reasoning behind each piece
docs/superpowers/plans/   task-by-task implementation plans
docs/known-divergences.md where a test and the hardware documentation disagree
scoreboard.json           the scoreboard above, as data (generated)
CLAUDE.md                 rules for the AI agents working in this repo
```

## Building

Windows and Visual Studio 2026 (with the C++ workload), plus Python 3.9 or
newer for the test-data fetcher and the scoreboard scripts. Open the folder in
Visual Studio, or from PowerShell:

```powershell
python third_party/sdl/fetch_sdl.py        # SDL3, pinned and hash-checked; configure fails without it
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
.\tools\dev.cmd ctest --preset release
python tools/sst/fetch_sst.py              # the test data, pinned and hash-checked
.\build\release\tools\sst\sst_runner.exe   # score the CPU
python tools/roms/fetch_roms.py            # the test ROMs, pinned and hash-checked
.\build\release\tools\roms\rom_runner.exe  # score the machine against the test ROMs
```

## Playing something

```powershell
.\build\release\app\fourshades_app.exe path\to\game.gb
```

Started with no argument - double-clicked, say - it opens a window that
invites a ROM to be dropped onto it, and a ROM dropped on a running window
replaces the one playing. A bad path or an unsupported cartridge on the
command line is reported on stderr and exits non-zero; a bad drop says why on
the window and waits for another one.

| key | |
|---|---|
| arrow keys | d-pad |
| Z, X | A, B |
| Enter, Backspace | Start, Select |
| P | toggle the grey and green palettes |
| M | mute; the machine keeps running, it just stops being audible |
| Space | pause; also writes the save to disk |
| R | reset: a power cycle, not a poke - the machine is rebuilt from the cartridge |

A cartridge whose header declares a battery gets a `.sav` beside the ROM, in
raw cartridge-RAM order so other emulators can read it. It is restored when
the ROM loads and written on exit, on pause, and before a dropped ROM
replaces the machine - always through a temporary file and a rename, so an
interrupted write cannot destroy the save that was already there. A reset
keeps that RAM, because that is what the battery is for; everything else
about the machine is thrown away.

## Planned scope

Six pieces, each gated on the test ROMs rather than on looking right:

| | Piece | State |
|---|---|---|
| 1 | **SM83 CPU** — every opcode, cycle by cycle | done: 499 / 500 |
| 2 | **The machine** — memory map, MBC1, timer, interrupts, serial, DMA, and the test-ROM scoreboard | done: 85 / 165 |
| 3 | **The PPU** — background, window, sprites, and the mid-scanline behaviour that makes this hard | done: 106 / 165 |
| 3b | **A window** — SDL3, so the frame can be seen and the buttons pressed | done |
| 4 | **Cartridge chips** — MBC2, MBC3 with its clock, MBC5 | done: 126 / 165 |
| 5 | **Sound** — the four channels, scored against blargg's twelve ROMs | done: 138 / 165 |
| 5b | **Audible output** — SDL3 audio, so the sound can be heard | in progress |
| 6 | **In the browser** — the same core compiled to WebAssembly, playable on the site | |

Every cartridge chip is in and the sound unit scores 12 / 12, so what is left
on the roadmap is making that sound audible and then the browser build. Of the
27 test ROMs still failing, 26 are `screen` tests and the twenty-seventh —
`boot_hwio` — is a PPU power-on phase error, so the remaining ROM work is all
in the picture.

## Licence

MIT. See [LICENSE](LICENSE).

## A note on authorship

The code in this repository is written with AI coding agents, directed
spec-first and test-first, and reviewed before it lands. Commits carry
`Co-Authored-By` trailers where that applies. That is the subject of the project
rather than a disclaimer at the bottom of it.

[gso]: https://arxiv.org/abs/2505.23671
