#include "core/mbc/Mbc5.h"

namespace fourshades {

namespace {
constexpr std::size_t kRamBank = 0x2000;
} // namespace

std::size_t Mbc5::romBank(u16 address) const {
    if (address < 0x4000) {
        // Bank 0 is genuinely selectable here: unlike MBC1, there is no
        // remap, so 0000-3FFF is simply always bank 0.
        return 0;
    }
    return (std::size_t{romBankHigh_} << 8) | romBankLow_;
}

bool Mbc5::ramReachable() const {
    return ramEnabled_ && ram_ != nullptr && !ram_->empty();
}

std::size_t Mbc5::ramOffset(u16 address) const {
    const std::size_t bank = ramBank_ & (ramBanks_ - 1);
    return bank * kRamBank + (address - 0xA000);
}

std::optional<u8> Mbc5::readRam(u16 address) const {
    if (!ramReachable()) {
        return std::nullopt;
    }
    return (*ram_)[ramOffset(address)];
}

void Mbc5::writeRam(u16 address, u8 value) {
    if (!ramReachable()) {
        return;
    }
    (*ram_)[ramOffset(address)] = value;
}

void Mbc5::writeControl(u16 address, u8 value) {
    if (address < 0x2000) {
        ramEnabled_ = (value & 0x0F) == 0x0A;
    } else if (address < 0x3000) {
        romBankLow_ = value;
    } else if (address < 0x4000) {
        romBankHigh_ = static_cast<u8>(value & 0x01);
    } else if (address < 0x6000) {
        // The motor bit (bit 3) never reaches the bank number on rumble
        // types; it drives nothing here.
        ramBank_ = static_cast<u8>(value & (rumble_ ? 0x07 : 0x0F));
    }
    // 6000-7FFF: nothing. The caller never passes anything at or above 0x8000.
}

std::unique_ptr<Mbc> Mbc5::clone() const {
    return std::make_unique<Mbc5>(*this);
}

} // namespace fourshades
