#include "core/mbc/Mbc2.h"

namespace fourshades {

std::size_t Mbc2::romBank(u16 address) const {
    if (address < 0x4000) {
        return 0;
    }
    return romBank_;
}

bool Mbc2::ramReachable() const {
    return ramEnabled_ && ram_ != nullptr && !ram_->empty();
}

std::size_t Mbc2::ramOffset(u16 address) const {
    return (address - 0xA000) & 0x01FF;
}

std::optional<u8> Mbc2::readRam(u16 address) const {
    if (!ramReachable()) {
        return std::nullopt;
    }
    return static_cast<u8>(0xF0 | ((*ram_)[ramOffset(address)] & 0x0F));
}

void Mbc2::writeRam(u16 address, u8 value) {
    if (!ramReachable()) {
        return;
    }
    (*ram_)[ramOffset(address)] = static_cast<u8>(value & 0x0F);
}

void Mbc2::writeControl(u16 address, u8 value) {
    if (address >= 0x4000) {
        // 4000-7FFF: nothing. Only the low window is decoded at all; the
        // caller never passes anything at or above 0x8000.
        return;
    }
    if ((address & 0x0100) == 0) {
        ramEnabled_ = (value & 0x0F) == 0x0A;
    } else {
        const u8 bank = static_cast<u8>(value & 0x0F);
        romBank_ = bank == 0 ? 1 : bank;
    }
}

std::unique_ptr<Mbc> Mbc2::clone() const {
    return std::make_unique<Mbc2>(*this);
}

} // namespace fourshades
