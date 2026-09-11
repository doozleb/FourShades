# FourShades

A Game Boy emulator written in C++20, built in public with AI coding agents —
and measured, honestly, against the public test ROMs.

<!-- scoreboard:start -->
```
cpu instructions  ███████████████░   499 / 500
test roms         ████████░░░░░░░░    85 / 165
```
<!-- scoreboard:end -->

**Status: the machine is done (piece 2 of 6).** The CPU line counts SM83
instructions passing every one of their 1,000
[SingleStepTests](https://github.com/SingleStepTests/sm83), which check every
register, every byte of memory and every bus cycle. 499 of the 500 pass. The
one that doesn't is STOP: the tests and the hardware documentation disagree
about it, and FourShades follows the documentation. The test-ROM line counts
the 165 original-Game-Boy tests that gbdev's Emulator Shootout runs with a
pass condition; a test passes only on its author's own pass signal. Piece 2
turned the CPU into a Game Boy that runs real test ROMs headless: the memory
map, MBC1, the timer, interrupts, serial, joypad and OAM DMA, plus a
placeholder LCD-line timer. The
groups that depend only on that machine are complete or nearly so (the
generated table below has each group's score), including Blargg's
`cpu_instrs`, the independent CPU cross-check. The remaining groups wait
on hardware piece 2 doesn't implement: pixel-accurate PPU timing (oam bug, ppu
timing, screen — piece 3), the other cartridge chips (mbc2/mbc5, mbc3/rtc, the
rest of mbc1 and oam dma — piece 4) and sound registers (boot state, sound —
piece 5). Every divergence between a test and the hardware documentation,
including the rule for resolving them, is in
[docs/known-divergences.md](docs/known-divergences.md).

The test-ROM line, group by group, with the first test each group fails:

<!-- groups:start -->
| group | passing | first failing test |
|---|---|---|
| cpu instructions | 11 / 11 |  |
| cpu timing | 8 / 8 |  |
| oam bug | 2 / 7 | `blargg/oam_bug/1-lcd_sync.gb`: Failed #3 |
| sound | 0 / 12 | `blargg/dmg_sound/01-registers.gb`: Failed #2 |
| cpu & interrupts | 31 / 31 |  |
| boot state | 2 / 3 | `mooneye/acceptance/boot_hwio-dmgABCmgb.gb`: failure bytes (0x42) over serial |
| oam dma | 5 / 6 | `mooneye/acceptance/oam_dma/sources-GS.gb`: cartridge: unsupported cartridge type 0x1B |
| ppu timing | 0 / 12 | `mooneye/acceptance/ppu/hblank_ly_scx_timing-GS.gb`: timeout after 6.5 s |
| serial | 1 / 1 |  |
| timer | 13 / 13 |  |
| mbc1 | 12 / 13 | `mooneye/emulator-only/mbc1/multicart_rom_8Mb.gb`: failure bytes (0x42) over serial |
| mbc2 / mbc5 | 0 / 15 | `mooneye/emulator-only/mbc2/bits_ramg.gb`: cartridge: unsupported cartridge type 0x06 |
| screen | 0 / 30 | `mooneye/manual-only/sprite_priority.gb`: needs the PPU (piece 3) |
| mbc3 / rtc | 0 / 3 | `cpp/rtc-invalid-banks-test.gb`: needs the PPU (piece 3) |

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
src/core/                 the emulator core: CPU, memory map, timer, serial, cartridge (no window, no files)
tests/                    unit tests (doctest)
tools/sst/                the SingleStepTests harness and pinned-data manifest
tools/roms/               the test-ROM harness, its pinned test list and manifest
tools/scoreboard.py       turns a test run into the scoreboard above
third_party/              vendored doctest and nlohmann/json, hash-pinned
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
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
.\tools\dev.cmd ctest --preset release
python tools/sst/fetch_sst.py              # the test data, pinned and hash-checked
.\build\release\tools\sst\sst_runner.exe   # score the CPU
python tools/roms/fetch_roms.py            # the test ROMs, pinned and hash-checked
.\build\release\tools\roms\rom_runner.exe  # score the machine against the test ROMs
```

## Planned scope

Roughly in order, each gated on the test ROMs rather than on looking right:

- **SM83 CPU** — the ~500 opcodes, then Blargg's `cpu_instrs`
- **Memory map and cartridge** — MBC1 at minimum
- **Timer and interrupts** — where "looks fine" and "is correct" first diverge
- **PPU** — background, window, sprites, and the mid-scanline behaviour that
  makes this hard
- **Input, then audio**

The first milestone is not "it plays Tetris". It is **the CPU passing every
SingleStepTests instruction, then Blargg's first test ROM**, at which point the
test-ROM line reads something other than zero.

## Licence

MIT. See [LICENSE](LICENSE).

## A note on authorship

The code in this repository is written with AI coding agents, directed
spec-first and test-first, and reviewed before it lands. Commits carry
`Co-Authored-By` trailers where that applies. That is the subject of the project
rather than a disclaimer at the bottom of it.

[gso]: https://arxiv.org/abs/2505.23671
