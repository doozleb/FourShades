# FourShades

A Game Boy emulator written in C++20, built in public with AI coding agents —
and measured, honestly, against the public test ROMs.

```
test roms  ░░░░░░░░░░░░░░░░  0 / 1300
```

**Status: not started.** There is no emulator code in this repository yet. The
scoreboard above is real, and it will stay at zero until it isn't.

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

- **The answers already exist.** Roughly 1,300 public test ROMs define exactly
  what correct behaviour is. There is no arguing with a failing test.
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
docs/superpowers/specs/   the reasoning behind the project
docs/superpowers/plans/   task-by-task implementation plans
```

Source, tests and build files arrive with the first task.

## Planned scope

Roughly in order, each gated on the test ROMs rather than on looking right:

- **SM83 CPU** — the ~500 opcodes, then Blargg's `cpu_instrs`
- **Memory map and cartridge** — MBC1 at minimum
- **Timer and interrupts** — where "looks fine" and "is correct" first diverge
- **PPU** — background, window, sprites, and the mid-scanline behaviour that
  makes this hard
- **Input, then audio**

The first milestone is not "it plays Tetris". It is **the CPU passing Blargg's
first test ROM** — at which point the scoreboard reads something other than
zero.

## Licence

MIT. See [LICENSE](LICENSE).

## A note on authorship

The code in this repository is written with AI coding agents, directed
spec-first and test-first, and reviewed before it lands. Commits carry
`Co-Authored-By` trailers where that applies. That is the subject of the project
rather than a disclaimer at the bottom of it.

[gso]: https://arxiv.org/abs/2505.23671
