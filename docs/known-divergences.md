# Known divergences

Places where a test expects something that Pan Docs says the hardware doesn't
do. We follow Pan Docs and leave the test failing, so the scoreboard never
claims more than the hardware documentation supports.

Each entry gives the test, what it expects, what Pan Docs says (with a link),
and what FourShades does.

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
  becomes visible while the CPU sits in STOP mode. DIV reset, and the
  button-held and interrupt-pending branches (which Pan Docs says make STOP a
  1-byte opcode or turn it into HALT), need the timer, joypad and interrupts,
  which arrive in piece 2.
- **Also noted by Pan Docs itself:** "stop is often considered a two-byte
  instruction, though the second byte is not always ignored.",
  [CPU Instruction Set](https://gbdev.io/pandocs/CPU_Instruction_Set.html#stop).
  "Not always" refers to the button-held and interrupt-pending branches, where
  STOP behaves differently — the branches this entry already covers, and which
  need piece 2's joypad and interrupts to implement.
- **Checked:** 2026-09-11.

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
