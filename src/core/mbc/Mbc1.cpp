#include "core/mbc/Mbc1.h"

namespace fourshades {

namespace {
constexpr std::size_t kRamBank = 0x2000;
} // namespace

std::size_t Mbc1::romBank(u16 address) const {
    if (address < 0x4000) {
        // The upper bits reach the low half only in mode 1.
        return mode1_ ? std::size_t{bankHigh_} << 5 : 0;
    }
    return (std::size_t{bankHigh_} << 5) | (bankLow_ == 0 ? 1u : bankLow_);
}

bool Mbc1::ramReachable() const {
    return ramEnabled_ && ram_ != nullptr && !ram_->empty();
}

std::size_t Mbc1::ramOffset(u16 address) const {
    // RAM banks switch in mode 1 only; mode 0 sees bank 0 whatever the upper
    // register holds.
    const std::size_t bank = mode1_ ? (bankHigh_ & (ramBanks_ - 1)) : 0;
    return bank * kRamBank + (address - 0xA000);
}

std::optional<u8> Mbc1::readRam(u16 address) const {
    if (!ramReachable()) {
        return std::nullopt;
    }
    return (*ram_)[ramOffset(address)];
}

void Mbc1::writeRam(u16 address, u8 value) {
    if (!ramReachable()) {
        return;
    }
    (*ram_)[ramOffset(address)] = value;
}

void Mbc1::writeControl(u16 address, u8 value) {
    if (address < 0x2000) {
        ramEnabled_ = (value & 0x0F) == 0x0A;
    } else if (address < 0x4000) {
        bankLow_ = static_cast<u8>(value & 0x1F);
    } else if (address < 0x6000) {
        bankHigh_ = static_cast<u8>(value & 0x03);
    } else {
        // 6000-7FFF; the caller never passes anything at or above 0x8000.
        mode1_ = (value & 0x01) != 0;
    }
}

std::unique_ptr<Mbc> Mbc1::clone() const {
    return std::make_unique<Mbc1>(*this);
}

} // namespace fourshades
