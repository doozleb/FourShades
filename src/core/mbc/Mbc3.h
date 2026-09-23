#pragma once

#include "core/mbc/Mbc.h"
#include "core/mbc/Rtc.h"

namespace fourshades {

// MBC3, per Pan Docs "MBC3": cartridge types 0x0F-0x13.
//
// A single RAM-and-timer enable gate, a 7-bit ROM bank register (0 acts as
// 1, the same shape as MBC1's low register but a bit wider and with no 0x1F
// mask), and a second register at 4000-5FFF that is overloaded: 0x00-0x03
// pick a RAM bank, 0x08-0x0C pick a clock register on a cartridge that has a
// clock, and everything else (0x04-0x07, 0x0D-0x0F) selects nothing, so the
// A000-BFFF window reads open bus and ignores writes.
//
// Types 0x0F and 0x10 are the ones with the clock. On the other three the
// 0x08-0x0C selects decode to nothing, exactly like 0x04-0x07.
//
// 6000-7FFF latches the clock: any write there copies the live registers over
// the latched ones. Pan Docs describes the 0x00-then-0x01 sequence a program
// uses, and says nothing about the values in between; the chip does not look
// at them. See docs/known-divergences.md, "MBC3's clock: the register widths
// and the latch, where Pan Docs is silent".
class Mbc3 : public Mbc {
public:
    Mbc3(std::size_t ramBanks, bool hasTimer)
        : ramBanks_(ramBanks), hasTimer_(hasTimer) {}

    std::size_t romBank(u16 address) const override;
    std::optional<u8> readRam(u16 address) const override;
    void writeRam(u16 address, u8 value) override;
    void writeControl(u16 address, u8 value) override;
    void tick() override;
    Rtc* rtc() override { return hasTimer_ ? &rtc_ : nullptr; }
    const Rtc* rtc() const override { return hasTimer_ ? &rtc_ : nullptr; }
    std::unique_ptr<Mbc> clone() const override;

private:
    bool clockSelected() const;
    bool ramBankSelected() const;
    bool ramReachable() const;
    std::size_t ramOffset(u16 address) const;

    std::size_t ramBanks_ = 0; // 8 KiB each
    bool hasTimer_ = false;
    bool ramEnabled_ = false;
    u8 romBank_ = 1;   // 2000-3FFF, 7 bits; 0 acts as 1
    u8 ramSelect_ = 0; // 4000-5FFF: 00-03 a RAM bank, 08-0C a clock register,
                       // 04-07 and 0D-0F nothing at all
    Rtc rtc_;
};

} // namespace fourshades
