#include "core/mbc/Mbc3.h"

namespace fourshades {

namespace {
constexpr std::size_t kRamBank = 0x2000;
} // namespace

std::size_t Mbc3::romBank(u16 address) const {
    if (address < 0x4000) {
        return 0;
    }
    return romBank_;
}

bool Mbc3::ramBankSelected() const {
    // 0x00-0x03 name a RAM bank. Everything else (0x04-0x0C: the unbuilt
    // clock registers alongside a gap Pan Docs doesn't name; 0x0D-0x0F:
    // invalid) selects nothing yet.
    return ramSelect_ <= 0x03;
}

bool Mbc3::ramReachable() const {
    return ramEnabled_ && ramBankSelected() && ram_ != nullptr && !ram_->empty();
}

std::size_t Mbc3::ramOffset(u16 address) const {
    const std::size_t bank = ramSelect_ & (ramBanks_ - 1);
    return bank * kRamBank + (address - 0xA000);
}

std::optional<u8> Mbc3::readRam(u16 address) const {
    if (!ramReachable()) {
        return std::nullopt;
    }
    return (*ram_)[ramOffset(address)];
}

void Mbc3::writeRam(u16 address, u8 value) {
    if (!ramReachable()) {
        return;
    }
    (*ram_)[ramOffset(address)] = value;
}

void Mbc3::writeControl(u16 address, u8 value) {
    if (address < 0x2000) {
        // Pan Docs' rule for RAM (and timer) enable, same shape as MBC1's.
        ramEnabled_ = (value & 0x0F) == 0x0A;
    } else if (address < 0x4000) {
        const u8 bank = static_cast<u8>(value & 0x7F);
        romBank_ = bank == 0 ? 1 : bank;
    } else if (address < 0x6000) {
        ramSelect_ = static_cast<u8>(value & 0x0F);
    }
    // 6000-7FFF: the latch, arriving with the clock in a later task; the
    // caller never passes anything at or above 0x8000.
}

std::unique_ptr<Mbc> Mbc3::clone() const {
    return std::make_unique<Mbc3>(*this);
}

} // namespace fourshades
