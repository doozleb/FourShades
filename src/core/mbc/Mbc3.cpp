#include "core/mbc/Mbc3.h"

namespace fourshades {

namespace {
constexpr std::size_t kRamBank = 0x2000;
constexpr u8 kFirstClockRegister = 0x08;
constexpr u8 kLastClockRegister = 0x0C;
} // namespace

std::size_t Mbc3::romBank(u16 address) const {
    if (address < 0x4000) {
        return 0;
    }
    return romBank_;
}

bool Mbc3::clockSelected() const {
    return hasTimer_ && ramSelect_ >= kFirstClockRegister && ramSelect_ <= kLastClockRegister;
}

bool Mbc3::ramBankSelected() const {
    // 0x00-0x03 name a RAM bank. Everything else selects nothing: 0x04-0x07
    // is a gap Pan Docs doesn't name, 0x08-0x0C is the clock (or, on a
    // cartridge without one, nothing at all), 0x0D-0x0F is invalid.
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
    // The enable gate at 0000-1FFF covers the clock as well as the RAM: Pan
    // Docs calls it "RAM and Timer Enable", and 0x0A opens both.
    if (ramEnabled_ && clockSelected()) {
        return rtc_.read(ramSelect_);
    }
    if (!ramReachable()) {
        return std::nullopt;
    }
    return (*ram_)[ramOffset(address)];
}

void Mbc3::writeRam(u16 address, u8 value) {
    if (ramEnabled_ && clockSelected()) {
        rtc_.write(ramSelect_, value);
        return;
    }
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
    } else {
        // 6000-7FFF, the latch. Pan Docs gives the 0x00-then-0x01 sequence a
        // program writes; the chip here latches on any write, whatever the
        // value. That is the simpler of two rules the available evidence
        // cannot tell apart from this one: a write latches only when its
        // value differs from the previous write to this range, the same
        // 0x00-then-0x01 sequence generalised to arbitrary values. See
        // docs/known-divergences.md, "MBC3's clock: the register widths and
        // the latch, where Pan Docs is silent".
        if (hasTimer_) {
            rtc_.latch();
        }
    }
    // The caller never passes anything at or above 0x8000.
}

void Mbc3::tick() {
    if (hasTimer_) {
        rtc_.tick();
    }
}

std::unique_ptr<Mbc> Mbc3::clone() const {
    return std::make_unique<Mbc3>(*this);
}

} // namespace fourshades
