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
- **Scored test affected:** `mooneye/manual-only/sprite_priority.gb` and the
  Mealybug object tests test exactly this and would arbitrate it, but both
  currently sit in the `screen` group, which isn't scored yet (fails with
  "needs the PPU (piece 3)"), so neither is available yet to confirm or
  refute the approximation.
- **Checked:** 2026-09-14.

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
- **Checked:** 2026-09-11, except the WY-latch entry above, checked
  2026-09-14.
