// FourShades in a browser.
//
// This is the whole of the WebAssembly side, and it is deliberately thin: the
// same rule the SDL window follows applies here, so `src/core/` gains nothing
// and knows nothing about either front end. Everything below is glue.
//
// The page drives it. There is no main loop in C++ -- JavaScript decides when
// a frame is due, because the browser's clocks (requestAnimationFrame, and
// the audio device's own) are the only honest source of time in a tab that
// can be throttled, backgrounded or moved to a different display. Piece 3b
// measured what happens when an emulator takes a 60.000 Hz signal for the
// Game Boy's 59.7275 Hz: +0.4565%, 7.9 cents sharp. A browser's rAF is that
// same 60.000 Hz, so the page paces on elapsed time instead and this file
// simply runs exactly as many cycles as it is asked for.
//
// Everything here is `extern "C"` and takes or returns plain numbers, so the
// JavaScript side needs no bindings library and no glue generator -- just
// cwrap over the exported names.

#include "core/Cartridge.h"
#include "core/GameBoy.h"
#include "core/Joypad.h"
#include "core/Ppu.h"
#include "core/apu/Apu.h"

#include <emscripten/emscripten.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using fourshades::Cartridge;
using fourshades::GameBoy;
using fourshades::Ppu;
using fourshades::u8;

namespace {

// The machine, and the ROM bytes it was built from. The bytes are kept so a
// reset can rebuild the cartridge from them rather than carrying a second
// copy of the machine around: a Cartridge holds battery RAM and, on an MBC3,
// a running clock, and a reset must not keep either.
std::unique_ptr<GameBoy> gb;
std::vector<u8> romBytes;
std::string lastError;

// The framebuffer the page reads, as bytes rather than the core's array, so
// JavaScript can take a view straight onto WebAssembly memory without a copy
// per frame. Shades 0-3, exactly as the PPU produced them; the page owns the
// palette, because which four greens or greys these become is a decision for
// whoever is looking at it.
u8 frameBytes[Ppu::kWidth * Ppu::kHeight];

// Audio. The core answers "what is the level right now" and has no sample
// rate, which is what let the SDL front end choose 48 kHz and lets this one
// take whatever the browser's AudioContext turns out to run at. The ratio is
// kept as an exact integer fraction for the same reason it is on the desktop:
// accumulating 21.845... M-cycles per sample as a float drifts inaudibly over
// a frame and audibly over a minute.
constexpr std::uint64_t kMCyclesPerSecond = 1048576;
std::uint32_t sampleRate = 48000;
std::uint64_t sampleNumerator = kMCyclesPerSecond;   // reduced at init
std::uint64_t sampleDenominator = 48000;             // reduced at init
std::uint64_t sampleAccumulator = 0;
std::uint64_t lastCycles = 0;

// The DC blocker, the same one the desktop uses: a DMG's output sits behind a
// capacitor, and without it four channels' offsets stack into a click on every
// trigger. The charge constant is computed from the real rate rather than
// written down, because this front end does not know its rate until the page
// tells it.
float dcCharge = 0.0f;
float dcCapacitorL = 0.0f;
float dcCapacitorR = 0.0f;

// Interleaved stereo, drained by the page each time it asks for audio.
std::vector<float> samples;

std::uint64_t gcd64(std::uint64_t a, std::uint64_t b) {
    while (b != 0) {
        const std::uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

void resetAudioClock() {
    const std::uint64_t d = gcd64(kMCyclesPerSecond, sampleRate);
    sampleNumerator = kMCyclesPerSecond / d;
    sampleDenominator = sampleRate / d;
    sampleAccumulator = 0;
    lastCycles = gb ? gb->cycles() : 0;
    dcCapacitorL = 0.0f;
    dcCapacitorR = 0.0f;
    // 0.999958 per T-cycle is the widely used DMG figure; it is an
    // approximation of a real capacitor, not a measurement, and is documented
    // as a choice in docs/known-divergences.md. The exponent is fractional --
    // 87.38 T-cycles to a sample at 48 kHz -- so this is std::pow rather than
    // a loop, which would silently drop the fraction and give a filter
    // tuned for a rate nobody asked for.
    const double tCyclesPerSample = 4.0 * static_cast<double>(kMCyclesPerSecond)
                                    / static_cast<double>(sampleRate);
    dcCharge = static_cast<float>(std::pow(0.999958, tCyclesPerSample));
}

// Pull every sample that has fallen due since the last call.
void drainAudio() {
    if (!gb) {
        return;
    }
    const std::uint64_t now = gb->cycles();
    if (now <= lastCycles) {
        return;
    }
    sampleAccumulator += (now - lastCycles) * sampleDenominator;
    lastCycles = now;
    while (sampleAccumulator >= sampleNumerator) {
        sampleAccumulator -= sampleNumerator;
        const auto s = gb->apu().sample();
        const float outL = s.left - dcCapacitorL;
        dcCapacitorL = s.left - outL * dcCharge;
        const float outR = s.right - dcCapacitorR;
        dcCapacitorR = s.right - outR * dcCharge;
        samples.push_back(outL);
        samples.push_back(outR);
    }
}

} // namespace

extern "C" {

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

// Hands the page a buffer to write ROM bytes into. Returns nullptr if the
// allocation fails, which on a machine that cannot hold a 8 MiB cartridge is
// the least of anyone's problems, but is reported rather than crashed on.
EMSCRIPTEN_KEEPALIVE u8* fs_rom_buffer(int size) {
    if (size <= 0) {
        return nullptr;
    }
    romBytes.assign(static_cast<std::size_t>(size), 0);
    return romBytes.data();
}

// Builds the machine from whatever is in that buffer. Returns 1 on success,
// 0 with a message in fs_last_error(): an unsupported controller is a normal
// thing for a person to run into, not a crash.
EMSCRIPTEN_KEEPALIVE int fs_load() {
    lastError.clear();
    std::string error;
    auto cart = Cartridge::load(romBytes, &error);
    if (!cart.has_value()) {
        lastError = error;
        gb.reset();
        return 0;
    }
    gb = std::make_unique<GameBoy>(std::move(*cart));
    resetAudioClock();
    samples.clear();
    return 1;
}

EMSCRIPTEN_KEEPALIVE const char* fs_last_error() {
    return lastError.c_str();
}

EMSCRIPTEN_KEEPALIVE int fs_loaded() {
    return gb ? 1 : 0;
}

// Rebuilds the machine from the ROM bytes already held. The cartridge is
// built afresh, so battery RAM and an MBC3's clock start over -- which is what
// a reset does to a Game Boy that has just been switched off and on.
EMSCRIPTEN_KEEPALIVE int fs_reset() {
    if (romBytes.empty()) {
        return 0;
    }
    return fs_load();
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

// Runs until the PPU finishes a frame, or until `maxCycles` M-cycles have
// passed, whichever comes first. The cap is not a nicety: the PPU completes no
// frames while the LCD is off, and a program can leave it off for as long as
// it likes, so an uncapped "run one frame" hands the tab an unbounded stretch
// of work and a lump of audio to match. The desktop front end learned that the
// hard way -- it produced half a second of latency at every screen transition.
// Returns 1 if a frame completed.
EMSCRIPTEN_KEEPALIVE int fs_run_frame(int maxCycles) {
    if (!gb) {
        return 0;
    }
    const std::uint64_t before = gb->ppu().frameCount();
    const std::uint64_t deadline = gb->cycles() + static_cast<std::uint64_t>(maxCycles);
    while (gb->ppu().frameCount() == before && gb->cycles() < deadline) {
        gb->step();
        // After each step, not once at the end of the frame. Apu::sample()
        // answers "what is the level right now", so draining at the frame
        // boundary emitted a whole frame of samples that all read the same
        // instant: the output became a staircase changing 60 times a second
        // instead of the machine's waveform. The desktop front end already
        // says this in app/main.cpp; this side had it wrong.
        drainAudio();
    }
    return gb->ppu().frameCount() != before ? 1 : 0;
}

// The 160x144 frame, shades 0-3, as a pointer into WebAssembly memory.
EMSCRIPTEN_KEEPALIVE const u8* fs_frame() {
    if (gb) {
        const auto& f = gb->ppu().frame();
        for (int i = 0; i < Ppu::kWidth * Ppu::kHeight; ++i) {
            frameBytes[i] = f[static_cast<std::size_t>(i)];
        }
    }
    return frameBytes;
}

EMSCRIPTEN_KEEPALIVE int fs_frame_width() { return Ppu::kWidth; }
EMSCRIPTEN_KEEPALIVE int fs_frame_height() { return Ppu::kHeight; }

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// The plain-English mask from core/Joypad.h: bit 0 Right, 1 Left, 2 Up,
// 3 Down, 4 A, 5 B, 6 Select, 7 Start. A set bit is held down. P1's
// active-low wiring is Joypad's business and nobody else's.
EMSCRIPTEN_KEEPALIVE void fs_set_buttons(int mask) {
    if (gb) {
        gb->setButtons(static_cast<u8>(mask & 0xFF));
    }
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

// Tells this side what the page's AudioContext actually runs at. Browsers pick
// their own rate -- 48000 on most desktops, 44100 on plenty of hardware -- and
// resampling on top of a rate we guessed wrong would be an octave of error
// hiding behind a plausible-looking number.
EMSCRIPTEN_KEEPALIVE void fs_set_sample_rate(int rate) {
    if (rate > 0) {
        sampleRate = static_cast<std::uint32_t>(rate);
        resetAudioClock();
        samples.clear();
    }
}

EMSCRIPTEN_KEEPALIVE int fs_audio_available() {
    return static_cast<int>(samples.size());
}

EMSCRIPTEN_KEEPALIVE const float* fs_audio_data() {
    return samples.data();
}

EMSCRIPTEN_KEEPALIVE void fs_audio_clear() {
    samples.clear();
}

// Throws the backlog away. The page calls this when its own queue has run too
// deep -- the same judgement the desktop makes, and for the same reason: above
// a few frames a backlog is not drift and correcting it a sample at a time
// takes minutes, which is heard as a lag that never goes away.
EMSCRIPTEN_KEEPALIVE void fs_audio_drop(int keepSamples) {
    const std::size_t keep = static_cast<std::size_t>(keepSamples) * 2;
    if (keep < samples.size()) {
        samples.erase(samples.begin(), samples.end() - static_cast<std::ptrdiff_t>(keep));
    }
}

// ---------------------------------------------------------------------------
// Battery saves
// ---------------------------------------------------------------------------

// Cartridge RAM, for the page to persist. The core reports whether a battery
// is behind it; what persistence means, and where it is kept, is the front
// end's business -- localStorage here, a file on the desktop.
EMSCRIPTEN_KEEPALIVE int fs_has_battery() {
    return gb && gb->cartridge().hasBattery() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int fs_ram_size() {
    return gb ? static_cast<int>(gb->cartridge().ram().size()) : 0;
}

EMSCRIPTEN_KEEPALIVE const u8* fs_ram_data() {
    return gb && !gb->cartridge().ram().empty() ? gb->cartridge().ram().data() : nullptr;
}

// Refuses anything but an exact-size match, exactly as the desktop's loader
// does: the header decides how much RAM a cartridge has, and a caller cannot
// talk it into a different amount.
EMSCRIPTEN_KEEPALIVE int fs_set_ram(const u8* data, int size) {
    if (!gb || data == nullptr || size < 0) {
        return 0;
    }
    std::vector<u8> bytes(data, data + size);
    return gb->cartridge().setRam(bytes) ? 1 : 0;
}

} // extern "C"
