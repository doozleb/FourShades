#pragma once

#include "core/mbc/Mbc.h"

namespace fourshades {

// MBC2, per Pan Docs "MBC2": cartridge types 0x05-0x06.
//
// A single control-write window (0000-7FFF), decoded not by which half but
// by bit 8 of the address: clear enables or disables RAM, set selects a ROM
// bank (4 bits, 0 becomes 1, so up to 16 banks). Writes at or above 0x4000
// reach neither decode and do nothing.
//
// The chip carries its own 512 x 4 bits of RAM, addressed A000-A1FF and
// mirrored through BFFF; only the low nibble of every stored byte means
// anything; the cartridge still owns the storage (bindRam), the same as
// every other controller.
class Mbc2 : public Mbc {
public:
    std::size_t romBank(u16 address) const override;
    std::optional<u8> readRam(u16 address) const override;
    void writeRam(u16 address, u8 value) override;
    void writeControl(u16 address, u8 value) override;
    std::unique_ptr<Mbc> clone() const override;

private:
    bool ramReachable() const;
    std::size_t ramOffset(u16 address) const;

    bool ramEnabled_ = false;
    u8 romBank_ = 1; // 4 bits; 0 acts as 1
};

} // namespace fourshades
