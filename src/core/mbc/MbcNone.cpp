#include "core/mbc/MbcNone.h"

namespace fourshades {

std::size_t MbcNone::romBank(u16 address) const {
    return address < 0x4000 ? 0 : 1;
}

std::optional<u8> MbcNone::readRam(u16) const {
    return std::nullopt;
}

void MbcNone::writeRam(u16, u8) {}

void MbcNone::writeControl(u16, u8) {}

std::unique_ptr<Mbc> MbcNone::clone() const {
    return std::make_unique<MbcNone>(*this);
}

} // namespace fourshades
