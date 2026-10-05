# Piece 3b: a window you can play in. Design

**Status:** implemented 2026-09-22. SingleStepTests stayed at 499 / 500 and
the test ROMs at 106 / 165 throughout, as this piece intended. Five
deviations were recorded under "Deviations from the spec" below; two of them
-- the opening window size and the unmeasured, untested frame pacing -- have
since been closed, and the measurement the "Frame loop" section asked for is
under "Frame pacing, measured" below.

## What this is

Everything needed to *show* a Game Boy picture already exists. `Ppu::frame()`
returns a 160x144 array of shade indices and `frameCount()` says when a new one
is ready; the joypad register has been in the core since piece 2, answering
"no buttons pressed" to every read. This piece turns that into a window on a
desk: load a ROM, see it, press buttons.

It is deliberately the smallest piece so far. The hard part - producing a
correct frame - is behind us, and the test-ROM scoreboard already measures it.

## Scope

In:

- SDL3, pinned and hash-verified, vendored under `third_party/` the same way
  doctest and nlohmann/json are.
- A new executable target that links `fourshades_core` and nothing test-related.
- Load a ROM from the command line; run and present frames; map keys to the
  joypad; quit cleanly.
- Joypad input reaching the core, which also closes STOP's wake-up - currently
  a recorded gap in `docs/known-divergences.md`.
- CI builds the new target.

Out, and staying out:

- Sound. That is piece 5.
- Cartridge types beyond what the core already supports. That is piece 4.
- Save states, rewind, debugger overlays, shaders, netplay.
- Any change to the emulator core's behaviour. If the window needs something
  the core does not expose, the core gains an accessor, not a behaviour.

## The pieces

**Vendoring.** SDL3 as a pinned release, its archive hash recorded and checked
the way `tools/sst/manifest.sha256` and `tools/roms/manifest.sha256` already
work for test data. A fetch script downloads and verifies; the binaries are
never committed. Windows only, matching the rest of the project.

**Target layout.** A new `app/` directory with its own CMake target. The core
stays isolation-clean: `tools/check_core_isolation.py` must keep passing, so
nothing in `src/core` learns that a window exists. The app depends on the core;
the core never depends on the app.

**The frame loop.** Run the machine 70,224 dots, then present. Pace to the DMG's
real 59.727 Hz rather than the monitor's 60, so audio in piece 5 does not
inherit a drift. Use vsync where the display cooperates and a sleep otherwise;
the choice is measured, not assumed, and recorded. (Measured: see "Frame
pacing, measured" below. The sleep holds the rate to 0.002%, vsync on this
display would be locked to 60.000 Hz and so 0.456% fast, and the sleep won.)

**Presentation.** One streaming texture, 160x144, nearest-neighbour, drawn at
the largest integer scale that fits the window, letterboxed with a neutral
border. Integer scaling only: a Game Boy pixel is either N screen pixels or the
image is wrong. Default window 4x, i.e. 640x576. Resizable.

**Palette.** Grey by default - white, light grey, dark grey, black - because
that is exactly what the reference images the scoreboard compares against
contain, so what is on screen is what is being judged. A key toggles the
classic green DMG palette. The palette is a presentation choice in the app; the
core keeps emitting shade indices 0-3 and knows nothing about colour.

**Input.** Arrow keys for the pad; Z and X for A and B; Enter and Backspace for
Start and Select. The de-facto layout, so muscle memory from other emulators
works. Keys are read once per frame and handed to the core as a button mask.

**Joypad in the core.** The register exists but always reports "no buttons".
This piece gives it real state and the interrupt that goes with it, and then
STOP can wake, which closes part of a divergence entry that has been open since
piece 1.

## Testing

The window itself cannot be unit-tested meaningfully, and pretending otherwise
would be the kind of test this project has repeatedly had to throw away. What
gets tested is everything underneath it:

- The joypad register: button state in, correct bits out, for every
  select-line combination, plus the interrupt on a high-to-low transition.
- STOP waking on a button press, which is a core behaviour with a hardware
  document behind it.
- The shade-to-colour mapping for both palettes, as a pure function.
- The frame-pacing arithmetic, as a pure function of elapsed time
  (`tests/test_frame_pacer.cpp`, against `app/FramePacer.h`).

CI builds the app target so it cannot rot, but does not run it headless.

## Decisions taken at review

1. **Battery-backed save RAM is in.** A cartridge whose header declares a
   battery gets a `.sav` written beside the ROM, loaded on start and saved on
   exit. It is written atomically - to a temporary file, then renamed - so a
   crash or a pulled plug part-way through cannot leave a half-written save
   where a good one used to be. Cartridges without a battery never write a
   file. This is the first thing this project writes that a person would be
   upset to lose, so the failure mode gets a test: a save interrupted before
   the rename must leave the previous save intact.

2. **No ROM argument: say so and exit.** A clear message naming what was
   expected, not a silent failure and not a file picker. The window also
   accepts a ROM dropped onto it, which is a few lines and covers the case a
   picker would have.

3. **Pause and reset are in.** Space pauses, R resets. Reset rebuilds the
   machine from the cartridge rather than poking the running one, so it cannot
   leave half-old state behind - and that is exactly the bug it would otherwise
   have, so it gets a test.

## Success criteria

- A ROM runs in a window at the DMG's real frame rate, with working buttons.
- `tools/check_core_isolation.py` still passes: nothing in `src/core` knows a
  window exists.
- The test-ROM and SingleStepTests scores are unchanged by this piece. It adds
  a way to look at the emulator, not a change to what the emulator does - with
  the single exception of the joypad register and STOP's wake-up, which are
  core behaviours with hardware documents behind them and get their own tests.
- CI builds the app target.
- A game with a battery keeps its progress across a close and reopen.

## Deviations from the spec

Five things shipped differently from what is written above. The first is the
one worth reading: it is a decision taken at review that did not survive
contact with a user.

- **"No ROM argument: say so and exit" (decision 2) was wrong, and is now the
  opposite.** The reasoning at review was that a message naming what was
  expected beats a silent failure. It does -- but only for someone who can
  see the message. A user double-clicked `fourshades_app.exe`, which gets no
  console to print into, saw a process appear and vanish, and reported "it
  does not run". The app now opens the window with no ROM and invites one to
  be dropped on it. Nothing about the command-line case changed: a bad path
  or an unsupported cartridge given as an argument is still reported on
  stderr and still exits non-zero, because that case has a console and a
  script reading it. Shipped in `8fcd815`.

- **Drag-and-drop arrived one task early.** Decision 2's "the window also
  accepts a ROM dropped onto it" was planned for the last task. It moved into
  the second one, because the waiting window above has nothing else it can
  do: a window that invites a drop and does not accept one is worse than no
  window. Same scope, earlier.

- **Reset keeps battery-backed cartridge RAM; the spec did not say.**
  Decision 3 says reset rebuilds the machine from the cartridge, and it does:
  `AppController::reset` constructs a new `GameBoy` over a pristine copy of
  the cartridge, so no CPU register, no byte of RAM and no MBC bank register
  survives. The one thing carried across is battery-backed cartridge RAM. A
  DMG has no reset button, so the nearest real thing is switching it off and
  on again, and that does not empty the battery -- a reset that wiped the
  save would lose a player's progress every time they used it. Cartridge RAM
  with no battery behind it is not carried: nothing was holding it up.
  Pausing also writes the save file, which the spec did not mention either;
  it only ever adds a chance for the save to survive, since the write is the
  same atomic one used at exit.

- ~~**The window opens at 3x, not the 4x under "Presentation".**~~ **Closed:
  the code now matches the spec.** 3x (480x432) shipped in `05c56a2` without
  a note and was recorded here rather than changed. Leaving the code and the
  spec disagreeing was the wrong call for a project whose credibility is its
  record, and 4x is the better number anyway: 640x576 is a quarter of the
  1920x1080 desktop it opens on, so the picture is large enough to see the
  dither patterns the screen tests are judged on without covering the
  screen. The window is `Ppu::kWidth * 4` by `Ppu::kHeight * 4` again;
  resizing and the integer-scaling rule never changed.

- ~~**Frame pacing is a sleep, never vsync, and is not unit-tested.**~~
  **Closed: measured, decided on the numbers, and tested.** What shipped was
  the sleep alone -- `SDL_DelayNS` to the DMG's frame period, no vsync path,
  no measurement -- with the pacing arithmetic four lines inline in
  `main.cpp`'s loop and no test on it. The measurement the "Frame loop"
  section asked for is now recorded under "Frame pacing, measured" below; the
  arithmetic is `app::paceFrame` and `app::framePeriodNs` in
  `app/FramePacer.h`, with `tests/test_frame_pacer.cpp` on both. The sleep
  stayed, on evidence rather than on the argument the spec made for it.

- Recorded 2026-09-22; the window size and frame pacing entries closed
  2026-09-22.

## Frame pacing, measured

The "Frame loop" section asked for the choice between waiting on vsync and
sleeping to be measured rather than assumed. These are the numbers it was
decided on. The method is repeatable: build the release app, set
`FOURSHADES_PACE_LOG` to anything, run it with a ROM, and it prints on exit
what the loop actually achieved -- frame count, mean rate, standard
deviation and extremes of the frame-to-frame interval, measured with
`SDL_GetTicksNS` at each present. Each run below was 75 seconds (about 4,555
frames) on a GeForce GTX 1660 driving one 1920x1080 display, closed by
posting `WM_CLOSE` to the window.

**What the sleep achieves.** Target 59.727500 Hz (4,194,304 / 70,224):

| run | ROM | mean | error | sd | min | max |
| --- | --- | --- | --- | --- | --- | --- |
| before | dmg-acid2 | 59.7277 Hz | +0.00034% | 0.641 ms | 3.87 ms | 30.13 ms |
| before | blargg 09-op_r,r | 59.7277 Hz | +0.00034% | 0.566 ms | 12.25 ms | 21.73 ms |
| after | dmg-acid2 | 59.7285 Hz | +0.00171% | 0.652 ms | 4.01 ms | 29.95 ms |
| after | blargg 09-op_r,r | 59.7288 Hz | +0.00221% | 0.568 ms | 13.25 ms | 21.19 ms |

The run-to-run difference is under 1.4 ms of total time across 76 seconds --
one scheduling hiccup's worth, not a drift. The mean holds the DMG's rate to
within 2 parts in 100,000 in every run.

**What vsync would achieve.** SDL reports this display as exactly 60.000 Hz
(`refresh_rate_numerator` 60, `refresh_rate_denominator` 1), so vsync is
locked to 60.000 Hz by construction: **+0.456%**, or 7.9 cents sharp. For
piece 5 that is not a subtlety. An emulator generating 44,100 Hz audio one
frame at a time against a 0.456%-fast clock produces 44,301 samples for
every second the sound card consumes 44,100 -- 201 samples of surplus per
second, which overruns a 4,096-sample buffer roughly every twenty seconds,
for as long as the game runs. The sleep's 0.002% produces less than one
surplus sample per second.

**Decision: sleep, and no vsync path.** 0.002% against 0.456% is not a close
call, and it is the number the spec's own sentence predicted -- now measured
rather than asserted. The cost accepted is that presents are not
synchronised to the display, so a tear line is possible; a 160x144 picture
of mostly static tiles makes that hard to see, and paying 0.456% of pitch
error to remove it would be the wrong trade for an emulator. If a variable
refresh display ever makes vsync cheap at the right rate, the pacing lives
behind one pure function and the loop reads `step.sleepNs` from it, so it is
a small change.

**Jitter, recorded rather than fixed.** The spread is about 0.6 ms of
standard deviation, with a handful of frames per minute reaching 30 ms while
the machine itself needs 4-12 ms per frame on this host. The excursions come
from the host, not the pacer: `paceFrame` keeps the deadlines on a fixed
grid, so a late frame is repaid out of the next frame's sleep and the mean
does not move. Narrowing the spread would need the present taken off the
frame loop's thread, which is not this piece's business. Audio in piece 5
will need a buffer of at least two frames to ride over it, which is the
normal arrangement anyway.
