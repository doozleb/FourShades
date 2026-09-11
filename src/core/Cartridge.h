#pragma once

#include "core/Types.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace fourshades {

// A cartridge: the ROM image plus its memory bank controller. Supports plain
// ROMs (type 0x00) and MBC1 (0x01-0x03), per Pan Docs "MBC1". Other
// controllers arrive in piece 4.
class Cartridge {
public:
    enum class Kind { RomOnly, Mbc1 };

    // Returns nullopt, with a message in *error, for images that are too small
    // or use a controller FourShades doesn't support yet.
    static std::optional<Cartridge> load(std::vector<u8> rom, std::string* error);

    u8 read(u16 address) const;        // 0000-7FFF and A000-BFFF
    void write(u16 address, u8 value); // MBC registers and cartridge RAM

    Kind kind() const { return kind_; }
    bool headerChecksumOk() const { return headerChecksumOk_; }
    u8 headerChecksum() const { return rom_[0x014D]; }

private:
    Cartridge() = default;
    std::size_t romOffset(u16 address) const;
    std::size_t ramOffset(u16 address) const;

    std::vector<u8> rom_;
    std::vector<u8> ram_;
    Kind kind_ = Kind::RomOnly;
    bool headerChecksumOk_ = false;
    std::size_t romBanks_ = 2; // 16 KiB each, always a power of two
    std::size_t ramBanks_ = 0; // 8 KiB each
    bool ramEnabled_ = false;
    u8 bankLow_ = 0;           // 2000-3FFF, 5 bits; 0 acts as 1
    u8 bankHigh_ = 0;          // 4000-5FFF, 2 bits
    bool mode1_ = false;       // 6000-7FFF
};

} // namespace fourshades
