#include "core/Cartridge.h"

#include "core/mbc/Mbc.h"
#include "core/mbc/Mbc1.h"
#include "core/mbc/MbcNone.h"

#include <utility>

namespace fourshades {

namespace {

constexpr std::size_t kRomBank = 0x4000;
constexpr std::size_t kRamBank = 0x2000;

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
    default: return fail("unsupported cartridge type " + hexByte(type));
    }
    const u8 sizeCode = rom[0x0148];
    if (sizeCode > 0x08) {
        return fail("unsupported ROM size code " + hexByte(sizeCode));
    }
    // The header's declared size is the truth: pad short dumps, drop excess.
    rom.resize(std::size_t{0x8000} << sizeCode, 0xFF);
    cart.romBanks_ = rom.size() / kRomBank;
    if (type == 0x02 || type == 0x03) {
        cart.ramBanks_ = ramBanksFor(rom[0x0149]);
        cart.ram_.assign(cart.ramBanks_ * kRamBank, 0x00);
    }
    // Of the types accepted above, only 0x03 (MBC1+RAM+BATTERY) declares a
    // battery; 0x02 has the same RAM with nothing holding it up.
    cart.hasBattery_ = type == 0x03;
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    cart.headerChecksumOk_ = sum == rom[0x014D];
    cart.rom_ = std::move(rom);
    switch (cart.kind_) {
    case Kind::RomOnly: cart.mbc_ = std::make_unique<MbcNone>(); break;
    case Kind::Mbc1: cart.mbc_ = std::make_unique<Mbc1>(cart.ramBanks_); break;
    }
    cart.bindMbc();
    return cart;
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
