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
  scored test ROMs (screen group). It is a screenshot test, and it passes as of
  2026-09-24, with 0 of 23,040 pixels differing. It had differed in 22,739 of
  them: the whole-screen difference was that the PPU kept drawing while the
  machine was stopped, not anything about STOP's decoding, its length or its
  wake-up (the wake-up landed on 2026-09-22 and did not move the count by a
  single pixel). See "STOP stops the PPU and blanks the LCD" below. The ROM
  never reads DIV and does not depend on STOP's length — its second byte is
  `00` — so it does not arbitrate the divergence above, and the
  SingleStepTests figure did not move when it started passing. (Updated
  2026-09-24.)
- **Also noted by Pan Docs itself:** "stop is often considered a two-byte
  instruction, though the second byte is not always ignored.",
  [CPU Instruction Set](https://gbdev.io/pandocs/CPU_Instruction_Set.html#stop).
  "Not always" refers to the button-held and interrupt-pending branches, where
  STOP behaves differently — the branches listed as still missing above.
- **Checked:** 2026-09-11; the wake-up re-checked against Pan Docs and
  implemented 2026-09-22.

## STOP stops the PPU and blanks the LCD (decided 2026-09-24)

Pan Docs does not say, in so many words, what a DMG's screen shows while the
machine sits in STOP mode with the LCD still enabled. Everything around that
sentence points one way, and a hardware photograph settles it.

- **What Pan Docs says**
  ([Reducing Power Consumption: Using the STOP Instruction](https://gbdev.io/pandocs/Reducing_Power_Consumption.html#using-the-stop-instruction)):
  "The STOP instruction is intended to switch the Game Boy into VERY low power
  standby mode." For CGB it is explicit that the picture goes: "On CGB, leaving
  the LCD enabled when invoking STOP will result in a black screen. Except if
  the LCD is in Mode 3, where it will keep drawing the current screen." For DMG
  it warns only about the opposite order — "On a DMG, disabling the LCD before
  invoking STOP leaves the LCD enabled, drawing a horizontal black line on the
  screen and very likely damaging the hardware" — which is the panel left
  undriven, the same picture as switching the machine off mid-frame.
- **And what a blank DMG panel reads** ([LCDC](https://gbdev.io/pandocs/LCDC.html)):
  "When the display is disabled the screen is blank, which on DMG is displayed
  as a white 'whiter' than color #0." Shade 0 is what FourShades already puts
  in the frame when LCDC bit 7 is cleared.
- **The inference, and it is one.** A DMG has one oscillator; the PPU has no
  clock of its own. Standby stops that clock, so the PPU stops with it and the
  panel gets no drive, which reads blank. Pan Docs states the stopped picture
  for CGB and the blank-panel shade for DMG, but never joins them for a DMG
  with the LCD left on.
- **The hardware evidence that settles it.** One of the 165 scored ROMs prints
  its status text, sets BGP so that colour 0 is dark, and enters STOP mode with
  no button held (screen group; see the entry for it below). Its reference is
  photographed from a DMG and is 23,040 pixels of shade 0 — an entirely white
  screen, the text included. Before this change FourShades kept the last frame
  on the screen and matched 301 of those pixels, exactly the white text; with
  the PPU stopped and the frame blanked it matches all 23,040.
- **What FourShades does (2026-09-24).** `GameBoy::tick` tells the PPU, every
  M-cycle, whether the CPU is in STOP mode. `Ppu::setClockStopped(true)` blanks
  the frame to shade 0 once, on the edge, and `Ppu::tick` then does nothing at
  all: no dot runs, no mode changes, no line advances, no frame is counted.
  Starting the clock again does not clear anything — the PPU picks up on the
  line and dot it stood on and draws over the blank frame.
- **HALT is not this.** HALT stops the CPU, not the clock, and Pan Docs' HALT
  page says nothing about the display; the PPU keeps running and the screen
  keeps its picture. A unit case pins that, because blanking on HALT too would
  be the easy mistake.
- **Not modelled: the rest of the standby.** "VERY low power standby mode"
  would stop the timer, the serial port and the sound chip as well, and this
  models only the PPU. They keep advancing while the machine is stopped. No
  scored test reaches that (the one ROM above reads no counter, and
  SingleStepTests runs a single instruction per case, so it never steps a
  stopped CPU), and DIV's reset on STOP is still owed — see the STOP entry
  above.
- **This changed nothing about STOP itself.** Not its decoding, not its length,
  not when it wakes. The SingleStepTests figure was 499/500 before and after,
  with the same single failure on `10`'s `pc`.
- **What would overturn this.** A DMG photograph of a machine in STOP mode with
  the LCD enabled showing anything other than a blank screen, or a Pan Docs
  sentence that gives the DMG case directly.

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
  runner from it: `bully` 346, `strikethrough` 53. It was a failing test
  either way at that date, so no verdict moved with it; `bully` passes as of
  2026-09-24 and has no row in the screenshot table below any more, and
  `strikethrough` is still 53 (see its own entry above). Mooneye's
  `boot_hwio-dmgABCmgb` still failed at that date, and a traced run on
  2026-09-22 put its first mismatch at $FF10 (NR10), the first sound
  register: the ROM wanted $80 and FourShades read $FF, because there was no
  APU until piece 5. The registers it checked before that, $FF00-$FF0F, all
  matched. The ROM stops at its first mismatch, so nothing was claimed here
  about the registers after $FF10 — STAT ($FF41) among them was never reached.
  It passes as of 2026-09-24: the APU landed in piece 5 and the power-on
  *phase* entry below settled the $FF41 and $FF44 rows it then went on to
  reach. STAT's power-on value recorded here needed no change for that.
- **Checked:** 2026-09-22.

## The PPU's power-on phase within line 153: solved to a band, chosen inside it (2026-09-24)

Pan Docs gives the PPU's power-on *state* but not its *phase*. The entry above
settles the state - line 153, mode 1, LY reading 0, STAT $85. Which dot of line
153 the boot ROM leaves the PPU on is a separate question, and it is not free:
it sets the phase of every later line against the CPU, so anything that reads
LY or STAT a fixed number of M-cycles after $0100 depends on it.

- **Pan Docs:** [Power Up Sequence](https://gbdev.io/pandocs/Power_Up_Sequence.html)
  lists the register values at PC = $0100 and nothing about where inside a line
  the PPU sits. There is no documented figure to follow or diverge from here.
- **What FourShades did:** `Ppu::dot_` started at 4 - the smallest dot that
  makes LY read 0 through the LY=153 quirk, which is how the value was picked
  when the state above was settled. It was never measured.
- **The two constraints that pin it.** One of the 165 scored ROMs (Mooneye's
  DMG power-on register walk, in the `boot state` group) reads $FF00-$FF7F
  against a table and stops at its first mismatch. Two of its rows depend on
  the phase, and a traced run on 2026-09-23 puts both to the M-cycle:
  - it reads $FF41 (STAT) **1139 M-cycles** after power-on, and the table wants
    $80 - mode 0, with LYC 0 unmatched;
  - it reads $FF44 (LY) **1190 M-cycles** after power-on, and the table wants
    **$0A**. FourShades returned $09: line 9, dot 204, 252 dots short of the
    line boundary.
- **How the band was derived.** Both reads land on a line whose position is
  `dot_ + 4 x M - 456` dots into the frame, so an advance of the power-on phase
  by dots moves both. Requiring LY = 10 at the second read puts the frame
  offset inside line 10's 456 dots; requiring STAT to still report mode 0 at
  the first read puts that one at or after the dot mode 0 begins on a bare line
  (dot 256, as reported, the boundary `intr_2_mode0_timing` pins). Together
  they leave an advance of **between 253 and 455 dots**.
- **How the multiple-of-four rule was derived, and it is not from that ROM.**
  The PPU steps four dots per M-cycle, so an advance that is not a multiple of
  four permanently offsets every dot-counted boundary from the M-cycle the CPU
  sees it on. `dot_ = 354` (an advance of 350) was built and measured on
  2026-09-24: the register walk still **passes** (`boot state` 3/3), and seven
  doctest cases fail - `tests/test_stat.cpp`'s "LY leads the mode 0 STAT
  interrupt by 50 M-cycles, fewer as SCX grows" (it reports 49), four
  `tests/test_oam_bug.cpp` cases, which step a register at offsets from a frame
  boundary the odd phase moves off the M-cycle, `tests/test_gameboy.cpp`'s "the
  CPU sees the PPU's blocking, and FEA0-FEFF follows OAM", and the power-on
  phase assertion below, which names 356 and so is expected to move. So the
  rule is pinned by the M-cycle arithmetic and by those unit tests, not by the
  ROM, and this entry does not claim the ROM has anything to say about it - it
  does not.
- **FourShades now:** `Ppu::dot_ = 356` (an advance of 352). `line_` is
  untouched at 153, so the documented power-on register values are unchanged -
  LY still reads 0 and STAT still reads $85 at PC = $0100.
- **356 is a choice, not a measurement.** The band admits 49 values in total:
  `dot_` of 260, 264, ... 452. Nothing in the suite distinguishes them.
  Measured on 2026-09-24, not argued: `dot_ = 260`, the bottom of the band,
  was built and the register walk **passes** exactly as at 356, the unit suite
  is otherwise green, and the only assertion that moves is the one that names
  356 itself. 356 is recorded
  here as the value that was tried first and measured, and
  `tests/test_ppu.cpp`'s "the PPU's power-on phase is the value chosen inside
  the solved band, and M-cycle aligned" pins it so that a future change to it
  is deliberate. The
  two constraints the ROM actually imposes are pinned separately, by M-cycle
  count rather than by the ROM's name, in "the power-on phase puts mode 0 at
  1139 M-cycles and LY 0x0A at 1190".
- **What would settle it.** Any one of: a second ROM that reads LY or STAT a
  known number of M-cycles from $0100 with a band that does not contain the
  whole of this one (two such ROMs would intersect to a point); a trace of the
  DMG boot ROM's own last instructions, which would give the phase outright
  rather than bounding it; or a hardware measurement of LY at a known M-cycle
  after $0100. Until one exists, the 49 values are equally supported and only
  the band is evidence.
- **Effect.** Test ROMs 138 -> 139 of 165: the register walk gained and nothing
  lost. SingleStepTests unchanged at 499/500. One screenshot count moved,
  `ashiepaws/bully` from 290 to 346 differing pixels, taking the `screen`
  group's total from 73,572 to 73,628; it is a failing test either way, and the
  screenshot section below records what is known about why. Every other pixel
  count in that section is identical.
- **A unit test inherited the old phase and was repositioned, not relaxed.**
  `tests/test_gameboy.cpp`'s "OAM DMA from VRAM blocks VRAM and OAM, but ROM
  and WRAM stay readable" idles 170 M-cycles from power-on and then expects
  $8000 to be readable. It was reading the power-on phase by accident: with the
  phase moved, those 170 M-cycles land in mode 3 and VRAM is locked by the PPU
  rather than by the DMA the test is about. It now switches the LCD off first,
  exactly as its WRAM-source sibling immediately above it already did for the
  same reason. No assertion in it changed.
- **Checked:** 2026-09-24.

## The VRAM the boot ROM leaves behind: where Pan Docs is silent (2026-09-24)

Not a divergence. Pan Docs documents the behaviour and FourShades now follows
it; what Pan Docs does not give is *where* in VRAM the result lands, and that
had to come from the boot ROM's own published code.

- **Pan Docs:** [Power Up Sequence](https://gbdev.io/pandocs/Power_Up_Sequence.html)
  says "The monochrome boot ROMs read the logo from the header, unpack it into
  VRAM, and then start slowly scrolling it down", and gives the register values
  that go with it - LCDC $91, BGP $FC, SCX $00, SCY $00, all of which this core
  already had. It gives no addresses, no unpacking rule and nothing about the
  background map.
- **Where the rest came from:** the DMG boot ROM's own code, as disassembled at
  [gbdev.gg8.se](https://gbdev.gg8.se/wiki/articles/Gameboy_Bootstrap_ROM).
  $0021-$0032 walks the 48 header bytes at $0104-$0133 with HL starting at
  $8010. The routine at $0095 doubles each of a nibble's four bits into two and
  stores the result twice, two addresses apart, then leaves HL four on - which
  is why only bit plane 0 is written, two tile rows at a time, and the logo
  comes out in colour 1 (black under BGP = $FC). Two header bytes therefore
  fill one tile, and 48 fill tiles $01-$18 from $8010 to $818F, twelve across
  and two down. $0034 copies eight bytes of the boot ROM's *own* data the same
  way - the (R) glyph, `3C 42 B9 A5 B9 A5 42 3C` - into tile $19 at $8190.
  $0040-$0053 writes the background map: $01-$0C at $9904-$990F, $0D-$18 at
  $9924-$992F, and the glyph at $9910, beside the top row. Everything else in
  VRAM stays zero, because the boot ROM clears all of it at $0004 first.
- **Derived from the cartridge, never stored here.** The 48 bytes are read
  through `Cartridge::read`, from the cartridge in the slot, because that is
  where the hardware reads them: the boot ROM unpacks them and only *afterwards*
  compares them against its own copy. A cartridge whose logo area holds
  something else therefore leaves something else in VRAM, and
  `tests/test_gameboy.cpp`'s "the logo in VRAM follows the cartridge, byte for
  byte" pins that. The (R) glyph and the map entries are the boot ROM's data,
  not the cartridge's, and do not move with the header.
- **What is not modelled: the comparison.** A DMG whose cartridge fails the
  logo check hangs inside the boot ROM at $00E9 and never reaches $0100.
  FourShades runs no boot ROM and starts every cartridge at $0100, so the
  post-boot VRAM it gives a cartridge with a wrong logo is a state no DMG would
  ever display. Nothing in the suite depends on the lockup.
- **Effect.** Test ROMs stay at 140 of 165, but eleven screenshot counts moved
  and none got worse:
  - `ashiepaws/bully` stops at neither of its two VRAM subtests now - "Invalid
    initial tile data" and "Invalid initial map data" - and fails a later one,
    "DMA bus conflict always reads $FF", so its count rose from 346 to 421
    differing pixels: a longer message on screen, further into the chain. Both
    messages were read off the frames in `build/frames`, with the seeding
    disabled and then enabled. It still fails, so the suite total does not
    move; what the new subtest wants is not diagnosed here. It was diagnosed
    later the same day and `bully` now passes - see "What a read that conflicts
    with an OAM DMA puts on the bus" below.
  - Nine Mealybug tests and `daid/ppu_scanline_bgp` improved, and the `screen`
    group's total error fell from 50,889 differing pixels to 47,799. The reason
    is visible in the references: `m3_obp0_change` and `m3_lcdc_bg_map_change`
    both draw objects whose tile is $19, the (R) glyph, and neither ROM ever
    writes tile data for it - they were captured on hardware that still had the
    boot ROM's VRAM, and FourShades was drawing those objects from zeros. The
    per-test counts in "Screenshot tests still failing once the pixel pipeline
    was finished" below are the post-change ones.
  - SingleStepTests unchanged at 499/500, and every non-`screen` group
    unchanged.
- **Checked:** 2026-09-24.

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
- **The value a blocked access returns is a different question**, and this
  entry does not answer it: see "What a read that conflicts with an OAM DMA
  puts on the bus" immediately below, decided 2026-09-24. Nothing there
  changes `GameBoy::dmaBlocks` or any of the nine tests above.
- **Checked:** 2026-09-11.

## What a read that conflicts with an OAM DMA puts on the bus (2026-09-24)

Not a divergence, and not a change to the entry above. That entry settled
*which* addresses an OAM DMA blocks. This one settles the separate question of
*what a blocked access returns*, which Pan Docs does not answer at all. The
answer turns out to be different for the two halves of the transfer, and
keeping them apart is what lets both bodies of evidence be satisfied at once.

- **The test:** `ashiepaws/bully.gb`, its last subtest. The ROM is a chain that
  prints the first check it fails; after the post-boot VRAM work of the same day
  it reached this one and printed "DMA bus conflict always reads $FF".
- **What the subtest actually does**, disassembled from the vendored ROM (the
  routine is the last entry of the table at $0E68, so it runs on every model):
  - $0990 writes $C5, $09 to $FF81-$FF82. That is the address of its own
    failure string at $09C5, left where the `rst $38` handler at $0038 ->
    $01FB will find it: the message is printed by the crash path, not by a
    check.
  - It saves SP, points it at $D000, pushes $FFFF twice and pops twice, so
    $CFFA-$CFFD hold $FF and SP is back at $CFFE with the old SP under it.
  - $09A6 starts an OAM DMA **from $0300 - ROM, the external bus** - and then
    runs straight on into its own code at $09AA, in ROM. Every fetch from there
    collides with the DMA.
  - $0300 is therefore not data but a **program**: `ld b,a / xor a / ld c,a /
    push bc / inc a / inc a / inc a / pop de / scf / inc de / ld hl,$FF81 /
    inc a / ld (hl+),a / inc a / ld (hl),a / inc a`, then 142 bytes of $00.
    The `inc a`s and the `scf` are padding: they sit where a multi-M-cycle
    instruction's later M-cycles eat the bytes behind its first, so the stream
    stays aligned to the opcode boundaries.
  - The checks at $0A4B want **A = 1, B = 3, C = 0, D = $13, E = $37**,
    `[$FF81] = [$FF82] = 1`, and `[$CFFC] = [$CFFD] = $FF` on DMG. Every one of
    those is produced by executing that program: `ld b,a` takes the $03 left in
    A by the write to $FF46 (B = 3); `push bc`'s two writes are discarded, so
    $CFFC-$CFFD keep their $FF; `pop de`'s two reads land on the discarded
    words and return the *next two source bytes*, $37 and $13; `ld hl,$FF81`
    takes its operand from the source too; the two stores to HRAM land, because
    HRAM is never blocked. The 142 trailing $00 are nops that walk PC through
    a nine-nop sled at $0A42 and into the checks - the test times itself by
    counting the DMA's own bytes.
- **So the subtest asks for exactly one thing:** while a DMA is copying, a CPU
  read of the bus the DMA is reading returns **the byte the DMA is transferring
  in that M-cycle**, whatever address the CPU named. The failure message names
  the alternative it rejects, and what FourShades did: always $FF. Reading $FF
  makes the fetch at $09AB a `rst $38`, the crash handler prints the string in
  $FF81-$FF82, and that is the frame that was on screen.
- **Pan Docs:** silent on the value.
  [OAM DMA Transfer: OAM DMA bus conflicts](https://gbdev.io/pandocs/OAM_DMA_Transfer.html#oam-dma-bus-conflicts)
  says which regions the CPU may reach ("On DMG, during OAM DMA, the CPU can
  access only HRAM") and nothing about what a read of the others yields - it
  never mentions $FF. The same page does state the matching case for the *other*
  master on the bus, and it states it the way this decision goes: "If OAM DMA is
  active during rendering (mode 3), the PPU reads whatever 16-bit word the DMA
  unit is writing to OAM when the object is fetched." So there is no Pan Docs
  sentence to overrule here; there is a gap, and a neighbouring sentence that
  points the same way.
- **Corroborating documentation:** GBEDG, by the same author as the ROM,
  [DMA Transfers](https://github.com/Ashiepaws/GBEDG/blob/master/dma/index.md):
  "When the CPU attempts to read a byte from ROM/RAM during a DMA transfer,
  instead of the actual value at the given memory address, the byte that is
  currently being transferred by the DMA transfer is returned", with "Writes to
  ROM and RAM are completely ignored", "The HRAM section of memory is unaffected
  by DMA Transfers", and "This also affects the CPU when fetching opcodes,
  allowing for code execution through DMA transfers". Its claim about *which*
  regions conflict is the same over-broad one the 2026-09-11 entry above already
  overruled with nine hardware-verified tests; its claim about the *value* is
  the one adopted here.
- **Why this does not disturb the 2026-09-11 decision, and could not.** The nine
  hardware-verified timing ROMs each run their DMA from **$8000, the video
  bus**, and read **OAM**. `ret_timing.s` shows they distinguish the two answers
  on purpose: round 1 writes $20 to $8000 and $80 to $FDFF, points SP at
  $FDFF-1 and times a `ret` so that its high-byte pop lands in OAM in the
  M-cycle that copies the transfer's last byte. If that pop returns $FF, PC
  becomes $FF80 and the HRAM stub there clears A; if it returned the byte under
  the DMA it would become $2080, where the ROM has planted `ld a,$01`. The test
  requires the first, on DMG, MGB, SGB, SGB2, CGB, AGB and AGS. Round 2 then
  repeats it one M-cycle later, after the DMA, and requires the *real* OAM byte.
  So an OAM read during a DMA must be $FF and must not leak the source.
- **Decision (2026-09-24).** The two halves are different mechanisms and are
  modelled as such, in `GameBoy::dmaConflictValue`:
  - **The source bus is a conflict.** The DMA's read and the CPU's read reach
    the same bus in the same M-cycle, the memory answers once, and both latch
    the same byte. A blocked read there returns the byte the DMA is
    transferring. `GameBoy::tickDma` keeps it in `dmaCurrentByte_`.
  - **OAM is a lock-out.** The DMA owns the destination outright and the CPU is
    shut out of it, exactly as the PPU shuts it out in modes 2 and 3, and a
    shut-out OAM read reads $FF - as it already did.
  - Writes are unchanged: a blocked write is discarded, and HRAM, I/O and IE are
    never blocked. `dmaBlocks` is untouched, so which addresses are blocked is
    exactly what 2026-09-11 decided.
- **Evidence that the split is load-bearing, not a convenience.** A throwaway
  build that dropped the OAM case and let every blocked read return the source
  byte took `cpu & interrupts` from 31 of 31 to **22 of 31** - precisely the
  nine ROMs of the 2026-09-11 entry - and `oam dma` from 6 of 6 to 3 of 6, and
  failed four unit assertions. A build that took the byte one M-cycle ahead
  (`peek(from + 1)`) failed 17 unit assertions, `oam dma` fell to 2 of 6, and
  `bully` failed at 248 pixels. The pre-change behaviour, $FF everywhere, is
  the red run the two new unit cases were written against: 16 assertions.
- **Effect.** `ashiepaws/bully.gb` goes from 421 differing pixels to **exact**.
  Test ROMs 140 -> **141 of 165**, `screen` 5 -> 6 of 30. A full run diffed
  test by test against the previous one shows **exactly one** changed verdict
  and not one other pixel anywhere in the suite. SingleStepTests unchanged at
  499 of 500 (the deliberate STOP-length divergence at the top of this file).
- **Checked:** 2026-09-24.

## `ashiepaws/strikethrough`: an OAM DMA that outruns the object scan (2026-09-24)

Diagnosed and **left failing**, at the 53 differing pixels it has had since the
pixel pipeline was finished. It is not a mid-scanline register question at all:
it is a race between an OAM DMA and the PPU's mode-2 object scan, and closing it
needs two things - one that Pan Docs states and FourShades does not implement,
and one that nothing in this repository determines.

### What the ROM does

Disassembled from the vendored ROM (header title `STRIKE`).

- $0150 waits for LY >= $90, blanks LCDC, sets BGP = OBP0 = OBP1 = $FC, copies an
  eight-byte OAM-DMA stub to $FF80, fills the tile map with tile $FF, clears
  tiles 1-$20, loads tile 0 from $094A and a font (tiles $21-$7A) from $026A, and
  prints "Everything is OK!" at $9902 - tile map row 8, so screen rows 64-71.
- **Tile 0 is `FF FF 00 00 ...`:** colour 3 across the whole of its row 0 and
  nothing else. One object drawn with it is an eight-pixel bar exactly one
  scanline high - the strike-through the ROM is named for.
- $01DB builds forty objects at $C000: **Y = $54 for every one of them**, which
  is screen row 68; tile 0; attributes 0; and X = $17 + 8i taken mod 256 - so
  X = 23, 31, ... 255 and then 7, 15, ... 79. X = $4F (79) occurs twice, at
  i = 7 and at i = 39. It DMAs that into OAM, sets LCDC = $93, STAT = $40,
  LYC = $43 (67), IE = $02 and enables interrupts.
- The main loop at $0209 copies a **second** table (ROM $08AA) into $C000 - every
  object at Y = 1 except the first at Y = 99, all with tile 1, which the ROM has
  blanked, and X = 1 - and halts. The LYC = 67 STAT handler at $0230 polls STAT
  until line 67 reaches mode 0, spends 28 `nop`s there, starts an OAM DMA from
  $C000 and returns. The loop then waits for mode 1 and DMAs the first table
  back.
- So OAM carries the forty bar objects for the whole visible frame, and an OAM
  DMA that replaces them with off-screen ones **starts in line 67's HBlank and is
  still copying all the way through line 68**. Line 68 is the only line any of
  the forty can appear on.

### What the reference shows, and what FourShades draws

The background of row 68 was reconstructed from the ROM's own font, string and
tile map and compared with the reference pixel by pixel: it accounts for every
reference pixel on that row **except x = 71-78**, a solid eight-pixel run of
shade 3 that is not on a tile boundary and so cannot be background. An
eight-pixel bar at screen x 71 is an object at OAM X = 79.

- **The DMG reference draws exactly one** of the forty objects, the one at
  OAM X = 79 (index 7, or index 39, which carries the same X).
- **FourShades draws ten**, at x = 63-142: objects 6-15, the first ten whose Y
  still read $54 when the scan ran.

### Why, measured

A temporary trace of every DMA byte against the PPU's line and dot (added,
measured, reverted) gives the timeline exactly: the DMA's **first byte lands at
line 67 dot 452**, and bytes 1-20 land at line 68 dots 0-76, one every four dots.
At line 68 dot 80 OAM holds objects 0-4 replaced, object 5 **half** replaced (new
Y, old X) and objects 6-39 untouched.

`Ppu::scanOam` runs once, at line dot 80 - the end of mode 2 - and evaluates all
forty entries in that one instant. Hardware reads one object every two dots
across mode 2's eighty. That is a real structural divergence, and it is what puts
the frontier at object 6.

**It is not the divergence that matters here, and correcting it alone makes the
picture worse.** A per-object scan (one object every two dots, wired up as a
throwaway experiment and reverted) selects objects 1-10 instead - x = 23-102,
**49** differing pixels - and changes nothing else: the `screen` group stays at
14 of 30 with every other test's pixel count identical to the pixel.

The reason neither answer can be right is structural. The DMA advances one object
every sixteen dots and a scan advances one every two, so the scan always outruns
the DMA: **once an object passes the Y test every later object passes too**,
whatever the phase. Thirty-four objects still carry Y = $54 when line 68 is
scanned, and any monotone rule takes the first ten of them. A reference with
exactly one object in it therefore requires a mechanism that suppresses objects
the scan *did* select.

### The mechanism Pan Docs states and FourShades does not implement

[OAM DMA Transfer: OAM DMA bus conflicts](https://gbdev.io/pandocs/OAM_DMA_Transfer.html#oam-dma-bus-conflicts):
"If OAM DMA is active during rendering (mode 3), the PPU reads whatever 16-bit
word the DMA unit is writing to OAM when the object is fetched."

FourShades' object fetch takes the tile index and the attributes from the entry
`scanOam` buffered and never looks at OAM again, so an in-flight DMA cannot reach
it. That sentence is not implemented at all. (The entry above,
"What a read that conflicts with an OAM DMA puts on the bus", quotes the same
sentence as corroboration for the *CPU* side and does not implement the PPU side
either.)

What implementing it alone would buy, **computed and not measured**: each of the
ten objects would take its tile from the DMA's source stream, which is $01
everywhere but one byte, and the ROM has blanked tile 1 - so no object would
draw, row 68 would be the plain background, and the count would fall from 53 to
**7**, the reference's surviving bar minus the one text pixel it covers at
x = 75. It does not make the ROM pass.

The one byte of that source table which is not $01 is **offset 46 - object 11's
tile byte - and it is $00**, which is tile 0, the bar. That is almost certainly
the ROM's marker, and the question it asks is which object's fetch collides with
it. FourShades transfers byte 46 at line 68 dot 180, and its object fetches read
their rows on dots 161, 169, 177, 185 ...; that byte's M-cycle (dots 180-183)
falls between two of them, so under this model no object would pick the byte up
at all. Putting the survivor at OAM X = 79 needs the fetch that reads OAM to land
inside that M-cycle, eleven to fourteen dots later than where this pipeline puts
it. **Nothing here derives that dot**, and no other ROM in the suite measures it.

### The decision, and what would settle it

Left failing at 53 pixels. Shipping the documented half on its own would replace
ten wrong objects with none, still fail, and add a PPU-to-DMA coupling that
nothing else in either suite exercises - the same trap as "Group E, measured to
the dot and not solved" below, where sixteen exact constraints have a unique fit
that would fit six blocks and break ten.

What would settle it, in the order it is worth trying:

1. **The dot an object fetch reads its OAM word on**, measured by something other
   than this ROM. The object fetch's other dots were pinned on 2026-09-24 (see
   "An object fetch waits for the pixel it pre-empts, and reads its row two dots
   before it"); its OAM read has never been placed, because nothing but an
   in-flight DMA can observe it.
2. **What the mode-2 scan sees while an OAM DMA is copying.** Pan Docs' sentence
   is about mode 3 only, and a scan that reads real OAM is what makes the
   selection monotone. If a scan read during a DMA also returned the DMA's word,
   no object would be selected on line 68 at all and the survivor would have to
   come from the fetch.
3. **The reference's provenance.** It is one image, fetched from
   `gbdev/GBEmulatorShootout` with the ROM; nothing in this repository states
   whether it is a photograph.

- **Checked:** 2026-09-24.

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
  - **Settled on 2026-09-24, and that reading was right about the mechanism and
    wrong about who pays.** A Mealybug Tearoom reference measures the same three
    dots from the pixel side and finds them *not* taken: the pixels pay Pan
    Docs' sum in full. The three dots are the fetcher's, and mode 3 ends with
    the fetcher. The sum this entry describes is no longer subtracted in
    `objectPenalty`; it is subtracted in `dotsRemaining` instead, which leaves
    every one of the 104 cases' figures exactly as they were. See "An object
    fetch costs the pixels three dots more than it costs the fetcher and mode 3"
    immediately below, which supersedes this bullet and nothing else in this
    entry.
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

## An object fetch costs the pixels three dots more than it costs the fetcher and mode 3 (2026-09-24)

Not a divergence: a timing model, and the answer to the "what is not settled"
bullet in the entry above. Two bodies of hardware evidence measure the same
object fetch from opposite sides and disagree by exactly three dots. Both are
satisfied, and neither is bent, only if the fetch costs the pixel stream three
dots more than it costs the background fetcher.

### The two measurements

- **The pixels: Pan Docs' sum in full.** Mealybug Tearoom's
  `m3_bgp_change_sprites` puts one object on every scanline - OAM X = 1 on lines
  0-7, X = 2 on lines 8-15, and so on up to X = 18 - and rewrites BGP at four
  known dots per line over a background that is entirely colour 0. Every pixel
  therefore shows the BGP in force on the dot it was drawn, and the seams say
  which pixel that was. Its sibling `m3_bgp_change`, which has no objects and
  the same handler shape, matches FourShades to the pixel, so the dots the
  writes land on are not in question.
  - Before this change FourShades' seams were **three pixels further along the
    line than the reference's, on all eighteen line blocks at once** - a uniform
    offset, independent of the object's X, so not the tile term and not the
    warm-up. Reading each block's seam back as a dot gives the penalty hardware
    charged the pixels: 10 dots at OAM X = 1, 9 at X = 2, 8 at 3, 7 at 4, 6 at
    5, 6 and 7, 11 at 8, 10 at 9 ... 11 at 16, 9 at 18. Those are Pan Docs' OBJ
    penalty algorithm exactly - flat 6 plus the tile term - **with no rebate**,
    at all eighteen values.
- **Mode 3: three dots less.** The hardware-verified
  `intr_2_mode0_timing_sprites` measures mode 3's length across 104 cases and
  wants that sum minus three, once per scanline. The entry above shows that no
  other constant fits it.

### The model, and what it predicts

The line's first object fetch stalls the pixel stream for Pan Docs' full sum,
and the background fetcher for three dots less: `PixelPipeline`'s
`kObjectFetcherLead`. The fetcher runs on through the stall's first three dots
(`objectLeadDots_`), the pixels it gains are carried in the FIFO
(`fifoLead_` - the hardware FIFO is sixteen pixels deep and has room for them
where a bare eight-pixel queue would not), and mode 3 ends with the fetcher
rather than with the last pixel, so `dotsRemaining` takes the three dots off
there instead. The last pixels of such a line reach the LCD three dots further
into HBlank than `kRenderLag` alone says.

That is one constant doing three jobs, and each of them is measured:

| what the lead does | what measures it |
| --- | --- |
| the pixels pay the full sum | `m3_bgp_change_sprites` (exact after this change), `m3_obp0_change` (exact), `m3_lcdc_bg_en_change` 855 -> 376 |
| the fetcher keeps three dots | `m3_scy_change` 661 -> 259, `m3_lcdc_tile_sel_change` 534 -> 410, `m3_lcdc_bg_map_change` 182 -> 124, `m3_scx_high_5_bits` 45 -> 12 |
| mode 3 ends with the fetcher | `intr_2_mode0_timing_sprites`, unmoved at 1/1 |

The three are separable and were separated. Each row below is a mutation run on
a forced rebuild with both executables' hashes recorded, against the filtered
unit set (`test_pixel_pipeline`, `test_objects`, `test_stat`, `test_ppu`,
`test_screen`: 105 cases) and a full ROM run:

| mutation | unit cases | test ROMs | `screen` pixels |
| --- | --- | --- | --- |
| the object fetch starts on the line's first rendering dot again | 5 | 146 | +2,333 |
| the fetcher is frozen for the whole stall (no lead) | 5 | 149 | +7,812 |
| the pixels are charged the rebated sum | 6 | 146 | +11,324 |
| mode 3 ends with the pixels, not the fetcher | 4 | **148** | 0 |
| the object's row is read when the fetch is triggered | 1 | 149 | +60 |
| the row read one dot later (`kObjectDataDots` = 1) | 1 | 149 | +40 |
| the row read one dot earlier (= 3) | 1 | 149 | +20 |
| LCDC bit 1 cannot cancel a fetch in flight | 1 | 149 | 0 |
| a push waits for a bare queue again (no FIFO lead) | **0** | 149 | +2,351 |
| the window's colour-0 pixel needs a bare queue again | **0** | **148** | +1 |

The clean tree was re-measured through the same harness before and after -
105/105 and 149/165 - and the restore was verified behaviourally as well as by
diff, since MSVC output is not bit-reproducible. The last two rows are the two
mutations no unit case catches; the last of them is caught by a verdict
(`m3_wx_4_change_sprites`, which the whole-row push rule below is there for) and
the other by nothing but pixel counts, which is recorded at the end of this
entry rather than guarded.

### What is still not explained

Why *three*, and why mode 3 follows the fetcher rather than the pixels. Pan
Docs' dot-by-dot walk of the object fetch has a fetcher advancement at each end
of it and calls one of them free when the line is over ("this advancement
lengthens mode 3 by 1 dot **if** the X coordinate of the current scanline is not
160"), which is the right shape for a fetcher that ends the line ahead of the
pixels, but it does not add up to three. What is claimed here is only that one
constant in one place satisfies both hardware sources at once, where the old
model satisfied the timing ROM and missed every picture with an object in it by
three pixels.

- **One measured rule had to be restated, not changed: the window's colour-0
  pixel.** "A WX changed while the window is drawing pushes one colour-0 pixel,
  and only onto an empty FIFO" (below) was measured when the FIFO was bare
  exactly on the dot a row went in. With the lead it is never bare on a line with
  an object, so "empty" stops picking out that dot and the rule has to be said in
  terms of what it always meant: the push lands only when the pixel the FIFO is
  about to hand over starts a row, i.e. when the queue holds whole rows
  (`queueSize_ % 8 == 0`). On every line without an object that is *identical* to
  the old test, and on a line with one it is what keeps `m3_wx_4_change_sprites`
  exact - the loose reading, "as much room as the fetcher's own push needs",
  inserts a pixel on a line hardware does not and still misses the one it does.
- **Also left open: the FIFO's depth is modelled only as far as the lead needs.**
  `queue_` is eight pixels plus `kObjectFetcherLead`, not the hardware's
  sixteen, and `tryPushRow` takes a row when the queue is down to the lead
  rather than Pan Docs' "only if it's empty" (which it still is on every line
  without an object). A full sixteen-pixel FIFO would be a different model of
  the same measurement; nothing here distinguishes them, because the lead is
  the only thing that ever occupies the extra space. Making the push wait for a
  bare queue again is caught by **no** unit case and **no** verdict - only by
  2,351 differing pixels across four references - which is recorded rather than
  guarded.

- **Effect:** test ROMs 147 -> **149 / 165** (`m3_bgp_change_sprites` and
  `m3_obp0_change` gained, nothing lost); `screen` 12 -> 14 / 30 and its
  differing pixels 15,907 -> 11,547. `ppu timing` 12/12,
  `intr_2_mode0_timing_sprites`, `m3_bgp_change`, `m3_scx_low_3_bits`,
  `dmg-acid2` and `oam bug` 7/7 all unmoved.
- **Checked:** 2026-09-24.

## An object fetch waits for the pixel it pre-empts, and reads its row two dots before it (2026-09-24)

Not a divergence: Pan Docs' pixel-FIFO page walked dot by dot instead of as a
lump penalty. Three separate things come out of it, and each is measured.

**1. The fetch waits.** Pan Docs: "the fetcher is advanced one step until it's
at step 5 **or until the background FIFO is not empty**". An object fetch needs a
pixel to pre-empt, so the line's first object is fetched on the dot the first row
reaches the FIFO - line dot 100 - and not on the line's first rendering dot,
which is where FourShades fetched it until now. Mode 3's length is identical
either way, which is why no timing ROM ever saw it, but the reads are not: the
old dot pushed the whole twelve-dot warm-up behind the stall and moved every
register read on the line with it. The first tile of `m3_scy_change`'s lines came
out as the reference's *previous* line because of it. `fetchStallDots() == 0` is
the fetcher's own statement that the row arrives on this dot, so the wait needs
no new state.

**2. The fetch reads its row two dots before that pixel, not when it is
triggered.** Pan Docs ends the fetch with "the lower address for the row of
pixels of the target object tile is now retrieved and lengthens mode 3 by 1 dot.
Once the address is retrieved this is the last chance for object fetch cancel to
occur. Exiting object fetch lengthens mode 3 by 1 dot" - the address, then one
more dot, then the pixel. `kObjectDataDots` is that 2, and it is measured rather
than counted: 1 costs `m3_lcdc_obj_size_change_scx` 40 pixels, 3 and 4 cost
`m3_lcdc_obj_size_change` 20, and 5 costs it 60. Two unit cases pin it from both
sides, the second of which needs a second object on the line to break the
M-cycle grid.

**3. LCDC can change under the fetch.** Everything the address is built from is
read on that dot: the object's height from LCDC bit 2, its tile, its row. A write
that lands between the trigger and the read therefore changes the object's height
mid-fetch, which is what `m3_lcdc_obj_size_change` and its `_scx` sibling
photograph (410 -> 310 and 270 -> 190). And Pan Docs: "Object fetching may be
canceled if LCDC.1 is disabled while the PPU is fetching an object from OAM" - so
bit 1 going low before the read drops the object's pixels while keeping its dots,
since the same page has the cancel lengthening mode 3 too.

- **The cancel is implemented and nothing in either suite measures it.** A unit
  case pins it - bit 1 cleared across the middle of a fetch and set again before
  the pixel is drawn, so the emission-time test cannot account for the result -
  but disabling the cancel entirely moves **no** ROM by a single pixel. The two
  ROMs that clear bit 1 mid-line, `m3_lcdc_obj_en_change` and its `_variant`,
  leave it low long past the dot the pixels are drawn on, so
  the emission-time test hides those objects with or without the cancel. This is
  recorded because it is a behaviour the suite does not arbitrate: it is here
  because Pan Docs documents it, not because a test demanded it.
  **Correction, 2026-09-24:** this bullet used to say both ROMs leave bit 1 low
  "for twenty M-cycles". `m3_lcdc_obj_en_change` does - it writes $81 at line dot
  108 (112 on lines 64-143) and $83 again at 204 - but the `_variant` writes $83
  again at 124, sixteen dots later, not twenty M-cycles. Both write-dot figures
  are traced; see "The LCDC bits that choose a pixel's colour are read one dot
  before the palette shades it" below. The conclusion is unchanged and was
  re-measured after that dot moved: disabling the cancel entirely still moves no
  ROM in the 165 by a single pixel, because the emission-time test hides the
  objects either way. What the `_variant`'s shorter pulse means for the cancel on
  each band was not worked out, and does not need to be for that.
- **Effect, and who gets the credit.** The start dot is worth 2,333 differing
  pixels on its own, `m3_scy_change` and the two window-fetch ROMs among them.
  The four object-LCDC ROMs improve too - `m3_lcdc_obj_en_change` 100 -> 56,
  `_variant` 532 -> 152, `m3_lcdc_obj_size_change` 410 -> 310, `_scx` 270 ->
  190 - but that is mostly the fetch's *dots* (the entry above) rather than these
  two reads: of the four, only the two size ROMs respond to the read dot at all,
  by the 20-60 pixels the sweep above quotes, and the cancel moves none of them.
- **Checked:** 2026-09-24.

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
- **What measures mode 3's length, and what does not (re-derived 2026-09-24).**
  The length is `dotsRemaining`'s whole output and STAT's mode field is the
  only place it appears, so an error of one, two or three dots is invisible in
  every picture *and* in every unit case that reads `Ppu::mode()` between
  ticks - `Ppu::tick` is four dots and mode 3 begins on a multiple of four, so
  a raw length of 177, 178, 179 or 180 all read as 180. That made the
  arithmetic the one part of the window work that could be wrong silently, and
  it was checked by measuring, not by reading:
  - **The terms.** Mode 3 is `Ppu::kMinDrawDots` (172) + SCX % 8 + six dots
    per window activation on the line + the object penalties. The three
    lengthening terms are independent and additive, and there is nothing else
    in it.
  - **What the ROM suite measures, established by mutation.** Making every
    line's count one dot long breaks seven of the twelve `ppu timing` ROMs:
    `hblank_ly_scx_timing-GS`, `intr_2_0_timing`, `intr_2_mode0_timing`,
    `intr_2_mode0_timing_sprites`, `intr_2_oam_ok_timing`, `lcdon_timing-GS`
    and `lcdon_write_timing-GS`. Dropping the SCX fine-scroll discard breaks
    `hblank_ly_scx_timing-GS` (and two pictures, `dmg-acid2` and
    `m3_scx_low_3_bits`). Dropping the object penalties from the count breaks
    `intr_2_mode0_timing_sprites` and nothing else. So the 172, the SCX term
    and the object term each have hardware-verified ROMs behind them.
  - **The window's term has no ROM behind it at all.** Charging an activation
    five dots instead of six, or seven instead of six, or not charging a
    pending activation while it is still to come, or narrowing the predicate
    from `>=` to `>`, or moving the WX bound by one - every one of those
    leaves the ROM suite at exactly 147 / 165, with the same differing-pixel
    count in every `screen` test. The window's six dots are derived from the
    restart the fetcher actually performs (`kWindowRestartDots`: Tile,
    DataLow and DataHigh at two dots each before Push can run again), and
    they are held by unit tests only.
  - **How the unit tests get below an M-cycle.** SCX's low three bits
    lengthen the line by one dot each and do it independently of the window,
    so running one scenario at each of SCX 0 to 7 gives eight whole-M-cycle
    readings that step from one multiple of four to the next at the two SCX
    values where the raw length crosses one. Exactly one raw length fits all
    eight. `tests/test_pixel_pipeline.cpp`'s `solveRawDots` asserts that there
    is exactly one and returns it, which makes every figure in the cases under
    "Mode 3's length, derived to the dot" an exact dot count rather than a
    rounded one. It also pins the SCX term itself: a term of two dots per bit,
    or one that saturated, leaves no length fitting all eight readings.
  - **Where the size of the pending charge shows, and where it cannot.**
    `dotsRemaining` charges an activation it can see coming six dots, and that
    charge only decides the boundary when the boundary would otherwise be
    decided before the activation fires - which needs `160 - pixelX_ + 6` to be
    down at the line's render lag while the trigger is still ahead of the
    counter. On line 0 the lag is three dots, and there it never happens for
    any WX: the activation has always fired first. On an ordinary line the lag
    is seven and the two cross at WX = 165 and WX = 166 only. Every other
    window figure in `tests/test_pixel_pipeline.cpp` is measured on line 0, so
    before this was checked the *size* of the charge was measured by nothing
    at all - five dots, six or seven all passed 474 unit cases and all 165
    ROMs. "The charge for an activation still to come is exactly six dots, and
    only an ordinary line measures it" is the case that closes it.
  - **Two activations on one line cost twelve dots**, one restart each, with
    the stop in between costing nothing. Nothing in either suite measures
    that length either; it is the six-dot restart applied twice.
  - **Outcome.** The arithmetic was found correct, to the dot, in every case
    listed: no line of `dotsRemaining` changed. What this task added is the
    ruler and eight mutations that the ruler catches and the ROM suite does
    not.
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
- **Checked:** 2026-09-24, the whole of the mode-3 length arithmetic re-derived
  from the pipeline and left unchanged; see the coverage bullet above.

## `daid/ppu_scanline_bgp`'s twelve dots are three M-cycles of interrupt latency, not the render lag (2026-09-24)

The entry above rests on `m3_bgp_change`, and the one ROM that contradicts it is
`daid/ppu_scanline_bgp`, by a uniform twelve dots over the whole image. The
decision - follow the Mealybug photographs - has not changed. What has changed is
that the twelve dots have been traced, and they are **not in the pixel pipeline
at all**: they are three M-cycles in a once-a-frame chain that `m3_bgp_change`
does not exercise, and every other link of which is pinned by a hardware-verified
Mooneye ROM that FourShades passes. The previous version of this said the
disagreement was "a question about the ROM's synchronisation, not about the
pipeline" and that it "has not been diagnosed"; that guess was right, and this is
the measurement.

### What the ROM does

Disassembled from the vendored ROM.

- $0150 waits for LY = $91, writes $01 to $9000, $9002 ... $900C and $FF to
  $900E (tile 0, under LCDC bit 4 = 0's $8800 signed addressing), then
  LCDC = $81, STAT = $40 (**LYC interrupt only**), IE = $03, LYC = 0, `c` = $47
  (so `ldh [c],a` writes BGP), `ei`, `halt`.
- The VBlank vector reaches $0180: `pop hl` - which throws the return address
  away - `ei`, `halt`.
- The STAT vector reaches $0184: `pop hl`, `ei`, `nop`, `ld hl,$01E7`, then **ten**
  repetitions of `ld a,[hl+]` ; `ldh [c],a`, then 70 `nop`s, then `jp $018A` -
  back to the ten writes, without reloading `hl`. That is
  10 x 4 + 70 + 4 = **114 M-cycles = 456 dots, exactly one scanline**, so the ten
  BGP writes fall on the same dots of every line and walk a 1,440-byte table from
  $01E7 at ten bytes per line. The table holds only $E4, $AA, $55 and $00, laid
  out as nested rectangles, so a line has at most a few seams in it.
- **The loop is never re-synchronised per line.** It is interrupted once a frame
  by the VBlank interrupt at LY = 144, whose handler halts, and re-entered at
  LY = 0 by the LYC = 0 STAT interrupt. The horizontal position of the whole
  144-line image is therefore set by one event chain, once a frame.

### What FourShades does, measured

From a temporary trace of every BGP write against the PPU's line and dot (added,
measured, reverted):

- On **all 144** visible lines the ten writes occupy the M-cycles beginning at
  line dots 80, 96, 112, ... 224 - four M-cycles apart - so each becomes visible
  to the PPU on dots 84, 100, 116, 132, 148, 164, 180, 196, 212 and 228. The
  first-write dot is 80 on every one of the 144 lines, and the phase changes only
  across VBlank, which is the re-synchronisation above.
- Pixel *p* leaves the PPU on dot 100 + *p*, so those writes land on pixels -16,
  0, 16, 32, 48, 64, 80, 96, 112 and 128. On line 100 the produced frame's seams
  start at x = 1, 18, 50, 82, 114 and 130; the one-dot palette short accounts for
  the +1 and +2.
- `ppu_scanline_bgp_2.dmg.png`, the closest of the three references, puts them at
  x = 13, 29, 61, 93, 125 and 141. The other two give 14, 30, 62, 94, 126 and
  13, 30, 62, 94, 126. On line 8, where only two of the ten writes change the
  value, FourShades puts the seams at x = 17 and 114 and all three references put
  them at 29 and 125-126.
- So the disagreement is **a uniform twelve dots - three M-cycles - with the
  reference later**, and the *spacing* agrees exactly: sixteen dots per write,
  thirty-two between the two seams the nested boxes produce. Nothing about the
  rate disagrees. Only the phase does, and it is set once a frame.

(The older text here said FourShades lands the writes "on line dots 100, 108,
116, 124 and so on". That was wrong by a factor of two - each write is
`ld a,[hl+]` plus `ldh [c],a`, four M-cycles, so they are sixteen dots apart, not
eight - and it is corrected above. The twelve dots and the first seam at pixel 1
were right.)

### Where the three M-cycles are

Working back from the first write's M-cycle, which begins on line 0 dot 80,
through `ldh [c],a`'s first M-cycle, `ld a,[hl+]` (2), `ld hl,nn` (3), `nop` (1),
`ei` (1), `pop hl` (3) and the five-M-cycle dispatch, puts the interrupt's
dispatch at **line 0 dots 16-35**. The references need it at **dots 28-47**.

So the twelve dots sit in the chain *LY becomes 0 -> the LYC = LY STAT interrupt
is requested -> the CPU leaves `halt` -> the five-M-cycle dispatch -> the
handler's prologue*. Not one link of that is the pixel pipeline.

**`m3_bgp_change` exercises none of it.** It re-arms from the **mode-2** STAT
interrupt on every line, with the CPU running NOPs rather than halted, and never
uses the LYC source. The two ROMs share exactly two things: the dispatch's length
and the dot a pixel leaves the PPU. So the twelve dots are not, and cannot be,
evidence about the seven-dot render lag - which is what the old wording implied by
noting that they had been five dots before the lag was added. (They had: pixel 0
moved from dot 93 to dot 100, which moves a fixed write dot seven pixels down the
line, so a five-dot offset became twelve. That is arithmetic about this ROM's
phase, not a second measurement of the lag.)

**And every other link is pinned by a hardware-verified Mooneye acceptance ROM
that FourShades passes:**

| link | ROM | status |
| --- | --- | --- |
| the LYC = LY STAT interrupt's dot | `ppu/stat_lyc_onoff` | passing |
| leaving `halt` with IME set | `halt_ime1_timing`, `halt_ime1_timing2-GS`, and `di_timing-GS`, which the "Timing model" section at the end of this file cites for the same rule | passing |
| the dispatch itself | `intr_timing`, `ppu/intr_1_2_timing-GS`, `ppu/intr_2_0_timing` | passing |
| the dot a pixel leaves the PPU | Mealybug `m3_bgp_change` (DMG photograph) | exact |

`ppu timing` is 12 of 12 and `cpu & interrupts` is 31 of 31. There is no
three-M-cycle hole left in the model for this ROM's twelve dots to occupy: they
contradict a hardware photograph **and** at least five hardware-verified ROMs at
once.

### The decision, and what would settle it

**Unchanged: left failing, at 7,186 differing pixels** against the closest of its
three references (7,741 and 7,640 against the other two). Under the rule at the
top of this file, Mealybug's photographs and Mooneye's hardware-verified ROMs
outrank an image whose provenance is not stated - and here they outrank it
together and unanimously, which is a stronger position than the previous version
of this entry claimed.

That the three references differ from each other by a pixel is the harness's
encoding of alternative accepted outputs (`tools/roms/RomRun.cpp` passes on a
match to any of them and reports the smallest difference), not a discovery about
them; none of the three carries provenance, and they agree with each other about
the twelve dots, so the figure is not an artefact of which one was picked.

What would overturn it:

- a DMG photograph of this ROM **with stated provenance** that agrees with its own
  references, which would make the two bodies of evidence equally hardware-backed
  and force the three M-cycles to be found rather than attributed to the ROM;
- a demonstration that one of the five Mooneye ROMs above passes through
  compensating errors - for instance that the LYC interrupt's dot and the `halt`
  exit are each wrong by an M-cycle and a half in opposite directions;
- or a ROM other than this one that measures the *halt -> LYC interrupt ->
  handler* chain against the LCD rather than against the serial port. Nothing in
  the 165 does.

- **Checked:** 2026-09-24.

## The background fetcher is five steps over eight dots, and the dot that leaves over (2026-09-24)

Not a divergence: a structure FourShades now implements. It is recorded because
Pan Docs gives the five steps but not which of their chances to push an
undisturbed line uses, and because the arithmetic that settles that leaves one
dot over which Pan Docs does not account for.

- **Evidence, Pan Docs, [Pixel FIFO](https://gbdev.io/pandocs/pixel_fifo.html):**
  "The pixel fetcher has 5 steps. The first four steps take 2 dots each and the
  fifth step is attempted every dot until it succeeds" - Get tile, Get tile data
  low, Get tile data high, Sleep, Push - and Get Tile Data High "also pushes a
  row of background/window pixels to the FIFO. This extra push is not part of
  the 8 steps, meaning there's 3 total chances to push pixels to the background
  FIFO every time the complete fetcher steps are performed."
- **What was here before.** Four steps over six dots: Get tile, both bitplanes
  at two dots each, and then a Push step that took a dot of its own and was
  retried until the FIFO emptied. Throughput and mode 3's length were right -
  eight pixels per eight dots either way - but the dot each stage read its
  register on was not, and it was not even consistent along a line. Writing P
  for the dot a tile's first pixel is drawn on, the line's first drawn tile read
  its index, low and high bytes on P-5, P-3 and P-1, and every tile after it on
  P-6, P-4 and P-2, because those fetches spend a dot on a push the FIFO
  refuses. A register write landing inside a fetch therefore had two different
  answers on one line depending on which tile it caught.
- **What it is now.** The five steps, the first four of two dots each, with the
  push attempted at the end of Get Tile Data High, again on each Sleep dot, and
  then every dot at the Push step. The chance at Get Tile Data High is the one
  an undisturbed line uses, every time: a row feeds eight pixels and a complete
  fetch is eight dots, so the FIFO empties exactly as that step completes. So
  the tile's first pixel is drawn on the dot its high bitplane is read, its low
  bitplane two dots earlier and its index two before that - **P-4, P-2 and P,
  for every fetch on the line**, the first one included.
- **The other two phases are ruled out, not chosen against.** A push only at
  step 5, after the eight dots of steps 1-4, leaves one dot per tile with an
  empty FIFO and nothing to emit: 160 pixels would take 180 dots, and mode 3 is
  measured at 172. A push that lands on a Sleep dot in the steady state cannot
  be reached from the top of a line, because the FIFO is empty when Get Tile
  Data High completes there and that chance cannot be refused.
- **The dot that is left over.** Twelve dots of rendering pass before a line's
  first push. That is not a choice: mode 3's length is 160 plus that number
  (`dotsRemaining` counts one dot per pixel not yet emitted, so the whole of the
  warm-up is inside it) and pixel 0's dot is the render lag plus it, so the
  hardware-verified 172-dot minimum and the Mealybug references' pixel 0 on line
  dot 100 are two measurements of the same twelve. Pan Docs explains the 172 by
  two warm-up fetches; two six-dot fetches come to twelve dots of *steps*, and
  with the push landing on the last of them the first pixel is drawn on the
  twelfth dot and the line comes out **171**. Letting the thrown-away fetch
  sleep as well makes it **173**. Both break the `ppu timing` ROMs. One dot
  charged to the fetcher's own reset - the dot the reset lands on, spent before
  Get Tile begins - lands on twelve exactly. The window agrees independently:
  `PixelPipeline::kWindowRestartDots` is six dots from the activation to the
  pre-empted pixel, which a bare reset to Get Tile would make five, and those
  six are held by four unit cases and by the previous task's SCX ruler. So two
  measurements that have nothing to do with each other both want the same dot.
  It is not in Pan Docs, and it is recorded here as what is left once the five
  steps are in place rather than as something documented.
- **What the second and third chances buy.** Nothing on an ordinary line: they
  are for the dots an extra pixel is in the FIFO. The colour-0 pixel a
  mid-window WX change pushes (see "A WX changed while the window is drawing
  pushes one colour-0 pixel" below) takes the push port that Get Tile Data High
  was going to use, and the chance one dot later is what keeps that pixel
  costing no dots. Deleting the two Sleep chances is the one mutation of the
  five tried that the new cases do *not* catch: it is caught by "a WX moved
  ahead of the counter while the window draws pushes one colour-0 pixel", and by
  nothing else in 480 cases.
- **What moved.** No verdict: **147 / 165** before and after, `screen` 12 / 30
  before and after. `m3_bgp_change` is the test that would have said the
  seven-dot render lag and the new fetch length disagree, and it did not - it is
  still exact, because the dot a row is pushed has not moved, only the reads
  that feed it. `ppu timing` 12 / 12, `dmg-acid2`, `m2_win_en_toggle` and
  `m3_scx_low_3_bits` all still exact. Six screenshot counts moved, all of them
  tests that write a register into a background or window fetch:

  | test | before | after |
  | --- | --- | --- |
  | `m3_scy_change` | 1256 | 2542 |
  | `m3_lcdc_tile_sel_change` | 688 | 1144 |
  | `m3_lcdc_bg_map_change` | 316 | 428 |
  | `m3_scx_high_5_bits` | 80 | 86 |
  | `m3_lcdc_win_en_change_multiple_wx` | 69 | 116 |
  | `m3_lcdc_win_map_change` | 1448 | **792** |

  Group D of the screen investigation (which dot of a background fetch samples
  which register) goes 3195 -> 5055 and group E (the same for a window fetch)
  2784 -> 2128; the `screen` group as a whole 17,405 -> 18,656. The reads are
  now where the structure puts them and uniform along the line, which is what
  pinning a register to a stage needs; the error those tests measure is not
  smaller for it, and this entry does not claim it is.
- **The choice this left, and how it was settled.** Inside a two-dot step this
  task's code sampled registers when the step *completed*. The other reading -
  the address is latched on the step's first dot and the data lands on its
  second - moves every read one dot earlier, to P-5, P-3 and P-1, and was
  measured at **15,907** differing pixels against **18,656**, with 17,405 before
  this task. It was left open here on purpose, because Mealybug's notes name
  different stages for TILE_SEL and for SCY and a 2,749-pixel aggregate is not
  evidence about a stage. It was then settled stage by stage, in favour of the
  first dot for all three: see "Each fetch stage samples its registers on its
  first dot" below, which has the per-stage measurements and the unit cases that
  separate the two dots. Note the shape of the older measurement - first-dot
  sampling under the *four*-step fetcher put the reads two dots before the
  tile's first pixel and cost about 5,000 pixels (recorded under
  "`m3_scx_high_5_bits`" in the screenshot table below); under the five-step
  fetcher it puts them one dot before and gains about 1,500. The two
  measurements bracket the same narrow window of dots, and the answer was inside
  it.
- **Tests.** `tests/test_pixel_pipeline.cpp`, "a background fetch reads its low
  bitplane four dots before the tile's first pixel" and "consecutive background
  fetches read their low bitplanes eight dots apart". Both were watched failing
  first. They draw a line whose tile has a different low bitplane on row 1 and
  row 2 and write SCY into the middle of it: the low bitplane is the one read
  whose dot crosses an M-cycle boundary when the fetcher goes from four steps
  over six dots to five over eight, so it is the only one of the three a test can
  separate at all - a write can only land at the end of an M-cycle, and the index
  and high reads move within one. Five mutations were tried and all five are
  caught: a six-dot fetch (no Sleep step) by the two new cases and nothing else;
  no extra push at Get Tile Data High by 30 cases; no reset dot by 37; a two-dot
  reset by 30; no Sleep chances by the one case named above.
- **Checked:** 2026-09-24.

## Each fetch stage samples its registers on its first dot (2026-09-24)

Not a divergence: a dot Pan Docs does not give and Mealybug's notes stop one
level short of. The notes say *which stages* a register is read at; this is which
of a stage's two dots.

- **Evidence, quoted in "The window's scanline X counter, and the evidence for
  it, quoted" above:** Mealybug Tearoom's PPU documentation, `TILE_SEL (bit 4)` -
  "`TILE_SEL` is read during the `0` and `1` stages of background tile data
  fetching. Changing its value during background tile data fetch allows for
  mixing tile bitplane data from two different tile patterns." - and `SCY $FF42` -
  "On the DMG ... the `SCY` register is read during the background tile fetch
  `B`, `0` and `1` stages. Changing the value during background tile data fetch
  allows for mixing tile bitplane data from different rows of the tile." `B` is
  the tile-index fetch and `0` and `1` are the two bitplane fetches. These are
  the author's notes on his own hardware photographs, so under the rule at the
  top of this file they stand.
- **What the notes do not say: which of a stage's two dots.** Each stage is two
  dots (Pan Docs, "Pixel FIFO"). FourShades now reads every register a stage's
  VRAM address is built from on the stage's **first** dot and takes the byte at
  the end of its second: the address goes out on the bus on one dot and the data
  comes back on the next. Writing P for the dot a tile's first pixel is drawn on,
  that is the tile index at P-5, the low bitplane at P-3 and the high bitplane at
  P-1, with the push still at P. Until 2026-09-24 it was the last dot of each
  stage - P-4, P-2 and P.
- **Derived per register, not assumed to be one rule.** The three stages were
  moved independently, because the notes name different stage sets for TILE_SEL
  and SCY and nothing said the dot had to be shared. All eight combinations were
  run over the reference photographs; the stages turned out to be separable, and
  each one independently wants its first dot, so they do share one rule:

  | stage moved to its first dot | which tests move, and how |
  | --- | --- |
  | `B`, the tile index (SCX, SCY, LCDC bit 3 / bit 6) | `m3_lcdc_bg_map_change` 428 -> 182, `m3_scx_high_5_bits` 86 -> 45, `m3_lcdc_win_en_change_multiple_wx` 116 -> 85, `m3_lcdc_win_map_change` 792 -> 852 |
  | `0`, the low bitplane (LCDC bit 4, SCY) | `m3_lcdc_tile_sel_change` 1144 -> 568, `m3_scy_change` 2542 -> 730 |
  | `1`, the high bitplane (LCDC bit 4, SCY) | `m3_lcdc_tile_sel_change` 568 -> 534, `m3_scy_change` 730 -> 661 |

  All three together: the `screen` group 18,656 -> **15,907** differing pixels,
  group D (which dot of a background fetch samples which register) 5055 ->
  **2277**, group E (the same for a window fetch) 2128 -> 2188. One test is worse
  - `m3_lcdc_win_map_change`, by 60 pixels, and only from stage `B`; every other
  moved test is better, and `m3_lcdc_bg_map_change`, which probes the same LCDC
  bit for the background rather than the window, is better by 246 at the same
  time. The window's map-select read is left sharing the rule rather than split
  off on 60 pixels: splitting it would be tuning rather than a measurement, and
  the window's own fetch has open questions ahead of it (see the WIN_EN entries
  above).
- **The fine-scroll discard shares SCX's sample, and nothing arbitrates its
  dot.** `discard_` takes SCX's low three bits at the same read that picks the
  line's first tile column ("The fine-scroll discard reads SCX at the line's
  first tile fetch" below). Reading it on the stage's last dot while the map
  column reads it on the first was measured: **every test in the `screen` group
  gives the identical pixel count either way**, `m3_scx_low_3_bits` included. So
  it stays welded to the map column - one sample, as that entry argues - and this
  is recorded rather than guarded.
- **How a unit test separates two dots at all.** It cannot on a plain line. A
  write lands at the end of an M-cycle, mode 3 begins on a dot that is a multiple
  of four, and a fetch is eight dots, so on an undisturbed line every stage's two
  dots sit on the same side of every M-cycle boundary and both readings draw the
  identical picture. That is why the previous task could measure the aggregate
  but not the dot. A **transparent object** breaks the alignment: its fetch costs
  six dots plus Pan Docs' tile term minus the first-object rebate (see the OBJ
  penalty entry above), which is *odd* at some OAM X - seven dots at OAM X = 9,
  five at OAM X = 11 - so the rest of the line's stages move across the M-cycle
  grid and a write can land between a stage's two dots. The object's tile is all
  colour 0, so it draws nothing and only its stall shows.
- **Tests,** in `tests/test_pixel_pipeline.cpp`, all four watched failing first
  and all four on **line 1**, because line 0 draws four dots early and a timing
  case placed there measures less than it appears to:
  - *"SCY reaches the low bitplane stage on the stage's first dot"* - object at
    OAM X = 9, so the tile at x = 8-15 has its stages on dots 110-111, 112-113
    and 114-115; a SCY written on dot 112 is visible from 113, so the low
    bitplane keeps row 1 (colour 0). Last-dot sampling reads it on 113 and draws
    colour 1.
  - *"SCY reaches the high bitplane stage on the stage's first dot"* - object at
    OAM X = 11, stages on 108-109, 110-111 and 112-113; the same write on dot 112
    is now after all three stages, so the tile is row 1 throughout. Last-dot
    sampling mixes row 2's high bitplane in and draws colour 2.
  - *"SCY reaches the tile-index stage on the stage's first dot"* - map row 0 is
    tile 0 and map row 1 is tile 1, so SCY = 8 swaps the tile without moving the
    row inside it and only stage `B` can see it. Written on dot 108, with the
    index read on 108, the tile keeps map row 0; last-dot sampling reads the index
    on 109 and draws tile 1 a tile early.
  - *"LCDC bit 4 written between a fetch's two bitplane stages mixes two tile
    patterns"* - the mixing the notes describe, and the only case here that needs
    two stages to disagree. Tile 0 at 0x8000 has a low bitplane only and tile 0
    at 0x9000 a high one only, so neither draws colour 3 alone; clearing bit 4 on
    dot 112 with the object at OAM X = 9 takes the low plane from 0x8000 and the
    high from 0x9000 and draws colour 3. Last-dot sampling takes both from 0x9000
    and draws colour 2, and no mixing is possible at all.
- **Mutations,** each on a forced rebuild with the executable's hash checked:
  sampling a stage one dot late is caught by 1 unit case for the tile index, 2
  for the low bitplane (its own and the mixing case) and 1 for the high bitplane;
  reading the low bitplane a whole stage late, at the high stage's dot, so that
  no bitplane mixing can happen at all, is caught by 2. Moving the fine-scroll
  discard's SCX read off the first dot is caught by **nothing** and moves no
  picture by a pixel, which is the bullet above stated as a mutation.
- **What is left in group D, and it is not sampling.** The residual is one tile
  per line, and in `m3_scy_change` the first tiles of each line come out as the
  reference's *previous* line - a whole fetch late. The cause was traced and it
  is an object: that ROM parks an object at OAM X = 8 on every line, FourShades
  fetches it on the line's very first rendering dot - before the fetcher has
  taken a step - and its eight-dot stall pushes every read on the line eight dots
  later. Pan Docs says the opposite: "the fetcher is advanced one step until it's
  at step 5 **or until the background FIFO is not empty**", so on hardware an
  object at screen X = 0 waits for the line's first push and the stall lands
  *after* the warm-up, leaving the reads where they were. Mode 3's length is the
  same either way, so no timing ROM sees it, and nothing here moved when the
  stages did. That is the object fetch's question (groups F and G of the screen
  investigation), not this entry's, and it is recorded here because it is what
  stands between group D and zero.
  - **Settled the same day, and it was bigger than this entry guessed:** see
    "An object fetch costs the pixels three dots more than it costs the fetcher
    and mode 3" below. The wait for the first push is real and is implemented,
    and with it group D's four sampling ROMs come to 805 pixels where they were
    2,277 here. The two interact - the object fetch's dots are what moved the
    stages across the M-cycle grid in this entry's own unit cases - so the
    figures below supersede the ones here, and the per-stage sweep was run again
    on top of them: see "Each fetch stage still samples its registers on its
    first dot once the object fetch's dots are right" below.
- **Effect:** `screen` 12 / 30 either way and 147 / 165 either way; the `screen`
  group's differing pixels 18,656 -> 15,907. `ppu timing` stays 12 / 12, and
  `m3_bgp_change`, `m3_scx_low_3_bits`, `dmg-acid2` and `m2_win_en_toggle` stay
  exact.
- **Checked:** 2026-09-24.

## Each fetch stage still samples its registers on its first dot once the object fetch's dots are right (2026-09-24)

Not a divergence: the re-measurement the entry above promised, plus the same
question asked of a **window** fetch, which no document answers at all.

The entry above measured its eight combinations while an object fetch started on
the line's first rendering dot, which moved every read on a line with an object
by eight to eleven dots. Every one of the six ROMs it quotes has an object on
every line, so the whole sweep was run on a grid that has since moved ("An
object fetch costs the pixels three dots more than it costs the fetcher and mode
3"). It was therefore run again, one stage at a time, and separately for window
fetches, which the Mealybug notes say nothing about: their TILE_SEL and SCY
sentences are both about "background tile data fetching".

`screen` group totals, one stage moved to its last dot at a time, on a full ROM
run each (the baseline is 11,399 differing pixels):

| variant | total | what moves |
| --- | --- | --- |
| **shipped: every stage, both sources, first dot** | **11,399** | |
| background tile-index stage on its last dot | 11,722 | `m3_lcdc_bg_map_change` +192, `m3_scx_high_5_bits` +74, `m3_scy_change` +26, `…win_en_change_multiple_wx` +31 |
| background low bitplane on its last dot | 13,789 | `m3_scy_change` +1,814, `m3_lcdc_tile_sel_change` +576 |
| background high bitplane on its last dot | 11,995 | `m3_scy_change` +438, `m3_lcdc_tile_sel_change` +158 |
| **window** tile-index stage on its last dot | 11,418 | `m3_lcdc_win_map_change` +4, `…win_en_change_multiple_wx` +15 |
| **window** low bitplane on its last dot | 11,409 | `m3_lcdc_tile_sel_win_change` +10 |
| **window** high bitplane on its last dot | 11,467 | `m3_lcdc_tile_sel_win_change` +68 |
| all three, both sources, last dot (Task 7's convention) | 15,116 | everything (measured on the 11,547 baseline) |

(The three background rows were measured on the 11,547 baseline, before the
window ordering below; their figures are quoted as differences for that reason.)

**All six independently prefer their first dot, and the six are separable** -
each moves a different set of tests. So a window fetch does sample on the same
dot of each stage as a background fetch, and that is now measured rather than
inherited.

### The 60-pixel asymmetry, explained by being gone

The entry above recorded one dissenter: `m3_lcdc_win_map_change` got **60 pixels
worse** from pinning the tile-index stage to its first dot, while
`m3_lcdc_bg_map_change` got 246 better from the same change to the same kind of
read. It was left sharing the rule and recorded, since splitting the window's
map-select read off on 60 pixels would have been tuning.

That was the right call, and the asymmetry was not about the window at all: it
was the object fetch's start dot. Every line of that ROM parks an object at OAM
X = 0-17, and with the fetch taken before the line's warm-up the window's tiles
were read eight to sixteen dots from where they belong - far enough that which
side of an eight-dot LCDC pulse a one-dot change lands on carries no information.
With the object fetch's dots right, the same measurement comes out the same way
round as the background's: the window's tile-index stage on its last dot now
makes `m3_lcdc_win_map_change` **worse**, not better. The sign flipped because
the grid moved, which is the whole of the answer.

### Three unit cases, and they need no object

A window fetch is easier to pin than a background one: the window's own restart
puts its stages wherever WX says, so no stalling object is needed to break the
M-cycle grid. The counter reaches WX on line dot 93 + WX, the activation spends
that dot resetting the fetcher, and the three stages follow on dots 94 + WX,
96 + WX and 98 + WX. A WX that puts a stage's first dot at the end of an M-cycle
therefore separates that stage's two dots on an undisturbed line:

| case | WX | stage's dots | the write |
| --- | --- | --- | --- |
| "a window fetch reads LCDC bit 6 on the tile-index stage's first dot" | 6 | 100-101 | bit 6 on dot 100, visible from 101: the first tile comes from $9800 and the second from $9C00 |
| "a window fetch reads LCDC bit 4 on the low bitplane stage's first dot" | 4 | 100-101 | bit 4 cleared on dot 100: the low plane from $8000 and the high from $9000 mix colour 3, which neither area draws alone |
| "a window fetch reads LCDC bit 4 on the high bitplane stage's first dot" | 6 | 104-105 | bit 4 cleared on dot 104: both planes come from $8000, colour 1 |

Each of the three, moved to its stage's last dot, is caught by exactly its own
case and by nothing else in either suite; before them the window's three stages
were caught by no unit case at all and by 4 to 68 differing pixels.

- **Checked:** 2026-09-24.

## A window fetch and the object fetch that lands on the dot it activates (2026-09-24)

Not a divergence: an ordering, measured. Two things can want the dot the line's
first row reaches the FIFO - the X counter reaching WX, which clears the FIFO and
sends the fetcher back to its first step, and an object at screen x = 0, whose
fetch pre-empts the pixel that row was about to feed. Which of them sees the
other decides whether the object's fetch runs on that dot or waits another
`kWindowRestartDots` for the window's own first row.

- **Measured:** it runs on that dot. `m3_lcdc_tile_sel_win_change` goes from
  1,016 differing pixels to **868** and nothing else in either suite moves by a
  pixel - no verdict, no other count, `ppu timing` 12/12,
  `intr_2_mode0_timing_sprites`, `m2_win_en_toggle`, `m3_bgp_change`,
  `m3_scx_low_3_bits` and `dmg-acid2` all unmoved.
- **How it is written:** `stepDot` reads "a pixel is due on this dot"
  (`queueSize_ > 0 || fetchStallDots() == 0`) once, at the top, before the
  counter is compared - so the object fetch's Pan Docs condition is evaluated
  against the FIFO as it was when the pixel became due, not against the empty one
  the activation leaves behind.
- **Unit case:** "an object fetch triggered on the dot the window activates does
  not wait for the window's row". WX = 7 puts the activation on line dot 100 and
  an object at OAM X = 8 costs eleven dots, so pixel 0 is drawn on dot 114 rather
  than 117, and a BGP write on dot 116 separates the two: the palette short
  (above) makes the three shades 1, 3 and 2 name the three sides of that
  boundary. Reverting the ordering is caught by that case and by the 148 pixels.

- **Checked:** 2026-09-24.

## The LCDC bits that choose a pixel's colour are read one dot before the palette shades it (2026-09-24)

Not a divergence: a dot Pan Docs does not give and Mealybug's notes do not
mention, measured from three of his DMG photographs and implemented.

- **Tests:** `m3_lcdc_bg_en_change` **376 -> 0**, `m3_lcdc_obj_en_change`
  **56 -> 0**, `m3_lcdc_obj_en_change_variant` **152 -> 96**. Two verdicts
  flipped; the suite went 149 -> 151.
- **Pan Docs:** silent. [LCDC](https://gbdev.io/pandocs/LCDC.html) says of bit 0
  only that when it is cleared "both background and window become blank (white),
  and the Window Display Bit is ignored in that case", and of bit 1 only that it
  enables objects. Neither sentence says when during mode 3 a change takes
  effect.
- **Mealybug's own notes:** silent too. The three passages quoted whole in "The
  window's scanline X counter, and the evidence for it, quoted" below cover
  `WIN_EN`, `TILE_SEL` and `SCY`, and say nothing at all about `BG_EN` or
  `OBJ_EN`. That is why none of plan 2's fetch work moved a single pixel of
  `m3_lcdc_bg_en_change`: these two bits are not fetch inputs.

### What the ROMs do, traced

All three are built the way the four of "Group D's residual is the OBJ penalty's
tile term" below are, and it was worth re-tracing rather than assuming:

- **One marker object per 8-line band, at OAM X = the band's index.** OAM entry
  *b* has Y = 16 + 8*b* and X = *b*, for *b* = 0...17. So band *b*'s object
  stalls the line by the OBJ penalty its own X earns - 11, 10, 9, 8, 7, 6, 6, 6
  dots for bands 0-7, the same again for bands 8-15 whose objects sit at screen
  x = 0...7, then 11 and 10 for bands 16 and 17. **That walk is the ROMs'
  sweep:** the register writes sit on the same dots of every line, and the
  object's stall slides the pixel stream under them band by band, one dot at a
  time. `m3_lcdc_bg_en_change`'s object tile is blank, so it is an invisible
  lever; the two `obj_en` ROMs use tile $19, a visible ring, because bit 1 is
  what decides whether it is drawn at all.
- **A four-write pulse on the same dots of every line.** `m3_lcdc_bg_en_change`
  writes LCDC = $92 on the M-cycle ending at line dot 108, $93 at 120, $92 at
  128 and $93 at 136, so bit 0 is low over dots 109-120 and 129-136. Line 0's
  four writes land at 104, 116, 124 and 132 - the four dots of `line_0_fix`, and
  line 0 draws four dots early to match ("Line 0 starts drawing four dots early"
  below), so the picture comes out the same.
- **The `obj_en` pair writes bit 1 twice a line, and moves the dot half way down
  the screen.** `m3_lcdc_obj_en_change` writes LCDC = $81 at line dot **108** on
  lines 0-63 and at **112** on lines 64-143, and $83 again at 204 (208 on the
  same lower lines), so objects are off across most of the line and the pulse's
  own edge is swept by four dots as well as by the object walk.
  `m3_lcdc_obj_en_change_variant` writes $81 at 108/112 and $83 at 124/128 - a
  short pulse instead of a long one - and then a BGP pulse at 256/260 and
  272/276, near the end of the line.
- **The background is text, not a flat colour,** so a blanked pixel only shows
  where the glyph under it is not colour 0. Every count below is therefore a
  lower bound on the dots that were wrong, which is why the sweep's numbers are
  worth recording as a table rather than as one figure.

### The residual's shape

The whole 376 was *one pixel per edge of the pulse*, on every line, and always in
the same direction: **every edge of the reference sits one pixel to the right of
where FourShades put it.** Band by band, with `P` the band's penalty and the
line's first pixel drawn on dot 100 + `P`, FourShades put the four edges at
pixels 9-`P`, 21-`P`, 29-`P`, 37-`P` and the photographs put them at 10-`P`,
22-`P`, 30-`P`, 38-`P`. Eighteen bands, four edges, and only the edges that fall
on a non-zero glyph pixel show - 376 of them.

That is a clean one-dot lag, and nothing about a fetch: the edges land in the
middle of background tiles (x = 10, 18, 26 with SCX = 0), not on tile
boundaries. **So the answer to the other half of the question is settled by the
same measurement: clearing bit 0 blanks pixels that are already in the FIFO.** If
it only reached the pixels fetched after the write, every edge in the reference
would sit at a multiple of 8. None of them does.

### The sweep

Every candidate lag, measured on the whole `screen` group. Only the three ROMs
that write these two bits mid-line move at all; the other 27 tests are identical
in every column, so only these three rows are given.

| lag on bit 0 | `bg_en` | lag on bit 1 | `obj_en` | `obj_en_variant` |
| --- | --- | --- | --- | --- |
| 0 dots (before) | 376 | 0 dots (before) | 56 | 152 |
| **1 dot** | **4** | **1 dot** | **2** | **98** |
| 2 dots | 403 | 2 dots | 60 | 156 |
| 3 dots | 855 | 3 dots | 100 | 196 |

**This is why the one dot is a finding and not a fit.** Three separate hardware
photographs, two different LCDC bits, one shared constant: each moves towards its
reference by exactly one dot and each moves *away* from it at two. Nothing in
`bg_en`'s pixels chose the value that `obj_en` also wants.

### Which read moved, and which did not

The dot is a **difference** between two reads of the same pixel, not a claim
about where the pixel stream sits, and that is what makes it independent of every
constant the stream is pinned by. The palette is read on the pixel's own dot, and
that is measured from the other side: a BGP write shows its old-OR-new short on
the pixel drawn on the first dot after the writing M-cycle ("Palette writes short
the old and new values together for one dot" below), and `m3_bgp_change`,
`m3_bgp_change_sprites` and `m3_obp0_change` are all exact. Lagging the palette
by the same dot as LCDC - a mutation run to check exactly this - takes
`m3_bgp_change` from 0 to **992** and `m3_bgp_change_sprites` from 0 to **706**
while leaving `bg_en` and `obj_en` at 0. So the dot sits *between* the colour
being chosen and the palette shading it, and moving the stream cannot produce it.

### The line's first pixel is the exception, and it is measured too

A one-dot lag alone leaves **4** pixels of `bg_en` and **2** of `obj_en`, and
every one of the six is the same pixel: **band 2, screen x = 0** - on the four
lines of that band where what `bg_en` draws there is not colour 0, and the two
where `obj_en`'s object covers it with a colour of its own. Band 2's object is at
OAM X = 2, whose penalty is nine dots, so the line's first pixel is drawn on dot
109 - the first dot the write landing at 108 is seen on, and `obj_en` writes at
108 on the lines band 2 covers. Both photographs show the *new* bit at that pixel:
`bg_en` blanks it and `obj_en` hides its object. A plain one-dot lag shows the old
bit in both.

It cannot be the penalty. If band 2's stall were ten dots the first edge would
land right, but the second would move to pixel 12 and both references put it at
13. `P` = 9 is pinned by the three later edges of the same line.

The rule that fits all eighteen bands of both ROMs is that **the lag is the gap
between one pixel's colour being chosen and the previous one being shaded, so the
line's first pixel - which has no predecessor - is chosen on the dot it is
shaded.** Equivalently: the sample is taken from the previous dot, but never from
before the line's first pixel.

Two wider readings were tried and both are refuted by measurement, which is what
makes this an exception found rather than an exception assumed:

| reading | `bg_en` | `obj_en` | `obj_en_variant` |
| --- | --- | --- | --- |
| lag 1, no exception | 4 | 2 | 98 |
| lag 1, **the line's first pixel only** | **0** | **0** | **96** |
| lag 1, every pixel that finds the queue empty | 4 | 2 | 98 |
| lag 1, every first pixel after an object stall | 0 | 4 | 100 |
| lag 2, the line's first pixel only | 395 | 54 | 150 |

- **"Every first pixel after an object stall"** is the reading the mechanism
  would suggest, and `obj_en` refutes it: its band 15 puts the object at OAM X =
  15, screen x = 7, and photographs the pixel that fetch pre-empts with the *old*
  bit. So a stall does not reset the stage; only the start of a line does.
- **"Every pixel that finds the queue empty"** - the reading that would have
  explained the exception as "the first pixel cannot be taken from the FIFO a dot
  early because it has not been pushed yet" - is refuted too, and by band 2
  itself: the fetcher's three dots of lead (`kObjectFetcherLead`) have already put
  that row in the queue by then, so the queue is not empty and the reading gives
  the unexceptional answer. **The physical reason for the exception is therefore
  not established.** What is established is that the one dot does not apply to
  the line's first pixel, from two references, and that neither wider rule holds.

Nothing in the suite arbitrates whether a *window activation*, which also clears
the queue and restarts the fetcher, resets the stage: the two readings differ
nowhere in the 165 (`m3_lcdc_win_en_change_multiple` was 468 under every column
above, and its own residual turned out to be elsewhere - see "A fetch in flight is
a window fetch until it ends" below). The stage is modelled as holding its pixel across an activation, which is
the narrower claim.

### FourShades

`PixelPipeline::kLcdcSelectLag` (1) and `lcdcSelectPipe_`, shifted once per dot
beside `wxPipe_` - the dots an object fetch stalls included, because the lag is in
dots - and `pixelStreamStarted_` for the exception, set when the first pixel
leaves the queue, a discarded one included. The two emission-time LCDC reads take
`selectLcdc`; `ppu.bgp()` and `ppu.obp()` keep the register. Nothing else changed:
bit 1's other two readers - the condition on starting an object fetch, and
`cancelObjectIfDisabled` - still read the register live, because they are fetch
dots and not emission dots, and no measurement here touches them.

### Mutations

Each was built on a forced rebuild and checked against the unit suite and a ROM
run; each restore was verified behaviourally, not by timestamp.

| mutation | unit cases failed | `bg_en` |
| --- | --- | --- |
| the lag removed (`ppu.lcdc()` at emission) | 3 | 376 |
| the exception removed (always the pipe) | 1 | 4 |
| the lag made two dots | 3 | 395 |
| the palette lagged the same dot | 0 | 0, but `m3_bgp_change` 992 |

The last row is why the palette's own read is quoted above as measured from the
other side: the unit suite does not catch it, and three pictures do.

### What is left

`m3_lcdc_obj_en_change_variant`'s remaining **96** pixels are a different thing
and are not touched by any of this - they are identical in every column of both
tables above, at 96, while the rest of that ROM's residual goes 152 -> 96. They
are a six-pixel block at the right edge of the line: x = 150-155 on band 16 and
151-156 on band 17, on all eight lines of each, shade 3 where the reference draws
0. Those are the two bands with the longest object stall (11 and 10 dots) and the
only two whose object is far enough right to delay the end of the line, and that
ROM's second pair of writes is the BGP pulse at dots 256/260 and 272/276 - so
what the block measures is where a line's *last* pixels land under the longest
stall, not anything about bit 1. It is the only `obj_en` residual left.

- **Checked:** 2026-09-24.

## Group D's residual is the OBJ penalty's tile term, and two references contradict each other over it (2026-09-24)

An open question, recorded with its measurements because the measurements are
exact, the residual's shape is sharp, and two of the four references turn out to
be **mutually inconsistent** with the structure of the fetch - which is worth
knowing before anyone spends another task on them.

The four ROMs are `m3_lcdc_tile_sel_change` (410 differing pixels),
`m3_scy_change` (259), `m3_lcdc_bg_map_change` (124) and `m3_scx_high_5_bits`
(12) - 805 together, and the four of the mid-line-register group that are pure
background fetches. Their frames were diffed line by line against the references
on 2026-09-24.

### What the four ROMs actually do

All four are built the same way, read off an instrumented run (a temporary trace
of every `$FF40`-`$FF4B` write with its line and dot, of every tile-index, low-
and high-bitplane read with its dot and value, and a dump of both tilemaps, the
tile data and OAM; added, measured, reverted, revert verified behaviourally):

- **One object per 8-line band, at OAM X = the band's index.** OAM entry *b* has
  Y = 16 + 8*b* (so it covers screen rows 8*b* to 8*b*+7) and X = *b*, for
  *b* = 0 to 17. So band *b*'s object sits at screen x = *b* - 8, and its OBJ
  penalty and the pixel it pre-empts change from band to band. **That is the
  ROMs' sweep**: the register writes are at a fixed dot, and it is the object
  that moves the fetch grid under them. In `m3_scy_change` and
  `m3_scx_high_5_bits` the object's tile is blank, so it is invisible and does
  nothing but move the dots; in the two LCDC ROMs it is a visible marker.
- **The register is written in a fixed eight-dot pulse on every visible line.**
  `m3_lcdc_bg_map_change` writes LCDC = $8B on the M-cycle ending at line dot
  108 and $83 on the one ending at 116; `m3_lcdc_tile_sel_change` writes $93 and
  $83 at the same two dots. So a read at line dots **109-116** sees the bit set
  and a read outside them does not. `m3_scy_change` walks SCY 0, 1, 2, 3, 4, 3,
  2, 1, 0 and round again, one write every eight dots from dot 84;
  `m3_scx_high_5_bits` writes SCX = 0 at dot 52 and SCX = LY & $F8 at dot 116.
- The tilemaps and tiles make each background tile uniform, so one tile column of
  one band is one bit of information. `m3_lcdc_bg_map_change`: map 0 is all tile
  $00 and map 1 all tile $01, with LCDC bit 4 clear throughout, so `$9000`
  (blank) against `$9010` (`FF FF` eight times over, solid) - the column is solid
  exactly when its **tile-index** read fell in the pulse.
  `m3_lcdc_tile_sel_change`: map 0 is all tile $00 with LCDC bit 3 clear
  throughout, `$8000` is `FF FF` eight times over and `$9000` is blank - the
  column's colour is `2 x (high read in the pulse) + (low read in the pulse)`, so
  it reports the **two bitplane** reads separately.

Note that the two LCDC ROMs have **identical** OAM, SCX = SCY = 0 and identical
write dots, and were measured to have identical fetch grids band for band. They
differ only in which LCDC bit they toggle, and therefore in which stage of the
fetch reports it.

### The shape of the residual

Every one of the 805 pixels is in **one background tile column of one band**, and
the bands and columns are these:

| ROM | bands 8-12, column 1 (x = 8-15) | bands 16-17, column 2 (x = 16-23) | elsewhere |
| --- | --- | --- | --- |
| `m3_lcdc_tile_sel_change` | 64, 60, 58, 54, 50 | 64, 60 | none |
| `m3_scy_change` | 32, 34, 34, 28, 33 | 45, 18 | 35, at the columns' left edge |
| `m3_lcdc_bg_map_change` | 64, band 8 only | 60, band 17 only | none |
| `m3_scx_high_5_bits` | none | 12, band 16 only | none |

A full tile is 64 pixels; the 60s are a full tile with four pixels of the marker
object's own glyph overlapping it. `m3_scy_change`'s 35 stragglers are one to
four pixels each - 29 in column 0 and 6 at x = 8 - and **every one of them is on
a line with LY % 8 = 6 or 7**, where the SCY walk moves the map row as well as
the row within the tile. They are a separate, much smaller effect and not part of
this shape.

**Band *b*'s object sits at screen x = *b* - 8, and the wrong tile is always the
one after the tile that pixel lands in.** Bands 8-12 put it at x = 0-4, inside
background tile 0, and tile 1 comes out wrong; bands 16-17 put it at x = 8-9,
inside tile 1, and tile 2 comes out wrong. And the bands that are **right** are
just as telling:

- **bands 13, 14, 15** - the object at x = 5, 6, 7. Its offset within the tile is
  5 or more, so Pan Docs' OBJ penalty tile term (`7 - (x & 7)`, minus 2, floored
  at zero) is **zero** and the penalty is the flat six dots.
- **bands 0-7** - the object at x = -8 to -1, off the left edge. The term is
  nonzero (5, 4, 3, 2, 1, 0, 0, 0) but the tile it is counted against is the
  pre-line tile the fetcher throws away.

So the whole 805-pixel residual lives in the **tile term**: the one to five dots
Pan Docs' step 2 charges for waiting for the background fetcher to finish the
tile, and only when that wait is against a tile the screen actually shows. Where
the term is zero, or where it is charged against the discarded fetch, all four
references are exact. FourShades spends the penalty as one stall of
`penalty - kObjectFetcherLead` dots (see "An object fetch costs the pixels three
dots more than it costs the fetcher and mode 3" above), which splits the affected
fetch - its tile index is read before the stall and its bitplane bytes after -
and that is what the references disagree with.

### First impossibility: band 0 against band 8

Bands 0 and 8 put the object at OAM X = 0 and OAM X = 8. Pan Docs gives both an
**11-dot** penalty (the X = 0 exception, and the general formula, agreeing at
SCX = 0), and both pre-empt **pixel 0**. So under any model whose object cost is
a function of (the dot the object pre-empts, the penalty in dots) the two bands
are bit-identical - and FourShades produces identical fetch grids for them,
traced dot for dot.

The references do not agree with each other. `m3_lcdc_bg_map_change` has column 1
solid in band 0 and blank in band 8; `m3_lcdc_tile_sel_change` the same.

**This is the same pair that blocks group E**, one entry below, where the
window's fetches show it instead of the background's. That entry says the X = 0
versus X = 8 pair "is the only place in the whole `screen` group where two lines
that this model says are identical photograph differently" - **that is now known
to be wrong**: it is also the cause of 64 of `m3_lcdc_bg_map_change`'s pixels and
64 of `m3_lcdc_tile_sel_change`'s, in the background fetcher, with no window in
sight. Whatever explains it explains part of both groups.

### Second impossibility: bands 16 and 17, one reference against the other

This one is new, and it is the harder of the two. Take band 16 (lines 128-135,
the object at OAM X = 16, screen x = 8) and call the dots on which fetch 2 - the
fetch that feeds screen column 2 - reads its tile index, its low bitplane and its
high bitplane T, L and H.

- `m3_lcdc_bg_map_change` draws column 2 blank, so **T is not in 109-116**.
- `m3_lcdc_tile_sel_change` draws column 2 solid shade 3, so **L and H are both
  in 109-116**.
- But T < L < H within a fetch (the tile index is what the bitplane addresses are
  built from), and on an undisturbed line fetch 2 reads its tile index on dot
  **111** - an object fetch can only delay the fetcher, never advance it. So
  T >= 111, and T outside 109-116 forces T >= 117, whence L > 117. Contradiction.

Band 17 is the same with the object at OAM X = 17.

This was checked exhaustively rather than argued only: with the pulse at 109-116
and the fetch grid as measured, a search over **every** trigger dot from 89 to
139 and **every subset** of the penalty's dots as the set the fetcher loses - the
most permissive physical family there is - finds a solution for all of bands 0 to
15 and **none at all** for bands 16 and 17.

Relaxing the structure until they can be solved says what would have to give: the
map-select bit (LCDC.3) has to reach the fetcher at least **three dots later**
than the tile-data-select bit (LCDC.4) does. With the two offset by three dots or
more, every band becomes solvable - but the per-band trigger dots and stall
lengths the solutions then want (trigger dots scattered over 89-108, stalls of 1
to 11 dots, in no relation to the object's X) are not a rule, and fitting them
would be exactly the tuning this project's rules forbid.

### Action, and what the next task should do

**Left failing, at 410 + 259 + 124 + 12 = 805 pixels, no line of `src/`
changed.** There is no mechanism here that explains all four references, and
there is no mechanism that explains even the two LCDC ones at bands 16 and 17.

- Start from the shape, not the ROMs: the residual is the OBJ penalty's **tile
  term**, and only where the term is charged against a visible tile. Anything
  that does not change how those one to five dots are spent cannot move these 805
  pixels, and anything that changes the flat six dots will move
  `intr_2_mode0_timing_sprites`, which is hardware-verified and passing.
- The X = 0 versus X = 8 pair is the thing to explain first, and it is now worth
  more than the group-E entry says it is.
- The band-16/17 contradiction needs a *third* reference or a hardware
  measurement, not more thought: it says one of three things this model treats as
  structural is wrong - the pulse's dots (pinned by bands 0-7 of these same two
  ROMs), the T-before-L-before-H order inside a fetch (pinned by the VRAM address
  arithmetic), or the undisturbed grid (pinned by `m3_scx_low_3_bits`,
  `m3_bgp_change`, `m3_window_timing` and `ppu timing` 12/12, all passing).
- **Checked:** 2026-09-24.

## Group E, measured to the dot and not solved: what the window's fetch cadence has to be (2026-09-24)

An open question, recorded with its evidence because the evidence is exact and
the next task should not have to re-derive it. `m3_lcdc_win_map_change` (724
differing pixels) and `m3_lcdc_tile_sel_win_change` (868) are the two ROMs left
in the `screen` group that are about window fetches, and what is left of them is
**not** which dot of a stage samples a register (the entry above measures that
from both sides). It is where the window's fetches fall.

### The measurement

`m3_lcdc_win_map_change` sets WX = 7 and WY = 0, so the window covers every line
from x = 0, fills the window's two tilemaps with a white tile and a black tile,
and sets LCDC bit 6 for exactly eight dots per line - line dots 109-116, read
off an instrumented run and consistent with the handler's two `ld [hl]` writes
two M-cycles apart. It parks one object per line, at OAM X = 0 on lines 0-7,
X = 1 on 8-15 and so on to X = 17. So each 8-line block is one measurement:
which window tile came out black says which of that line's window fetches read
its tilemap inside those eight dots, and the object's own penalty P is what
moves the fetches from block to block.

Writing fx0, fx1, fx2 for the dots the window's first three fetches read their
tilemap on, the reference requires:

| OAM X | P | in the pulse | FourShades' fx0, fx1, fx2 |
| --- | --- | --- | --- |
| 0 | 11 | fx0 | 101, 117, 125 |
| 1 | 10 | fx0 **and** fx1 | 101, 116, 124 |
| 2 | 9 | fx0 **and** fx1 | 101, 115, 123 |
| 3 | 8 | fx1 | 101, 114, 122 |
| 4 | 7 | fx1 | 101, 113, 121 |
| 5-7 | 6 | fx1 | 101, 112, 120 |
| 8 | 11 | none | 101, 117, 125 |
| 9-15 | 10-6 | none | 101, 109-110, 120-124 |
| 16 | 11 | fx2 | 101, 109, 125 |
| 17 | 10 | fx2 | 101, 109, 118 |

### What fits, and what does not

For the eight blocks whose object sits off the left edge - OAM X = 0-7, where the
fetch is triggered at pixel 0 - the sixteen constraints above have a **unique**
two-parameter solution, and it is exact:

- **fx0 = 100 + P.** The object's stall runs from dot 100 (the dot the
  background's first row was due) and the window's restart fetch begins its
  tile-index stage on the dot the stall ends. P = 9 must land inside the pulse and
  P = 8 outside it, which pins the constant to 100 and no other value.
- **fx1 = fx0 + 6.** P = 10 must put both fetches inside an eight-dot pulse and
  P = 11 must not, which pins the gap to exactly 6 - a fetch's three stages with
  nothing between them. FourShades puts 8 there, because after the extra push at
  Get Tile Data High it spends the two Sleep dots before the next Tile stage.

Those two together say the fetcher spends the wait *after* a fetch's push rather
than before the next fetch's first stage, which on an undisturbed line is
invisible (the FIFO empties exactly as the push lands, so the fetcher waits
either way) and after a window restart is not.

**It is not implemented, because it is not the whole answer.** The blocks from
OAM X = 8 up are not satisfied by it, and one pair rules out *any* model built
only from the dots above: OAM X = 0 and OAM X = 8 have the same penalty (11 dots,
Pan Docs' exception and its general formula agreeing at SCX = 0), trigger on the
same pixel and therefore have identical timing in every model here - yet the
reference makes the first window tile black on one and white on the other. So
something about an object *off the left edge* differs in time from one at the
edge, and nothing in this repository identifies it. Implementing the two rules
above without that would be fitting six blocks and breaking ten.

- **What the next task should do:** start from the table, not from the ROM. The
  two rules are worth testing against `m3_lcdc_tile_sel_win_change` as well,
  whose residual is the same shape, and the X = 0 versus X = 8 pair is the thing
  to explain first. **Correction, 2026-09-24:** this bullet used to call that pair
  "the only place in the whole `screen` group where two lines that this model says
  are identical photograph differently". It is not - the four background ROMs of
  group D show the same pair, in the background fetcher and with no window
  involved, for 128 of their 805 remaining pixels. See "Group D's residual is the
  OBJ penalty's tile term, and two references contradict each other over it"
  above.
- **Checked:** 2026-09-24.

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
- **The object half: what has changed.** Every measurement
  above was BGP's, and extending the same one-dot short to OBP0 and OBP1 assumed
  the three palette registers behave alike. `m3_obp0_change`, the test that shows
  it, failed at 432 differing pixels then, 108 once the window and fetcher work
  landed, and **0** since the object fetch's dots were split between the fetcher
  and the pixels (2026-09-24). It writes OBP0 during mode 3 over objects at the
  left edge of every line, so it exercises the object palette's short on the same
  dot BGP's is measured on, and the assumption is at least consistent with a
  hardware photograph now instead of resting on nothing. What has *not* been done
  is the mutation that would make it a measurement: removing the short from OBP
  alone and checking that this ROM notices. Until that is run, "confirmed" is too
  strong - the honest statement is that the test which would have refuted the
  assumption now passes.
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

## The window's scanline X counter, and the evidence for it, quoted (2026-09-24)

Not a divergence: an evidence record. The window work in `PixelPipeline`
depends on two documents, one of which is **not vendored with the ROMs** —
`tools/roms/data/mealybug-tearoom-tests/` holds only `ppu/`, no text — so it
has to be fetched from the web, and a web page can change or go away. Both are
quoted here in full so that the tasks that follow read them from the
repository instead of re-fetching them, and so the evidence survives if the
page moves.

### Source 1: Mealybug Tearoom's own PPU notes

- **File:** `the-comprehensive-game-boy-ppu-documentation.md` in
  [`mattcurrie/mealybug-tearoom-tests`](https://github.com/mattcurrie/mealybug-tearoom-tests).
- **Raw URL fetched:**
  `https://raw.githubusercontent.com/mattcurrie/mealybug-tearoom-tests/master/the-comprehensive-game-boy-ppu-documentation.md`
- **Last upstream change:** commit `875cf1e27444a4d50bcb932f74b6901e583085a3`,
  2019-04-03. **SHA-256 of the file as fetched on 2026-09-24:**
  `358e6eb2af268fbceb2c50088728c5deb05617d30d4d326e861ae1897c19fde9`
  (3676 bytes). Quote it from here; if a later task needs to re-fetch it, that
  digest says whether it has changed.
- **Standing:** these are the author's notes on what his own test ROMs
  measured on hardware, and the ROMs' references are photographs of real DMG
  output. Under the rule at the top of this file they outrank a Pan Docs
  simplification.
- **What it does *not* say:** the notes describe no X counter. The counter
  itself comes from Pan Docs (source 2). What the notes give is the mid-line
  WIN_EN behaviour that the counter makes expressible.

`LCDC $FF40`, `WIN_EN (bit 5)`, quoted whole:

> If WIN_EN is set then the window will be displayed when the WX and WY
> conditions are satisifed.
>
> Obscure behavior:
>
> - WIN_EN can be disabled during mode 3.  The disabling will take effect at
>   the end of the current window tile being drawn. When the current window
>   tile has finished being drawn, the PPU will start drawing background tiles
>   again.
> - When the background resumes drawing it is on a tile boundary. The low 3
>   bits of SCX have no effect.
> - Setting WIN_EN again during mode 3 on the same scanline will have no effect
>   unless WX has been updated to set the window to activate on a pixel that
>   hasn't been drawn yet.
> - If WX has been updated correctly and WIN_EN is set again then the PPU stops
>   drawing the background, and will activate the window again, but it will
>   start drawing the **next row** of the window, on the same scanline.

The same file's two other passages, quoted here for the fetch-sampling tasks
that come after the window ones — the stage names `B`, `0` and `1` are the
tile-index fetch and the two bitplane fetches:

> `TILE_SEL` is read during the `0` and `1` stages of background tile data
> fetching. Changing its value during background tile data fetch allows for
> mixing tile bitplane data from two different tile patterns.

> The `SCY` register can be written to at any time. Writes will take effect
> immediately on the DMG. On CGB and AGB devices, writes appear to take effect
> 2 T-cycles later.
>
> On the DMG and CGB revisions up to and including the "CPU GBC C" revision,
> the `SCY` register is read during the background tile fetch `B`, `0` and `1`
> stages. Changing the value during background tile data fetch allows for
> mixing tile bitplane data from different rows of the tile.
>
> On the AGB and CGB revisions "CPU GBC D" and greater, the `SCY` register is
> only read during the `B` stage, so no tile bitplane data mixing can occur.

### Source 2: Pan Docs, [Window behavior](https://gbdev.io/pandocs/Window.html)

This is where the counter is written down. Quoted on 2026-09-24:

> the PPU maintains a counter, initialized to 0 at the beginning of each
> scanline. The counter is incremented for each pixel rendered; however, it
> also increments 7 times before the first pixel is actually rendered (this
> covers pixels discarded during the initial "fine scroll" adjustment). When
> this counter is equal to `WX`, if the *Y condition* is true and the Window
> enable bit is set in `LCDC`, background rendering is reset, beginning anew
> from the active row of the Window's tilemap. **The coordinate of the active
> Window row is then incremented.**

> **This process can happen more than once per scanline**, making the Window's
> "tilemap Y coordinate" increase more than once in the scanline. … However,
> this requires "disabling" the Window by briefly clearing its enable bit from
> `LCDC` first.

> If `WX` is equal to 0, the Window is switched to before the initial "fine
> scroll" adjustment, causing it to be shifted left by SCX % 8 pixels.

> On monochrome systems, `WX` = 166 (which would normally show a single Window
> pixel, along the right edge of the screen) exhibits a bug: the Window spans
> the entire screen, but offset vertically by one scanline.

> On monochrome systems, if the Window is disabled via `LCDC`, but the other
> conditions are met *and* it would have started rendering exactly on a BG tile
> boundary, then where it would have started rendering, a single pixel with ID
> 0 is inserted.

The two sources agree with each other, which is the strongest position
available here: implement what both say.

### What FourShades does with it now

`PixelPipeline` keeps the counter as `windowX_`: zero at `startLine`, then
`kWindowCounterHeadStart` (7) free increments taken one per dot by
`advanceWindowCounter`, then one per pixel rendered. WX is compared against it
rather than against `pixelX_` arithmetic, and a match runs `startWindow`.

Since 2026-09-24 all four of the WIN_EN passages above **are** modelled: see
"Clearing LCDC bit 5 part-way along a line stops the window" for the first two
and "The window can start more than once on a scanline" for the last two.

Where the free increments fall, and the two dots the comparator lags WX by, are
measured rather than documented: see "The window's X counter is compared once
per dot, against a WX two dots old" below. `WX = 0` gets Pan Docs' "shifted
left by SCX % 8 pixels" out of that, because a match before the line's first
push leaves the fine-scroll discard still owed when the window's own first tile
arrives.

- **Checked:** 2026-09-24.

## Clearing LCDC bit 5 part-way along a line stops the window, and where it does not say enough (2026-09-24)

Not a divergence for the behaviour itself — the behaviour is implemented, and
Mealybug's own notes are quoted for it in the section above. What is recorded
here is one thing those notes do **not** pin, and the measurement.

- **Evidence:** Mealybug Tearoom's PPU notes, `WIN_EN (bit 5)`, quoted whole in
  the section above: "WIN_EN can be disabled during mode 3. The disabling will
  take effect at the end of the current window tile being drawn. When the
  current window tile has finished being drawn, the PPU will start drawing
  background tiles again." / "When the background resumes drawing it is on a
  tile boundary. The low 3 bits of SCX have no effect." / "Setting WIN_EN again
  during mode 3 on the same scanline will have no effect unless WX has been
  updated to set the window to activate on a pixel that hasn't been drawn
  yet." These are the author's notes on his own hardware photographs, so under
  the rule at the top of this file they stand.
- **FourShades:** `window_` means "the fetcher is drawing the window", and
  `stopWindowIfDisabled` clears it on the dot LCDC bit 5 goes low. A bare
  re-enable then does nothing, which is the third sentence, because the X
  counter's comparison against WX is an equality and the counter has already
  gone past an unchanged WX - see the entry below; it needs no latch of its
  own. The queue is not cleared and the fetcher is not restarted, so the pixels
  of the window tile already queued are drawn (the first sentence), the switch
  costs no dots, and no fresh SCX fine-scroll discard is taken (the second).
- **What the notes do not say: which background tile column resumes.** The
  fetcher has one column counter, `fetcherX_`, which the window reset to 0 and
  then counted window tiles with. FourShades lets it keep counting, so the
  first background tile after the stop is the column the window's count
  reached rather than the column that would have been there had the window
  never drawn. The alternative — deriving the column from the screen position
  instead, `SCX / 8 + (pixelX_ + queueSize_) / 8` — was built and measured on
  2026-09-24: **every test in the `screen` group gives the identical pixel
  count either way**, so nothing in the suite arbitrates it. The shared counter
  shipped because it needs no extra state and because "the background resumes
  on a tile boundary, the low 3 bits of SCX have no effect" reads like the
  description of a counter that was reset rather than of a recomputed column.
  If a later ROM does arbitrate it, this is the knob.
- **A mid-fetch write does not mix the two sources.** Until 2026-09-24 this
  bullet said the opposite: bit 5 was read on every dot, so a clear landing
  between the fetcher's tile-index step and its bitplane steps left a tile index
  read from the window map being addressed with the background's row, and no
  reference decoded so far was thought to measure it. Both halves were wrong.
  Two references measure it, and they say the fetch in flight stays a window
  fetch to its end: bit 5 is read once per fetch, on the dot the fetch completes.
  See "A fetch in flight is a window fetch until it ends: LCDC bit 5 is read
  once per fetch" below, which is where the 468 pixels this bullet was blamed
  for went.
- **A WX below 7 leaves a discard owed, and a stop does not cancel it.** A WX
  below 7 adds `kWindowCounterHeadStart` - WX pixels to `discard_` when the
  window starts (see "A WX below 7 pushes the window's leftmost pixels off the
  screen" below). Until 2026-09-24 those pixels were a separate clip that the
  fetcher's push dropped, and the clip was cancelled if a cleared bit 5 turned
  the tile it was owed to into a background one; now they are ordinary
  discarded pixels and nothing cancels them. Since bit 5's sample moved to the
  dot a fetch ends (see the entry below), a clear landing in the six dots
  between the activation and its push cannot take them out of a background tile
  either: that fetch is still the window's, so they come out of the window's own
  first tile, which is what they are owed to. A clear landing in a later fetch's
  dots does still leave them to be spent on a background tile. Nothing in either
  suite reaches that window - the write would have to land inside one particular
  M-cycle - and no reference measures it, so it is recorded rather than guarded.
  The reading that would argue for a guard is Mealybug's "when the background
  resumes drawing it is on a tile boundary".
- **Effect:** `m3_lcdc_win_en_change_multiple` 8316 differing pixels -> 5760,
  `m3_lcdc_win_en_change_multiple_wx` 5942 -> 1228 when this landed; both moved
  again with the window X counter work of 2026-09-24, to 468 and 69, and then
  the `_wx` one twice more with the five-step fetcher and its stage dots, to
  116 and then 85. Both moved once more on 2026-09-24, when bit 5's sample turned
  out to be once per fetch rather than once per dot: **`…_multiple` passes
  (468 -> 0) and `…_multiple_wx` stands at 5**, and those five are Pan Docs'
  colour-0 insertion rather than anything in this entry. See "A fetch in flight
  is a window fetch until it ends" below. `ppu timing` stays 12 / 12,
  `m3_bgp_change`, `dmg-acid2` and `m2_win_en_toggle` stay exact.
- **Checked:** 2026-09-24.

## A fetch in flight is a window fetch until it ends: LCDC bit 5 is read once per fetch (2026-09-24)

Not a divergence: this is Mealybug's own first WIN_EN sentence, taken at its word
for the first time. What is recorded here is which of bit 5's readers the residual
was about, the sweep that picked the dot, and the one thing left in the two ROMs.

- **Evidence:** Mealybug Tearoom's PPU notes, `WIN_EN (bit 5)`, quoted whole in
  "The window's scanline X counter, and the evidence for it, quoted" above:
  "WIN_EN can be disabled during mode 3. The disabling will take effect at the
  end of the current window tile being drawn. When the current window tile has
  finished being drawn, the PPU will start drawing background tiles again."
  The sentence is **ambiguous about which tile**: the fetcher runs a tile ahead of
  the pixels, so "the current window tile being drawn" can mean the one being
  emitted or the one being fetched. FourShades had read it as the emitted one -
  the queue drains unchanged and the fetch in flight becomes a background fetch on
  the dot bit 5 goes low. The two references below pick the **fetched** one, and
  that is all this entry is.
- **Tests:** Mealybug Tearoom `m3_lcdc_win_en_change_multiple` (468 differing
  pixels before this) and `m3_lcdc_win_en_change_multiple_wx` (85).

### What the two ROMs do, traced

Neither has an object anywhere, so neither has the OBJ-penalty sweep the group-D
ROMs use; what varies from line to line is the tile row and, in the second, WX.

- **`…_multiple`** writes, on every visible line but line 0, WX = `$18` on the
  M-cycle ending at line dot 88, WX = `$78` at 128, LCDC = `$D3` (bit 5 clear) at
  152 and LCDC = `$F3` (bit 5 set) at 168. Line 0's four writes land at
  84 / 124 / 148 / 164, four dots earlier, which line 0's four early drawing dots
  cancel. The line's first pixel is drawn on dot 100, the window's X counter
  reaches WX = 24 on dot 117 and the window starts there at screen x = 17; it is
  stopped by the write at 152 (visible from 153) and starts again at WX = 120 on
  dot 219, screen x = 113. **So there is exactly one measured write dot, and it
  lands between a window fetch's low-bitplane read (dot 152) and its high-bitplane
  read (dot 154).**
- **`…_multiple_wx`** writes WX = LY at dot 88 and then LCDC = `$C1` at 100,
  `$E1` at 108, `$C1` at 128, `$E1` at 136 - two pulses of bit 5 eight dots wide,
  with WX walking the whole screen underneath them. That is its sweep: the
  activation dot is 93 + WX, so LY decides where the activation falls among the
  pulses.

### The residual's shape

All 468 pixels of `…_multiple` are in columns **49-56** - eight columns, the
fifth window tile, which starts at x = 49 because the window itself started at
x = 17 - and the pattern is **identical in every 8-line band**, differing only
with LY % 8. Per band: 4 pixels on rows 1 and 5, 6 on row 3, 2 on rows 4 and 6,
8 on row 7, none on rows 0 and 2.

That is the signature of one mis-addressed row. The window tile there is tile
`$45` at `$8450`, whose eight rows are `FF FF`, `FF 81`, `FF 9F`, `FE 82`,
`FF 9F`, `FF 81`, `FF FF`, `00 00`. FourShades read its low bitplane from the
window's row and, because bit 5 had gone low in between, its high bitplane from
the **background's** row - LY + SCY instead of the window's row counter. Pairing
those two rows reproduces the diff row for row, and rows 0 and 2 are clean exactly
because the two rows' high bytes happen to be equal there. The reference is the
whole window row, both bitplanes.

### The sweep: which dot of a fetch the fetcher reads bit 5 on

Bit 5 has three readers in `PixelPipeline`: the activation test (`windowEnabled`,
at the top of every dot), `stopWindowIfDisabled`, and - through `window_` - the
fetch's own tilemap, column and **row**. The residual is about the last of those,
which is why none of the emission-time work of "The LCDC bits that choose a
pixel's colour are read one dot before the palette shades it" touched it. Every
candidate below was measured over the whole `screen` group, and **no test outside
these two moved by a single pixel under any of them**.

| where the fetcher reads bit 5 | `…_multiple` | `…_multiple_wx` | `screen` group total |
| --- | --- | --- | --- |
| every dot (what HEAD did) | 468 | 85 | 10,911 |
| the tile-index (`B`) stage's dot | 0 | 85 | 10,443 |
| the low-bitplane (`0`) stage's dot | 288 | 83 | 10,729 |
| the high-bitplane (`1`) stage's dot | 468 | 29 | 10,855 |
| **the dot the fetch completes** | **0** | **5** | **10,363** |

Two families of rival explanation were measured and rejected:

| rival | `…_multiple` | `…_multiple_wx` | what it costs elsewhere |
| --- | --- | --- | --- |
| the fetch's row source latched at the `B` stage, bit 5 still read every dot | 0 | 85 | nothing |
| the same latched at the `0` stage | 0 | 85 | nothing |
| the fetcher reading `window_` 1 dot late | 468 | 85 | nothing |
| ... 2 dots late | 0 | 85 | nothing |
| ... 3 dots late | 0 | 85 | nothing |
| ... 4 dots late | 576 | 85 | `m2_win_en_toggle` 0 -> 101, `m3_wx_4_change` 0 -> 315, `m3_wx_5_change` 0 -> 245, `m3_wx_6_change` 0 -> 239 |

### Why this is a mechanism and not a fit

- It is **one rule with no constant in it**: bit 5 is read once per fetch, at the
  fetch boundary, and nothing anywhere is offset by a tuned number of dots.
- It is **the document's own sentence**, disambiguated rather than contradicted.
- It moves **both** ROMs - 468 of 468 and 80 of 85 - where every other candidate
  moves at most one of them. The second ROM's 80 pixels are a different situation
  from the first's (a window activated on the dot bit 5 went low, four times over,
  rather than a write landing between two bitplane stages), so they are
  independent support and not more of the same pixels.
- The dot inside the fetch is pinned from both sides: the fetch's own last dot is
  the only one of the five candidates that both references agree on, and reading
  the bit two dots later, at the next fetch's tile-index stage, loses the second
  reference's 80 pixels again.
- The lag family that would also fix the first ROM has a **two-wide plateau**
  (2 or 3 dots, identical results) and four dots breaks four exact tests by 900
  pixels - the shape of a fitted constant, not a measurement.

### What is left, and it is not this

`…_multiple_wx` keeps **5** pixels, on four lines:

- **Lines 15 and 39** are Pan Docs' documented insertion: "On monochrome systems,
  if the Window is disabled via `LCDC`, but the other conditions are met *and* it
  would have started rendering exactly on a BG tile boundary, then where it would
  have started rendering, a single pixel with ID 0 is inserted." WX = LY on this
  ROM, so those two lines are exactly the ones whose WX - 7 (8 and 32) is a
  background tile boundary *and* whose counter reaches WX on a dot bit 5 is low:
  dot 108 and dot 132. The references draw colour 0 there and FourShades draws the
  background. That pixel is not modelled anywhere yet.
- **Lines 16 and 44** are the two lines whose counter reaches WX on the very dot
  bit 5 becomes visible again (dots 109 and 137). Both references put the whole
  window band one pixel to the right of FourShades' - background at WX - 7, window
  through the end of the third window tile - which is 2 pixels on line 16 and 1 on
  line 44. Nothing here decides why an activation on that dot should start a pixel
  late; it is the next thing for this ROM.

### What was implemented

`stopWindowIfDisabled` is no longer called from `stepDot` at all. It is called
from `stepFetcher`, at the end of the `DataHigh` stage - the dot the fetch's tile
index and both its bitplane bytes are in hand - and what it decides is whether the
**next** tile the fetcher goes for is a window tile. Nothing else changed: the
queue is still not cleared, the fetcher is still not restarted, `fetcherX_` still
keeps counting, and no fresh SCX fine-scroll discard is taken.

Three unit cases in `tests/test_pixel_pipeline.cpp` cover it, on a ruler whose
window tile carries a different colour on the window's row than on the
background's, so that a fetch split across the two is visible: a write landing
between a window fetch's two bitplane stages, a write landing on the dot a fetch
completes, and a write landing on the dot a freshly activated window's first fetch
reads its tile index.

| mutation | unit cases failed | `…_multiple` | `…_multiple_wx` |
| --- | --- | --- | --- |
| bit 5 read on every dot (what HEAD did) | 2 | 468 | 85 |
| read at the tile-index stage instead | 1 | 0 | 85 |
| read at the low-bitplane stage instead | 3 | 288 | 83 |
| never read by the fetcher at all | 10 | 8874 | 3741 |

- **Effect:** `m3_lcdc_win_en_change_multiple` **468 -> 0** (passes),
  `m3_lcdc_win_en_change_multiple_wx` **85 -> 5**. Nothing else in the 165 moved
  by a pixel: `ppu timing` 12 / 12, `intr_2_mode0_timing_sprites`,
  `m3_bgp_change`, `dmg-acid2`, `m3_scx_low_3_bits`, the four `m3_wx_*` and
  `m2_win_en_toggle` - the canary for the window - all still exact. The `screen`
  group's differing-pixel total went 10,911 -> **10,363** and the suite
  151 -> **152 / 165**.
- **Checked:** 2026-09-24.

## The window can start more than once on a scanline, and its row advances at each start (2026-09-24)

Not a divergence: the behaviour is implemented, and both documents quoted above
say the same thing about it. What is recorded here is the shape of the rule, the
one comparison that had to change with it, and what it measured.

- **Evidence:** Pan Docs' Window page - "When this counter is equal to `WX` ...
  background rendering is reset, beginning anew from the active row of the
  Window's tilemap. **The coordinate of the active Window row is then
  incremented.**" and "**This process can happen more than once per
  scanline**, making the Window's "tilemap Y coordinate" increase more than once
  in the scanline. ... However, this requires "disabling" the Window by briefly
  clearing its enable bit from `LCDC` first." - together with Mealybug's
  "Setting WIN_EN again during mode 3 on the same scanline will have no effect
  unless WX has been updated to set the window to activate on a pixel that
  hasn't been drawn yet." and "If WX has been updated correctly and WIN_EN is
  set again then the PPU stops drawing the background, and will activate the
  window again, but it will start drawing the **next row** of the window, on the
  same scanline." Both are quoted in full in the section above.
- **FourShades:** `PixelPipeline::windowConditions` is now Pan Docs' two
  conditions (LCDC bit 5 and the Y condition, both read live) plus "the window
  is not already drawing", and the per-dot comparison in `stepDot` is
  `windowX_ == WX`. `startWindow` reads the window's line counter and then
  advances it, so it advances **per activation**: a line that never matches WX
  leaves it alone, a line that matches twice advances it twice and the second
  band draws the row after the first.
- **The once-at-a-time rule needs no latch.** The activation latch this
  replaced (`windowActivated_`) was there to stop a bare re-enable restarting a
  stopped window. It is unnecessary once the comparison is an equality: the
  counter only counts up, so an unchanged WX cannot be matched twice and a WX
  moved *behind* the counter cannot be matched at all. That is Mealybug's third
  sentence, and it falls out of the arithmetic rather than being asserted on top
  of it. The `!window_` term rules out the one case the counter cannot: WX
  raised to a value still ahead of the counter while the window is already
  drawing. Pan Docs' pixel FIFO page says that case pushes a colour-0,
  lowest-priority pixel instead of restarting the window, so a match there is
  not an activation - and modelling that pixel is a later task.
- **The `>=` and the re-activation are one change, not two.** The comparison
  used to be greater-or-equal so that a window enabled, or a WX lowered, after
  the counter had gone past WX would still start; with re-activation allowed
  that looseness would re-fire on *every* dot the counter sits past WX, which is
  exactly the bare re-enable the notes forbid. Measured separately on
  2026-09-24, the strict equality alone moved
  `m3_lcdc_win_en_change_multiple_wx` from 5942 to 3759 and `m3_window_timing`
  from 28 to 33 - pictures in both directions. Together with re-activation the
  same equality is a large net win (below).
- **The consequence to be honest about:** a window enabled, or a WX changed, to
  a value the counter has already passed now draws nothing at all on that line,
  where it used to start late. Pan Docs and Mealybug both describe an equality,
  so this is what they say; the only test in the suite that moved the wrong way
  is `m3_window_timing`, +5 pixels, and its remaining error is the WX-below-7
  start-up cost recorded in the entry below rather than the comparison.
- **Effect:** `m3_lcdc_win_en_change_multiple` 5760 differing pixels -> 468,
  `m3_lcdc_win_en_change_multiple_wx` 1228 -> 77, `m3_window_timing` 28 -> 33.
  Nothing else in the suite moved by a pixel: `m3_wx_6_change` stays 13799,
  `m3_wx_5_change` 638, `m3_wx_4_change` 229, `m3_wx_4_change_sprites` 10, and
  `ppu timing` 12 / 12 with `m3_bgp_change`, `dmg-acid2` and, the canary for the
  row counter, `m2_win_en_toggle` all still exact. The `screen` group's
  differing-pixel total went 40108 -> 33670.
- **What was left in the two that moved, and where it went.**
  `m3_lcdc_win_en_change_multiple`'s 468 pixels were all in columns 49-56, a
  single tile wide: the tile the window hands back to the background on, which is
  where a fetch in flight samples LCDC bit 5, not the activation rule. That was
  measured on 2026-09-24 and the ROM now passes - see "A fetch in flight is a
  window fetch until it ends" below. `m3_lcdc_win_en_change_multiple_wx`'s 77
  were all in columns 0-9 and 28-37; the same measurement took 80 of them (its
  count had reached 85 by then), and the 5 that are left are Pan Docs' colour-0
  insertion where a disabled window would have started on a background tile
  boundary, recorded in the same entry.
- **Checked:** 2026-09-24.

## A WX changed while the window is drawing pushes one colour-0 pixel, and only onto an empty FIFO (2026-09-24)

Not a divergence: the behaviour is implemented and Pan Docs states it. What is
recorded here is the two things Pan Docs' sentence leaves open, which the DMG
references settle, and the one-match-per-counter-value rule the implementation
needed.

- **Evidence, Pan Docs, [Pixel FIFO](https://gbdev.io/pandocs/pixel_fifo.html):**
  "When the value of WX changes after the window has started rendering and the
  new value of WX is reached again, a pixel with color value of 0 and the
  lowest priority is pushed onto the background FIFO."
- **Tests:** Mealybug Tearoom `m3_wx_4_change`, `m3_wx_5_change` and
  `m3_wx_4_change_sprites`. All three run the same mode-2 STAT handler: set WX
  to 4 (or 5) while still in mode 2, then WX = LY and then WX = 80 during mode
  3. Traced, the second write lands on line dot 96 and the third on dot 192,
  and the line's first pixel is emitted on dot 100 - so the second write is a
  WX moved *ahead* of a counter that is already at 7, with the window drawing.
- **It is a push, not a substitution.** Each reference is, line for line,
  FourShades' own line with one colour-0 pixel **inserted**: the rest of the
  line moves one pixel right and its last pixel falls off the edge. Searching
  every insertion position for each failing line gave an exact 23,040-pixel
  match, and the position is always screen x = WX - 7, the pixel at which the
  counter equals the new WX.
- **It costs no dots.** The dot the pushed pixel is emitted on is the dot the
  fetcher's own push was going to use, so the fetcher pushes one dot later and
  the line still emits 160 pixels in the same number of dots; one fetched pixel
  is lost at the right-hand edge instead. Mode 3's length is unchanged, which
  `ppu timing` 12 / 12 and the unit suite's mode-3 lengths both hold to.
- **"Onto the background FIFO" is literal: the FIFO takes a push only when it
  is empty.** This is measured, not assumed. `m3_wx_4_change` differs on
  lines 12, 20, 28 ... 92 and nowhere else; `m3_wx_5_change` on lines 13, 21,
  ... 93. Those are exactly the lines whose insertion point, WX - 7 = LY - 7,
  falls on one of the drawing window's tile boundaries - x = 5, 13, 21 ... for
  WX = 4 (three pixels clipped) and x = 6, 14, 22 ... for WX = 5 (two) - which
  is where the background FIFO is empty at the top of the dot. On every other
  line the match lands part-way through a tile and the reference shows no shift
  at all. Pushing at the back of a non-empty FIFO instead was built and
  measured: it puts the pixel where the FIFO's tail is rather than where the
  references show it, and the lines that should be untouched shift. So the
  colour-0 pixel contends for the FIFO's single push port exactly as the
  fetcher's own push does.
  - **Restated on 2026-09-24, same behaviour.** Once the FIFO carries the three
    pixels an object fetch leaves the fetcher ahead by ("An object fetch costs
    the pixels three dots more than it costs the fetcher and mode 3" above), it
    is no longer bare on the dot a row goes in, so "empty" stops naming the dot
    this bullet measured. The test is now that the queue holds whole rows -
    `queueSize_ % 8 == 0`, i.e. the pixel it is about to hand over starts a row -
    which on every line without an object is bit-for-bit the old test and on a
    line with one keeps `m3_wx_4_change_sprites` exact. The pixel is also
    inserted in front of the row now rather than written into a queue known to be
    empty, which is the same insertion this entry measured.
- **The pixel is a background colour 0, not a shade.** `m3_wx_4_change_sprites`
  runs the same sequence under `BGP = $1B`, where background colour 0 shades to
  3, and its reference shows the pushed pixel as shade 3 on a band that is
  otherwise flat shade 0. So it goes through BGP at emission like any other
  background pixel. Its "lowest priority" then costs nothing to model: an
  object already beats a background colour of 0 whatever the object's own
  priority flag says. That same ROM shows it from the other side - its line 20
  is the one line whose insertion point is covered by an object, and the
  reference draws the object there, not the pushed pixel.
- **The window's row does not advance.** Pan Docs advances "the coordinate of
  the active Window row" when a match *resets background rendering*; this match
  does not, so it is not an activation. `m2_win_en_toggle`, the canary for the
  row counter, stays exact.
- **One match per counter value, which needed a field.** The comparison runs on
  every dot but the counter does not move on every dot: it stands still for the
  six dots an activation's fetcher restart takes, for every dot an object fetch
  stalls, and - all at once, within one dot - across the whole of its seven free
  increments. Without a memo of the highest counter value already compared
  (`windowComparedX_`), every activation would be followed by a colour-0 pixel
  from its own match, and a WX written to any value from 0 to 7 after the free
  increments had been through them would push one for a value the counter was
  merely already sitting on rather than had "reached again". That second case is
  not hypothetical: it is line 7 of all three ROMs, where the handler writes
  WX = LY = 7 with the counter already at 7, and it was 72, 121 and 1 differing
  pixels before the memo covered the free increments.
- **Effect:** `m3_wx_4_change` 229 differing pixels -> **0, passing**,
  `m3_wx_5_change` 638 -> **0, passing**, `m3_wx_4_change_sprites` 10 -> **0,
  passing**. `m3_wx_6_change` 13799 -> 13810 (see the next entry: its root
  cause is elsewhere, and those 11 pixels are a consequence of it). No other
  test in the suite moved by a pixel; `ppu timing` 12 / 12, and
  `m3_bgp_change`, `dmg-acid2` and `m2_win_en_toggle` all still exact. The
  `screen` group's differing-pixel total went 33670 -> 32804.
- **Checked:** 2026-09-24.

## The window's X counter is compared once per dot, against a WX two dots old (2026-09-24)

Not a divergence: the measurement Pan Docs does not make. Pan Docs says the
counter "increments 7 times before the first pixel is actually rendered" and
says nothing about *when* those increments fall, or how fast WX reaches the
comparator. Three Mealybug Tearoom DMG references between them pin both, and a
fourth constrains it; all four are photographs of real hardware, so under the
rule at the top of this file they decide it.

FourShades' dot numbering is used throughout: mode 3 begins on line dot 80,
`startLine` runs on dot 87 (`kRenderLag`), the first (thrown-away) fetch takes
dots 88-93, the second 94-99, and the first pixel is emitted on dot 100. A
register write completing on the M-cycle that ends on dot *d* is visible to the
PPU from dot *d* + 1.

### The rule

1. **The free increments are one per dot.** The counter holds value *v* on line
   dot 93 + *v*, so it holds 0 on the sixth dot of rendering and
   `kWindowCounterHeadStart` (7) on dot 100 - the dot of the line's first push,
   which is what lines a WX of 7 up with screen x = 0. The
   `kWindowCounterLeadDots` (5) dots before that are not compared at all.
2. **The comparator sees WX `kWindowCounterWxLag` (2) dots late.** So the
   comparison made on dot 93 + *v* is against WX as it stood on dot 91 + *v*.
3. **A match acts on the dot it is made**, pre-empting the pixel the pipeline
   was about to move, which costs the ordinary `kWindowRestartDots` (6) dots.
   For a match during the free increments that pixel is the first push, so the
   window's own first tile is pushed on dot 99 + *v*.
4. **The `kWindowCounterHeadStart` - WX free increments left over after a match
   are discarded pixels**, one dot each, in the same `discard_` counter the SCX
   fine scroll uses. That is what makes 1 and 3 add up: a match that comes *n*
   dots early has exactly *n* pixels to throw away, so the line's first pixel
   lands on dot 106 for **every** WX from 0 to 7 - six dots later than a plain
   line's, the same six a mid-line activation costs.
5. **The window's restart is one fetch, not two.** The thrown-away first fetch
   belongs to the line; a window that restarts part-way through it does not owe
   it again. Without this, point 3 would not hold for a match on dots 93-95.

### The evidence, reference by reference

- **`m3_window_timing`** writes WX = LY at dot 88 of each line and cuts a band
  out of the line by setting BGP = 0 at dot 96 and back at dot 108, so the
  number of dark pixels at the left of each line is the count of pixels emitted
  on dots 98-108. Its reference reads **3, 3 ... 3** for WX = 0 to 10, then
  **4, 5, 6, 7, 8** for WX = 11 to 15, then **9** from WX = 16 on. The flat run
  of 3 is point 4: the first pixel is on dot 106 for every WX at or below 7. The
  WX = 8 and WX = 9 rows pin the restart at exactly six dots - five or seven
  give 4 or 2 there - which is point 3. The saturation at 9 pins the first pixel
  of an undisturbed line to dot 100.
- **`m3_wx_6_change`** writes WX = 6 during mode 2, WX = LY at dot 96 and
  WX = 80 at dot 192, with WY = 4. Its reference has no window on lines 4 and 5,
  the window from line 6 with its left edge at LY - 7, and no window from line
  102 - read off its own decoded tilemaps, one line at a time. Lines 4, 5 and 6
  pin point 2 exactly: the comparison that could have matched 5 must still see
  the old WX and the one that could have matched 6 must already see the new one,
  which with one comparison per dot leaves no freedom. Line 101 pins it from the
  other end: the counter reaches 101 on dot 194 and the third write is visible
  from dot 193, so a one-dot lag would lose that line's window and a three-dot
  lag would give line 102 one.
- **`m3_wx_4_change_sprites`** puts an object at screen x = 0, which is fetched
  before the fetcher's first step and stalls eight dots there. Its reference
  still shows the window starting where the mode-2 WX says, so the free
  increments are **dots, not pixels**: they carry on through a stall. Holding
  them back with the fetcher lets the mode-3 write overtake them and moves the
  window a tile and a half right.
- **`m3_window_timing_wx_0`** is the same band trick with WX = 0 and SCX swept
  from 0 to 7 line by line. Its reference gives **11, 9, 8, 7, 6, 5, 4, 3** for
  SCX % 8 = 0 to 7. The rule gives 11, 10, 9, 8, 7, 6, 5, 4: **exact at
  SCX % 8 = 0 and one pixel out for the other seven values.** See the residual
  below.

### The residual, stated plainly

`m3_window_timing_wx_0` needs the line's first pixel one dot later than this
rule puts it whenever SCX % 8 is not zero: dot 106 at SCX % 8 = 0 and
dot 107 + SCX % 8 otherwise, against the rule's 106 + SCX % 8. One extra dot,
appearing only when a fine-scroll discard is still owed at the activation. It is
**not** a different restart cost and **not** a different comparison dot - both
of those are pinned to the dot by the other three references, and moving either
breaks them - and no mechanism in the model produces it:

- the activation for WX = 0 falls on dot 93 whatever SCX is, so the restart
  cannot depend on SCX;
- the fetcher has two dots of slack per tile, so crossing into the window's
  second tile - which is what SCX % 8 >= 1 makes the discard do - costs nothing;
- a discard of SCX % 8 + 1 pixels, or a seven-dot restart, fits these lines and
  breaks `m3_window_timing`'s flat run of 3 at SCX = 0.

So it is left failing, at **126 pixels** - one per line on seven of every eight
lines - rather than closed with a rule that has no mechanism and no second
reference behind it. Anyone picking it up should start by asking whether a
window activation that lands before the line's first push can leave the
fine-scroll discard unadvanced for one dot; that is the shape the numbers have.

### Files

`PixelPipeline::kWindowCounterLeadDots`, `kWindowCounterWxLag`,
`advanceWindowCounter`, `wxPipe_`, `windowCounterLead_` and `startWindow`'s
`discard_` arithmetic. `windowSkip_` is gone; see the entry below.

- **Checked:** 2026-09-24.

## The fine-scroll discard reads SCX at the line's first tile fetch (2026-09-24)

Not a divergence: another measurement Pan Docs does not make. The SCX % 8
fine-scroll discard has to read SCX at *some* dot, and FourShades read it when
`startLine` ran (line dot 87). One M-cycle later - at the dot the line's first
tile fetch reads SCX for its map column, dot 89 - is what two Mealybug Tearoom
references show, and it is the same read: one SCX sample serves both the column
and the fine scroll.

- **`m3_scx_low_3_bits`** sweeps the dot it rewrites SCX on. Over its first 71
  lines the write lands on dot 92, after both candidates, and either answer
  agrees with the reference; from line 72 it lands on dot 88, between them, and
  the reference follows the **new** value. Reading SCX when `startLine` runs
  puts four to six pixels of each of those lines in the wrong place: **324
  differing pixels to 0**, and the ROM passes.
- **`m3_window_timing_wx_0`** agrees from the other side. It rewrites SCX on
  dot 88 of every line, stepping it by one, so the two candidates differ on
  every line; the line where SCX goes from 7 to 0 is seven pixels out under the
  old read and exact under this one. **584 to 126** over the whole task, of
  which this read accounts for 371 to 126.

Nothing else in either suite writes SCX inside that M-cycle, so nothing else
moved. `m3_scx_high_5_bits` (80 pixels then, 86 after the five-step fetcher) and
`m3_scy_change` (1256 then, 2542 after it) are a
different question - which dot of which fetch stage reads what - and are
untouched.

- **Checked:** 2026-09-24.

## `m3_wx_6_change`: solved, and it was the free increments' timing, not WX = 6 (2026-09-24)

This replaces the "it has not been diagnosed" note further down. **The ROM now
passes**: it was diagnosed on paper first and then fixed, and both halves are
kept here because the diagnosis is what the fix was verified against.

- **What the ROM does.** Identical to `m3_wx_4_change` and `m3_wx_5_change`
  except for one constant: its mode-2 handler writes WX = 6 (line dot 52,
  still mode 2), then WX = LY (dot 96), then WX = 80 (dot 192), with WY = 4.
- **What its reference shows,** read off the decoded tilemaps rather than
  guessed at:
  - lines 4 and 5 have **no window at all** - they are background rows 4 and 5;
  - from line 6 the window activates **once** per line at screen
    x = LY - 7, so its left edge walks one pixel right per line, and it draws
    one window row per activation starting at row 0 on line 6;
  - from line 102 there is no window again, because the counter only reaches
    LY after the third write has moved WX to 80.
- **Reproduced exactly.** A frame computed from that rule and the ROM's own
  decoded background and window tilemaps matches the DMG reference in **all
  23,040 pixels**. The same computation with any other last-activating line than
  101 does not (9, 66 and 73 pixels wrong for 102, 100 and 103), so the rule is
  pinned, not fitted.
- **So the initial WX = 6 never activates the window on hardware, and WX = 4
  and WX = 5 do.** The difference is timing, not the value: the second write
  lands on dot 96, so the counter must already have compared 4 and 5 by then -
  hence the WX = 4 and WX = 5 activations, which those two references confirm -
  and must not compare 6 until after it. On line 6, where the second write
  leaves WX at 6, the comparison then matches and the window starts; on lines 4
  and 5 WX has been moved behind the counter first, so it never does.
- **What FourShades used to do.** All seven free increments, and all eight
  comparisons, **within one dot** - the first dot of the pipeline, line dot 88.
  Every value from 0 to 7 was therefore compared before dot 96, so WX = 6
  activated the window on lines 4 and 5 too and the frame was wrong from line 4
  down: 13,810 differing pixels.
- **How it was fixed.** The increments were spread one per dot and the
  comparator given its two-dot lag on WX - see "The window's X counter is
  compared once per dot, against a WX two dots old" above, which this ROM is
  half the evidence for. Line 5 and line 6 fix the lag from one side and line
  101 from the other; nothing about this ROM was special-cased.
- **The interaction it predicted, and what came of it.** Spreading the
  increments makes `m3_wx_5_change`'s line 6 a WX moved from 5 to 6 while the
  counter still sits inside its free increments and the window has just
  activated - a colour-0 push before the line's first pixel. Its reference shows
  the plain WX = 5 picture there, which is Pan Docs' "after the window has
  started rendering" taken at its word: the window is on, but nothing of it has
  reached the FIFO yet, so nothing is pushed. `PixelPipeline::windowRendering_`
  is that condition, and `m3_wx_5_change` still passes exactly.
- **Effect:** 13,810 differing pixels -> **0, passing**.
- **Checked:** 2026-09-24.

## A WX below 7 pushes the window's leftmost pixels off the screen (2026-09-21)

- **Tests:** Mealybug Tearoom `m3_wx_4_change` and `m3_wx_5_change` set WX to
  4 and 5 before mode 3 and photograph the line. Both references show the same
  picture WX = 7 would give, moved three and two pixels left, with that many
  more window pixels visible at the right-hand edge.
- **Pan Docs, [LCD Position and Scrolling](https://gbdev.io/pandocs/Scrolling.html):**
  WX "is the window's leftmost pixel's X position, plus 7", and WX values 0
  and 166 are called unreliable. It does not say what WX = 1-6 draws.
- **FourShades:** the count is the window's X counter's free increments left
  over after the match - `kWindowCounterHeadStart` - WX of them - and since
  2026-09-24 they are *discarded pixels*, added to the same `discard_` counter
  the SCX fine scroll uses and costing a dot each. `windowSkip_`, which dropped
  them at the push for no dots, is **gone**.
- **What they cost in dots is now evidenced.** This entry used to say the dot
  cost was picked by group total, with a dot each leaving `m3_window_timing`
  differing in 24 pixels and `m3_window_timing_wx_0` in 692, and free leaving 28
  and 584 - neither reproducing the constant the reference shows. Both figures
  were measured against free increments taken all in one dot, which was the
  actual fault: spread one per dot, a dot each is the only answer that makes
  `m3_window_timing`'s flat run come out, and it does so exactly. See "The
  window's X counter is compared once per dot, against a WX two dots old"
  above for the derivation and for what a dot each buys.
- **Effect:** `m3_wx_4_change` 10138 differing pixels -> 229 (2026-09-21) -> 0,
  `m3_wx_5_change` 9521 -> 638 -> 0, both passing since 2026-09-24;
  `m3_window_timing` 28 -> 0; `m3_wx_6_change`, which is not a shift at all,
  13281 -> 13799 -> 0.
- **Checked:** 2026-09-24.

## Screenshot tests still failing once the pixel pipeline was finished (2026-09-21)

**Re-measured from a full run on 2026-09-24, after the fetcher was found to read
LCDC bit 5 once per fetch rather than once per dot** (see "A fetch in flight is a
window fetch until it ends: LCDC bit 5 is read once per fetch" above; before that,
after the LCDC bits that choose a pixel's colour were separated from the palette
by one dot, fourteen failed and came to 10,911, and before *that*, with the object
fetch's dots split between the fetcher and the pixels, sixteen failed and came to
11,399). **Thirteen** of the thirty tests in the
`screen` group still fail, and they come to **10,363** differing pixels out of
23,040 each. Each is listed with its count, what
it measures and why it is not fixed. Seventeen pass: `acid/dmg-acid2`,
`mooneye/manual-only/sprite_priority`, `daid/stop_instr`, `ashiepaws/bully`,
and `mealybug-tearoom-tests/ppu/`'s `m2_win_en_toggle`, `m3_bgp_change` (this
section's own task), `m3_bgp_change_sprites`, `m3_obp0_change`,
`m3_wx_4_change`, `m3_wx_4_change_sprites`,
`m3_wx_5_change`, `m3_wx_6_change`, `m3_window_timing`, `m3_scx_low_3_bits`,
`m3_lcdc_bg_en_change`, `m3_lcdc_obj_en_change` and
`m3_lcdc_win_en_change_multiple`.
Passing tests have no row below; the notes after the table say what each of them
was and what settled it. For the history of the figures: the group stood at
47,378 before the mid-line LCDC bit 5 work of 2026-09-24, 40,108 after it,
33,670 before the colour-0 push work, 32,804 after it, 17,405 after the free
increments' timing, 18,656 after the five-step fetcher - a rise, explained in
"The background fetcher is five steps over eight dots" above - 15,907 once each
of that fetcher's three stages was pinned to its first dot ("Each fetch stage
samples its registers on its first dot"), 11,547 once an object fetch stopped
charging the pixels three dots less than Pan Docs' sum and stopped happening
before the line's warm-up ("An object fetch costs the pixels three dots more than
it costs the fetcher and mode 3", and "An object fetch waits for the pixel it
pre-empts"), 11,399 once an object fetch stopped waiting for the
window's row on the dot the window activates, 10,911 once LCDC's two
colour-selection bits were separated from the palette by a dot, and **10,363
now**, once the fetcher's read of LCDC bit 5 became one per fetch. The object move
changed two verdicts, the LCDC dot changed two more and bit 5's sample dot changed
one; the other four moves changed none. Every figure in the table
below was re-checked against a fresh full run on 2026-09-24, at the end of the
piece, and all thirteen agree to the pixel.

**What the window still gets wrong is the two mid-line LCDC bit 5 tests.** The
paragraph that stood here said the window never re-activates mid-line and that
this was the largest unmodelled behaviour left; both halves of that were
overtaken on 2026-09-24. Mealybug's own
[PPU documentation](https://github.com/mattcurrie/mealybug-tearoom-tests/blob/master/the-comprehensive-game-boy-ppu-documentation.md)
states the rule for LCDC bit 5: disabling the window during mode 3 takes effect
at the end of the window tile being drawn, the background then resumes on a tile
boundary with SCX's low bits ignored, and re-enabling it has no effect unless
WX has been moved to a pixel not yet drawn - in which case the window starts
again *on the next window row*, on the same scanline. All four of those
sentences are implemented: see "Clearing LCDC bit 5 part-way along a line stops
the window" and "The window can start more than once on a scanline" above. Where
inside a fetch a bit 5 write lands was settled on 2026-09-24 as well - the
fetcher reads the bit once per fetch, on the dot the fetch completes, so a write
landing between two of a fetch's stages cannot split it ("A fetch in flight is a
window fetch until it ends" above). `m3_lcdc_win_en_change_multiple` **passes**
with that, down from 8,316, and `m3_lcdc_win_en_change_multiple_wx` is at 5, down
from 5,942; those five are Pan Docs' colour-0 insertion for a window disabled on a
background tile boundary, and an activation that lands on the dot bit 5 comes back,
neither of which is modelled.

| test | pixels | why it still fails |
| --- | --- | --- |
| `m3_scx_high_5_bits` | 12 | one background tile per affected line takes the wrong SCX; 80, then 86, then 45 once the fetch stages were pinned |
| `ashiepaws/strikethrough` | 53 | an OAM DMA still copying through line 68's object scan; diagnosed and left failing, see its own entry above |
| `m3_lcdc_win_en_change_multiple_wx` | 5 | not the bit 5 sample any more: Pan Docs' colour-0 insertion on two lines, and a window activated on the dot bit 5 returns on two more; 5942, then 77, then 69, then 116 under the five-step fetcher, then 85. See "A fetch in flight is a window fetch until it ends" above |
| `m3_lcdc_obj_en_change_variant` | 96 | not bit 1 any more: a six-pixel block at the right edge of the last two bands, where the longest object stall meets its end-of-line BGP pulse; 532 before the object fetch's dots, 152 before LCDC's colour-selection dot. See "The LCDC bits that choose a pixel's colour are read one dot before the palette shades it" above |
| `m3_lcdc_bg_map_change` | 124 | mid-line LCDC bit 3 changes; 316, 428, then 182 |
| `m3_window_timing_wx_0` | 126 | one dot, only when SCX % 8 is not 0 (see the residual above) |
| `m3_lcdc_obj_size_change_scx` | 190 | mid-line LCDC bit 2 changes; 270 before it |
| `m3_scy_change` | 259 | mid-line SCY; 1256, 2542, then 661 |
| `m3_lcdc_obj_size_change` | 310 | mid-line LCDC bit 2 changes; 350, 410, then this |
| `m3_lcdc_tile_sel_change` | 410 | mid-line LCDC bit 4 changes; 688, 1144, then 534 |
| `m3_lcdc_win_map_change` | 724 | mid-line LCDC bit 6 changes; 1646 before the counter work, 1448 before the five-step fetcher, 792, then 852. Not sampling either: same entry |
| `m3_lcdc_tile_sel_win_change` | 868 | mid-line LCDC bit 4 changes, with a window; 1904 before the counter work, 1336, then 1016. Not sampling: see "Group E, measured to the dot and not solved" above |
| `daid/ppu_scanline_bgp` | 7186 | disagrees by 12 dots, which are three M-cycles in its once-a-frame `halt` -> LYC-interrupt sync and not in the pipeline; see its own entry above |

The mid-line LCDC, SCX and SCY entries left in the table are all the same shape:
the register is read live, at the dot the fetcher needs it. (**The three that were
not** all pass since 2026-09-24. LCDC bits 0 and 1 are read when a pixel leaves
the FIFO and not by any fetch, which is why none of the fetch-sampling work ever
moved them - see "The LCDC bits that choose a pixel's colour are read one dot
before the palette shades it" above. LCDC bit 5 *is* read by the fetch, but once
per fetch rather than at each stage, so a write cannot split one - see "A fetch in
flight is a window fetch until it ends" above.) Which dot that is **is**
now pinned - Mealybug's PPU documentation names the stages (TILE_SEL at the two
bitplane stages, SCY at all three), and each stage reads on the first of its two
dots; see "Each fetch stage samples its registers on its first dot" above for the
per-stage measurements and the four unit cases that separate the two dots. Its own
"what is left in group D" bullet blamed the residual on the object fetch's start
dot, and that turned out to be right and to be worth more than it guessed: the
four of these that are pure background fetches came to 2,277 pixels then and come
to 805 now. What is left of *them* is the OBJ penalty's tile term, and two of the
four references contradict each other over it: see "Group D's residual is the OBJ
penalty's tile term, and two references contradict each other over it" above.

Notes on the ones that are more than "a behaviour not written yet":

- **`m3_window_timing` (28 -> 0) and `m3_window_timing_wx_0` (584 -> 126).**
  These set WX to LY, or SCX to LY, and cut a band out of the line with BGP, so
  the number of dark pixels at the left of each line measures the dot the
  line's first pixel is drawn on. Both were wrong because the counter's free
  increments were all taken in one dot; both are covered by "The window's X
  counter is compared once per dot, against a WX two dots old" above, which
  also states what is left of `m3_window_timing_wx_0` and why it was not
  closed.
- **`m3_wx_6_change` (13799 -> 0).** Not the same shape as WX = 4 and WX = 5.
  Its reference draws the window two rows behind and two pixels right of where
  WX = 5's does, and shows the background on lines the window covers in the
  WX = 5 image. Since the three ROMs differ only in that one constant, WX = 6
  was doing something else on hardware. **Diagnosed and then fixed on
  2026-09-24:** it was the dots the window X counter's free increments are
  spread over - see "`m3_wx_6_change`: solved, and it was the free increments'
  timing, not WX = 6" above.
- **`m3_scx_low_3_bits` (324 -> 0).** Solved on 2026-09-24 by moving the
  fine-scroll discard's SCX read one M-cycle later, to the dot the line's first
  tile fetch reads SCX for its map column; see "The fine-scroll discard reads
  SCX at the line's first tile fetch" above. It is no longer a "mid-line SCX
  change inside the fetch" at all.
- **`m3_scx_high_5_bits` (12).** Only the third background tile of a line
  (x = 16-23) is ever wrong, and only on the 28 lines where SCX = LY crosses a
  tile boundary: the SCX write lands within a dot or two of that tile's map
  read. 80 pixels until the five-step fetcher, 86 under it, **45** once the
  tile-index stage was pinned to its first dot. Sampling the tile index, and
  both bitplane bytes, on the first dot of their two-dot fetch stages instead of
  the second was first tried under the *four*-step fetcher; it took this test
  from 80 to 77 but the `screen` group as a whole from 73,628 differing pixels
  to 78,855, and was reverted. Under the five-step fetcher the same change goes
  the other way - this test 86 to 45 and the group as a whole 18,656 to
  15,907 - because the stages themselves had moved, and that is the change that
  shipped; see "Each fetch stage samples its registers on its first dot" above.
  The two measurements bracket the same one-dot window from either side, which
  is why neither on its own settled it. The 73,628 figures above were taken
  before either power-on change moved `ashiepaws/bully` by 56 pixels - down,
  then back up - and before the window counter work; the group's total today is
  **11,399**. What decided the first revert was the 5,000-pixel rise, which
  neither power-on change touches either way.
  - **45 to 12** came later the same day, with the object fetch's dots: this ROM
    parks an object on every line, so it moved with the four other background
    ROMs. See "An object fetch costs the pixels three dots more than it costs the
    fetcher and mode 3" above. The 12 that are left are all in **band 16** - lines
    128-134, the band whose object sits at OAM X = 16, screen x = 8 - and they are
    the OBJ penalty's tile term rather than a mid-line SCX question. The sentence
    above about SCX = LY crossing a tile boundary is the old diagnosis and is
    superseded: see "Group D's residual is the OBJ penalty's tile term, and two
    references contradict each other over it" above.
- **`m3_lcdc_obj_size_change` (310) and `m3_lcdc_obj_size_change_scx`
  (190).** These two probe the same thing - LCDC bit 2, the object height
  bit, written during mode 3 - and the seven-dot rendering lag moved them in
  opposite directions: the plain variant went from 350 differing pixels to
  410, its `_scx` sibling from 350 to 270. **Why is not known.** The count
  rose by 60 under a change that lowered the group as a whole by 22%, and
  nothing here explains the sign.
  - **Partly answered on 2026-09-24:** LCDC bit 2 was being read when the object
    fetch was *triggered*, so a write landing inside the fetch could not change
    the height it used. It is now read on the dot the fetch builds its address,
    two dots before the pixel it pre-empts, and these are the two ROMs that pin
    that dot - from either side, by 20 and 40 pixels. See "An object fetch waits
    for the pixel it pre-empts, and reads its row two dots before it" above.
    Together with the fetch's dots that takes them to 310 and 190. The 60-pixel
    divergence between them under the rendering lag is not explained by this and
    is still not explained.
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
- **`daid/ppu_scanline_bgp` (7186).** This one disagrees with the Mealybug
  references rather than with a behaviour, by a uniform 12 dots (three
  M-cycles) over the whole image. **Diagnosed on 2026-09-24 and still left
  failing:** the three M-cycles are in the chain that synchronises the ROM's
  BGP loop once a frame - LY becomes 0, the LYC = LY STAT interrupt is
  requested, the CPU leaves `halt`, the dispatch runs - and not in the pixel
  pipeline, which `m3_bgp_change` pins from a completely different direction
  (the mode-2 STAT interrupt, on a running CPU, every line). Every other link
  in that chain is held by a hardware-verified Mooneye ROM this emulator
  passes. The full disassembly, the measured write dots, the seam positions of
  all three references and what would overturn the decision are in
  "`daid/ppu_scanline_bgp`'s twelve dots are three M-cycles of interrupt
  latency, not the render lag" above, which replaces the paragraphs that stood
  here.
- **`daid/stop_instr` (was 22739, now 0).** Diagnosed and fixed on 2026-09-24:
  it was not a pixel-pipeline failure at all. The PPU kept drawing while the
  CPU sat in STOP mode, so the screen held the pre-STOP frame instead of going
  blank. It was out of scope for the task that built the PPU, and it was
  recorded here as needing STOP's wake-up; the wake-up landed on 2026-09-22 and
  the count stayed at 22,739 exactly, which is what pointed at the screen
  rather than at STOP. See "STOP stops the PPU and blanks the LCD" near the top
  of this file.
- **`ashiepaws/strikethrough` (53) and `ashiepaws/bully` (was 421, now 0).**
  `strikethrough` was diagnosed on 2026-09-24 and is still left failing, at the
  same 53 pixels it has had throughout: it is a race between an OAM DMA that is
  still copying through line 68's object scan and the scan itself, its reference
  draws exactly one of the ROM's forty objects where this emulator draws ten,
  and closing it needs both a Pan Docs sentence that is not implemented and a
  dot that nothing in this repository measures. See
  "`ashiepaws/strikethrough`: an OAM DMA that outruns the object scan" above.
  `bully`
  was the same kind of entry until 2026-09-24, when three pieces of work in a
  row walked it down its chain of subtests to a pass. Its counts went 346, 290,
  346, 421, 0:
  - it was 346 until the first power-on change (the PPU starting at line 153 in
    mode 1 rather than line 0 in mode 2) took it to 290 - measured on
    2026-09-22 by building the commit before that change and running the ROM
    runner from it. That commit's own entry recorded only that no ROM's verdict
    moved, which is a narrower claim than nothing moving, and this section then
    kept the old figure.
  - it went back to 346 on 2026-09-24 with the power-on *phase* (`dot_` 4 ->
    356, its own entry above). The two 346s are not the same picture. `bully`
    is a chain of subtests that prints the first one it fails, and the
    2026-09-23 investigation disassembled it: it was failing on "Invalid
    initial DIV" ($FF04 read at line 0 dot 0, so a function of the PPU's phase,
    wanting $AD where FourShades gave $AE), and with the phase moved that check
    is satisfied and the ROM reaches "Invalid initial tile data" instead - a
    longer message, hence more differing pixels. What that next subtest wants is
    the Nintendo logo the DMG boot ROM unpacks into VRAM, which FourShades
    leaves zeroed. So the rise is a step forward measured in pixels, which is
    what a screenshot comparator does to a chain of serial-style subtests, and
    not a regression. Measured 2026-09-24: 290 -> 346, no other screenshot
    count in this table moved by a pixel.
  - it went to 421 later the same day, when the post-boot VRAM landed (its own
    entry above). The ROM now prints "DMA bus conflict always reads $FF" -
    longer again, hence 421 - so it stops at neither of the two VRAM subtests
    its strings carry, "Invalid initial tile data" and "Invalid initial map
    data". Both messages were read off the frames in `build/frames`, with the
    seeding disabled and then enabled, so the move is a reading and not an
    inference. What the new one wants has not been diagnosed: the wording
    suggests a read conflicting with OAM DMA is expected to return something
    other than $FF, which is not what this core does, but that is a guess from
    a message and the ROM has not been disassembled at that point. It is the
    next thing to look at, not something this entry claims to know. That run
    also moved ten other screenshot counts, all downwards; the post-boot VRAM
    entry says why. It was disassembled later the same day, and the sub-bullet
    below replaces the guess with what it actually wants.
  - it went to **0 - exact - the same day**, once that subtest was disassembled
    and turned out to be asking for a behaviour rather than a power-on value: a
    CPU read that conflicts with an OAM DMA sees the byte the DMA is
    transferring, not $FF. "What a read that conflicts with an OAM DMA puts on
    the bus" above carries the disassembly, the evidence and the decision. The
    guess above was in the right direction and wrong about the scope - the ROM
    does not want $FF replaced everywhere, only on the bus the DMA is reading.
    `bully` is the only screenshot ROM whose count has ever risen on its way to
    passing, which is what a picture comparator does to a chain of subtests
    that prints the first one it fails.

- **Checked:** 2026-09-22; `bully`'s count and the group total re-measured
  2026-09-24. Every count in this section is from a full run of `rom_runner`; the per-pixel diff maps quoted above were taken from the
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
write of $F7 (12). Sixty-nine phases, two T-cycles apart, checksummed. The
trigger delay moves all three sequences, so getting it wrong fails all three
checksums. The access window moves two of the three: forcing it open costs the
read ROM and the write ROM and leaves the retrigger ROM passing, which is the
10/12 measured below. The corruption window moves only one: of the three ROMs
only the retrigger one retriggers, so that figure is checked by that ROM alone
-- the sentence this entry used to carry, that any of the three figures being
wrong fails the checksum, was too broad.

**How the expected sequences were obtained.** Not by guessing a phase and
rerunning until one stuck. blargg's checksum scheme was reconstructed first --
CRC-32 over the raw bytes printed, with spaces and newlines not checksummed,
and `print_hex` checksumming the byte value rather than the digits it prints
-- and validated against a ROM whose result was already known, whose first
`check_crc $F604603B` reproduced exactly. With the scheme trusted, the
expected 69-byte sequence was solved for rather than guessed: the read timing
was written as a parametric model and its parameters searched for the sequence
whose CRC-32 is the DMG constant `$118A3620` that the read ROM checks. That
gave one sequence, six T-cycles away from what the first implementation
produced, which is where `kTriggerDelay` comes from. The retrigger ROM's
constant `$533D6D4D` was solved the same way and gave both the two T-cycle
lead and the next-read index. So the two figures were read out of the
checksums, and the ROMs then confirmed them rather than produced them.

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

**Every neighbouring value of both constants fails** (measured the same way,
2026-09-23). This is the strongest evidence in this entry: the two figures are
not a range that happens to contain the right answer, they are single values
with nothing beside them.

- `kTriggerDelay` at 4, 5, 7 or 8 -- one and two T-cycles either side of 6:
  sound **9/12** at every one of them, all three wave ROMs failing. With 0 it
  is 9/12 as well. Only 6 scores 12.
- `kAboutToRead` at 0, 1, 3, 4 or 6 -- from no lead at all to three T-cycles:
  sound **11/12** at every one of them, the retrigger ROM failing each time.
  Only 2 scores 12.

**What would falsify this.** Only the *spacing* between the CPU's access and
the channel's read is claimed here, not the absolute phase of either. This
emulator performs the access at the end of an M-cycle and the two constants
are measured against that, so a model that put the access two T-cycles earlier
and shortened the trigger delay to four produces the identical 69 sequences;
no ROM in the suite can tell those two apart. What would separate them: a
hardware trace of where in the M-cycle the CPU's access actually lands
relative to a wave read, or any test that varies the offset of the access
independently of the trigger -- all three ROMs here hold that offset fixed at
208 T-cycles and vary only the period. Either would fix the absolute phase,
and so fix each constant on its own; either could also show this emulator's
pair to be the wrong point on the right line.

**What is fitted rather than derived.** The two figures are expressed against
this emulator's own advance-then-access order, so what they really fix is the
distance between the CPU's access and the channel's read — six T-cycles of
trigger delay and a two T-cycle lead for the corruption. What the ROMs settle
is that relative spacing, and that is what the two constants carry; the
falsifier above says what it would take to settle the rest.

**Where the window is decided (2026-09-23).** "The channel's read coincided
with the CPU's access" is a statement about which T-cycle of an *M-cycle* the
read fell on, so `WaveChannel` counts that phase itself rather than taking it
from however many T-cycles a caller happens to hand over at a time. It used to
be read off the length of the call, which was right only while every caller
ticked whole M-cycles: a caller that subdivided one — audio resampling, say —
would have opened the wave RAM window on every read. Nothing in the ROM suite
would have noticed. Measured: that, and a channel 3 that keeps reading wave
RAM while it is switched off, both leave sound at 12/12; only the unit suite
catches either.

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
  only once the X counter reaches WX, and the window is drawn there only
  if bit 5 is set at that moment. (Until 2026-09-24 the comparison was against
  WX − 7, which is the same trigger pixel written the other way round; the
  counter now takes Pan Docs' seven free increments before pixel 0, so it is
  compared against WX itself. See "The window's X counter is compared once per
  dot, against a WX two dots old" above.)
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
  without LY changing. FourShades models that mid-scanline multiple-advance behaviour as of
  2026-09-24 - the row advances once per activation, and a line that matches WX
  twice advances it twice; see "The window can start more than once on a
  scanline, and its row advances at each start" above. Mealybug's
  `m3_lcdc_win_en_change_multiple` and `m3_lcdc_win_en_change_multiple_wx`
  probe it; the first passes since the fetcher's read of LCDC bit 5 became one per
  fetch, and the second is at 5 differing pixels, neither of them the advance
  rule.
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
