#pragma once

// Keyboard to Game Boy buttons. This is the only place in FourShades that
// knows a keyboard exists: the core is handed a mask of pressed buttons and
// nothing else, which tools/check_core_isolation.py keeps true.

#include "core/Joypad.h"
#include "core/Types.h"

#include <SDL3/SDL_scancode.h>

#include <array>

namespace app {

struct Binding {
    SDL_Scancode key;
    fourshades::u8 button;
};

// Arrows for the d-pad, Z and X for A and B, Enter and Backspace for Start
// and Select.
//
// These are physical keys (scancodes), not characters, because that is how
// SDL's keyboard-state array is indexed. The arrows, Enter and Backspace sit
// in the same place on every layout; Z and X are the US positions, so on a
// layout that moves them (AZERTY's W, QWERTZ's Y) the two keys stay where a
// player's left hand expects them rather than where the letters are.
inline constexpr std::array<Binding, 8> kBindings{{
    {SDL_SCANCODE_RIGHT, fourshades::button::Right},
    {SDL_SCANCODE_LEFT, fourshades::button::Left},
    {SDL_SCANCODE_UP, fourshades::button::Up},
    {SDL_SCANCODE_DOWN, fourshades::button::Down},
    {SDL_SCANCODE_Z, fourshades::button::A},
    {SDL_SCANCODE_X, fourshades::button::B},
    {SDL_SCANCODE_RETURN, fourshades::button::Start},
    {SDL_SCANCODE_BACKSPACE, fourshades::button::Select},
}};

// Which buttons are held, from SDL_GetKeyboardState's array: `state` is
// indexed by scancode and `numKeys` is its length. The whole keyboard is read
// once per frame rather than key-down and key-up events being tracked, so no
// key can stay stuck down when the window loses focus in the middle of a
// press -- SDL clears the array, and the next frame sees nothing held.
// A null array, or one too short to reach a binding, simply holds nothing.
fourshades::u8 buttonMask(const bool* state, int numKeys);

} // namespace app
