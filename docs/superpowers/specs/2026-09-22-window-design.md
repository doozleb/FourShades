# Piece 3b: a window you can play in. Design

**Status:** implemented 2026-09-22. SingleStepTests stayed at 499 / 500 and
the test ROMs at 106 / 165 throughout, as this piece intended. Five
deviations are recorded under "Deviations from the spec" below.

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
the choice is measured, not assumed, and recorded.

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
- The frame-pacing arithmetic, as a pure function of elapsed time.

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

- **The window opens at 3x, not the 4x under "Presentation".** 480x432
  rather than 640x576. It is resizable and the integer-scaling rule is
  exactly as specified; only the opening size differs. This shipped in
  `05c56a2` without a note, and is recorded here rather than changed, since
  nothing depends on the number.

- **Frame pacing is a sleep, never vsync, and is not unit-tested.** The
  "Frame loop" section asks for vsync where the display cooperates, a sleep
  otherwise, and for the choice to be measured and recorded. What shipped is
  the sleep alone: `SDL_DelayNS` to the DMG's own 59.727 Hz frame period,
  with no vsync path written and no measurement of one taken. The reason for
  the sleep is in the spec's own sentence -- vsync paces to the monitor's
  60 Hz, which is the drift piece 5's audio would inherit -- but the
  comparison the spec asked for was not made, so that clause is unmet rather
  than satisfied. The "Testing" section's "frame-pacing arithmetic, as a pure
  function of elapsed time" is likewise unmet: the arithmetic is four lines
  inline in `main.cpp`'s loop and has no test. Both are open, and cheap to
  close if the pacing is ever revisited.

- Recorded 2026-09-22.
