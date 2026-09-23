#include "core/mbc/Mbc1.h"

namespace fourshades {

namespace {
constexpr std::size_t kRamBank = 0x2000;
} // namespace

std::size_t Mbc1::romBank(u16 address) const {
    // The high register's two bits sit just above the low register's own
    // width: bits 5-6 of the bank number ordinarily, bits 4-5 on a
    // multicart, where the low register is one bit narrower.
    const unsigned highShift = multicart_ ? 4 : 5;
    // The "0 acts as 1" substitution is a property of the full 5-bit low
    // register, evaluated before a multicart's wiring drops its top bit —
    // not a property of the narrowed 4-bit value. A write of 0x10 leaves the
    // 5-bit register at 16, which is not zero, so the substitution does not
    // fire; only then does the multicart drop bit 4, giving a bank
    // contribution of 0. See docs/known-divergences.md for the hardware
    // evidence this is measured from, not assumed.
    const u8 low = bankLow_ == 0 ? 1u : bankLow_;
    const u8 lowBits = multicart_ ? static_cast<u8>(low & 0x0F) : low;
    if (address < 0x4000) {
        // The upper bits reach the low half only in mode 1.
        return mode1_ ? std::size_t{bankHigh_} << highShift : 0;
    }
    return (std::size_t{bankHigh_} << highShift) | lowBits;
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
        // Always 5 bits wide in storage, multicart included: the top bit is
        // simply not wired to the bank output (see romBank()), but it is
        // still latched, and the zero substitution still reads it.
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
