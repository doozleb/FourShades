#pragma once

#include "core/mbc/Mbc.h"

namespace fourshades {

// MBC5, per Pan Docs "MBC5": cartridge types 0x19-0x1E.
//
// A 9-bit ROM bank register split across two write windows, a 4-bit RAM bank
// register (3 bits on the rumble variants, whose fourth bit is the motor and
// never reaches banking), and no 6000-7FFF mode register at all. Unlike
// MBC1, bank 0 is genuinely selectable at 4000-7FFF: there is no 0 -> 1
// remap of a write. Power-on is a different question from that write path:
// like every other MBC, the chip comes up already showing bank 1 at
// 4000-7FFF, before software ever touches the bank register.
class Mbc5 : public Mbc {
public:
    // rumble is true for types 0x1C-0x1E, where bit 3 of the RAM bank
    // register is the motor and must never reach the bank number.
    Mbc5(std::size_t ramBanks, bool rumble) : ramBanks_(ramBanks), rumble_(rumble) {}

    std::size_t romBank(u16 address) const override;
    std::optional<u8> readRam(u16 address) const override;
    void writeRam(u16 address, u8 value) override;
    void writeControl(u16 address, u8 value) override;
    std::unique_ptr<Mbc> clone() const override;

private:
    bool ramReachable() const;
    std::size_t ramOffset(u16 address) const;

    std::size_t ramBanks_ = 0; // 8 KiB each
    bool rumble_ = false;
    bool ramEnabled_ = false;
    u8 romBankLow_ = 1;  // 2000-2FFF, all 8 bits; powers on at bank 1, not 0
    u8 romBankHigh_ = 0; // 3000-3FFF bit 0
    u8 ramBank_ = 0;     // 4000-5FFF, 4 bits (3 on rumble types; the motor bit is masked off at the write)
};

} // namespace fourshades
