#include "core/Cartridge.h"

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
    u8 sum = 0;
    for (u16 a = 0x0134; a <= 0x014C; ++a) {
        sum = static_cast<u8>(sum - rom[a] - 1);
    }
    cart.headerChecksumOk_ = sum == rom[0x014D];
    cart.rom_ = std::move(rom);
    return cart;
}

std::size_t Cartridge::romOffset(u16 address) const {
    std::size_t bank = 0;
    if (kind_ == Kind::Mbc1) {
        if (address < 0x4000) {
            bank = mode1_ ? std::size_t{bankHigh_} << 5 : 0;
        } else {
            bank = (std::size_t{bankHigh_} << 5) | (bankLow_ == 0 ? 1u : bankLow_);
        }
    } else if (address >= 0x4000) {
        bank = 1;
    }
    bank &= romBanks_ - 1;
    return bank * kRomBank + (address & 0x3FFF);
}

std::size_t Cartridge::ramOffset(u16 address) const {
    const std::size_t bank = mode1_ ? (bankHigh_ & (ramBanks_ - 1)) : 0;
    return bank * kRamBank + (address - 0xA000);
}

u8 Cartridge::read(u16 address) const {
    if (address < 0x8000) {
        return rom_[romOffset(address)];
    }
    if (address >= 0xA000 && address < 0xC000 && ramEnabled_ && !ram_.empty()) {
        return ram_[ramOffset(address)];
    }
    return 0xFF;
}

void Cartridge::write(u16 address, u8 value) {
    if (kind_ != Kind::Mbc1) {
        return;
    }
    if (address < 0x2000) {
        ramEnabled_ = (value & 0x0F) == 0x0A;
    } else if (address < 0x4000) {
        bankLow_ = static_cast<u8>(value & 0x1F);
    } else if (address < 0x6000) {
        bankHigh_ = static_cast<u8>(value & 0x03);
    } else if (address < 0x8000) {
        mode1_ = (value & 0x01) != 0;
    } else if (address >= 0xA000 && address < 0xC000 && ramEnabled_ && !ram_.empty()) {
        ram_[ramOffset(address)] = value;
    }
}

} // namespace fourshades
