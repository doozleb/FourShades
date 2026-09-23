# Piece 5b: Audible Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play the APU's output through the speakers, and prove by
measurement that what comes out is the pitch the registers asked for.

**Architecture:** `AudioResampler` — no SDL — turns the machine's cycle
count into samples, applies the DC-blocking filter and holds the buffer.
`Audio` wraps SDL3's audio stream around it and owns the drift policy. The
core is untouched: it already answers `Apu::sample()` and has no sample rate.

**Tech Stack:** C++20, MSVC, CMake + Ninja, doctest, SDL3 3.4.16 (vendored,
gitignored, fetched by `third_party/sdl/fetch_sdl.py`).

**Spec:** `docs/superpowers/specs/2026-09-23-audio-output-design.md`.

## Global Constraints

- **No test ROM scores this piece.** The suite must stay at exactly
  **138/165** and SST at **499/500**. Any movement is a regression and a
  finding, not a bonus.
- `src/core/` is not modified by this piece at all. If a task believes it
  needs a core change, stop and say so rather than making one.
- `app/` must not include anything from `tools/`; `src/core/` must not
  include SDL. `python tools/check_core_isolation.py` enforces the latter.
- Never edit the README scoreboard block or `scoreboard.json` — nothing
  moves, so neither file should change.
- Never edit `tools/roms/data/`, `tools/sst/data/`, either `manifest.sha256`,
  or `tools/roms/tests.json`.
- Stage explicit paths in every commit. Never `git add -A`.
- New `app/**` .cpp files go in `app/CMakeLists.txt`. `tests/*.cpp` is
  globbed and needs no CMake edit.
- Build only through `tools\dev.cmd` — `cmake` and `cl` are not on PATH.
- **Write the test, run it, watch it fail, then implement.** Mutation-test
  on top of that, not instead of it.

## Commands

```
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
.\build\release\fourshades_tests.exe
.\build\release\app\fourshades_app.exe <rom>
.\build\release\tools\roms\rom_runner.exe          (full run, must stay 138/165)
python tools/check_core_isolation.py
```

---

### Task 1: Cycles into samples, and the filter

**Files:**
- Create: `app/AudioResampler.h`, `app/AudioResampler.cpp`, `tests/test_audio_resampler.cpp`
- Modify: `app/CMakeLists.txt`

**No SDL in this task.** `tests/test_audio_resampler.cpp` must compile and
run without an audio device, the same way `tests/test_frame_pacer.cpp` does
for the pacer — read that file first for the house pattern.

**The numbers — use verbatim:**
- Output rate **48000** Hz.
- The machine runs at **1048576 M-cycles** per second (`GameBoy::cycles()`
  counts M-cycles; 4194304 T-cycles a second, four to an M-cycle).
- A sample is therefore due every **1048576 / 48000 = 21.8453…** M-cycles.
  Keep that ratio in fixed point or as an integer numerator/denominator
  pair; do not accumulate a float per sample, which drifts.
- The DC-blocking filter is
  `out = in − capacitor; capacitor = in − out × charge`,
  with `charge = pow(0.999958, 4194304.0 / 48000.0)`. Compute it from the
  rate rather than writing the result down.

**Interfaces — produces** (names are yours to refine, but say what you chose
in your report, because Task 3 consumes them): something that takes the
machine's current cycle count and a way to ask the APU for its level, and
appends the samples now due to an interleaved stereo buffer of floats;
plus a way to drain that buffer, and a mute flag.

- [ ] **Step 1: Read `app/FramePacer.h` and `tests/test_frame_pacer.cpp`** for the SDL-free-and-testable pattern this task follows.
- [ ] **Step 2: Write `tests/test_audio_resampler.cpp` and run it — watch it fail.**
  - one second of emulated time (1048576 M-cycles, fed in realistic steps rather than one jump) produces 48000 samples, ±1
  - two seconds produce 96000, ±1 — this is what catches a per-sample float accumulator drifting
  - feeding cycles in irregular steps produces the same total as feeding them evenly
  - a constant input decays toward zero through the filter, and is within one part in a thousand of zero after a second
  - a symmetric square wave keeps its peak-to-peak amplitude through the filter
  - mute produces zeroes while the sample count still advances
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run the tests.** Expected: all pass.
- [ ] **Step 5: Mutation-test.** Apply each, rebuild, confirm a red, revert: accumulate the ratio as a float per sample; drop the filter entirely; use 44100 in the charge constant while emitting at 48000; let mute stop the sample counter as well as the output.
- [ ] **Step 6: Commit.** No push — nothing user-visible yet.

---

### Task 2: The tone test

**Files:**
- Create: `tests/test_audio_tone.cpp`

This task writes no production code. It is the measurement the whole piece
exists for, and it is worth its own task because it is the only test in the
project that can catch an APU that satisfies every sound ROM and still
produces the wrong pitch.

**What to build.** A headless `GameBoy` — no SDL, no window — with a ROM
built the way `tests/test_cartridge.cpp`'s helper builds one. Write the
audio registers directly through the machine's bus to set channel 1 to a
known frequency, trigger it, run a second of emulated time, push every
sample through `AudioResampler`, and measure.

**The arithmetic — use verbatim.** A pulse channel's frequency in hertz is

    131072 / (2048 − frequency)

so frequency 1750 gives 131072 / 298 = **439.8 Hz**, near enough concert A
to be a sensible fixture. Set NR11's duty to 50% (value 0x80) so the
waveform is symmetric and zero crossings are evenly spaced, NR12 to a
non-zero volume with the envelope period 0 so the amplitude holds, and
enable the channel in NR51 on both sides.

**Measuring.** Count zero crossings in one direction across the second and
compare with the expected frequency. Allow a tolerance of about 1% — the
filter shifts the waveform slightly and the last partial period is cut off.
State the tolerance you chose and why in the test's comment.

- [ ] **Step 1: Write the test and run it.** If it fails, **do not adjust the tolerance to make it pass.** Work out whether the APU, the sampler or the test's own arithmetic is wrong, and report which.
- [ ] **Step 2: Add a second frequency** — frequency 1024, which is 131072 / 1024 = 128 Hz — so the test cannot pass by coincidence at one value.
- [ ] **Step 3: Add a silence case:** with every channel's DAC off, a second of samples is all zeroes.
- [ ] **Step 4: Mutation-test.** Apply each, rebuild, confirm a red, revert: halve the pulse channel's period; ignore NR51 routing so both sides always sound; make the resampler emit at 44100 while the test expects 48000.
- [ ] **Step 5: Commit.**

---

### Task 3: The device, the drift policy, and the key

**Files:**
- Create: `app/Audio.h`, `app/Audio.cpp`
- Modify: `app/main.cpp`, `app/CMakeLists.txt`, and the key list in `app/main.cpp`'s header comment

**Consumes:** `AudioResampler` from Task 1 — your dispatch will carry its
exact interface.

**What to build.**
- Open an SDL3 audio stream at 48000 Hz, stereo, float samples. If the device
  will not open, **the emulator still runs** — silently, with the reason
  printed once. A missing sound card is not a reason to refuse to play a
  game, and the window already has to survive being launched with no ROM.
- In the frame loop, after each machine step, push the samples now due.
- **The drift policy.** Keep about two frames of audio queued — a frame is
  48000 / 59.7275 = **803.6** samples. Ask SDL how much is queued; above the
  high-water mark drop one sample that frame, below the low-water mark repeat
  one. Choose the marks, state them in the code with the reasoning, and do
  not correct by more than one sample per frame.
- **M** toggles mute. Update the key list in the comment at the top of
  `app/main.cpp` and anywhere else the keys are written down.
- While paused, the machine does not step, so no samples are produced. Make
  sure that silences cleanly rather than repeating the last buffer.

- [ ] **Step 1: Write the tests that can be written without a device** — the drift policy's thresholds are arithmetic and belong in a test: given a queued count above the high-water mark, one sample is dropped; below the low mark, one is repeated; between them, neither. Run them and watch them fail.
- [ ] **Step 2: Implement `Audio` and wire it into the loop.**
- [ ] **Step 3: Build and run the whole unit suite.** Expected: all pass.
- [ ] **Step 4: Mutation-test.** Apply each, rebuild, confirm a red, revert: correct by more than one sample a frame; invert the two thresholds; keep pushing samples while paused.
- [ ] **Step 5: Play a ROM and listen.** `.\build\release\app\fourshades_app.exe <a ROM>` — confirm there is sound, that M silences it and restores it, that Space still pauses and R still resets, and that the sound does not degrade over a few minutes. **Report what you heard**, including anything that sounded wrong; "it worked" with no detail is not a report.
- [ ] **Step 6: Commit and push.**

---

### Task 4: The piece closed

- [ ] **Step 1: Clean configure and build.**
- [ ] **Step 2: Whole unit suite.** Expected: all pass, no skips.
- [ ] **Step 3: `python tools/check_core_isolation.py`.** Expected: passes — no SDL reached `src/core/`.
- [ ] **Step 4: Both full suites.**

```
.\build\release\tools\sst\sst_runner.exe
.\build\release\tools\roms\rom_runner.exe
```

Expected: **499/500** and **138/165**, unchanged. This piece scores nothing,
so any movement in either direction is a regression to report, and the
scoreboard should not need updating. Run `python tools/scoreboard.py check`
to confirm the checked-in scoreboard still matches.

- [ ] **Step 5: Confirm `git status` is clean** and that `README.md` and `scoreboard.json` are untouched by this piece.
- [ ] **Step 6: Verify CI** for the pushed commit through the GitHub API, and **read the response**. Never write "CI green" without having looked at it.

## Self-review

**Spec coverage.** The SDL-free split (1); cycles into samples and the
filter (1); the tone measurement the piece exists for (2); the device, the
drift policy and the mute key (3); the guarantee that nothing scored moves
(4 and the global constraints).

**Placeholders.** None. The two frequencies, the ratio, the charge constant
and the per-frame sample count are exact; the drift thresholds are
deliberately left to the implementer with a stated bound of one sample per
frame, because the right value depends on the device's buffer and must be
chosen where it can be measured.

**Type consistency.** `AudioResampler` and `Audio` are spelled the same in
the spec, here and in the file table. Task 1's interface is deliberately left
for its implementer to name and is carried forward by the controller rather
than guessed here.
