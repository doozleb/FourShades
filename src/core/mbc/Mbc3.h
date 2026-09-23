#pragma once

#include "core/mbc/Mbc.h"

namespace fourshades {

// MBC3, per Pan Docs "MBC3": cartridge types 0x0F-0x13. This task builds only
// the banking; the real-time clock behind register selects 0x08-0x0C arrives
// in a later task.
//
// A single RAM-and-timer enable gate, a 7-bit ROM bank register (0 acts as
// 1, the same shape as MBC1's low register but a bit wider and with no 0x1F
// mask), and a second register at 4000-5FFF that is overloaded: 0x00-0x03
// pick a RAM bank, 0x08-0x0C would pick a clock register once one exists,
// and 0x0D-0x0F are invalid. Until the clock exists, any value 0x04 and
// above simply selects nothing: the A000-BFFF window reads open bus and
// ignores writes, whatever RAM the cartridge has.
class Mbc3 : public Mbc {
public:
    explicit Mbc3(std::size_t ramBanks) : ramBanks_(ramBanks) {}

    std::size_t romBank(u16 address) const override;
    std::optional<u8> readRam(u16 address) const override;
    void writeRam(u16 address, u8 value) override;
    void writeControl(u16 address, u8 value) override;
    std::unique_ptr<Mbc> clone() const override;

private:
    bool ramBankSelected() const;
    bool ramReachable() const;
    std::size_t ramOffset(u16 address) const;

    std::size_t ramBanks_ = 0; // 8 KiB each
    bool ramEnabled_ = false;
    u8 romBank_ = 1;   // 2000-3FFF, 7 bits; 0 acts as 1
    u8 ramSelect_ = 0; // 4000-5FFF: 00-03 a RAM bank, 08-0C a clock register
                       // (not yet built), 0D-0F invalid; 04-0C and 0D-0F
                       // alike select nothing until Task 6 gives this chip
                       // a clock.
};

} // namespace fourshades
