# Piece 4: cartridge chips — design

**Date:** 2026-09-23
**Status:** approved
**Pieces:** 4 of 6. Piece 3 (PPU) and 3b (the window) are done.

## Why

Only MBC1 exists. Everything else in the slot is refused at load with
`unsupported cartridge type 0xNN`, which is 18 unclaimed test ROMs and the
reason Pokémon will not start.

| group | now | after |
| --- | --- | --- |
| mbc2 / mbc5 | 0 / 15 | 15 / 15 |
| mbc3 / rtc | 0 / 3 | 3 / 3 |
| oam dma | 5 / 6 | 6 / 6 |
| mbc1 | 12 / 13 | 13 / 13 |

106 / 165 → 126 / 165. `mooneye/acceptance/oam_dma/sources-GS.gb` is not an
OAM DMA gap at all: it is cartridge type 0x1B, and it starts passing the
moment MBC5 exists.

Games this makes loadable: Pokémon Red/Blue/Yellow (MBC3), Gold/Silver/
Crystal (MBC3 + RTC), and the MBC5 library.

## Scope

In:

- MBC2 (types 0x05, 0x06)
- MBC3 (0x0F, 0x10, 0x11, 0x12, 0x13), including the real-time clock
- MBC5 (0x19–0x1E), including the rumble variants' register masking
- MBC1 multicart detection
- RTC persistence, and real elapsed time while the emulator is closed

Out: sound (piece 5), the browser build (piece 6), MBC6/MBC7/HuC/Pocket
Camera (no test ROMs, no games anyone is asking for), rumble as an actual
force-feedback effect (the register bit is masked; no motor is driven).

## Architecture

### One interface, five chips

`Cartridge` is 121 lines with MBC1's banking written inline. Five
controllers in that switch would be roughly 500 lines of interleaved state,
and it would not even work cleanly: MBC3 maps its clock registers *into the
cartridge RAM window*, so the read path cannot be a RAM-offset function.

`Cartridge` keeps what it already owns — header parsing, ROM and RAM
storage, the save interface — and delegates banking to an `Mbc` interface:

```
src/core/Cartridge.h/.cpp     header parsing, storage, save interface
src/core/mbc/Mbc.h            the interface
src/core/mbc/MbcNone.h/.cpp   no controller (type 0x00)
src/core/mbc/Mbc1.h/.cpp      MBC1, including multicart
src/core/mbc/Mbc2.h/.cpp      MBC2 and its built-in nibble RAM
src/core/mbc/Mbc3.h/.cpp      MBC3 and the clock register window
src/core/mbc/Mbc5.h/.cpp      MBC5
src/core/mbc/Rtc.h/.cpp       the clock itself, independent of MBC3
```

The interface is deliberately narrow:

```cpp
class Mbc {
public:
    virtual ~Mbc() = default;

    // Which ROM bank 0000-3FFF and 4000-7FFF currently see. The caller
    // masks the result against the cartridge's bank count.
    virtual std::size_t romBank(u16 address) const = 0;

    // A000-BFFF. Returns nullopt when the window reads open bus (RAM
    // disabled, no RAM fitted, or a bank number that decodes to nothing).
    virtual std::optional<u8> readRam(u16 address) const = 0;
    virtual void writeRam(u16 address, u8 value) = 0;

    // 0000-7FFF: the control registers.
    virtual void writeControl(u16 address, u8 value) = 0;

    // One M-cycle. Only MBC3 with a timer does anything.
    virtual void tick() {}
};
```

`readRam` returning `std::optional<u8>` is what lets MBC3 answer with a
clock register and MBC2 answer with a nibble, without `Cartridge` knowing
either exists.

Each chip owns the RAM it addresses through a reference to `Cartridge`'s
storage, so persistence has exactly one owner and `setRam` keeps working
unchanged.

**Rejected:** a data-driven table of bank widths and masks, one generic
banking engine parameterised per chip. MBC2's 4-bit RAM, MBC3's clock
registers and MBC5's 9-bit bank each break the shape; the parameters end up
carrying the special cases anyway, less legibly than five small classes do.

### Order of work

Task 1 is the extraction alone, with **no behaviour change**: MBC1 and
RomOnly move behind the interface and the existing 12 MBC1 ROM passes plus
every unit test stay green. The refactor is proven before a new chip exists.

## The chips

### MBC2 (0x05, 0x06)

Maximum 256 KiB ROM (16 banks) and 512 × 4 bits of RAM built into the
controller — the header's RAM size byte is 0x00 on every MBC2 cartridge and
must be ignored.

- Writes to 0000–3FFF are decoded by **bit 8 of the address**, not by the
  address range: clear selects RAM enable (`value & 0x0F == 0x0A`), set
  selects the ROM bank (`value & 0x0F`, 0 → 1).
- RAM is 512 half-bytes at A000–A1FF, mirrored 15 more times across
  A000–BFFF: the effective offset is `(address - 0xA000) & 0x1FF`.
- A read returns `0xF0 | value`. The upper nibble does not exist.
- Stored as 512 bytes, one nibble per byte in the low four bits, which is
  what every other emulator's `.sav` for an MBC2 contains.

Tests: `bits_ramg`, `bits_romb`, `bits_unused`, `ram`, `rom_512kb`,
`rom_1Mb`, `rom_2Mb`.

### MBC3 (0x0F, 0x10, 0x11, 0x12, 0x13)

- 0000–1FFF: RAM and timer enable. Pan Docs gives `value & 0x0F == 0x0A`,
  the same rule as MBC1; some hardware is reported to require the whole
  byte to be 0x0A. `ramg-mbc3-test` is the ROM that distinguishes them.
  Implement Pan Docs first, and if the test disagrees, follow the test and
  record the decision in `docs/known-divergences.md` with the evidence —
  do not quietly write whichever one passes.
- 2000–3FFF: ROM bank, 7 bits, 0 → 1.
- 4000–5FFF: 0x00–0x03 select a RAM bank; 0x08–0x0C select a clock
  register; 0x0D–0x0F are what `rtc-invalid-banks-test` exercises.
- 6000–7FFF: writing 0x00 then 0x01 latches the clock.

Types 0x0F and 0x10 carry the timer; 0x0F has the timer and a battery but
no RAM.

Tests: `ramg-mbc3-test`, `latch-rtc-test`, `rtc-invalid-banks-test`.

### MBC5 (0x19–0x1E)

- 2000–2FFF: low 8 bits of the ROM bank. 3000–3FFF: bit 8. Nine bits
  altogether, which `rom_64Mb.gb` needs — 8 MiB is 512 banks.
- **Bank 0 is selectable.** There is no 0 → 1 remap; writing 0 to
  2000–2FFF really does map bank 0 into 4000–7FFF.
- 4000–5FFF: RAM bank, 4 bits. On the rumble types (0x1C–0x1E) bit 3 is the
  motor, so those cartridges mask to 3 bits and the bit drives nothing.
- RAM enable: `value & 0x0F == 0x0A`.

Tests: `rom_512kb` through `rom_64Mb` (8 ROMs), plus
`acceptance/oam_dma/sources-GS.gb`.

### MBC1 multicart

Compilation cartridges wire the same MBC1 so that the 2000–3FFF register is
**4 bits wide instead of 5**, and 4000–5FFF supplies bits 4–5 of the bank.
No header byte declares this, so it is detected from the ROM's contents:

> The cartridge type is an MBC1 (0x01–0x03), the ROM is exactly 1 MiB, and
> the Nintendo logo — the 48 bytes a header carries at 0x0104–0x0133 —
> appears at three or more of 0x00104, 0x40104, 0x80104 and 0xC0104.

Three, not four, because the outer game's own header is one of them and
some carts leave a slot blank. This is a heuristic, it is what every
emulator that supports these carts does, and it gets an entry in
`docs/known-divergences.md` stating what it is and what would overturn it.

Test: `emulator-only/mbc1/multicart_rom_8Mb.gb`.

## The clock

### It counts cycles, not seconds

`Rtc` is ticked from `GameBoy::tick()`, which already runs once per
M-cycle. 1,048,576 M-cycles is one second. The core never reads a host
clock, so every test ROM stays deterministic and the ROM runner — which
loads no save — always starts an RTC cartridge at zero.

State: five live registers (seconds, minutes, hours, day-low, day-high),
five latched copies, and a sub-second cycle counter.

- Day is 9 bits: day-low plus bit 0 of day-high, 0–511. Overflow wraps to 0
  and sets bit 7 of day-high, the carry flag, which stays set until the
  program clears it.
- Bit 6 of day-high halts the clock. While set, ticks change nothing.
- Writing a register writes the live value and resets the sub-second
  counter, so a program that writes seconds gets a full second before the
  next increment.
- Reads return the **latched** copies. Latching (0x00 then 0x01 to
  6000–7FFF) copies live to latched.

Out-of-range values written by a program (seconds = 0x3F, say) are kept as
written and counted from; the hardware has no range check and neither does
this.

### Real time while the emulator is closed

The save file carries the standard 48-byte RTC footer after the cartridge
RAM — the layout BGB and VBA use, so a FourShades save and theirs remain
interchangeable:

| offset | size | contents |
| --- | --- | --- |
| 0 | 4 × 5 | live seconds, minutes, hours, day-low, day-high (u32 LE each) |
| 20 | 4 × 5 | the latched copies, same order |
| 40 | 8 | Unix time the save was written (u64 LE) |

On load the app reads the footer, computes `now - written`, and advances
the clock by that many seconds unless the halt bit is set. That is the
whole of the "quit on Monday, come back on Friday" behaviour, and it lives
in `app/Save.cpp`. The core exposes:

```cpp
bool hasTimer() const;                       // type 0x0F or 0x10
RtcState rtcState() const;                   // live + latched registers
bool setRtcState(const RtcState& state);
void advanceRtcSeconds(std::uint64_t seconds);
```

`advanceRtcSeconds` takes a number. It does not look anything up. The rule
that the core never reads the clock, the filesystem or the test harness is
not bent for this.

A consequence worth stating: while the emulator is paused the machine does
not tick, so the in-game clock stops — exactly as a real Game Boy's does
not, since its cartridge keeps counting. The next launch's catch-up covers
the difference, so the clock is right whenever a save is reloaded and slow
only within a single paused session.

### The save file's length rule stays

`loadSave` refuses any file whose length is not exactly the cartridge's RAM
size, and leaves it on disk untouched, because that file is the player's
only copy. One addition: a cartridge with a timer also accepts RAM size +
48. Every other length is still refused, including RAM + 48 on a cartridge
with no timer, and a truncated footer.

A timer cartridge with a battery but no RAM (type 0x0F) saves a 48-byte
file that is footer only.

## Error handling

- Unsupported controller: unchanged — `unsupported cartridge type 0xNN`,
  no machine is constructed, and the window shows the message rather than
  vanishing.
- ROM size code above 0x08: refused as now.
- A ROM shorter than its declared size is padded with 0xFF and one longer
  is truncated, as now. The header is the truth.
- A save of the wrong length: refused, file untouched, session runs with
  fresh RAM and writes nothing back.
- A save whose footer is present but whose timestamp is in the future
  (a clock that moved backwards): elapsed time is treated as zero rather
  than as a huge negative, so a wrong host clock cannot run the in-game
  clock backwards or wrap it.

## Testing

Unit tests, doctest, one file per chip — `tests/test_mbc2.cpp`,
`test_mbc3.cpp`, `test_mbc5.cpp`, `test_rtc.cpp`, `test_mbc1_multicart.cpp`
— covering at minimum:

- bank wrapping at each chip's register width, including MBC5's 9 bits and
  MBC5 bank 0 being selectable
- RAM enable and disable, and reads through a disabled window
- MBC2's address-bit-8 decode, nibble masking and 512-byte mirroring
- MBC3's clock register window, latching, halting, the day carry, and the
  invalid bank numbers
- multicart detection: a positive, and a 1 MiB ROM with one logo that must
  **not** be detected
- the RTC footer: round trip, catch-up arithmetic, halt suppressing
  catch-up, a backwards host clock, and every refused length

**Every task mutation-tests its own tests.** Break the behaviour
deliberately, rebuild, watch the test go red; if it stays green the test was
decorative. Tests that passed for the wrong reason were the recurring defect
of pieces 3 and 3b — four separate times — and reading a test is not enough
to catch it.

Then the ROM groups, then `tools/scoreboard.py update`.

## A prediction on the record

`GameBoy.cpp` maps DMA sources at or above 0xE000 down by 0x2000 and says:

> Pan Docs lists DMA sources $00-$DF only. Mapping E000 and above onto C000
> and above (as the echo area does) is inferred, to be confirmed by the
> MBC5-dependent sources-GS test in piece 4.

That test runs this piece. If the inference was wrong it fails, and the
failure is the point of having written the prediction down.

## Out of scope, deliberately

- MBC30's 8 RAM banks. No test ROM, and Crystal fits in MBC3's four.
- Multicart *writing* quirks beyond the bank width.
- Any attempt to drive rumble hardware.
