#pragma once

#include "core/mbc/Mbc.h"

namespace fourshades {

// MBC1, per Pan Docs "MBC1": cartridge types 0x01-0x03.
//
// Two bank registers and a mode bit. In mode 0 the upper two bits extend the
// ROM bank only, 0000-3FFF is fixed at bank 0 and cartridge RAM is fixed at
// bank 0; in mode 1 those upper bits also reach 0000-3FFF and select the RAM
// bank instead.
class Mbc1 : public Mbc {
public:
    // multicart narrows the bank number's low half from 5 bits to 4 and
    // moves the high register's two bits from bits 5-6 of the bank number to
    // bits 4-5, per the compilation cartridges wired that way; everything
    // else, including mode 1 and the low register's own "0 acts as 1"
    // substitution (still evaluated on the full 5-bit register before the
    // top bit is dropped — see romBank()), is unchanged. Cartridge decides
    // this from the ROM's own bytes (see docs/known-divergences.md for how,
    // why, and the hardware-verified test the bit-drop timing is measured
    // from) and it carries across clone() like every other register here.
    explicit Mbc1(std::size_t ramBanks, bool multicart = false)
        : ramBanks_(ramBanks), multicart_(multicart) {}

    std::size_t romBank(u16 address) const override;
    std::optional<u8> readRam(u16 address) const override;
    void writeRam(u16 address, u8 value) override;
    void writeControl(u16 address, u8 value) override;
    std::unique_ptr<Mbc> clone() const override;

private:
    bool ramReachable() const;
    std::size_t ramOffset(u16 address) const;

    std::size_t ramBanks_ = 0; // 8 KiB each
    bool ramEnabled_ = false;
    u8 bankLow_ = 0;           // 2000-3FFF, 5 bits always; 0 acts as 1 (before
                               // a multicart's wiring drops bit 4 — see romBank())
    u8 bankHigh_ = 0;          // 4000-5FFF, 2 bits
    bool mode1_ = false;       // 6000-7FFF
    bool multicart_ = false;
};

} // namespace fourshades
