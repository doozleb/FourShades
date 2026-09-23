#include "core/Cartridge.h"

#include "core/mbc/Mbc.h"
#include "core/mbc/Mbc1.h"
#include "core/mbc/Mbc2.h"
#include "core/mbc/Mbc3.h"
#include "core/mbc/Mbc5.h"
#include "core/mbc/MbcNone.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <utility>

namespace fourshades {

namespace {

constexpr std::size_t kRomBank = 0x4000;
constexpr std::size_t kRamBank = 0x2000;

// The 48-byte Nintendo logo a header carries at 0x0104-0x0133; every
// cartridge that boots on real hardware has this at its own header, since the
// boot ROM refuses to run otherwise.
constexpr std::array<u8, 48> kNintendoLogo = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
    0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
    0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

// A compilation cartridge wires an ordinary MBC1 chip to pick between several
// smaller games rather than banking one large one: the low bank register
// narrows to 4 bits and the high register's bits supply bits 4-5 instead of
// 5-6, so each quarter of the image is a self-contained 256 KiB game with its
// own header, logo included. No header byte declares this, so it is inferred
// from the ROM's own bytes: a 1 MiB image whose logo (the bytes every
// cartridge must carry at its own header to boot at all) also appears at
// three or more of the four 256 KiB boundaries. See
// docs/known-divergences.md for why three rather than four, and what would
// overturn the heuristic.
bool logoAt(const std::vector<u8>& rom, std::size_t offset) {
    return std::equal(kNintendoLogo.begin(), kNintendoLogo.end(), rom.begin() + static_cast<std::ptrdiff_t>(offset));
}

bool looksLikeMulticart(const std::vector<u8>& rom) {
    constexpr std::size_t kMulticartRomSize = 0x100000; // 1 MiB
    if (rom.size() != kMulticartRomSize) {
        return false;
    }
    constexpr std::size_t offsets[] = {0x00104, 0x40104, 0x80104, 0xC0104};
    int matches = 0;
    for (const std::size_t offset : offsets) {
        if (logoAt(rom, offset)) {
            ++matches;
        }
    }
    return matches >= 3;
}

std::string hexByte(u8 value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    return std::string("0x") + digits[value >> 4] + digits[value & 0x0F];
}

std::size_t ramBanksFor(u8 code) {
    switch (code) {
    case 0x03: return 4;
    case 0x04: return 16;
    case 0x05: return 8;
    default: return 1; // 0x02 is 8 KiB; 0x00/0x01 on a RAM cartridge also get 8 KiB
    }
}

} // namespace

Cartridge::Cartridge(const Cartridge& other)
    : rom_(other.rom_),
      ram_(other.ram_),
      mbc_(other.mbc_ ? other.mbc_->clone() : nullptr),
      kind_(other.kind_),
      headerChecksumOk_(other.headerChecksumOk_),
      hasBattery_(other.hasBattery_),
      romBanks_(other.romBanks_),
      ramBanks_(other.ramBanks_) {
    bindMbc();
}

Cartridge::Cartridge(Cartridge&& other) noexcept
    : rom_(std::move(other.rom_)),
      ram_(std::move(other.ram_)),
      mbc_(std::move(other.mbc_)),
      kind_(other.kind_),
      headerChecksumOk_(other.headerChecksumOk_),
      hasBattery_(other.hasBattery_),
      romBanks_(other.romBanks_),
      ramBanks_(other.ramBanks_) {
    bindMbc();
}

Cartridge& Cartridge::operator=(const Cartridge& other) {
    if (this != &other) {
        rom_ = other.rom_;
        ram_ = other.ram_;
        mbc_ = other.mbc_ ? other.mbc_->clone() : nullptr;
        kind_ = other.kind_;
        headerChecksumOk_ = other.headerChecksumOk_;
        hasBattery_ = other.hasBattery_;
        romBanks_ = other.romBanks_;
        ramBanks_ = other.ramBanks_;
        bindMbc();
    }
    return *this;
}

Cartridge& Cartridge::operator=(Cartridge&& other) noexcept {
    if (this != &other) {
        rom_ = std::move(other.rom_);
        ram_ = std::move(other.ram_);
        mbc_ = std::move(other.mbc_);
        kind_ = other.kind_;
        headerChecksumOk_ = other.headerChecksumOk_;
        hasBattery_ = other.hasBattery_;
        romBanks_ = other.romBanks_;
        ramBanks_ = other.ramBanks_;
        bindMbc();
    }
    return *this;
}

Cartridge::~Cartridge() = default;

void Cartridge::bindMbc() {
    if (mbc_) {
        mbc_->bindRam(ram_);
    }
}

std::optional<Cartridge> Cartridge::load(std::vector<u8> rom, std::string* error) {
    const auto fail = [&](const std::string& message) -> std::optional<Cartridge> {
        if (error != nullptr) {
            *error = message;
        }
        return std::nullopt;
    };
    if (rom.size() < 0x8000) {
        return fail("ROM image is smaller than 32 KiB");
    }
    Cartridge cart;
    const u8 type = rom[0x0147];
    switch (type) {
    case 0x00: cart.kind_ = Kind::RomOnly; break;
    case 0x01: case 0x02: case 0x03: cart.kind_ = Kind::Mbc1; break;
    case 0x05: case 0x06: cart.kind_ = Kind::Mbc2; break;
    case 0x0F: case 0x10: case 0x11:
    case 0x12: case 0x13: cart.kind_ = Kind::Mbc3; break;
    case 0x19: case 0x1A: case 0x1B:
    case 0x1C: case 0x1D: case 0x1E: cart.kind_ = Kind::Mbc5; break;
    default: return fail("unsupported cartridge type " + hexByte(type));
    }
    const u8 sizeCode = rom[0x0148];
    if (sizeCode > 0x08) {
        return fail("unsupported ROM size code " + hexByte(sizeCode));
    }
    // The header's declared size is the truth: pad short dumps, drop excess.
    rom.resize(std::size_t{0x8000} << sizeCode, 0xFF);
    cart.romBanks_ = rom.size() / kRomBank;
    if (type == 0x05 || type == 0x06) {
        // The header's RAM-size byte is 0x00 on every MBC2 cartridge and
        // means nothing here: the 512 x 4 bits belong to the controller
        // itself, not to a header-declared bank count, so the type alone
        // decides this allocation.
        cart.ram_.assign(512, 0x00);
    } else if (type == 0x02 || type == 0x03 ||
        type == 0x10 || type == 0x12 || type == 0x13 ||
        type == 0x1A || type == 0x1B || type == 0x1D || type == 0x1E) {
        // Type 0x0F (TIMER+BATTERY) is deliberately excluded here: it has a
        // battery and a timer but no RAM at all.
        cart.ramBanks_ = ramBanksFor(rom[0x0149]);
        cart.ram_.assign(cart.ramBanks_ * kRamBank, 0x00);
    }
    // Of the types accepted above, only 0x03 (MBC1+RAM+BATTERY), 0x06
    // (MBC2+BATTERY), 0x0F (MBC3+TIMER+BATTERY, no RAM), 0x10
    // (MBC3+TIMER+RAM+BATTERY), 0x13 (MBC3+RAM+BATTERY) and 0x1B / 0x1E
    // (MBC5+RAM+BATTERY, plain and rumble) declare a battery; the other
    // RAM-bearing types have the same RAM with nothing holding it up.
    cart.hasBattery_ = type == 0x03 || type == 0x06 ||
        type == 0x0F || type == 0x10 || type == 0x13 ||
        type == 0x1B || type == 0x1E;
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    cart.headerChecksumOk_ = sum == rom[0x014D];
    cart.rom_ = std::move(rom);
    switch (cart.kind_) {
    case Kind::RomOnly: cart.mbc_ = std::make_unique<MbcNone>(); break;
    case Kind::Mbc1:
        cart.mbc_ = std::make_unique<Mbc1>(cart.ramBanks_, looksLikeMulticart(cart.rom_));
        break;
    case Kind::Mbc2: cart.mbc_ = std::make_unique<Mbc2>(); break;
    case Kind::Mbc3:
        // 0x0F and 0x10 are the two MBC3 types with a clock.
        cart.mbc_ = std::make_unique<Mbc3>(cart.ramBanks_, type == 0x0F || type == 0x10);
        break;
    case Kind::Mbc5:
        // 0x1C-0x1E are the rumble variants: their RAM-bank register's bit 3
        // is the motor and must not reach the bank number.
        cart.mbc_ = std::make_unique<Mbc5>(cart.ramBanks_, type >= 0x1C);
        break;
    default:
        // Every Kind above is built here; a new one added without a case
        // here would otherwise leave mbc_ null and crash far from the cause.
        assert(false && "Cartridge::load: unhandled Kind");
        break;
    }
    cart.bindMbc();
    return cart;
}

void Cartridge::tick() { mbc_->tick(); }

bool Cartridge::hasTimer() const { return mbc_->rtc() != nullptr; }

RtcState Cartridge::rtcState() const {
    const Rtc* rtc = mbc_->rtc();
    return rtc != nullptr ? rtc->state() : RtcState{};
}

bool Cartridge::setRtcState(const RtcState& state) {
    Rtc* rtc = mbc_->rtc();
    if (rtc == nullptr) {
        return false;
    }
    rtc->setState(state);
    return true;
}

void Cartridge::advanceRtcSeconds(std::uint64_t seconds) {
    if (Rtc* rtc = mbc_->rtc()) {
        rtc->advanceSeconds(seconds);
    }
}

bool Cartridge::setRam(const std::vector<u8>& bytes) {
    if (bytes.size() != ram_.size()) {
        return false;
    }
    ram_ = bytes;
    return true;
}

u8 Cartridge::read(u16 address) const {
    if (address < 0x8000) {
        // Bank counts are powers of two, so the mask is the whole of the
        // wrapping rule and the controller can hand back any width it likes.
        const std::size_t bank = mbc_->romBank(address) & (romBanks_ - 1);
        return rom_[bank * kRomBank + (address & 0x3FFF)];
    }
    if (address >= 0xA000 && address < 0xC000) {
        if (const std::optional<u8> value = mbc_->readRam(address)) {
            return *value;
        }
    }
    return 0xFF; // open bus
}

void Cartridge::write(u16 address, u8 value) {
    if (address < 0x8000) {
        mbc_->writeControl(address, value);
    } else if (address >= 0xA000 && address < 0xC000) {
        mbc_->writeRam(address, value);
    }
}

} // namespace fourshades
