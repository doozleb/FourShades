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
- **Still missing after piece 2** (the timer, joypad and interrupts now exist,
  but STOP doesn't use them yet):
  - STOP never wakes. Pan Docs' way out of STOP mode is a button press, and
    there is no joypad input yet (P1 always reads "no buttons pressed").
    Joypad input arrives with the window in piece 3, and STOP's wake-up with it.
  - STOP doesn't reset DIV. That is to be done with the planned
    centralisation of the system counter's edge handling (so a reset's
    falling edges reach the timer, and later the sound chip, from one place),
    before piece 5.
  - The interrupt-pending branch, where Pan Docs makes STOP a 1-byte opcode,
    isn't implemented: STOP is always 2 bytes. It is planned for piece 3,
    together with the button-held branches, which need joypad input.
- **Scored test affected:** `daid/stop_instr.gb (DMG)` is one of the 165
  scored test ROMs (screen group). It is a screenshot test, so it fails for
  now on "needs the PPU (piece 3)" before STOP's behaviour is ever checked.
- **Also noted by Pan Docs itself:** "stop is often considered a two-byte
  instruction, though the second byte is not always ignored.",
  [CPU Instruction Set](https://gbdev.io/pandocs/CPU_Instruction_Set.html#stop).
  "Not always" refers to the button-held and interrupt-pending branches, where
  STOP behaves differently — the branches listed as still missing above.
- **Checked:** 2026-09-11.

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

## STAT at power-on (0xFF41): 0x86 instead of 0x85

- **Test:** `tests/test_gameboy.cpp`'s power-on test checks `FF41 == 0x86`.
- **Pan Docs:** [Power Up Sequence](https://gbdev.io/pandocs/Power_Up_Sequence.html)
  lists STAT = $85 and LY = $00 for DMG at PC = $0100 — mode 1 (VBlank) while
  LY already reads 0, i.e. the end of line 153.
- **FourShades:** the LCD timing (`src/core/LcdTiming`) is a placeholder
  until the PPU (piece 3) and starts at LY 0 in mode 2, so STAT reads $86.
- **Affected:** no test ROM at present. Mooneye's `boot_hwio-dmgABCmgb` reads
  STAT and LY only after walking $FF00-$FF3F, when the placeholder happens to
  agree with hardware; it fails first on NR10 ($FF10 reads $FF, expected
  $80), because sound registers arrive in piece 5. With the sound registers
  stubbed to their power-on values in a throwaway build, it passed.
- **Resolution:** the piece-3 PPU must reproduce the line-153 behaviour.
- **Checked:** 2026-09-11.

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
- **Decision:** shipped deliberately in Task 6 (2026-09-14) as an
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
  at OAM X 160-167 doing exactly that; the fetcher itself is not counted,
  because a fetch takes six dots and feeds eight pixels, so it is never the
  binding constraint over the handful of dots at the end of a line. Mode 0
  is entered when that count drops to the line's lag. This replaced an
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
  finishes, whatever the prediction said. Nothing in the suite reaches it.
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
- **Checked:** 2026-09-21.

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

## Screenshot tests still failing after Task 9 (2026-09-21)

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
| `ashiepaws/bully` | 346 | not diagnosed |
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
| `daid/stop_instr` | 22739 | needs STOP's wake-up, which needs joypad input |

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
  pixels to 78,855, so it was reverted. The remaining error is under two dots
  and is not yet pinned to a stage.
- **`m3_lcdc_obj_size_change` (410) and `m3_lcdc_obj_size_change_scx`
  (270).** These two probe the same thing - LCDC bit 2, the object height
  bit, written during mode 3 - and Task 9's seven-dot shift moved them in
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
  over the whole image, in the opposite direction to the seven-dot lag Task 9
  added (it was a five-dot offset before).
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
- **`daid/stop_instr` (22739).** Out of scope for this task: it needs STOP to
  wake, which needs joypad input. See the STOP entry at the top of this file.
- **`ashiepaws/strikethrough` (53) and `ashiepaws/bully` (346).** Not
  diagnosed. Both were failing before this task with the same counts, so
  nothing here moved them either way.

- **Checked:** 2026-09-22. Every count in this section is from a full run of
  `rom_runner`; the per-pixel diff maps quoted above were taken from the
  frames it writes into `build/frames` and the reference images
  `tools/roms/tests.json` names.

## Timing model (not a divergence: where Pan Docs is silent)

Pan Docs gives cycle counts but not every within-M-cycle order. These are the
choices FourShades makes, and the hardware-verified test ROMs that pin them.

- **Advance, then access.** Every bus call first advances the timer, serial
  port, LCD timing and OAM DMA by one M-cycle, then does the CPU's read or
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
- **The WY == LY coincidence ("Y condition") latches independently of LCDC
  bit 5.** `Ppu::stepDot` sets `windowReached_` at the start of mode 2 on
  every line whenever `LY == WY`, whether or not the window is enabled at
  that instant. Bit 5 is checked separately, in `PixelPipeline::stepDot`,
  only once the X counter reaches WX − 7, and the window is drawn there only
  if bit 5 is set at that moment. Pan Docs' own model keeps these two checks
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
  Checked 2026-09-14.
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
