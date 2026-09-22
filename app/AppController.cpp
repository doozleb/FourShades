#include "app/AppController.h"

namespace app {

bool AppController::loadRom(std::vector<fourshades::u8> bytes) {
    std::string error;
    std::optional<fourshades::Cartridge> cartridge = fourshades::Cartridge::load(std::move(bytes), &error);
    if (!cartridge.has_value()) {
        lastError_ = error;
        gameBoy_.reset();
        state_ = AppState::Waiting;
        return false;
    }
    // emplace() destroys any machine already running and constructs a fresh
    // one in its place: a second drop starts clean rather than reusing the
    // running machine's state.
    gameBoy_.emplace(std::move(*cartridge));
    lastError_.clear();
    state_ = AppState::Running;
    return true;
}

} // namespace app
