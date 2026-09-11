# FourShades piece 1: foundation and SM83 CPU

**Status:** implemented 2026-09-11 (CPU 499/500; see docs/known-divergences.md)
**Date:** 2026-09-11
**Piece:** 1 of 6 (foundation + CPU → machine + test-ROM scoreboard → graphics
+ window → cartridge chips → sound → browser build)

## Goal

Stand up the FourShades codebase and build a cycle-accurate SM83 (Game Boy CPU)
core. The proof that it works is the public SingleStepTests suite. The piece is
done when **all 500 instruction files pass all 1,000 of their tests**,
checking final registers, final memory and every machine cycle's bus activity.

The score is published as a second scoreboard line, **"CPU instructions N /
500"**, generated from a real test run and checked by CI. The existing "test
ROMs 0 / 1300" line stays at 0 until piece 2.

## Scope

**In:**
- The project skeleton: CMake + Ninja, MSVC, C++20, Windows only.
- A `Bus` interface and a cycle-stepped `Cpu` that implements every legal
  opcode: 244 base opcodes and 256 CB-prefixed ones.
- CPU-side interrupt *state*: IME, EI's one-instruction delay, DI, RETI. Also
  HALT and STOP as CPU states, and illegal opcodes as a locked state.
- A downloader for the test data, pinned to one upstream commit and verified by
  hash.
- The SingleStepTests runner, a results file, and a script that generates the
  scoreboard.
- doctest unit tests, and GitHub Actions CI on Windows.
- `CLAUDE.md` rules for the agents working in the repo.

**Out (later pieces):** the real memory map, interrupt *delivery* (nothing
raises interrupts yet), timers, serial, graphics, window, sound, cartridges,
test ROMs, the browser build. No Linux build: Windows only, by decision.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Language | C++20 | Fully supported by MSVC and by Emscripten for the later browser build. C++23 support is still patchy. |
| Build | CMake + Ninja + MSVC, via `CMakePresets.json` | Bundled with Visual Studio 2026, which also opens CMake projects directly. Emscripten builds CMake projects, so piece 6 needs no new build system. |
| Platform | Windows only (local and CI) | Owner's decision. |
| CPU timing model | Cycle-stepped: each bus access costs exactly one M-cycle | The timing ROMs in piece 2 need it, and SingleStepTests check it cycle by cycle. Instruction-at-a-time would pass the final-state half and then need a rewrite. |
| Test framework | doctest, vendored single header | Small and fast. Already proven in Litharia. |
| JSON parsing | nlohmann/json, vendored single header, version pinned | Only the test runner uses it; the core never parses JSON. 167 MB of test data parses in seconds, which is fine for a score run. |
| Window library (piece 3) | SDL3 planned, SFML as fallback | Recorded here so the core stays free of either. Decided in piece 3. |

## Architecture

```
fourshades_core (static library: no window, no files, no JSON)
  core/Types.h       u8/u16 aliases, bit helpers
  core/Bus.h         abstract interface: read, write, idle, each one M-cycle
  core/Registers.h   AF BC DE HL SP PC, flag accessors (F's low nibble is always 0)
  core/Cpu.h/.cpp    state, step(), fetch/decode, interrupt state, HALT/STOP/lock
  core/CpuLoads8.cpp 8-bit loads
  core/CpuAlu8.cpp   8- and 16-bit arithmetic and logic, flag rules
  core/CpuWide.cpp   16-bit loads, stack, 16-bit arithmetic
  core/CpuControl.cpp jumps, calls, returns, RST
  core/CpuCb.cpp     CB-prefixed rotates, shifts, BIT/RES/SET

tools/sst/ (the test harness; never linked into the emulator)
  fetch_sst.py       downloads the pinned commit into tools/sst/data/ (gitignored)
  manifest.sha256    committed SHA-256 of every test file
  RecordingBus       flat 64 KB memory that logs every cycle (type, address, value)
  sst_runner.cpp     loads tests, runs the CPU, compares, writes results JSON

tools/scoreboard.py  results JSON → scoreboard.json + README scoreboard block
tests/               doctest unit tests for core and harness pieces
third_party/         doctest.h, nlohmann/json.hpp (versions noted in a README)
```

The opcode groups above ended up as one file per group —
`CpuLoads8.cpp`/`CpuAlu8.cpp`/`CpuWide.cpp`/`CpuControl.cpp`/`CpuCb.cpp` —
rather than the single `CpuAlu.cpp` this sketch originally named, matching the
implementation plan's task split. The architecture is otherwise unchanged.

**How the pieces fit.** The CPU only reaches the outside world through `Bus`.
Every `read` and `write` is one M-cycle. Cycles with no memory access call
`idle()`, which is also one M-cycle. The CPU never counts cycles itself; the
bus sees every one. That is what makes it cycle-accurate by construction. In
piece 2 the real memory map implements `Bus` and ticks the timers and graphics
on each call. The CPU code does not change.

`Bus` is a virtual interface. At about a million M-cycles a second, virtual
calls cost nothing measurable, and they keep the CPU testable against any bus.

**Match the test model.** SingleStepTests (JSMoo-generated, M-cycle
granularity) start each test with PC pointing at the opcode, and the first
recorded cycle is that opcode's fetch. `Cpu::step()` therefore fetches and
executes one full instruction starting at PC. The runner compares:
1. **Registers**: A F B C D E H L SP PC and IME. The runner fails loudly if a
   test file has any key it doesn't understand, so no field is silently
   skipped. Initial-state keys seen so far: `pc sp a b c d e f h l ime ie ram`,
   and the README also shows `ei`. The runner accepts exactly the keys actually
   present across all 500 files, and the plan must list them.
2. **Memory**: every `[address, value]` in `final.ram`.
3. **Cycles**: the same number of cycles. Each read (`r-m`) and write (`-wm`)
   cycle must match on type, address and value. Idle cycles (`---`) are checked
   for type and count only. The address and value the generator logs on an
   idle cycle are leftover bus contents from its own model, not something
   software can observe. This rule is written down so it's a stated choice, not
   a hidden loosening.

**HALT (76) and STOP (10).** These tests encode the generator's model of two
instructions whose real behaviour depends on interrupts that don't exist yet.
Implement them to match Pan Docs. If a test disagrees with Pan Docs, don't
bend the CPU to pass it. Record the disagreement in `docs/known-divergences.md`
with the evidence and leave the instruction failing. The scoreboard counts it
as failing. A post explains why.

## Scoring and results

- An opcode **passes** only if all 1,000 of its tests pass. The score is
  passing files out of 500.
- The runner writes `build/sst-results.json`. For each file it records the pass
  count and, if it fails, the **first failing test**: its name, the field that
  differs, expected and actual values, and the cycle index for cycle
  mismatches. It also prints a short summary.
- An opcode the CPU doesn't implement yet is reported as `unimplemented`, not
  as a crash.
- The runner exits non-zero only on *harness* errors: missing data, a hash
  mismatch, a malformed or unknown test format. A low score is not an error.
- `tools/scoreboard.py` writes the committed `scoreboard.json` and rewrites the
  README block between `<!-- scoreboard:start -->` and `<!-- scoreboard:end -->`.
  `--check` mode exits non-zero if the committed files don't match the results.

## Keeping the score honest

- **Pinned, hashed data.** `fetch_sst.py` downloads upstream commit
  `f9c30210245dd691661db39f5ace022c465ecc2f`. The runner refuses to run if any
  file's SHA-256 differs from `manifest.sha256`.
- **CI recomputes the score.** CI builds, runs the unit tests, fetches the data,
  runs the runner, then `scoreboard.py --check`. A commit whose README or
  `scoreboard.json` claims a score the code doesn't produce, higher or lower,
  fails CI.
- **Agent rules (`CLAUDE.md`).** Never edit `tools/sst/data/`,
  `manifest.sha256` or the scoreboard block by hand. Never make core code
  depend on test names, files or the harness. Never special-case a test. Record
  disagreements with a test in `known-divergences.md` instead.
- **Structural check.** `fourshades_core` links nothing from `tools/`, and CI
  fails if `src/core` mentions `sst`, `json`, `fopen`/`ifstream` or a test path
  (whole-word, case-insensitive, so `assert` and the like never trip it).
- **Cross-check in piece 2.** Blargg's `cpu_instrs` ROMs are a separate CPU
  test written by someone else. A CPU that passes SingleStepTests but fails
  them has been fitted to one suite, and that gets published.

## Unit tests (doctest)

These cover what SingleStepTests can't isolate, or what must hold before the
runner is trusted:
- Registers: pair views, and F's low nibble always staying 0 after a write.
- `RecordingBus`: logs read/write/idle in order with the right values.
- The test loader: parses a small handwritten test and rejects unknown keys.
- The comparator: a deliberately wrong CPU result is caught on each of
  registers, memory and cycles. This tests the tester.
- EI delay, DI, the illegal-opcode lock, and HALT entering the halted state.

## CI (GitHub Actions)

A `windows-latest` job:
1. set up the MSVC environment;
2. `cmake --preset release`, then build;
3. `ctest`;
4. fetch the test data, cached by commit hash;
5. run the SST runner;
6. `scoreboard.py --check`;
7. the structural check.

Target: under 10 minutes.

## Repository housekeeping

- Replace the leftover website entries in `.gitignore` (`node_modules/`,
  `dist/`, `.astro/`) with `build/`, `out/`, `.vs/`, `tools/sst/data/`.
- Add a "Building" section to the README: open the folder in Visual Studio
  2026, or run `cmake --preset release` from a developer prompt.

## Success criteria

1. The CPU scores 500 / 500 on SingleStepTests. Any instruction still failing
   has a documented Pan Docs divergence.
2. A full runner pass takes under 60 seconds in Release on this machine.
3. CI is green, and CI fails when the scoreboard is edited by hand. Verify this
   once on purpose.
4. The README shows both scoreboard lines, the CPU line generated.
5. Specs, plans and test output are committed, so every claim in a post points
   to a real artefact.

## Risks

| Risk | Response |
|---|---|
| The JSMoo model of HALT, STOP or idle-cycle addresses differs from hardware | Follow Pan Docs, document the divergence, keep it visible on the scoreboard |
| Agents fit the CPU to the tests instead of the hardware | Hash-pinned data, CI score check, structural check, Blargg cross-check in piece 2 |
| MSVC environment not on PATH (no `cmake`/`cl` in a normal shell) | Presets plus a VS developer shell. The plan runs the build through `vcvars64.bat`. |
| The 167 MB download is slow or flaky in CI | Cache by commit hash. The fetch retries, then fails clearly. |
