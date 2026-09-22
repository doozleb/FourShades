#pragma once

#include "core/Types.h"

namespace fourshades {

// The bits of the button mask handed to Joypad::setButtons. They are the
// positions of the 2x4 matrix Pan Docs draws for P1 ("Joypad Input"): the low
// nibble is the d-pad group, selected by P1 bit 4, and the high nibble the
// action group, selected by P1 bit 5. Within each nibble the order is P1's
// own: bit 3 Down/Start, bit 2 Up/Select, bit 1 Left/B, bit 0 Right/A. A set
// bit means held down -- the mask is the plain-English one; it is P1 that is
// active low, and only Joypad knows that.
namespace button {
constexpr u8 Right = 0x01;
constexpr u8 Left = 0x02;
constexpr u8 Up = 0x04;
constexpr u8 Down = 0x08;
constexpr u8 A = 0x10;
constexpr u8 B = 0x20;
constexpr u8 Select = 0x40;
constexpr u8 Start = 0x80;
} // namespace button

// P1/JOYP (FF00), per Pan Docs "Joypad Input". Only bits 4 and 5 are
// writable, and each selects a group of four buttons when it is 0. The low
// nibble is the state of the four matrix lines P10-P13, read-only and active
// low: a line reads 0 when a button on it is held in a selected group.
//
// Both select bits can be 0 at once. The four lines are then shared by both
// groups, so a line is low when either group's button on it is held -- the
// two nibbles wired together, which reads as their bitwise AND. Pan Docs says
// as much where it explains the interrupt: "If both are selected and, for
// example, a bit is already held Low by an action button, pressing the
// corresponding direction button would make no difference"
// (https://gbdev.io/pandocs/Interrupt_Sources.html, INT $60). With neither
// group selected the lines are driven by nothing and read 0xF, which is Pan
// Docs' "If neither buttons nor d-pad is selected ($30 was written), then the
// low nibble reads $F (all buttons released)".
//
// Bits 7 and 6 are unused and always read 1.
class Joypad {
public:
    // The buttons held right now, as a mask of the button:: constants above.
    // Returns true when this changed at least one line from high to low, which
    // is the joypad interrupt: "The Joypad interrupt is requested when any of
    // P1 bits 0-3 change from High to Low" (Pan Docs, INT $60).
    bool setButtons(u8 pressed);

    // A write to FF00. Only bits 4-5 land; the rest are ignored. Returns true
    // on the same high-to-low line transition setButtons() reports, because a
    // write that selects a group with a button already held drives that line
    // low just as pressing the button would.
    bool write(u8 value);

    // The whole register: bits 7-6 set, the select bits as written, the four
    // lines in the low nibble.
    u8 read() const;

    // True while any line reads low. Pan Docs' way out of STOP mode is
    // "one of the P10 to P13 lines going low" (Reducing Power Consumption:
    // Using the STOP Instruction), so this is what the CPU watches there.
    bool anyLineLow() const { return lines() != 0x0F; }

private:
    // The low nibble: bit n is 0 when a selected button on line n is held.
    u8 lines() const;

    u8 pressed_ = 0x00; // button:: bits, 1 = held
    u8 select_ = 0x00;  // P1 bits 4-5 as written; 0 = that group is selected
};

} // namespace fourshades
