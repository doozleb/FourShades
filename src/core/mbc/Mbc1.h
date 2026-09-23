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
    explicit Mbc1(std::size_t ramBanks) : ramBanks_(ramBanks) {}

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
    u8 bankLow_ = 0;           // 2000-3FFF, 5 bits; 0 acts as 1
    u8 bankHigh_ = 0;          // 4000-5FFF, 2 bits
    bool mode1_ = false;       // 6000-7FFF
};

} // namespace fourshades
