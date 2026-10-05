# Piece 5: sound — design

**Date:** 2026-09-23
**Status:** approved
**Pieces:** 5 of 6. Pieces 1-4 are done: SST 499/500, test ROMs 126/165.

## Why

FF10-FF3F reads 0xFF and writes go nowhere. That is twelve unclaimed test
ROMs, and one more that nobody would guess: `boot_hwio-dmgABCmgb` fails
because it reads every hardware register at power-on and the audio ones
answer 0xFF instead of their real values.

| group | now | after |
| --- | --- | --- |
| sound | 0 / 12 | 12 / 12 |
| boot state | 2 / 3 | 3 / 3 |

126 / 165 → **139 / 165**. After this piece every remaining failure in the
suite is a screen test.

> **Correction, 2026-09-23, after the piece landed.** The four claims above
> this line are left as they were written, so the record shows what was
> predicted; what happened is this.
>
> The piece landed at **138 / 165**, not 139, and `boot state` stayed at
> **2 / 3**, not 3 / 3. `boot_hwio-dmgABCmgb` does not fail on sound at all.
> With every audio register answering correctly it fails at **FF44 — LY**,
> reading 09 where the ROM wants 0A: about 63 M-cycles of PPU power-on phase
> error that the unimplemented sound block had been hiding. Fixing it means
> moving a PPU phase that `ppu timing` 12 / 12 and the `screen` group
> currently pin, so **`boot_hwio` moved to the screen piece**, where the PPU
> is being worked on anyway. The same correction is in the plan, dated the
> same day, made when Task 2 ran.
>
> So this piece is **12 ROMs, not 13**, and the sentence "after this piece
> every remaining failure in the suite is a screen test" is wrong by one:
> `boot_hwio` remains, and it is a PPU failure. The screen piece picks up 26
> screen tests plus `boot_hwio` to reach 165.

## Two stages

Piece 3 built the picture and piece 3b put it in a window. That split worked
because no test ROM needs a window, and sound is the same shape: blargg's
twelve ROMs test registers, length counters, sweep, triggers and wave RAM,
and not one of them needs a speaker.

- **Piece 5** — the APU. Headless, scored, 13 ROMs.
- **Piece 5b** — audible output through SDL3. Scores nothing, and is tested
  by measuring the samples rather than by listening to them.

This spec covers piece 5. Piece 5b gets its own.

## Architecture

`src/core/apu/`, split the way the cartridge chips were:

| File | Responsibility |
| --- | --- |
| `Apu.h/.cpp` | the registers, NR50-NR52, power, the frame sequencer, the mixer |
| `PulseChannel.h/.cpp` | channels 1 and 2 — a duty cycle, an envelope, and on channel 1 a sweep |
| `WaveChannel.h/.cpp` | channel 3 and its 16 bytes of wave RAM |
| `NoiseChannel.h/.cpp` | channel 4 and its LFSR |
| `LengthCounter.h` | shared: the length counter and its quirks |
| `VolumeEnvelope.h` | shared: the envelope |
| `FrequencySweep.h` | channel 1 only, but its own unit — three of the twelve ROMs are about it |

The split is not tidiness. Blargg's tests target exactly these components —
`02-len_ctr`, `04-sweep`, `05-sweep_details`, `07-len_sweep_period_sync` —
so a failing ROM names the file to open.

`Apu::read`/`write` cover FF10-FF3F; `GameBoy::readIo`/`writeIo` route to
them, replacing the `return 0xFF` that currently stands in for the whole
range. `Apu::tick()` is one M-cycle, called from `GameBoy::tick()` beside
the timer, serial and PPU ticks.

## The frame sequencer is not a timer

The 512 Hz sequencer that clocks lengths, envelopes and sweep is driven by
a **falling edge of bit 12 of the 16-bit system counter** — the counter
whose high byte is DIV. It is not free-running. Three things follow, and
the ROMs test all three:

- Writing to DIV resets that counter, which can produce an edge and clock
  the sequencer early, or skip one.
- STOP resets DIV, so it moves the sequencer too.
- The sequencer's phase at power-on is decided by the counter, not by zero.

Piece 2's final review left a must-do for exactly this: **centralise the
system-counter edges** rather than let each subsystem reach into
`Timer::counter()`. Serial already peeks at bit 8 and the APU would be the
second peeker. `Timer` gains an explicit edge query, and serial moves onto
it in this piece; STOP's DIV reset then goes through one path instead of
three.

The sequencer's eight steps, in order: length, -, length+sweep, -, length,
-, length+sweep, envelope. Lengths at 256 Hz, sweep at 128 Hz, envelope at
64 Hz.

## What the twelve ROMs are actually about

- **Read-back masks.** Every register ORs in the bits that read as 1 —
  NR10 reads `| 0x80`, NR11 `| 0x3F`, NR52 `| 0x70`, and so on. This is
  `01-registers`, and it is also the whole of `boot_hwio`'s audio half.
- **Length counters.** A length counter that reaches zero disables its
  channel. Triggering a channel whose length is zero reloads it to full.
  Enabling length in the first half of a sequencer period clocks it
  immediately — the "extra clocking" quirk. `02-len_ctr`,
  `07-len_sweep_period_sync`.
- **Trigger.** Writing bit 7 of NRx4 restarts the channel: reload length if
  zero, reload the envelope and its timer, reload the frequency timer, and
  for channel 1 copy the frequency into the sweep's shadow register and run
  the overflow check immediately. `03-trigger`, `06-overflow_on_trigger`.
- **Sweep.** The shadow register, the negate-mode latch, and the overflow
  check that disables the channel — including the check that runs on
  trigger before any sweep step. `04-sweep`, `05-sweep_details`.
- **Power.** Clearing bit 7 of NR52 zeroes every register in FF10-FF25 and
  makes writes to them do nothing. On DMG the length bits of NRx1 stay
  writable while powered off, which is the whole of `08-len_ctr_during_power`.
  Powering on resets the sequencer's step. `11-regs_after_power`.
- **Wave RAM on DMG.** While channel 3 is playing, a read of FF30-FF3F
  returns the byte the channel is reading *at that instant*, and 0xFF
  otherwise; a write lands on that same byte. Triggering channel 3 while it
  is already playing corrupts the first bytes of wave RAM on DMG.
  `09-wave_read_while_on`, `10-wave_trigger_while_on`,
  `12-wave_write_while_on`.

Where Pan Docs and a ROM disagree, the rule is the project's: Pan Docs
beats an emulator-generated test, a hardware-verified test beats a Pan Docs
simplification, and either way the decision goes in
`docs/known-divergences.md` with the evidence. Blargg's sound tests are
hardware-derived, so where one contradicts a Pan Docs simplification, the
ROM wins and the entry gets written.

## Output the core produces

The APU produces a stereo sample pair on demand, not on a schedule: the
core exposes the current mixed level, and the caller decides when to sample
it. That keeps the core free of a sample rate, which belongs to whatever is
playing the sound — SDL3 in piece 5b, the browser in piece 6.

```cpp
struct StereoSample { float left; float right; };
StereoSample Apu::sample() const;
```

Each channel contributes a digital level 0-15, gated by NR51's panning and
scaled by NR50's master volume. No filtering happens in the core: the DMG's
high-pass capacitor is an output-stage concern and lives with the output
stage.

## Testing

Unit tests per component, in `tests/test_apu.cpp`, `test_pulse_channel.cpp`,
`test_wave_channel.cpp`, `test_noise_channel.cpp`, `test_length_counter.cpp`,
`test_sweep.cpp`, covering at minimum:

- every register's read-back mask, and the full power-on set that
  `boot_hwio` checks
- a length counter reaching zero disabling its channel; the extra-clocking
  quirk on both halves of the sequencer period
- trigger reloading length, envelope and frequency timer
- the sweep's shadow register, negate latch, and the overflow check on
  trigger
- power off zeroing the registers, ignoring writes, and leaving the DMG
  length bits writable
- wave RAM readable only while channel 3 is reading it
- the sequencer stepping from a DIV write, and from STOP's DIV reset

**Every task mutation-tests its own tests.** In piece 4 three named
mutations stayed green and each one exposed a test that could not fail.
That is the only technique that has reliably caught them.

## The scoreboard, and CI

CI re-runs both suites and fails if `scoreboard.json` disagrees with what
that run produces. Piece 4 kept the scoreboard until its closing task and
was red for six commits as a result. In this piece **every score-moving
task ends with a full run and `python tools/scoreboard.py update`**, so
main stays green and the public number climbs commit by commit.

## Out of scope, deliberately

- Audible output, resampling, buffering and the high-pass filter — piece 5b.
- CGB audio registers and double-speed sequencer timing. This is a DMG
  emulator and the suite is the DMG set.
- Any attempt to make the mix "sound nicer" than the hardware.
