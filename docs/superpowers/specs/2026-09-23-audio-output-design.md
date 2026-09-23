# Piece 5b: audible output — design

**Date:** 2026-09-23
**Status:** approved
**Follows:** piece 5, which closed at test ROMs 138/165 with the sound group
at 12/12.

## Why

The APU is complete and correct against every sound ROM in the suite, and
you cannot hear any of it. `Apu::sample()` returns a stereo pair and nothing
calls it.

This piece scores nothing. No test ROM checks a speaker, which is exactly
why it was split off from piece 5 — the same split as the PPU and its window
in pieces 3 and 3b.

## The shape, and why it is this shape

Piece 3b built `FramePacer` as a class with no SDL in it, so the pacing
arithmetic could be tested without a window, and that decision is the reason
the 59.7275 Hz measurement exists at all. Audio takes the same split:

| File | Responsibility | SDL? |
| --- | --- | --- |
| `app/AudioResampler.h/.cpp` | cycles to samples, the DC-blocking filter, the stereo buffer | no |
| `app/Audio.h/.cpp` | the SDL audio stream, the device, the drift policy, mute | yes |

Everything worth testing lives in the half with no SDL in it.

## Turning cycles into samples

The core has no sample rate and will not get one — that was settled in piece
5. `Apu::sample()` answers "what is the level right now", and the caller
decides when to ask.

`GameBoy::cycles()` counts M-cycles since power-on. At 1,048,576 M-cycles a
second and an output rate of 48,000 Hz, a sample is due every **21.8453…**
M-cycles. `AudioResampler` keeps that ratio in fixed point and, given the
machine's current cycle count, answers how many samples are now due — so the
app's frame loop asks after each step and pushes that many.

48,000 rather than 44,100 because it divides the ratio more kindly and every
sound device in use supports it; SDL3 resamples to the device's real rate if
it differs.

## The filter

A DMG's audio output sits behind a capacitor that blocks DC, which is why a
channel sitting at a constant level fades to silence rather than holding a
steady offset. Without it, four channels' DC offsets stack into a click on
every trigger.

    out = in − capacitor
    capacitor = in − out × charge

where `charge` is 0.999958 raised to the number of T-cycles per sample. That
constant is the widely used DMG figure and it is an approximation of a real
capacitor, so it is documented as a choice rather than a measurement.

## Drift, and who is the master clock

The frame pacer is the master. It sleeps to hold 59.7275 Hz, measured in
piece 3b, and the audio device consumes samples at its own crystal's rate.
Those two will drift.

The policy: keep roughly two frames of audio queued. When SDL reports the
backlog above the high-water mark, drop one sample that frame; below the
low-water mark, repeat one. One sample at 48 kHz is 21 microseconds and is
inaudible, and doing it once a frame corrects far faster than any drift
between two crystals accumulates.

This is ours, not the hardware's, and it is documented as such. The
alternative — driving the emulator from the audio callback — makes audio the
master clock and the picture the thing that stutters, which is the wrong way
round for an emulator whose frame timing is already measured and correct.

Piece 3b's measurement is what makes this safe: vsync was rejected there
because this display runs at exactly 60.000 Hz, which would be +0.4565% —
201 surplus samples a second against a 44.1 kHz card, enough to overrun a
4096-sample buffer every twenty seconds. The sleep-based pacer is 270 times
closer, so the drift correction here has almost nothing to do.

## What the person gets

Sound, from the moment a ROM loads. **M** mutes and unmutes, which joins the
existing keys — arrows and Z/X/Enter/Shift to play, P for the palette, Space
to pause, R to reset. Mute silences the output without pausing the machine,
so a muted game still runs.

No volume control: the DMG has one, in NR50, and games use it. A second
volume on top would make the emulator's output disagree with what the
program asked for, and the system mixer already exists.

## Testing

Audio cannot be checked by listening, so it is checked by measuring.

- **The tone test, which is the point of the piece.** Drive a real `GameBoy`
  headless: set channel 1 to a known frequency, trigger it, run a second of
  emulated time, collect the samples through `AudioResampler`, and count
  zero crossings. Assert the measured frequency is within a tolerance of the
  frequency the registers asked for. This proves the whole chain — APU,
  mixer, sampler, filter — in one assertion, and it is the only test in the
  project that would catch an APU that is correct against every ROM and
  still produces the wrong pitch.
- **The rate.** A second of emulated time produces 48,000 samples, ±1.
- **The filter.** A constant input decays toward zero; a symmetric square
  wave keeps its shape.
- **Mute.** Silence out, and the machine still advancing.
- **Drift.** The correction fires at the thresholds and not between them.

Every task mutation-tests its own tests, and writes the test first, runs it,
and watches it fail before implementing. In piece 5 three tasks skipped that
order and the fourth, which followed it, caught one of its own tests passing
against unfixed code — that is the evidence for the rule.

## Out of scope

- A volume control, for the reason above.
- Recording audio to a file.
- Any CGB audio behaviour.
