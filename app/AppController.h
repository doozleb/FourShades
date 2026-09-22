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
    std::optional<fourshades::GameBoy> gameBoy_;
};

} // namespace app
