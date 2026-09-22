#pragma once

// The window's state machine, kept free of SDL so it can be unit-tested
// without a window: waiting with no ROM, running one, or back to waiting
// after a failed load. main.cpp drives this and only this from the event
// loop; it never touches Cartridge or GameBoy construction directly.

#include "core/Cartridge.h"
#include "core/GameBoy.h"
#include "core/Types.h"

#include <optional>
#include <string>
#include <vector>

namespace app {

enum class AppState { Waiting, Running };

class AppController {
public:
    AppController() = default;
    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    // Attempts to load `bytes` as a cartridge and start a fresh machine from
    // it. On success, moves to Running -- replacing any machine already
    // running, rather than reusing it -- and returns true. On failure,
    // (re)moves to Waiting, discards any running machine, records
    // Cartridge::load's message verbatim (see lastError()), and returns
    // false.
    bool loadRom(std::vector<fourshades::u8> bytes);

    // Rebuilds the machine from the cartridge this controller loaded, as a
    // power cycle would: a brand new GameBoy over a pristine copy of the
    // cartridge, never the running one poked back towards its starting
    // state. Everything the machine held -- CPU registers, RAM, VRAM, the
    // PPU's frame counter, the cartridge's bank registers -- is gone with
    // it.
    //
    // The one thing that survives is battery-backed cartridge RAM, because
    // that is what a battery is for: a DMG has no reset button, so the
    // nearest real thing is switching it off and on again, and a saved game
    // lives through that. RAM with no battery behind it does not, and is
    // cleared.
    //
    // Does nothing and returns false when no ROM is loaded.
    bool reset();

    AppState state() const { return state_; }

    // Cartridge::load's message, verbatim, from the most recent failed
    // loadRom() call. Empty after a successful load.
    const std::string& lastError() const { return lastError_; }

    // Only valid when state() == Running.
    fourshades::GameBoy& gameBoy() { return *gameBoy_; }
    const fourshades::GameBoy& gameBoy() const { return *gameBoy_; }

private:
    AppState state_ = AppState::Waiting;
    std::string lastError_;
    // The cartridge exactly as it was loaded, untouched by the machine that
    // runs: reset() copies this rather than re-parsing the image, so a reset
    // cannot inherit a bank register or a byte of RAM from the session it
    // ends.
    std::optional<fourshades::Cartridge> pristine_;
    std::optional<fourshades::GameBoy> gameBoy_;
};

} // namespace app
