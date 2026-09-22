#include "app/AppController.h"

namespace app {

bool AppController::loadRom(std::vector<fourshades::u8> bytes) {
    std::string error;
    std::optional<fourshades::Cartridge> cartridge = fourshades::Cartridge::load(std::move(bytes), &error);
    if (!cartridge.has_value()) {
        lastError_ = error;
        gameBoy_.reset();
        pristine_.reset();
        state_ = AppState::Waiting;
        return false;
    }
    pristine_ = *cartridge;
    // emplace() destroys any machine already running and constructs a fresh
    // one in its place: a second drop starts clean rather than reusing the
    // running machine's state.
    gameBoy_.emplace(std::move(*cartridge));
    lastError_.clear();
    state_ = AppState::Running;
    return true;
}

bool AppController::reset() {
    if (state_ != AppState::Running || !pristine_.has_value()) {
        return false;
    }
    fourshades::Cartridge cartridge = *pristine_;
    if (gameBoy_->cartridge().hasBattery()) {
        // The battery held this through the power cut, so it holds it
        // through the reset. setRam only accepts the exact size, which is
        // the same cartridge's own, so it cannot fail here.
        cartridge.setRam(gameBoy_->cartridge().ram());
    }
    gameBoy_.emplace(std::move(cartridge));
    lastError_.clear();
    return true;
}

} // namespace app
