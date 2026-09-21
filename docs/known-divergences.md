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
