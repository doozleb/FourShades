#pragma once

#include "core/mbc/Mbc.h"

namespace fourshades {

// No controller at all: cartridge type 0x00, 32 KiB of ROM wired straight to
// the bus. Writes below 0x8000 land on nothing, and there is no RAM window.
class MbcNone : public Mbc {
public:
    std::size_t romBank(u16 address) const override;
    std::optional<u8> readRam(u16 address) const override;
    void writeRam(u16 address, u8 value) override;
    void writeControl(u16 address, u8 value) override;
    std::unique_ptr<Mbc> clone() const override;
};

} // namespace fourshades
