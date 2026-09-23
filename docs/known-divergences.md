# Known divergences

The project's rule, refined 2026-09-11: Pan Docs outranks a test that only
another emulator generated (SingleStepTests, for instance) — the test is left
failing and recorded here, with the evidence. But a hardware-verified test —
one run and checked against real DMG, MGB, SGB, SGB2, CGB, AGB or AGS
hardware, as Mooneye's test suite marks its own — outranks a Pan Docs
sentence that turns out to be a simplification, and that decision is recorded
here too, with the evidence, instead of silently changing behaviour.

Each entry gives the test, what it expects, what Pan Docs says (with a link),
what FourShades does, and, where the hardware-verified rule applies, the
decision and its date.

## STOP (0x10): Pan Docs says STOP is 2 bytes here; the test advances PC by 1

- **Test:** SingleStepTests `v1/10.json` expects PC + 1, cycles `r-m`, `---`,
  `---`, and no other register change, in all 1,000 tests.
- **Pan Docs:** the STOP flowchart's branch for no button held, no speed switch
  requested and no interrupt pending ends in "STOP is a 2-byte opcode, STOP mode
  is entered, DIV is reset", and the text above it says "STOP is normally a
  2-byte instruction where the second byte is ignored",
  [Reducing Power Consumption: Using the STOP Instruction](https://gbdev.io/pandocs/Reducing_Power_Consumption.html#using-the-stop-instruction).
  A DMG takes the "no speed switch" branch: KEY1 is "(CGB Mode only)",
  [CGB Registers](https://gbdev.io/pandocs/CGB_Registers.html).
- **FourShades:** follows Pan Docs, so `10` fails all 1,000 tests on `pc`
  (PC + 2 where the test expects PC + 1).
- **Not settled by Pan Docs, so left as the test has it:** whether the second
  byte is fetched with a bus read. Pan Docs says only that it is "ignored", so
  FourShades skips it without a bus cycle and keeps the test's `r-m`, `---`,
  `---` pattern. Pan Docs also doesn't say at which cycle the PC increment
  becomes visible while the CPU sits in STOP mode.
- **The wake-up: done 2026-09-22.** P1 has real state from this date, so the
  branch that was impossible is now implemented. Pan Docs' way out is "STOP
  is terminated by one of the P10 to P13 lines going low"
  ([Reducing Power Consumption: Using the STOP Instruction](https://gbdev.io/pandocs/Reducing_Power_Consumption.html#using-the-stop-instruction)),
  so the CPU leaves STOP mode when any line the program selected reads low.
  Two things about that:
  - It is not an interrupt. IME and IE have no part in it, and the CPU
    resumes at the instruction after STOP rather than at a vector. Pan Docs
    describes the exit as the line, not the interrupt, and the same page tells
    a program to write $00, $10 or $20 to P1 before STOP "depending on which
    buttons you want to terminate the STOP on" — so with $30 written, no line
    can fall and nothing ends STOP mode.
  - **A decision, not a documented behaviour:** FourShades watches the line's
    *level*, not its falling edge, so a STOP executed with a selected line
    already low ends immediately. Pan Docs gives no behaviour for that case
    inside STOP mode — its flowchart branches away from STOP mode before
    entering it when a button is held (see the button-held branch below,
    which is still not implemented) — so there is no documented answer to
    match. Waking immediately was chosen over waiting forever for an edge
    that had already happened. No scored test reaches it: SingleStepTests
    runs one instruction per case, so it never steps a stopped CPU, and the
    165 test ROMs were run before and after the change on 2026-09-22 with
    every verdict, failure reason and serial byte identical.
- **Still missing** (the joypad now exists; these no longer wait on it, they
  are simply not written):
  - STOP doesn't reset DIV. That is to be done with the planned
    centralisation of the system counter's edge handling (so a reset's
    falling edges reach the timer, and later the sound chip, from one place),
    before piece 5.
  - The interrupt-pending branch, where Pan Docs makes STOP a 1-byte opcode,
    isn't implemented: STOP is always 2 bytes.
  - The button-held branch, where Pan Docs' flowchart never enters STOP mode
    at all, isn't implemented either: STOP always enters STOP mode, and then
    leaves it again on the first step, as the level rule above says.
    (Rewritten 2026-09-22, when the wake-up landed.)
- **Scored test affected:** `daid/stop_instr.gb (DMG)` is one of the 165
  scored test ROMs (screen group). It is a screenshot test; with the PPU
  built (piece 3) it still fails, its image differing from the reference in
  22,739 of 23,040 pixels as of 2026-09-22 — a whole-screen difference, so
  what it says about STOP itself is still not being read. The wake-up landing
  on 2026-09-22 did not move that number by a single pixel, which is all that
  is known about why it fails: it is not the wake-up alone. (Updated
  2026-09-22; it previously said the PPU was missing, and before that that
  the wake-up was what it needed.)
- **Also noted by Pan Docs itself:** "stop is often considered a two-byte
  instruction, though the second byte is not always ignored.",
  [CPU Instruction Set](https://gbdev.io/pandocs/CPU_Instruction_Set.html#stop).
  "Not always" refers to the button-held and interrupt-pending branches, where
  STOP behaves differently — the branches listed as still missing above.
- **Checked:** 2026-09-11; the wake-up re-checked against Pan Docs and
  implemented 2026-09-22.

## HALT (0x76): matches Pan Docs

HALT (76) was checked against Pan Docs on 2026-09-11 for the no-pending-interrupt
case the tests model and matches: one byte, PC + 1, and the CPU enters HALT
mode.

- **Pan Docs, [HALT](https://gbdev.io/pandocs/halt.html):** "If no interrupt is
  pending, halt executes as normal, and the CPU resumes regular execution as
  soon as an interrupt becomes pending."
- **Pan Docs, [CPU Instruction
  Set](https://gbdev.io/pandocs/CPU_Instruction_Set.html):** lists `halt` as
  the single-byte encoding `01110110`, with no operand field — unlike `ld r8,
  r8`, which the page notes `halt` is the one exception to (encoding `[hl],
  [hl]` yields `halt` instead).

## STAT at power-on (0xFF41): resolved (2026-09-22)

- **Test:** `tests/test_gameboy.cpp`'s power-on test checks `FF41 == 0x85`,
  and `tests/test_ppu.cpp`'s "the PPU starts where the boot ROM left it"
  checks the PPU's own power-on state.
- **Pan Docs:** [Power Up Sequence](https://gbdev.io/pandocs/Power_Up_Sequence.html)
  lists STAT = $85 and LY = $00 for DMG at PC = $0100 — mode 1 (VBlank) while
  LY already reads 0, i.e. the end of line 153.
- **What FourShades did:** the LCD timing placeholder that stood in for the
  PPU until piece 3 (`src/core/LcdTiming`, deleted when the PPU replaced it)
  started at LY 0 in mode 2, so STAT read $86.
- **Resolution (2026-09-22):** the PPU powers on at line 153, dot 4, in mode 1.
  LY therefore already reads 0 (the LY=153 quirk), LYC is 0 so STAT's bit 2 is
  set, and `read(0xFF41)` returns `0x80 | 0x04 | 1` = $85 with no special
  case. The whole machine starts there, so every test that assumed power-on
  was line 0 in mode 2 was given an explicit starting point instead.
- **Test ROMs:** no ROM gained or lost (106 / 165 before and after). One
  screenshot test's picture did change, which the original version of this
  entry did not say: `ashiepaws/bully` went from 346 differing pixels to 290,
  taking the `screen` group's total from 73,628 to 73,572. Measured
  2026-09-22 by building the commit before this change and running the ROM
  runner from it: `bully` 346, `strikethrough` 53. It is the only count in
  the screenshot table below that differs between that run and one made
  today, and it is a failing test either way, so no verdict moved with it. Mooneye's `boot_hwio-dmgABCmgb` still fails, and a traced run on
  2026-09-22 puts its first mismatch at $FF10 (NR10), the first sound
  register: the ROM wants $80 and FourShades reads $FF, because there is no
  APU until piece 5. The registers it checks before that, $FF00-$FF0F, all
  match. The ROM stops at its first mismatch, so nothing is claimed here
  about the registers after $FF10 — STAT ($FF41) among them is never reached.
- **Checked:** 2026-09-22.

## OAM DMA bus conflicts: resolved in favour of the hardware-verified tests (2026-09-11)

- **Tests:** Mooneye `add_sp_e_timing`, `call_cc_timing`, `call_timing`,
  `jp_cc_timing`, `jp_timing`, `ld_hl_sp_e_timing`, `reti_timing`,
  `ret_cc_timing`, `ret_timing` (all "verified: DMG, MGB, SGB, SGB2, CGB,
  AGB, AGS"). Each starts an OAM DMA from $8000 (VRAM) and, while it runs,
  executes an instruction from ROM, or from echo RAM at $FDFE/$FDFF, whose
  operand or stack bytes lie in OAM. They expect the ROM and WRAM accesses to
  work and only the OAM ones to read $FF, which places each memory access of
  the instruction to the M-cycle.
- **Pan Docs:** "On DMG, during OAM DMA, the CPU can access only HRAM (memory
  at $FF80-$FFFE)", and, by contrast, "On CGB, the cartridge and WRAM are on
  separate buses",
  [OAM DMA Transfer: OAM DMA bus conflicts](https://gbdev.io/pandocs/OAM_DMA_Transfer.html#oam-dma-bus-conflicts).
- **What FourShades did before this decision:** followed Pan Docs and blocked
  everything below $FF00 while bytes are copied (I/O stayed reachable, so DMA
  could be restarted from HRAM). The tests' opcode fetches from ROM or WRAM
  read $FF (`rst $38`), the program ran away, and each test timed out.
- **Evidence that per-bus blocking is the only cause:** a throwaway build
  that blocked only OAM and the bus the DMA reads from (VRAM for a $80-$9F
  source, the external bus otherwise) passed all nine and lost no other test
  (76 → 85 of 167).
- **Decision (2026-09-11):** the project's rule is now that a hardware-verified
  test outranks a Pan Docs sentence that turns out to be a simplification.
  The DMG sentence above reads as advice to programmers ("only HRAM is safe
  to use"), not as a specification of what the bus actually does, and the
  nine tests above show DMG's video bus (VRAM) is already separate from its
  external bus (ROM, cartridge RAM, WRAM) — the same separation Pan Docs
  documents outright for CGB. `GameBoy::dmaBlocks` now blocks only OAM
  (always) and whichever bus the DMA is currently reading from (the video
  bus for a VRAM source, the external bus otherwise); I/O, HRAM and IE stay
  reachable throughout. Test-ROM score: 76 → 85 of 167 (the nine tests
  above gained, nothing lost).
- **Checked:** 2026-09-11.

## Object priority when sprites overlap: approximates Pan Docs' smaller-X-then-OAM-index rule (2026-09-14)

- **Pan Docs, [OAM: Drawing priority](https://gbdev.io/pandocs/OAM.html#drawing-priority)**
  (source: [`src/OAM.md`](https://github.com/gbdev/pandocs/blob/master/src/OAM.md)):
  "In Non-CGB mode, the smaller the X coordinate, the higher the priority.
  When X coordinates are identical, the object located first in OAM has
  higher priority."
- **FourShades:** `PixelPipeline::startObject` (`src/core/PixelPipeline.cpp`)
  merges each object's pixels into the pending row on a first-claim basis —
  whichever object reaches a pixel first wins it, full stop — while objects
  are started left to right as `pixelX_` reaches each one's screen X. This
  usually agrees with the documented rule: objects are fetched in increasing
  screen-X order, so the first one to claim a pixel is usually the one with
  the smallest X, and two objects sharing the same X both trigger on the
  same dot and get scanned in OAM order, which happens to match the
  documented tie-break exactly.
- **Where it differs:** every object whose OAM X places it at or left of the
  screen edge (X = 1-8, i.e. screen X <= 0) triggers at the same time,
  `pixelX_ == 0`, regardless of the objects' true relative X values — the
  trigger check only asks whether `screenX < 0 && pixelX_ == 0`, not which
  of several such objects has the smaller X. The winner among them is
  whichever the OAM-order scan reaches first. So two such objects
  overlapping a pixel, with X = 2 and X = 6 respectively, are not a tie
  under Pan Docs (X = 2 must win outright), but FourShades lets OAM order
  decide and will let the X = 6 object win if it has the lower OAM index.
- **Decision:** shipped deliberately with the object renderer (2026-09-14) as an
  approximation, to get objects rendering without also building the exact
  priority sort. The code comment at the merge site in
  `PixelPipeline::startObject` names the documented rule, says plainly that
  this is an approximation of it, and says what would arbitrate it.
- **Scored test affected:** `mooneye/manual-only/sprite_priority.gb` is the
  test that arbitrates this. Since the screenshot comparator was built
  (2026-09-14) it is scored, and it passes. That is evidence the
  approximation is harmless for what that test draws — not proof it is
  correct: whether it puts two objects at or left of the screen edge over the
  same pixel, which is the case where FourShades and Pan Docs part company,
  has not been checked.
  The Mealybug object tests do **not** arbitrate this. The ones in the suite
  are `m3_lcdc_obj_en_change`, `m3_lcdc_obj_en_change_variant`,
  `m3_lcdc_obj_size_change` and `m3_lcdc_obj_size_change_scx`, which probe
  mid-line changes to the object enable and size bits, not overlap priority.
  An earlier version of this entry claimed they did; that was wrong.
- **Checked:** 2026-09-14.

## OBJ penalty: the first object fetched on a line gets a three-dot rebate against Pan Docs' algorithm (2026-09-21)

- **Test:** Mooneye `acceptance/ppu/intr_2_mode0_timing_sprites` ("verified:
  DMG, MGB, SGB, SGB2, CGB, AGB, AGS"). Its 104 cases each place 1-10 objects
  on one scanline at chosen OAM X coordinates, then tune a `nops` count so
  that the first STAT poll after the mode 0 interrupt is exactly the M-cycle
  mode 0 begins; the case's "extra cycles" figure is how many M-cycles later
  than a bare line that is.
- **Pan Docs, [Rendering: OBJ Penalty
  Algorithm](https://gbdev.io/pandocs/Rendering.html):** for each object,
  "Determine the tile (background or window) that The Pixel is within. If that
  tile has not been considered by a previous OBJ yet: Count how many of that
  tile's pixels are strictly to the right of The Pixel. Subtract 2. Incur this
  many dots of penalty, or zero if negative." and "Incur a flat, 6-dot penalty
  (from fetching the OBJ's tile)."
- **What the test measures:** that sum, minus three dots, once per scanline.
  A single object whose tile term is zero (OAM X mod 8 of 5, 6 or 7) costs
  Pan Docs' flat 6, but the test only allows it one extra M-cycle, so the
  line can have grown by at most 4 dots. Ten objects in one tile at OAM X = 0
  cost Pan Docs' 11 + 9 x 6 = 65 but are allowed 16 extra M-cycles, i.e. 61
  to 64 dots. Ten objects in ten tiles at OAM X = 0, 8, ... 72 cost 110 and
  are allowed 27, i.e. 105 to 108 dots. Solving all 104 cases at once leaves
  exactly one constant: Pan Docs' sum minus 3, charged against the first
  object fetched on the line. No other single constant fits, and no
  per-object or per-tile adjustment fits either (a per-tile -3 would put the
  ten-tile case at 20 extra M-cycles instead of 27).
  - **The stronger, more checkable version of that argument:** the OAM X = 0
    series alone admits a rebate of either 3 or 4 - it does not pin the
    constant by itself. What pins it to 3 is a single-object pair elsewhere
    in the 104 cases: an object at X = 3 must report 2 extra M-cycles, and one
    at X = 4 must report 1. A rebate of 4 puts the X = 3 case at 176 raw
    dots, which the ROM forbids; a rebate of 2 puts an OAM X = 0 object at
    181 raw dots (184 once rounded up to the next whole M-cycle), which the
    ROM also forbids. Only 3 survives both.
  - **All 104 cases run at SCX = 0.** The rebate's interaction with SCX is
    therefore entirely unmeasured. `tests/test_objects.cpp`'s "an object at
    OAM X = 0 always costs eleven dots, unlike the general formula" checks
    183 raw dots (reported as 184) for such an object with SCX = 3 - that figure comes from
    applying the model above, not from the hardware ROM (which never reaches
    a non-zero SCX for this case), and should be read as a prediction the
    unit test pins, not a measurement.
- **Decision (2026-09-21):** the hardware-verified test outranks the Pan Docs
  formula, per the rule at the top of this file. `PixelPipeline::startObject`
  computes Pan Docs' two terms and then takes three dots off the first object
  of each scanline. Test-ROM score: 100 -> 101 of 165
  (`intr_2_mode0_timing_sprites` gained; nothing lost).
- **What is not settled:** why the three dots. The likeliest reading is that
  Pan Docs' "flat 6" describes the fetch in isolation, while on the real
  pipeline the first object fetch of a line overlaps three dots the background
  fetcher would have spent stalled anyway - but nothing in the test
  distinguishes that from the constant belonging somewhere else, so the code
  says only what was measured.
- **Known limitation: the tile term ignores the window.** The tile-index half
  of the penalty (`PixelPipeline::startObject`'s `penaltyTile`) is computed in
  background coordinates - SCX plus the object's own X - unconditionally.
  Once the window is drawing, tile boundaries actually follow WX - 7 instead,
  so on a line with both a window and an object this term can be wrong by up
  to 5 dots. No test ROM in the 165 currently combines a window and an object
  on the same line in a way that exposes this, so it is left for the Mealybug
  window tests to arbitrate.
- **Known limitation: the memo remembers one tile, not every tile.** Pan
  Docs' condition is "If that tile has not been considered by a previous OBJ
  yet", which is a set of every tile considered so far on the line.
  `PixelPipeline` keeps one slot (`lastPenaltyTile_`), so an object whose
  tile matches the one immediately before it pays no tile term, and an
  object whose tile was considered earlier than that pays it again. The two
  readings agree whenever objects arrive in non-decreasing tile order, which
  is what a left-to-right walk of the line gives. They part company off the
  left edge: every object at OAM X 1-8 triggers at pixel 0 in OAM order
  regardless of its own X, so the tiles can arrive out of order - at SCX = 0,
  OAM X of 1, 8 and 2 gives tiles -1, 0, -1, and the third object is charged
  for tile -1 a second time, up to 5 dots that Pan Docs' wording does not
  charge. What is known is only that the one-slot version passes what the
  suite has: `ppu timing` 12 / 12, the hardware-verified object timing ROM
  included. Whether any of its 104 cases, or any other ROM in the 165,
  reaches the out-of-order arrangement has not been checked, and the set
  version has not been built and measured, so nothing here says which
  reading is right - only that one slot is what the cases that were solved
  needed. Recorded 2026-09-22.
- **Two details the same test settles, which Pan Docs states loosely:**
  - "The Pixel" is the object's leftmost pixel at screen X = OAM X - 8, and
    that is used even when it is off the left edge. Objects at OAM X = 0-7 are
    charged by their own X mod 8 (X = 5 pays no tile term at all), and an
    object at OAM X = 0 and one at OAM X = 8 pay two separate tile terms even
    though both are fetched on the dot the pixel counter is still 0. Taking
    the term at the clamped pixel counter instead, as FourShades did before,
    charged every one of them as if it sat at X mod 8 = 0.
  - Pan Docs' exception, "an OBJ with an OAM X position of 0 always incurs a
    11-dot penalty, regardless of SCX", replaces the *tile term*, not the
    whole penalty: ten objects at OAM X = 0 cost 11 + 9 x 6, not 10 x 11,
    because the second onwards finds the tile already considered. At SCX = 0
    the general rule gives 11 for such an object anyway, and no ROM in the 165
    reaches one at a non-zero SCX, so the exception is kept as Pan Docs states
    it.
- **Checked:** 2026-09-21.

## Rendering runs seven dots behind the mode-3 window (2026-09-21)

Not a divergence: a timing model FourShades now implements, recorded here
because two bodies of hardware-verified evidence pin its two ends and neither
alone explains it.

- **Pan Docs:** silent on any gap between mode 3 and the pixels reaching the
  LCD. [Rendering](https://gbdev.io/pandocs/Rendering.html) explains mode 3's
  172-dot minimum by the fetcher's two warm-up fetches at the top of the
  mode, and the placement this entry replaces - the fetcher starting on the
  first dot of mode 3 - was derived from exactly that reading. The Mealybug
  references are photographed from real DMG hardware and put pixel 0 seven
  dots later than that placement does. Under the rule at the top of this
  file a hardware-verified image outranks a Pan Docs sentence that turns out
  to be a simplification, so the drawing moved and the mode boundaries, which
  the hardware-verified timing ROMs measure, did not.
- **What the Mealybug Tearoom images measure.** Every `m3_*` test runs its
  handler from the mode-2 STAT interrupt in a field of NOPs and writes a PPU
  register a known number of cycles later, so each reference image names the
  pixel a write at a given dot of the line first reaches. `m3_bgp_change`
  pins it to the dot: its handler writes BGP seven times a line, and the six
  that land once drawing has started are on line dots 100, 112, 172, 184, 244
  and 256 (measured inside FourShades, on line 100), while the DMG reference
  shows their seams at pixels 1, 13, 73, 85, 145 and 157. So the
  pixel drawn on line dot D is pixel D - 100: **pixel 0 leaves the PPU on
  line dot 100**, twenty dots after mode 3 begins on dot 80.
- **What the LCD timing ROMs measure.** `intr_2_mode3_timing` and
  `intr_2_mode0_timing` ("verified: DMG, MGB, SGB, SGB2, CGB, AGB, AGS") pin
  STAT's own view on the bare lines they set up: mode 3 is reported there for
  172 dots, from line dot 81 to line dot 252 inclusive, with the PPU's
  internal transitions an M-cycle earlier. 172 is the minimum, not a
  constant: SCX's low bits, the window's restart and every object fetched on
  the line lengthen mode 3, as the OBJ-penalty entry above documents and
  `intr_2_mode0_timing_sprites` measures directly. Those two ends must not
  move, and don't.
- **FourShades before this task** started the fetcher on the first dot of
  mode 3, which put pixel 0 on line dot 93 - seven dots early against the
  images, so every mid-line write landed seven pixels to the right of where
  hardware puts it. Correcting it moved the `screen` group's differing-pixel
  total from 118,088 to 91,894 - a fifth of the group's error, and the
  largest single step found in that task - but on its own it made no test
  pass and lost none. `m3_bgp_change` went from 5084 differing pixels to 517
  and needed the palette seam and the line-0 entry below before it matched
  exactly.
- **FourShades now** runs the pipeline `PixelPipeline::kRenderLag` = 7 dots
  behind the mode-3 window at both ends. The fetcher starts on line dot 87
  and pixel 0 is emitted on dot 100; mode 3 still ends where it did, seven
  dots before the last pixel reaches the LCD, so the final seven pixels of
  every line are drawn during the first dots of HBlank. `Ppu::stepDot` keeps
  the pipeline running after `mode_` has gone to 0 for exactly that reason.
- **How the mode-0 boundary is found, seven dots early.**
  `PixelPipeline::dotsRemaining` counts what the line still owes: one dot per
  pixel not yet emitted, plus the stall the fetch in progress still owes,
  plus the penalties of the object fetches still to come over those pixels.
  A pixel count alone would not do, because an object fetched over the last
  few pixels stalls them and `intr_2_mode0_timing_sprites` measures objects
  at OAM X 160-167 doing exactly that. The fetcher itself is not counted for
  ordinary background pixels, because a fetch takes six dots and feeds eight
  pixels, so it is never the binding constraint over the handful of dots at
  the end of a line - except for a window activation still to come: the
  window's trigger point (WX - 7) can land on the line's very last pixel
  (WX up to 166), and `Ppu::stepDot` clears the queue and restarts the
  fetcher from its first step the moment the trigger fires, so that pixel
  waits a full `PixelPipeline::kWindowRestartDots` (six dots) rather than
  one. `dotsRemaining` charges those six dots twice over, in two different
  states: while the activation is still to come (the window enabled, WY
  already reached, no activation yet this line, and the trigger point
  anywhere on the line), and while one is actually running, through
  `fetchStallDots` - if the queue is empty, no pixel can be emitted until
  the fetcher pushes again, and those dots are on top of the one-per-pixel
  count. Mode 0 is entered when the count drops to the line's lag.
  **The second of those was missing until 2026-09-22**, and it is a genuine
  defect that was in the shipped code, not a tidying-up. On the dot the
  window actually triggered, `window_` turned true and the six-dot stall
  then in progress was counted by nothing at all: the count read five dots
  short, so mode 3 could end up to five dots early. It bites when the trigger
  lands at pixel 153 or later, which is where a line has only the render lag
  left to run: five dots early at WX 160 on a bare line, one fewer per WX
  after that, and nothing from WX 165, where the short count and the true one
  cross the lag on the same dot. What was *measured*, on 2026-09-22, by
  running WX 156 to 167 through both versions of the arithmetic: STAT's own
  mode-3 length, which only changes on whole M-cycles, came out one M-cycle
  short for WX 160, 161, 162 and 163 and identical for every other WX in
  that range - WX 164's single dot rounds away. An object fetched late on the
  line adds to both counts, so which WX values differ moves with it.
  Nothing scored moved when it was fixed - SingleStepTests 499 / 500, test
  ROMs 106 / 165, the `screen` group's total unchanged at 73,572 - which is
  all the suite has to say about it: no verdict and no pixel count in it
  depends on a window trigger in that band. `tests/test_pixel_pipeline.cpp`'s "a window
  trigger inside the line's last dots still lengthens mode 3 by six dots"
  pins it at WX 160, and was watched failing first - mode 3 came out four
  dots short of the six-dot cost, the five rounded to the M-cycle.
  The same arithmetic had a second hole: the pending charge was made only
  when the trigger point was at or ahead of the pixel counter, so setting
  LCDC bit 5 mid-line with WX - 7 already behind the counter - which restarts
  the pipeline on the very next dot - was never charged for. It is charged
  now. Unlike the first hole, no sequence has been found where that one
  changes the boundary the PPU picks, because such a restart can only happen
  on a dot at or after the one the boundary was already decided on; it is
  fixed because the predicate was wrong, not because a wrong answer was
  observed. This replaced an
  earlier version that ran a *copy* of the pipeline seven dots forward: the
  two were run side by side over the whole 165-ROM suite and the unit tests,
  disagreed on no dot of any line, and the count is the cheaper and the
  narrower of the two - it reads live state at the dot it is asked rather
  than projecting a frozen register snapshot seven dots ahead, and it needs
  no mutable access to the PPU. It is still a prediction: a register the CPU
  changes *after* the boundary has been decided is not something any
  predictor here can see, and no ROM in the suite currently pairs such a
  change with a tail object.
- **The backstop.** If the count were ever wrong in the direction that never
  fires, `mode_` would sit at 3 with the pipeline already stopped, and STAT
  would report mode 3 with VRAM locked until the next line's mode 2.
  `Ppu::stepDot` therefore enters mode 0 unconditionally on the dot the line
  finishes, whatever the prediction said. This is dead code today, not just
  untested: `dotsRemaining` is exactly 0 at the dot the 160th pixel is
  emitted - no pixels left, no pending activation left to count (one that
  had not fired by then would have fired on that dot), and the fetcher's own
  stall charged only while pixels remain - so the check above it always sets
  mode 0 on that dot or an earlier one first. It stays
  in as a guard against a future regression in the formula, not because
  anything currently reaches it.
- **What this is physically.** The natural reading is that mode 3 ends when
  the PPU has finished reading VRAM for the line while pixels are still
  shifting out of the FIFO, which is also why the fetcher can start a little
  after VRAM locks. FourShades does not claim more than the two measurements
  above: it puts the seven dots where the images put them and leaves the mode
  boundaries where the timing ROMs put them.
- **What was ruled out.** Moving the mode-2 STAT interrupt eight dots earlier
  produces the same images but breaks six of the twelve LCD timing ROMs
  (every `intr_2_*`), and delaying mode 3 itself by seven dots breaks eight
  of them. Both were tried and reverted; the lag is the only placement
  measured that satisfies both sets.
- **Checked:** 2026-09-22.

## Palette writes short the old and new values together for one dot (2026-09-21)

- **Test:** Mealybug Tearoom `m3_bgp_change` (DMG reference image,
  photographed from hardware). On line 100 the handler drives BGP
  0x46 -> 0x47 -> 0x46 -> 0x48 -> 0x46 -> 0x45 -> 0x46 during mode 3, and the
  reference shows a one-pixel seam at each of the six writes. At the
  0x46 -> 0x45 write the seam pixel is shade 3, which is colour 0 under
  neither palette (0x46 gives 2, 0x45 gives 1) but is colour 0 under
  0x46 | 0x45 = 0x47. The same seam appears at every write on every line of
  the image, so it is the write that causes it, not those two values.
- **Pan Docs:** silent. [Palettes](https://gbdev.io/pandocs/Palettes.html)
  describes BGP, OBP0 and OBP1 as plain registers and says nothing about
  writing one during mode 3.
- **FourShades:** `Ppu::write` records `old | new` alongside the new value and
  `Ppu::bgp()` / `Ppu::obp()` hand that to the pipeline for exactly one dot -
  the first dot after the writing M-cycle - after which the register's own
  value is used. $FF47-$FF49 always read back the value written.
- **Effect:** with this and the seven-dot lag above, `m3_bgp_change` matches
  its reference in all 23,040 pixels.
- **The object half is unconfirmed.** Every measurement above is BGP's.
  Extending the same one-dot short to OBP0 and OBP1 assumes the three palette
  registers behave alike; nothing here measures that. `m3_obp0_change`, the
  test that would show it, still fails (432 differing pixels) for a reason
  that has not been separated from this one, so the object half is neither
  confirmed nor refuted.
- **Checked:** 2026-09-21, extended 2026-09-22.

## Line 0 starts drawing four dots early (2026-09-21)

- **Test:** every Mealybug Tearoom `m3_*` test. Each runs its handler from the
  mode-2 STAT interrupt and opens with `inc/utils.asm`'s `line_0_fix` macro,
  whose comment reads "line 0 timing is different by 4 cycles, so jump only
  when on line 0": on every line except line 0 the handler takes a `jr` and so
  spends four extra T-cycles before its first write. In `m3_bgp_change`'s DMG
  reference - the one image the four dots were measured against - line 0 then
  comes out identical to line 1, so on hardware something at the top of a
  frame gives line 0 those four dots back. Whether that holds across all of
  the roughly two dozen Mealybug reference images was not checked; the figure
  comes from this one.
- **Pan Docs:** silent. [Rendering](https://gbdev.io/pandocs/Rendering.html)
  gives one mode-2 length (80 dots) for every drawn line.
- **What it is not.** It is not the interrupt: `intr_1_2_timing-GS`
  ("verified: DMG, MGB, SGB, SGB2") times the gap from the mode-1 STAT
  interrupt at line 144 to the next mode-2 STAT interrupt, which is line 0's,
  and pins it. Raising line 0's mode-2 source four dots late was tried and
  fails that ROM and `stat_irq_blocking`.
- **FourShades:** line 0 starts its pipeline four dots earlier than other
  lines (`Ppu::stepDot`, the `renderLag_` assignment), leaving its mode
  boundaries alone - mode 3 still begins 80 dots in and still lasts at least
  172. No ROM in the 165 fails either way with the boundaries left where they
  are, and no experiment was run to find one that arbitrates them on line 0,
  so the placement is unarbitrated rather than established: the images
  measure the drawing, so the drawing is what moved, and the boundaries were
  left alone rather than moved on a claim nothing here tests. The line the
  LCD was switched on keeps the ordinary lag: `lcdon_timing-GS` measures that
  line directly.
- **Effect:** line 0 was the only line of `m3_bgp_change` still wrong once the
  seven-dot lag and the palette seam were in; with this it is exact.
- **Checked:** 2026-09-21.

## A WX below 7 pushes the window's leftmost pixels off the screen (2026-09-21)

- **Tests:** Mealybug Tearoom `m3_wx_4_change` and `m3_wx_5_change` set WX to
  4 and 5 before mode 3 and photograph the line. Both references show the same
  picture WX = 7 would give, moved three and two pixels left, with that many
  more window pixels visible at the right-hand edge.
- **Pan Docs, [LCD Position and Scrolling](https://gbdev.io/pandocs/Scrolling.html):**
  WX "is the window's leftmost pixel's X position, plus 7", and WX values 0
  and 166 are called unreliable. It does not say what WX = 1-6 draws.
- **FourShades:** `PixelPipeline::stepDot` sets `windowSkip_` to 7 - WX when
  the window starts, and the fetcher's push drops that many pixels off the
  front of the tile it has just fetched.
- **What the clipped pixels cost in dots is not evidenced: the placement was
  chosen by group total.** The clipping itself is pinned by the two
  references above and stays. Its dot cost is a different question, and the
  test that would arbitrate it is `m3_window_timing`, which sets WX to LY on
  lines 0-9 - and FourShades gets lines 0-8 of that test, precisely the lines
  where WX is below 7, wrong. Both placements were built and measured:
  dropping the clipped pixels at the output, which costs a dot each, leaves
  `m3_window_timing` differing in 24 pixels and `m3_window_timing_wx_0` in
  692; dropping them at the push, which costs none, leaves 28 and 584.
  Neither reproduces the constant the reference shows (see the
  `m3_window_timing` note further down). The free version shipped because
  the group total is lower with it, which is a tuning decision, not a
  measurement, and is recorded here as one.
- **Effect:** `m3_wx_4_change` 10138 differing pixels -> 229,
  `m3_wx_5_change` 9521 -> 638. `m3_wx_6_change` is not a shift at all (see
  below) and went 13281 -> 13799.
- **Checked:** 2026-09-21.

## Screenshot tests still failing once the pixel pipeline was finished (2026-09-21)

Twenty-six of the thirty tests in the `screen` group still fail. Each is
listed with the number of the 23,040 pixels that differ, what it measures and
why it is not fixed. Four pass: `acid/dmg-acid2`,
`mooneye/manual-only/sprite_priority`,
`mealybug-tearoom-tests/ppu/m2_win_en_toggle` and, new in this task,
`mealybug-tearoom-tests/ppu/m3_bgp_change`.

**The window re-activates mid-line, and FourShades never does** - the single
largest unmodelled behaviour left, and the cause of five of the entries below.
Mealybug's own
[PPU documentation](https://github.com/mattcurrie/mealybug-tearoom-tests/blob/master/the-comprehensive-game-boy-ppu-documentation.md)
states it for LCDC bit 5: disabling the window during mode 3 takes effect at
the end of the window tile being drawn, the background then resumes on a tile
boundary with SCX's low bits ignored, and re-enabling it has no effect unless
WX has been moved to a pixel not yet drawn - in which case the window starts
again *on the next window row*, on the same scanline. `m3_wx_4_change`'s own
comment shows the same thing happens for a WX write alone, with a "window
reactivation zero pixel" appearing when the re-activation dot coincides with
the window's tile-map read. FourShades starts the window at most once per
line. This is the same behaviour the older "window line counter" note below
records for `m3_lcdc_win_en_change_multiple`.

| test | pixels | why it still fails |
| --- | --- | --- |
| `m3_wx_4_change_sprites` | 10 | window re-activation: one zero pixel per affected line |
| `m3_window_timing` | 28 | the WX < 7 window start costs the wrong number of dots |
| `ashiepaws/strikethrough` | 53 | not diagnosed |
| `m3_scx_high_5_bits` | 80 | one background tile per affected line takes the wrong SCX |
| `m3_lcdc_obj_en_change` | 146 | mid-line LCDC bit 1 changes |
| `m3_wx_4_change` | 229 | window re-activation |
| `m3_lcdc_obj_size_change_scx` | 270 | mid-line LCDC bit 2 changes |
| `ashiepaws/bully` | 290 | not diagnosed |
| `m3_lcdc_obj_size_change` | 410 | mid-line LCDC bit 2 changes; 60 worse under the seven-dot shift, cause unknown (see below) |
| `m3_obp0_change` | 432 | object pixels in the leftmost 18 columns |
| `m3_scx_low_3_bits` | 540 | mid-line SCX changes inside the fetch |
| `m3_lcdc_obj_en_change_variant` | 578 | mid-line LCDC bit 1 changes |
| `m3_window_timing_wx_0` | 584 | the WX < 7 window start costs the wrong number of dots |
| `m3_wx_5_change` | 638 | window re-activation |
| `m3_lcdc_bg_map_change` | 714 | mid-line LCDC bit 3 changes |
| `m3_lcdc_bg_en_change` | 855 | mid-line LCDC bit 0 changes |
| `m3_lcdc_tile_sel_change` | 1070 | mid-line LCDC bit 4 changes |
| `m3_scy_change` | 1256 | mid-line SCY changes inside the fetch |
| `m3_lcdc_win_map_change` | 2044 | mid-line LCDC bit 6 changes |
| `m3_lcdc_tile_sel_win_change` | 2286 | mid-line LCDC bit 4 changes, with a window |
| `m3_bgp_change_sprites` | 3076 | as `m3_bgp_change`, plus objects |
| `m3_lcdc_win_en_change_multiple_wx` | 5942 | window re-activation (LCDC bit 5) |
| `daid/ppu_scanline_bgp` | 7187 | disagrees with the Mealybug references by 12 dots |
| `m3_lcdc_win_en_change_multiple` | 8316 | window re-activation (LCDC bit 5) |
| `m3_wx_6_change` | 13799 | WX = 6 is not a one-pixel shift of WX = 7 |
| `daid/stop_instr` | 22739 | undiagnosed; the wake-up landed 2026-09-22 and the count did not move |

The mid-line LCDC, SCX and SCY entries above are all the same shape: the
register is read live, at the dot the fetcher needs it, but which of a fetch's
six dots reads what has not been pinned to the dot. Mealybug's PPU
documentation says TILE_SEL (bit 4) is read during the two bitplane stages and
SCY during all three stages, which is what `PixelPipeline::stepFetcher` and
`tileRowAddress` do; the remaining error is smaller than a stage, and the
references have not been decoded far enough to say which dot of which stage is
wrong. They are left failing rather than tuned by trial.

Notes on the ones that are more than "a behaviour not written yet":

- **`m3_window_timing` (28) and `m3_window_timing_wx_0` (584).** These set WX
  to LY on lines 0-9 and change BGP during the window's six-dot start-up
  fetch, so the number of pale pixels at the left of each line measures the
  dot the window starts on. The reference gives the same three pixels for
  every WX from 0 to 10 and then grows by one per line from WX = 11: the
  window start costs six dots wherever it happens, and a WX below 7 neither
  delays nor advances it. FourShades gets lines 9 upwards right and lines 0-8
  wrong, by between one and six pixels: when the window triggers on the first
  dot of the pipeline the fetcher has not yet done anything, so the restart
  costs nothing instead of six dots, and the clipped pixels leave the FIFO
  short enough to stall the refill. Two placements of the clipping were
  measured - dropping the pixels at the output, which costs a dot each (24 and
  692 differing pixels), and dropping them at the push, which costs none (28
  and 584) - and neither reproduces a constant six. The evidence points at the
  window comparison running against a pixel counter that has not started
  counting at the top of mode 3, which FourShades does not model.
- **`m3_wx_6_change` (13799).** Not the same shape as WX = 4 and WX = 5. Its
  reference draws the window two rows behind and two pixels right of where
  WX = 5's does, and shows the background on lines the window covers in the
  WX = 5 image. Since the three ROMs differ only in that one constant, WX = 6
  is doing something else on hardware; it has not been diagnosed. The WX
  clipping above made it 518 pixels worse, which is not evidence against the
  clipping - the WX = 4 and WX = 5 references pin that - only a sign that
  whatever WX = 6 does is not a clip.
- **`m3_scx_high_5_bits` (80).** Only the third background tile of a line
  (x = 16-23) is ever wrong, and only on the 28 lines where SCX = LY crosses a
  tile boundary: the SCX write lands within a dot or two of that tile's map
  read. Sampling the tile index, and both bitplane bytes, on the first dot of
  their two-dot fetch stages instead of the second was tried; it took this
  test from 80 to 77 but the `screen` group as a whole from 73,628 differing
  pixels to 78,855, so it was reverted. Those two figures are the pair as it
  was measured, before the power-on change above moved `ashiepaws/bully` by
  56 pixels; the group's total today, and the sum of the table above, is
  73,572. What decided the revert is the 5,000-pixel rise, which the
  power-on change does not touch either way. The remaining error is under two
  dots and is not yet pinned to a stage.
- **`m3_lcdc_obj_size_change` (410) and `m3_lcdc_obj_size_change_scx`
  (270).** These two probe the same thing - LCDC bit 2, the object height
  bit, written during mode 3 - and the seven-dot rendering lag moved them in
  opposite directions: the plain variant went from 350 differing pixels to
  410, its `_scx` sibling from 350 to 270. **Why is not known.** The count
  rose by 60 under a change that lowered the group as a whole by 22%, and
  nothing here explains the sign.
  - What the diff map does show, comparing the produced frame with the
    reference pixel by pixel: the errors are not spread over the image. They
    sit in two narrow column clusters per 16-line block, each two to five
    pixels wide - one around x = 27-39 and one around x = 3-22 - which is
    where object edges fall, and every differing pixel carries a shade an
    object palette produces. In the plain variant both clusters march one
    pixel to the right every 16 lines and every block is affected; in the
    `_scx` variant the clusters do not move at all and two whole blocks
    (lines 32-65) come out exact.
  - That is consistent with the general shape of every other mid-line entry
    here - a register write landing on the wrong side of the fetch that
    reads it - and with the fact that SCX shifts when an object's fetch
    happens, both through the pixel discard and through the SCX term in the
    object penalty above, so the same write can fall on the other side of
    the fetch in one ROM and not the other. That is a description of the
    two ROMs' difference, not a demonstration of the cause; no experiment
    here isolates it, and the 60 pixels are recorded as unexplained.
- **`daid/ppu_scanline_bgp` (7187).** This one disagrees with the Mealybug
  references rather than with a behaviour. It writes BGP repeatedly during
  mode 3; on line 100 FourShades lands those writes on line dots 100, 108,
  116, 124 and so on and draws their seams from pixel 1, while the reference
  puts the first seam at pixel 13 - a uniform 12-dot (three M-cycle) offset
  over the whole image, in the opposite direction to the seven-dot lag that
  was added with it (it was a five-dot offset before).
  - **Which reference.** `tools/roms/tests.json` lists three images for this
    ROM, which is this harness's encoding of *alternative accepted outputs*:
    `tools/roms/RomRun.cpp` passes on a match to any one of them and reports
    the smallest difference. That the three differ is how the test list is
    built, not a discovery about them. The reported 7187 and the 12 dots are
    measured against `ppu_scanline_bgp_2.dmg.png`, the closest of the three;
    the other two come out at 7741 (`_0`) and 7640 (`_1`) against the same
    frame, and on line 100 they put the first band edge at pixel 14 and
    pixel 13 where `_2` puts it at 13, so the 12 dots are not an artefact of
    which one was picked. (`_1` is also the only one of the three whose image
    contains a fourth shade, which is where the one-pixel palette seam above
    shows up; the other two have three. That is a difference between the
    images, and no more than that - nothing here establishes where any of
    them came from.)
  - **Why FourShades follows Mealybug anyway.** Mealybug's references are
    photographed from real DMG hardware, its `m3_*` images agree with each
    other about where a mid-line write lands, and `m3_bgp_change` now matches
    its own to the pixel across 144 lines and six writes per line. The
    hardware-verified rule at the top of this file puts that above an image
    whose provenance is not stated. What remains is a difference in where
    this ROM thinks a line starts - a question about the ROM's
    synchronisation, not about the pipeline - and it has not been diagnosed.
  - **What would overturn this.** Any of: a DMG photograph of
    `ppu_scanline_bgp` with a stated provenance that agrees with its own
    references, which would make the two bodies of evidence equally
    hardware-backed and force the 12 dots to be explained rather than
    attributed to the ROM; a Mealybug `m3_*` reference shown to disagree
    with `m3_bgp_change` about where a write lands, which would break the
    unanimity the seven dots rest on; or a decoding of this ROM's own
    synchronisation showing it starts its line 12 dots from where FourShades
    puts it, which would move the seven dots rather than this entry. Until
    one of those exists the seven dots stand as `m3_bgp_change` measures
    them.
- **`daid/stop_instr` (22739).** Out of scope for this task, which built the
  PPU. It was recorded here as needing STOP's wake-up; the wake-up landed on
  2026-09-22 and the count stayed at 22,739 exactly, so that was not the
  whole story and the failure is undiagnosed. See the STOP entry at the top
  of this file.
- **`ashiepaws/strikethrough` (53) and `ashiepaws/bully` (290).** Neither is
  diagnosed, and both were failing before this task and still are. Their
  counts did not both stand still, though, and an earlier version of this
  entry said they did. `strikethrough` has been 53 throughout.
  `bully` was 346 until the power-on change above (the PPU starting at line
  153 in mode 1 rather than line 0 in mode 2) took it to 290 - measured on
  2026-09-22 by building the commit before that change and running the ROM
  runner from it. That commit's own entry recorded only that no ROM's
  verdict moved, which is a narrower claim than nothing moving, and this
  section then kept the old figure.

- **Checked:** 2026-09-22. Every count in this section is from a full run of
  `rom_runner`; the per-pixel diff maps quoted above were taken from the
  frames it writes into `build/frames` and the reference images
  `tools/roms/tests.json` names.

## The OAM corruption bug: the phase Pan Docs does not give (2026-09-22)

Pan Docs describes the bug
([OAM Corruption Bug](https://gbdev.io/pandocs/OAM_Corruption_Bug.html)) in
terms of a read FourShades does not have: OAM is 20 rows of 8 bytes; during
mode 2 the PPU reads one row per M-cycle; any CPU access anywhere in
$FE00-$FEFF during one of those M-cycles scrambles the row the PPU is reading,
and the address used and the value written have no say in it. The 16-bit
increment/decrement unit counts as an access because it is tied straight to
the address bus, which is what `Bus::iduCycle` reports.

**There is no per-row read here to collide with.** Mode 2 in FourShades is
`Ppu::scanOam()`, which runs once, over all 40 objects, at dot 80, with no
internal row pointer - `src/core/Ppu.h` says so at the declaration. What is
implemented is the *effect*: all three of Pan Docs' patterns verbatim, the
four-input "Read During Increase/Decrease" expression included, applied to
the row `Ppu::oamScanRow()` names for the M-cycle the access landed in. That
row index is a convention fitted to put the OAM-bug ROMs' corruptions where
they measure them, not a row a PPU read is ever caught mid-flight on, and the
rest of this entry says what fitted it.

One consequence of having no per-row read, which the ROMs do not arbitrate:
because the whole of OAM is read at dot 80, a corruption applied to a row the
per-row model would already have passed still changes which objects that line
selects. Under the model Pan Docs describes, scrambling a row the PPU has
finished reading could not change that line's selection, only what a later
read of OAM sees. Nothing in
the 165 test ROMs distinguishes the two - the OAM-bug ROMs check OAM's
contents, not the objects drawn from it - and it is recorded here rather than
modelled.

What follows is what Pan Docs leaves open and what decided it.

- **Which row an access is fitted to.** Pan Docs says the PPU reads one row
  per M-cycle but not where inside the line the first of the twenty sits
  relative to a CPU access. `Ppu::oamScanRow` answers `dot_ / 4`, evaluated at
  the end of the M-cycle (`GameBoy` ticks the hardware and then performs the
  access), so an access is fitted to the row whose read would begin at that
  boundary. Three ROMs pin it. The scanline-timing one steps a 16-bit register
  at successive offsets from a frame boundary and requires no corruption one
  M-cycle before the window, corruption at its first and last M-cycle, and
  none one M-cycle after: nineteen consecutive corrupting M-cycles, which can
  only be rows 1-19, with row 0 immune because it has no preceding row. The
  timing-edges one repeats that at the start of the first and second visible
  lines and at the end of the last. The no-bug one steps a register at
  `dot_ == 0` and at `dot_ == 80` on every one of the 144 visible lines and
  checks OAM against a CRC of the untouched fill. Taking `dot_ / 4 - 1`
  instead - the other plausible reading of "the row being read in this
  M-cycle" - does **not** fail at `dot_ == 80`: `oamScanRow`'s bound check
  (`if (dot_ >= kOamScanDots) return -1;`) runs before the row expression and
  returns early there regardless of which expression follows it, so changing
  only the expression cannot corrupt anything at that boundary, and measuring
  it confirms `6-timing_no_bug` still passes. What the expression change does
  do: `oamCorrupt` refuses row 0 as well as any negative row ("row 0 has no
  preceding row"), so shifting every row down by one turns the M-cycle that
  used to map to row 1 into one that maps to row 0 and no longer corrupts,
  while the M-cycle that used to map to row 19 now maps to row 18 - row 19
  becomes unreachable, and the window shrinks from nineteen consecutive
  corrupting M-cycles to eighteen. Measured on 2026-09-22 (row expression
  changed alone, bound left as committed): `oam bug` 4/7, not the 3/7 an
  earlier version of this entry implied - `4-scanline_timing` fails at its
  `Failed #3` (the M-cycle expected to be the nineteenth and last of the
  corrupting run no longer corrupts, because row 19 is unreachable),
  `5-timing_bug` fails at `Failed #2`, and `8-instr_effect` fails at
  `Failed #2` (its whole-OAM CRC catches the row 19 gap directly);
  `6-timing_no_bug` passes as the bound argument above predicts. The lower
  3/7 the earlier version of this entry cited must have come from a variant
  that also moved the bound, which was never written down; this entry now
  states only the mutation actually measured; one-line change to
  `oamScanRow`'s `return` statement, bound untouched.
- **`inc rr` and `dec rr` drive the address bus in their second M-cycle.**
  Pan Docs gives the instruction two M-cycles but does not say which of them
  the IDU runs in. There is a physical account, not only a measured one: in
  the instruction's first M-cycle the address bus is carrying PC, because
  that M-cycle is the opcode fetch - the increment/decrement unit cannot also
  be driving `rr` onto the same bus at the same time, so its activity has to
  belong to the instruction's second, internal M-cycle instead. `Cpu::executeWide`
  now calls `bus_.idle()` before `bus_.iduCycle(before)`, which puts the
  report after the M-cycle it belongs to, as every other `iduCycle` call site
  already did. With the two lines the other way round the `oam bug` group
  scores 3/7 instead of 7/7; that was measured too, not only derived. No
  cycle moves either way, and SingleStepTests stays at 499/500.
- **The pattern is applied at the end of the M-cycle, not at the access.**
  A read and a write in the same M-cycle mean something other than either of
  them alone, so `Ppu` records what the CPU did to the bus (`corruptRead_`,
  `corruptWrite_`) and `flushOamCorruption` applies one pattern at the start
  of the next `tick()`. This is invisible to software: OAM cannot be read back
  until the scan and mode 3 are over, many M-cycles later. It is what makes
  `ld a,(hl+)` and an opcode fetch from OAM produce Pan Docs' combined
  pattern, and what makes `ld (hl+),a` and the middle M-cycle of a `push`
  produce one write rather than two.
- **`pop` produces two `ReadWrite` M-cycles; Pan Docs' wording describes two
  M-cycles of its own, one of them different.** Pan Docs says `pop` "will
  trigger the bug only 3 times (instead of the expected 4 times); one read,
  one glitched write, and another read without a glitched write" - which
  already reconciles to two M-cycles on its own terms: the first a read plus
  a glitched write (the combined pattern), the second a read with *no*
  glitched write (a plain read). `Cpu::pop16` instead calls `bus_.iduCycle`
  after each of its two reads, so both M-cycles get the combined `ReadWrite`
  pattern - the sentence is a real divergence, not a description that turns
  out to match once reworded.
  - **Measured, not assumed, on 2026-09-22.** Suppressing the second
    `iduCycle` call, so the second M-cycle records a plain read exactly as
    Pan Docs describes, and running the full suite gives identical scores to
    the committed code either way: `oam bug` 7/7, test roms 106/165 (no group
    moved), SingleStepTests 499/500. The instruction-effect ROM's CRC over
    the whole of OAM after `pop bc` from $FEF0 passes under both readings.
    Both behaviours satisfy every test in the suite; nothing measured here
    distinguishes them.
  - **Decision:** FourShades keeps the committed two-`ReadWrite` behaviour.
    That is a choice, not a finding - it is what falls out of `pop16` calling
    `bus_.iduCycle` after each read the same way every other call site does,
    with no special case carved out for `pop`'s second read to match Pan
    Docs' wording on no evidence that it matters. The divergence from Pan
    Docs' prose stands, unresolved by anything in the suite; a future test
    ROM that arbitrates it should decide this properly.
- **Accesses the PPU's lock refuses still corrupt.** `GameBoy::read` and
  `GameBoy::write` report the access to the PPU before `busRead`/`writeMemory`
  decide whether it goes through, so a read that returns $FF and a write that
  is dropped both corrupt. So does an access to the unusable $FEA0-$FEFF
  stretch, which Pan Docs names explicitly. Removing those two reports drops
  the instruction-effect ROM's `pop` subtest; the rest of the group still
  passes, so they are pinned by one subtest only.

### Still unimplemented: the PC increment on an operand byte, the CB byte included

Pan Docs: "If a multi-byte opcode is executed from $FDFF or $FDFE, [the] bug
will similarly trigger twice for every read from OAM" - once for the read and
once for the IDU write that increments PC. FourShades reports the IDU for the
first byte of every opcode fetch (`Cpu::step`, which reads and then calls
`bus_.iduCycle` on the same M-cycle) but not for any byte `Cpu::fetch8` reads:
`fetch8` performs the read and advances PC without calling `bus_.iduCycle`, a
decision taken in the CPU task and left alone here. So an instruction whose
*operand* bytes lie in OAM produces a read corruption where hardware produces
the combined read-and-write one.

**The gap includes the CB prefix's second byte, not just genuine operands.**
`Cpu::executeCb` reads that byte with `fetch8`, on its own M-cycle, and like
any other `fetch8` call it reports no IDU write for the PC increment that
goes with it. An earlier version of this entry said FourShades reports the
IDU for opcode fetches "including a CB prefix's second byte" - that was
wrong; the CB byte gets exactly the plain-read shape this section describes
as unclosed, the same as a genuine operand byte would.

**`executeCb` used to make a manual `bus_.iduCycle(regs.pc)` call of its own,
and the claim that it was a harmless no-op was wrong in exactly the case this
section is about.** It was deleted on 2026-09-22. `Cpu::step` advances PC
before calling `execute`, so inside `executeCb` `regs.pc` is the *CB byte's*
address, not the prefix's, and no tick has run since the prefix was fetched:
the call landed on the M-cycle `step()` had already reported, but named the
wrong address. Where both addresses are inside OAM, or both outside it, that
is genuinely a no-op - the row a corruption lands on comes from the dot, not
from the address, and the write flag was already set. It is not a no-op for a
prefix at $FDFF, which is one of the two addresses Pan Docs' unimplemented
case names: `step()` reports $FDFF, outside OAM, and the manual call reported
$FE00, inside it, so the M-cycle corrupted a row on the strength of an
address the CPU never drove. Every other `iduCycle` call site reports the
address the unit stepped *from*. The call was deleted rather than moved,
because there is nothing left for it to report that `step()` has not; the
gap this section records is unchanged by it, and closing that gap means
changing `fetch8` for every operand byte. `tests/test_cpu_idu.cpp` states
both halves: a CB instruction reports the prefix fetch only, and a CB prefix
at $FDFF names no address inside OAM.

**Measured against the full suite, not just `oam bug` and SingleStepTests.**
Adding the call to `fetch8` was tried and the full 165-ROM suite was run with
it in place: test roms 106/165, with every group unchanged, including
`oam bug` 7/7; SingleStepTests stays 499/500. The suite is neutral on this
gap in full, not merely on the seven `oam bug` ROMs. The change was reverted
rather than landed on no evidence. Deleting the manual call above is neutral
in the same way, measured the same way: test roms 106/165 with no group
moved, SingleStepTests 499/500, every doctest case passing.

- **Checked:** 2026-09-22, from a full `rom_runner` run over all 165 ROMs
  (`oam bug` 7/7, test roms 106/165, no group moved) and a full `sst_runner`
  run (499/500), with the `fetch8` change in place for the measurement and
  reverted afterward.

## MBC5: ROM bank register initializes to 1, not 0 (2026-09-23)

- **Tests:** the eight Mooneye MBC5 ROMs, `mbc5/rom_512kb.gb` through
  `mbc5/rom_64Mb.gb`. They sit in Mooneye's `emulator-only/` tier, not
  `acceptance/`, and the ROM binaries themselves contain no `verified` string
  at all — so the "marked 'verified: DMG'" wording an earlier draft of this
  entry used was simply wrong, and is corrected here. The hardware marking is
  in the upstream source instead: each of the eight `.s` files in
  `Gekkio/mooneye-test-suite`, `emulator-only/mbc5/`, carries the line

      ; Results have been verified using a flash cartridge with a genuine MBC5 chip
      ; and support for configuring ROM/RAM sizes.

  read from upstream and checked in all eight on 2026-09-23. That is the same
  form of marking, reached the same way, as the hardware-verified MBC1
  multicart ROM cited further down this file ("using a flash cartridge with a
  genuine MBC1B1 chip"). What it verifies is the ROMs' *expected results*
  against a real MBC5 — not a direct probe of the power-on register, which is
  why the "what would overturn it" line below still asks for one.
- **Pan Docs:** [Memory Bank Controllers](https://gbdev.io/pandocs/MBCs.html) is silent on MBC5's reset value for the ROM bank register at 0x2000-0x3FFF. The page describes general MBC5 features but gives no power-on state for the register.
- **What FourShades does:** `src/core/mbc/Mbc5.h` initializes `romBank_` to 1, so bank 1 is visible at 0x4000-0x7FFF at power-on, before any write to the bank register.
- **Evidence:** In `mbc5/rom_512kb.gb`, the code at 0x0150 (entry point after boot, with no prior bank selection) calls into switchable ROM at address 0x48DB without writing the bank register. At file offset 0x08DB (bank 0's copy of that address) the byte is 0xFF, padding; at 0x48DB (bank 1's copy) it is 0x78, real code — `LD A,B`, the first byte of the copy loop `78 B1 C8 1A 22 13 0B 18` (`LD A,B; OR C; RET Z; LD A,(DE); LD (HL+),A; INC DE; DEC BC; JR`). An earlier draft of this entry glossed 0x78 as `RET`; `RET` is 0xC9, and the 0xC8 two bytes further on is the `RET Z` that ends the loop. With the register defaulting to 0, all eight Mooneye MBC5 ROMs run into padding and hang; with it defaulting to 1, all eight pass. MBC5 has no remap of bank 0 — writing 0x00 to 0x2000-0x2FFF really does select bank 0 — so the reset value is the only mechanism that can route execution to bank 1 before the first write.
- **Decision (2026-09-23):** the rule at the top of this file applies, on the
  upstream marking quoted above rather than on the tier the ROMs live in.
  Mooneye's `emulator-only/` tier means the ROM needs no reference hardware
  *image* to score, not that its results were never measured; the per-ROM
  source comment is where Mooneye records that they were, and all eight MBC5
  ROMs carry it. The step from "these results were measured on a genuine
  MBC5" to "the register powers on at 1" is the ROM's own structure: it cannot
  reach its test code at all unless bank 1 is mapped before the first bank
  write, so a run that produced the verified results on real hardware is a run
  in which the hardware had bank 1 mapped at power-on. Pan Docs is silent, so
  nothing is contradicted either way. Piece 4, task 2 ships with this
  behaviour.
- **What would overturn it:** a measurement of MBC5's ROM bank register power-on state from real DMG or CGB hardware, or discovery of a cartridge that depends on bank 0 being mapped at power-on — which would be incompatible with this one.
- **Checked:** 2026-09-23.

## MBC3's clock: the register widths and the latch, where Pan Docs is silent (2026-09-23)

- **Tests:** the Emulator Shootout's `cpp` set, `cpp/latch-rtc-test.gb` and
  `cpp/rtc-invalid-banks-test.gb` (screenshot tests at the pinned Shootout
  commit; both ROMs carry a "Built 2021-04-22" string). Neither is marked
  hardware-verified the way Mooneye marks its own, so what follows rests on
  what the ROMs measure, set out in full below. To be precise about what that
  marking is, since the MBC5 entry above turns on the same point: Mooneye's
  marking is a comment in the ROM's own upstream source recording that its
  expected results were measured on real hardware, and it is independent of
  which tier (`acceptance/`, `emulator-only/`) the ROM is filed under. The
  Shootout ROMs here carry nothing of the kind, in their binaries or upstream.
- **Pan Docs, [MBC3](https://gbdev.io/pandocs/MBC3.html):** lists the five
  clock registers with the ranges a *running* clock keeps to — RTC S 0-59,
  RTC M 0-59, RTC H 0-23, RTC DL 0-255 — and names bits 0, 6 and 7 of RTC DH,
  saying nothing about bits 1-5. Of 6000-7FFF it says only that "when writing
  $00, and then $01 to this register, the current time becomes latched into
  the RTC registers". It does not say how *wide* the registers are, what a
  program that writes a value outside the range gets back, or what a write of
  any other value to 6000-7FFF does. All three findings below fill that
  silence; none of them contradicts a Pan Docs sentence.
- **What `rtc-invalid-banks-test` does.** It opens the RAM-and-timer gate,
  writes the number *n* into whatever 4000-5FFF's value *n* selects for each
  n in 0x00-0x0F, latches, and displays the sixteen bytes read back from
  0xA000. The reference image reads
  `00 01 02 03 FF FF FF FF 08 09 0A 0B 00 FF FF FF`.
- **What `latch-rtc-test` does.** It seeds a 32-bit counter at 0xC1A4 and runs
  a small LCG at 0x00DF. Each of its 53 iterations writes five pseudo-random
  bytes into the five clock registers, reads the five *latched* registers back
  into the display buffer, and then writes one more pseudo-random byte to
  0x6000 before the next iteration. The reference image is the resulting
  16 x 16 table of 256 bytes, and re-running the ROM's own LCG reproduces
  every byte of it.
- **Finding 1: the registers are narrower than a byte** — six bits of seconds,
  six of minutes, five of hours, eight of day-low and three of day-high.
  Evidence from `latch-rtc-test`'s first two iterations alone: 0x7F written to
  hours reads back 0x1F, 0xCF to day-high reads 0xC1, then 0x67 to seconds
  reads 0x27, 0xEE to minutes reads 0x2E, 0xFC to hours reads 0x1C and 0xB2 to
  day-high reads 0x80. Every one of the 256 bytes agrees with masks of 0x3F,
  0x3F, 0x1F, 0xFF and 0xC1. `rtc-invalid-banks-test` says the same thing from
  the other end: the 0x0C it writes into the day-high register reads back as
  0x00, because 0x0C is bits 2 and 3 and neither exists.
- **Finding 2: a write of a value other than 0x00 or 0x01 to 6000-7FFF
  latches**, and no 0x00-then-0x01 sequence is needed — that is what the ROM
  proves, not the stronger claim that *any* write latches. Evidence: the 52
  bytes `latch-rtc-test` writes to 0x6000 are `D6 40 14 96 7B E9 73 1F 62 21
  B0 D5 C4 23 06 F2 DC 28 AD AF E2 6B E1 46 11 26 DA F2 A3 92 D4 ED D3 EA 08
  71 DA 68 B0 B7 F1 45 06 F9 54 BB 44 72 3C 05 A4 5C E3` — not one 0x00 and
  not one 0x01 among them — and yet every iteration reads back exactly the
  five values written before that write. Under the 0x00-then-0x01 rule the
  latched copy would keep the zeroes the ROM latched during setup for the
  whole run: 243 of the 256 bytes would be wrong, and the frame differed from
  the reference in 2270 pixels. Pan Docs' sentence stays true either way,
  since a 0x00 followed by a 0x01 is two writes and so latches under this
  rule too.
  **What the ROM cannot separate this from.** No two of the 52 bytes above
  are consecutive duplicates — each differs from the one before it — so
  every one of the 52 writes latches equally well under a narrower,
  edge-triggered rule: *a write to 6000-7FFF latches only when its value
  differs from the previous write to that range* (the generalisation of Pan
  Docs' 0x00-then-0x01 sequence to arbitrary values, rather than a departure
  from it). This ROM cannot tell that rule apart from "any write latches",
  because it never repeats a byte on consecutive writes. FourShades
  implements "any write latches" as the simpler of the two, but the
  change-triggered rule is a live alternative this evidence does not rule
  out.
- **Finding 3: an out-of-range counter wraps at its own width**, not at 256.
  This is inferred from finding 1 rather than measured directly: a six-bit
  seconds register cannot hold 64, so the old behaviour (write 63, count
  63, 64, ... 255, 0) was impossible. The carry into minutes still happens
  only when the counter steps off 59, so writing 60 counts 60, 61, 62, 63, 0,
  1, ... 59, and only then carries.
- **What FourShades does:** `src/core/mbc/Rtc.cpp` masks each register to its
  width on write and in `setState`, and `advanceField` wraps an out-of-range
  value at the register's span. `src/core/mbc/Mbc3.cpp` latches on any write
  to 6000-7FFF.
- **What the ROMs cannot distinguish.** Whether the hardware narrows a value
  as it is written, only as it is read, or only as it is latched: both ROMs
  write, latch and then read in that order every time, so all three are the
  same to them. A third alternative, narrow-on-latch — the live register
  keeps the full byte written and only the copy `latch()` makes is masked —
  is exactly as invisible as narrow-on-read, for the same reason. FourShades
  narrows on write, which is what a register with no wire for bit 6 would do,
  and that choice is visible only through the save-state API.
  Two more the same way, recorded here because nothing else records them:
  - **The bank-select register's own width.** `Mbc3::writeControl` masks
    4000-5FFF's value to four bits (`ramSelect_ = value & 0x0F`) rather than
    storing the whole byte and masking where it is used. Neither ROM writes a
    value that could tell the two apart: `rtc-invalid-banks-test` sweeps only
    0x00-0x0F, every one of which is already four bits wide, and
    `latch-rtc-test` never writes to 4000-5FFF outside that range either. A
    program that wrote, say, 0x18 would distinguish them if the hardware kept
    bit 4 somewhere a later read could see it; nothing here says whether it
    does.
  - **Which register writes restart the sub-second divider.** `Rtc::write`
    zeroes the sub-second accumulator (`ticks_`) only on a write to the
    seconds register; the design note this was built from said a write to any
    clock register restarts it. Neither ROM can separate the two: both write
    all five registers and then read the latched copies back within the same
    second, so the accumulator's state never reaches an output either of them
    checks. Restarting on seconds alone is the narrower claim, and is what a
    divider gated by the counter it feeds would do, but it is a choice, not a
    measurement.
- **Not settled by either ROM:** what happens to the sub-second accumulator
  across a save. `RtcState` has no field for it, so a restored clock starts a
  fresh second; a save made a fraction of a second early or late is within the
  error of the elapsed-time estimate the app layer hands `advanceSeconds`
  anyway.
- **Effect:** `latch-rtc-test` 2270 differing pixels -> 0, and
  `rtc-invalid-banks-test` 32 -> 0. The `mbc3 / rtc` group goes 1 / 3 -> 3 / 3.
- **What would overturn it:** a measurement from real MBC3 hardware showing
  that a write of some particular value to 6000-7FFF does *not* latch, or that
  the seconds, minutes, hours or day-high registers read back bits these masks
  drop; or a ROM (or hardware measurement) that writes the same non-0x00/0x01
  value to 6000-7FFF twice in a row and shows the second write does not
  latch, which would settle "any write latches" against the change-triggered
  alternative Finding 2 leaves standing.
- **Checked:** 2026-09-23.

## MBC1 multicart: detected by counting logos, since no header byte declares one (2026-09-23)

- **What a multicart is.** Pan Docs' MBC1 page describes "MBC1M" compilation
  cartridges directly: the chip "ignores the top bit of the main ROM banking
  register (making it effectively a 4-bit register for banking, though the
  full 5 bit register is still used for 00→01 translation) and applies the
  2-bit register to bits 4-5 of the bank number (instead of the usual bits
  5-6)",
  [MBC1: "MBC1M": 1 MiB Multi-Game Compilation Carts](https://gbdev.io/pandocs/MBC1.html#mbc1m-1-mib-multi-game-compilation-carts).
  The same page also states the problem this entry is about: "these carts have
  an alternative wiring" that **the header cannot distinguish from an ordinary
  MBC1 ROM** — cartridge type, ROM size code and RAM size code are all the same
  either way.
- **The detection rule FourShades uses — a heuristic, not a measurement.**
  `Cartridge::load` (`looksLikeMulticart` in `src/core/Cartridge.cpp`) treats a
  cartridge type of 0x01, 0x02 or 0x03 as a multicart when the ROM is exactly
  1 MiB (0x100000 bytes) — the header's own declared size after
  `Cartridge::load` has resized the image to `0x8000 << sizeCode`, not
  necessarily the file's length on disk — *and* the 48-byte Nintendo logo that
  every header carries at 0x0104-0x0133 also appears at three or more of the
  four 256 KiB quarter-boundaries (0x00104, 0x40104, 0x80104, 0xC0104). Pan Docs names the
  same signal for the general case — "These carts can normally be identified
  by having a Nintendo copyright header in bank $10" (same section) — but
  gives no threshold; three of four, not four of four, is FourShades' own
  choice, made because one of the four boundaries is the outer menu's own
  header, which is required by the boot ROM anyway, and real dumps exist where
  a compilation leaves one of the other three slots blank (no sub-game
  installed in that quarter). Requiring all four would miss those; the
  heuristic accepts three so it still fires on them.
- **What this can get wrong, honestly.** This is inference from content, not
  a header flag, so it can misfire in both directions, and nothing in the 165
  test ROMs or the unit suite proves it cannot:
  - **False positive.** Nothing stops an ordinary, non-multicart 1 MiB MBC1
    ROM from happening to carry the 48-byte logo at three or more of those
    same offsets — for instance a ROM whose sub-banks happen to start with
    that exact byte sequence for an unrelated reason, or a deliberately
    constructed one. FourShades would then narrow its bank registers on a
    cartridge that needs the full 5-bit ones, which would corrupt its
    banking. No such ROM is known to exist; this is a description of what the
    heuristic cannot rule out, not a report of it happening.
  - **False negative.** A multicart with the logo present at only one or two
    of the three non-primary boundaries — two blank sub-game slots rather
    than one — is banked as an ordinary MBC1 instead, per the "two logos ...
    not detected" case `tests/test_mbc1_multicart.cpp` covers. Pan Docs'
    "normally" (in the identification sentence quoted above) implies this
    already: the signal is typical, not universal.
- **Evidence this rule is not merely guessed.** Mooneye's hardware-verified
  multicart test ROM (marked as measured "using a flash cartridge with a
  genuine MBC1B1 chip", not just emulator-generated) is exactly 1 MiB, type
  0x01, and carries the logo at all four boundaries (every bank but bank 0 in
  its source puts the logo at its own $0104, and bank 0 — the real header —
  always must). Running it through `Cartridge::load` sets the multicart flag,
  and the ROM group's `mbc1` count goes 12/13 -> 13/13 with this task.
- **What would overturn it:** a hardware-verified test, or a real dumped
  cartridge, that this heuristic misidentifies either way — a genuine
  MBC1M cart scored as ordinary MBC1, or an ordinary MBC1 cart scored as a
  multicart — would be grounds to tighten or loosen the threshold, or to add
  a second signal (Pan Docs' other identifying mark, "duplicate content in
  banks $10-$1F ... and banks $30-$3F", is one candidate). Absent that, three
  of four logos is what this task shipped, chosen for the reason above and
  not otherwise measured.
- **A related finding this task made, not a divergence:** the low bank
  register's own "0 acts as 1" substitution is a property of the *full*
  5-bit register, evaluated before a multicart's wiring drops its top bit —
  not a property of the narrowed 4-bit value. Pan Docs states this
  explicitly in the sentence quoted above ("though the full 5 bit register is
  still used for 00→01 translation"), and the Mooneye ROM's own
  `expected_banks` table measures it directly: writing 0x10 to 0x2000-0x3FFF
  leaves the 5-bit register at 16, which is not zero, so bank $4000-$7FFF
  reads bank 0 (16 with its top bit dropped), not bank 1. An earlier version
  of this task's own working notes described the register as simply "4 bits
  wide (value & 0x0F, still 0 → 1)", which reads as the substitution applying
  to the already-narrowed value — that phrasing would return bank 1 for this
  case instead of bank 0, and the first implementation of this task did
  exactly that and failed the Mooneye ROM (12/13) until corrected.
  `Mbc1::romBank` now masks to 4 bits only after the substitution, and
  `tests/test_mbc1_multicart.cpp`'s "the zero substitution reads the full
  5-bit register" case pins it. This is recorded here as a correction made
  during the task, in the same spirit as the MBC3 latch entry above, not as a
  live divergence — Pan Docs, the hardware-verified ROM and FourShades all
  agree.
- **A second, undocumented departure the same review found: mode 1 narrows
  too.** The task brief for this piece said "everything else, including mode
  1, is unchanged", but Pan Docs' quoted sentence above puts the 2-bit
  register on bits 4-5 "of the bank number" without carving out an exception
  for mode 1's mapping of 0x0000-0x3FFF — the narrowed register drives the
  whole bank number, not only the 0x4000-0x7FFF half, so mode 1 moves too.
  `Mbc1::romBank` already did this correctly (`highShift` is 4 for a
  multicart in both branches of the `address < 0x4000` check), but nothing
  said so and nothing tested it: the reviewer of this task mutated the
  multicart shift back to 5 (the ordinary, non-multicart width) and the
  308-case unit suite still passed 308/308, because no unit test exercised
  mode 1 on a detected multicart — only Mooneye's hardware-verified multicart
  ROM caught the mutation, since its own test steps mode 1. Covered now by
  `tests/test_mbc1_multicart.cpp`'s "mode 1 also narrows to bits 4-5 in the
  low region, not just 5-6" case, which was verified to fail (bank 0x20
  instead of the expected 0x30) with the shift temporarily reverted to 5.
- **Checked:** 2026-09-23.

## Channel 3's wave RAM window: the two figures Pan Docs does not give (2026-09-23)

While channel 3 is playing, a monochrome console's CPU cannot reach wave RAM
freely. Pan Docs describes the three consequences and gives no number for any
of them:

- **The access window.** "On monochrome consoles, wave RAM can only be
  accessed on the same cycle that CH3 does. Otherwise, reads return $FF, and
  writes are ignored", and "the byte accessed will be the one CH3 is currently
  reading ... regardless of the address being used",
  [Audio Registers: FF30-FF3F](https://gbdev.io/pandocs/Audio_Registers.html#ff30ff3f--wave-pattern-ram).
  "The same cycle" is the whole of it.
- **The corruption.** "Triggering the wave channel on the DMG while it reads a
  sample byte will alter the first four bytes of wave RAM. If the channel was
  reading one of the first four bytes, the only first byte will be rewritten
  with the byte being read. If the channel was reading one of the later 12
  bytes, the first FOUR bytes of wave RAM will be rewritten with the four
  aligned bytes that the read was from",
  [Audio Details: Obscure Behavior](https://gbdev.io/pandocs/Audio_details.html#obscure-behavior).
  The NR34 note calls the same moment "retriggering CH3 while it's *about to
  read* a byte from wave RAM". Which of the two it is — the read, or the
  moment before it — is the difference this entry had to settle.
- **The trigger's own reload.** "The period divider is set to the contents of
  NR33 and NR34",
  [Audio Registers: FF1E](https://gbdev.io/pandocs/Audio_Registers.html#ff1e--nr34-channel-3-period-high--control),
  with no extra wait mentioned.

blargg's own write-up of the same hardware ends its to-do list with "Document
exact timing for DMG wave issues"
([Game Boy sound hardware](https://gbdev.gg8.se/wiki/articles/Gameboy_sound_hardware)),
so the figures were never written down anywhere; the same page records that he
measured this behaviour on DMG-03, DMG-05, DMG-06 and MGB-01, which makes the
three ROMs that measure it hardware-verified and the only arbiter there is.

**What FourShades does.** `GameBoy::tick` advances the hardware by one M-cycle
and then performs the CPU's access, so an access sits on the last T-cycle of
its M-cycle, and the channel's reads are placed against it:

- The CPU reaches wave RAM only when channel 3's own read lands on that same
  last T-cycle (`WaveChannel::readingNow`). Every other M-cycle reads 0xFF and
  drops the write. The byte reached is the one the channel last read, whatever
  address the CPU named.
- A trigger corrupts wave RAM when the channel's next read is **two T-cycles
  away** (`WaveChannel::aboutToRead`) — two T-cycles *earlier* than the access
  window, which is Pan Docs' "about to read" rather than its "while it reads"
  — and the bytes copied to the front are the ones **that** read was going to
  come out of (`WaveChannel::nextReadIndex`), one sample past the one last
  read.
- The first period after a trigger is **six T-cycles longer** than the ones
  after it (`WaveChannel::kTriggerDelay`). Nothing in either document says so.

**How the three ROMs pin all of it down.** Each of them runs the same 69
iterations: it loads wave RAM, triggers channel 3 with a period of
(256 - b) * 2 T-cycles where b counts up by one per iteration, and then
immediately writes a period of 4. That second period only takes effect at the
following reload — Pan Docs' "Period changes (written to NR33 or NR34) only
take effect after the following time wave RAM is read" — so the channel's
first read, and with it the phase of every read after it, moves two T-cycles
earlier each iteration while the CPU's own access stays a fixed 208 T-cycles
after the trigger. Each iteration then prints what it saw: the byte a read of
$FF30 returned (09), wave RAM after a retrigger (10), or wave RAM after a
write of $F7 (12). Sixty-nine phases, two T-cycles apart, checksummed. Any of
the three figures above being wrong moves the whole sequence and fails the
checksum.

**Measured, 2026-09-23** (one change at a time, rebuilt, `--only dmg_sound`,
then reverted):

- `kTriggerDelay` 6 → 0 (the reload Pan Docs describes, with no extra wait):
  sound 9/12, all three wave ROMs failing.
- `kAboutToRead` 2 → 4, moving the corruption onto the read itself rather than
  the moment before it: sound 11/12, the retrigger ROM alone failing. This is
  the measurement that chooses Pan Docs' "about to read" wording over its
  "while it reads" wording.
- `Apu::waveRamReachable` forced to true, which is the CGB rule (Pan Docs: "On
  other consoles, the byte accessed will be the one CH3 is currently reading"):
  sound 10/12, the read and write ROMs failing, the retrigger ROM unaffected.
- Period `(2048 - frequency) * 2` → `* 4`, a pulse channel's: sound 9/12, all
  three failing, and the 392-case unit suite red.
- Low nibble read before high: unit suite red; sound 12/12, because the ROMs
  read bytes out of wave RAM and never listen to a nibble.
- Output level 3 shifting by 3 instead of 2: unit suite red; sound 12/12, for
  the same reason.

**What is fitted rather than derived.** The two figures are expressed against
this emulator's own advance-then-access order, so what they really fix is the
distance between the CPU's access and the channel's read — six T-cycles of
trigger delay and a two T-cycle lead for the corruption. A model that put the
CPU's access two T-cycles earlier in the M-cycle and shortened the trigger
delay to four would produce the identical 69 sequences; the ROMs cannot tell
the two apart, and neither can this entry. What the ROMs do settle is the
relative spacing, and that is what the two constants carry.

- **Checked:** 2026-09-23.

## Timing model (not a divergence: where Pan Docs is silent)

Pan Docs gives cycle counts but not every within-M-cycle order. These are the
choices FourShades makes, and the hardware-verified test ROMs that pin them.

- **Advance, then access.** Every bus call first advances the timer, serial
  port, PPU and OAM DMA by one M-cycle, then does the CPU's read or
  write. All 13 Mooneye `timer` tests (which race TIMA, TMA, TAC and DIV
  writes against the reload and overflow cycles) pass with this order, so it
  was kept.
- **Interrupts are sampled at the end of the opcode-fetch M-cycle.** A request
  raised during that M-cycle, such as the timer's in its reload cycle
  ("cycle B"), is taken instead of the fetched instruction, whose fetch becomes
  the first of Pan Docs' two wait M-cycles (dispatch stays 5 M-cycles). Pan
  Docs hedges on what those two M-cycles are: "2 M-cycles pass while nothing
  happens; presumably the CPU is executing `nop`s during this time" ([Interrupts:
  Interrupt handling](https://gbdev.io/pandocs/Interrupts.html#interrupt-handling)).
  FourShades' first one is the opcode read `step()` already performed before
  the interrupt was seen; that read has no side effects, so it's unobservable
  and doesn't need to be told apart from a `nop`. Pinned by
  `timer/rapid_toggle`: the interrupt must arrive before the `dec bc` whose
  fetch coincides with cycle B.
- **A halted CPU wakes within the M-cycle an interrupt becomes pending,** and
  that M-cycle is also the fetch of the next opcode, so HALT services an
  interrupt exactly as a run of NOPs would (Pan Docs: "the CPU simply wakes
  up, and before executing the instruction after the halt, the interrupt
  handler is called normally"). Pinned by `halt_ime0_nointr_timing`,
  `halt_ime1_timing2-GS` and `di_timing-GS`, which compare the two paths to
  the M-cycle. Halted M-cycles with nothing pending make no bus access, as
  SingleStepTests' HALT cycles (`---`) have it.
- **System counter at power-on: 0xABC8** before the fetch at 0x0100. Pan Docs
  gives DIV = $AB only. `boot_div-dmgABCmgb` (DMG A-C, MGB) sees DIV turn $AC
  in the 14th M-cycle from the fetch at 0x0100, which puts the counter at
  0xABC8 before that fetch (0xABCC, the value often quoted, is the counter
  just after it); `boot_div` needs only this counter value to pass. Only
  `serial/boot_sclk_align-dmgABCmgb` needs both this and the interrupt
  sampling point above together: the serial interrupt it checks lands at a
  time that depends on both.
- **OAM DMA:** the M-cycle after the FF46 write is a start-up cycle; the CPU
  is then locked out for the 160 M-cycles that copy bytes, the last included
  (`oam_dma_timing`, `oam_dma_restart`, `push_timing`, `rst_timing`,
  `call_timing2`, `call_cc_timing2`).
- **The WY == LY coincidence ("Y condition") latches at the beginning of
  each scanline, and independently of LCDC bit 5.** `Ppu::latchWindowY`,
  called from `Ppu::stepDot` on the dot a drawn line begins, sets
  `windowReached_` whenever `LY == WY`, whether or not the window is enabled
  at that instant. Bit 5 is checked separately, in `PixelPipeline::stepDot`,
  only once the X counter reaches WX − 7, and the window is drawn there only
  if bit 5 is set at that moment.
  Pan Docs' own model keeps these two checks
  apart the same way: "At the beginning of each scanline, if the value of
  `WY` is equal to `LY`, the *Y condition* becomes true (and remains so for
  subsequent scanlines)" — with no mention of LCDC bit 5 — and only the
  later, separate check gates on it: "When this counter is equal to `WX`,
  if the *Y condition* is true and the [Window enable bit] is set in
  `LCDC`, background rendering is reset, beginning anew from the active row
  of the Window's tilemap,"
  [Window behavior: Window rendering criteria](https://gbdev.io/pandocs/Window.html#window-rendering-criteria).
  The same section goes on: "The coordinate of the active Window row is
  then incremented," and that toggling the Window enable bit off and on
  mid-scanline "can happen more than once per scanline, making the
  Window's 'tilemap Y coordinate' increase more than once in the
  scanline" — advancing the row counter this way needs no fresh `WY ==
  LY` match, which only supports an ungated latch: if the coincidence had
  to be re-established, a second advance mid-scanline couldn't happen
  without LY changing. FourShades doesn't model that mid-scanline
  multiple-advance behaviour yet; Mealybug's `m3_lcdc_win_en_change_multiple`
  and `m3_lcdc_win_en_change_multiple_wx` tests probe it and are left for a
  later task.
  (Pan Docs does note that on GBC, clearing bit 5 resets the Y condition
  too — but says so only for GBC, which FourShades doesn't model yet, so it
  doesn't bear on this DMG-era decision.) The alternative — gating the latch
  on bit 5 too — would mean a window enabled mid-frame could never start on
  that frame: it would have missed the one line where LY == WY, and the
  coincidence never recurs before the next VBlank resets `windowReached_`.
  FourShades takes the ungated latch as the more likely hardware behaviour.
  Mooneye's and Mealybug's window tests arbitrate this in later pieces of
  work; if that evidence says the gate belongs, the test that depends on the
  ungated latch (`the window's counter does not advance on lines LCDC
  disables it...` in `tests/test_pixel_pipeline.cpp`) gets restructured to
  match — the implementation is not to be bent to keep that test passing.
  **Where in the line the latch sits was wrong here until 2026-09-22**, in
  the code and in this entry. The latch sat at the start of mode 3, dot 80,
  so a WY write landing during a line's OAM scan still took effect on that
  line - which Pan Docs' "beginning of each scanline" excludes - while this
  entry said it was at the start of mode 2, which is what Pan Docs says and
  what the code did not do. Nothing in the 165 test ROMs measures which of
  the two it is, so Pan Docs decides it under the rule at the top of this
  file, and the latch moved to the beginning of the line. The line the LCD
  is switched on for has no line boundary of its own - the PPU picks it up
  one M-cycle in - so it latches at that pick-up, the only beginning that
  line has. This is a change of behaviour, and it moved no score:
  SingleStepTests 499 / 500, test ROMs 106 / 165 with every group identical,
  and the `screen` group's differing-pixel total unchanged at 73,572. One
  unit test (`the window does not draw above WY`) had been setting WY during
  the OAM scan of the line it expected the write to govern, and now sets it
  with the LCD off, before that line begins.
  Checked 2026-09-14; the placement within the line, 2026-09-22.
- **STAT's mode field, and the STAT interrupt sources, trail the PPU's own
  mode by one M-cycle; the VRAM and OAM locks do not.** Pan Docs describes
  STAT bits 1-0 only as "Indicates the PPU's current status"
  ([STAT](https://gbdev.io/pandocs/STAT.html)) and says nothing about when
  within a mode change that becomes readable. `lcdon_timing-GS` and
  `lcdon_write_timing-GS` (DMG, MGB, SGB, SGB2) read LY, STAT, OAM and VRAM at
  24 and 19 fixed cycle offsets across lines 0, 1 and 2 and pin all of it: LY
  increments and OAM locks one M-cycle before STAT reports mode 2; VRAM locks
  one M-cycle before STAT reports mode 3; both unlock on the M-cycle STAT
  reports mode 0; an OAM *write* still gets through on the M-cycle the PPU has
  left mode 2 for mode 3, and a VRAM write is refused only while STAT reports
  mode 3. `Ppu` keeps the PPU's own `mode_` and the CPU-visible `visibleMode_`
  side by side for exactly this. The same one-M-cycle trail is what makes
  `hblank_ly_scx_timing-GS` come out right: the mode 0 interrupt lands 50
  M-cycles before LY increments at SCX mod 8 = 0, not 51. Checked 2026-09-21.
- **The line the LCD is switched on for is 452 dots long and has no mode 2.**
  Pan Docs says only "When re-enabling the LCD, the PPU will immediately start
  drawing again, but the screen will stay blank during the first frame"
  ([LCDC](https://gbdev.io/pandocs/LCDC.html)). `lcdon_timing-GS`'s own header
  states what it measures - "line 0 starts with mode 0 and goes straight to
  mode 3", "line 0 has different timings because the PPU is late by 2
  T-cycles" - and its table fixes the rest: STAT reports mode 0 until mode 3
  begins 80 dots in, and LY turns 1 after 452 dots, not 456. FourShades models
  that as the PPU picking the line up one M-cycle in (`dot_ = 4`) in mode 0,
  which reproduces every entry of the table. That much is what the ROM
  measures. That there is therefore no OAM scan, and so no objects are
  selected on that line, is FourShades' inference from the absence of mode 2
  - a reasonable one, but not itself something the ROM checks. Whether any
  of the other 164 tests would notice has not been established either:
  what is known is only that no test's verdict moves either way, which is
  weaker than no test exercising it. Checked 2026-09-21.
- **The mode 2 STAT source is pulsed at the top of line 144, on the same dot
  as the VBlank interrupt.** Pan Docs' STAT page describes the mode 2 source
  only as "the Mode 2 condition". `vblank_stat_intr-GS` (DMG, MGB, SGB, SGB2)
  times VBlank-to-VBlank against VBlank-to-mode-2-STAT with DIV and expects
  the same value, which places the pulse on VBlank's own dot rather than an
  M-cycle later with the visible mode. `Ppu::oamSourceHigh` adds that one dot;
  `intr_1_2_timing-GS`, which starts counting after the pulse has passed, is
  unaffected by it and still measures the distance to line 0's mode 2.
  Checked 2026-09-21.
- **The LY=LYC comparison reads as "no match" for the M-cycle LY changes in,
  and stops entirely while the LCD is off.** Pan Docs says the comparison is
  constant ("The Game Boy constantly compares the value of the LYC and LY
  registers", [STAT](https://gbdev.io/pandocs/STAT.html)).
  `lcdon_timing-GS`'s LYC = 1 table shows the flag turning 1 one M-cycle after
  LY turns 1, but dropping to 0 on the same M-cycle LY turns 2 - a
  one-M-cycle hole at each line boundary, not a delay. `stat_lyc_onoff`
  (verified on every model) shows the flag and the STAT level line both
  keeping their last value while the LCD is off, with writes to LYC having no
  effect there, and being re-evaluated when it comes back on - so a result
  that does not change across the off period produces no new interrupt, and
  one that changes from false to true produces one. Checked 2026-09-21.
- **A write raises the STAT level line inside its own M-cycle.** Pan Docs' DMG
  STAT bug reads "It behaves as if $FF were written for one M-cycle, and then
  the written value were written the next M-cycle"
  ([STAT](https://gbdev.io/pandocs/STAT.html)); FourShades now takes that
  literally, with the $FF M-cycle being the write's own. `Ppu::write` returns
  the IF bits it raises and `GameBoy::writeIo` ORs them in before the
  instruction ends. `stat_lyc_onoff`'s round 4 needs this for a different
  register: it switches the LCD on and has `di` as the very next instruction,
  so an interrupt raised an M-cycle later would never be taken.
  Checked 2026-09-21.
- **The STAT-write quirk and the LCD-enable level-line update are both
  suppressed while they would fire on no evidence.** Landing the return-value
  change above (`Ppu::write` raising `irq::Lcd` within its own M-cycle) opened
  two paths nothing in the suite measures. First: while the LCD is off,
  `visibleMode_` is forced to 0, which satisfies the mode-0 (HBlank) STAT
  source unconditionally, so evaluating the $FF41 write quirk's
  `statConditions(0x78)` there would raise `irq::Lcd` on any write to STAT
  during the off period whenever the level line happens to be low.
  `Ppu::write`'s `0xFF41` case now gates that evaluation on `lcdOn()`,
  restoring what the pre-M-cycle-accurate code did (the old `tick()` reset
  the write quirk to nothing on its very next M-cycle while off, so it never
  actually fired then either). `stat_lyc_onoff` does not write STAT during
  its off period, so this is evidence-neutral: the full suite (`ppu timing`
  12/12, test roms 101/165, SingleStepTests 499/500, all doctest cases) is
  unchanged with the gate in place, which is why it was kept. Second, the
  same shape exists when the LCD is switched on: `Ppu::write`'s `0xFF40`
  case re-evaluates the level line to let a fresh LY=LYC match raise
  an interrupt inside the enabling M-cycle (needed for `stat_lyc_onoff`'s
  round 4), but at that instant `visibleMode_` is also forced to 0, so the
  same mode-0 source would fire immediately if it happened to be selected.
  That evaluation now masks the mode-0 source out (`statSelect_ & 0x70`); the
  LYC and OAM sources are unaffected, and the mode-0 source still applies from
  the very next M-cycle onward through the normal per-tick evaluation in
  `stepDot`. Also evidence-neutral: the same four suite results are unchanged
  with this mask in place. No test in the 165 enables the LCD with the mode-0
  source selected, so neither gate is proven correct by the suite - only that
  closing an unevidenced interrupt path costs nothing that is currently
  measured. Checked 2026-09-21.
- **Checked:** 2026-09-11, except the WY-latch entry above, checked
  2026-09-14, and the six entries above it dated 2026-09-21.
