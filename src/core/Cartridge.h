#pragma once

#include "core/Types.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fourshades {

class Mbc;

// A cartridge: the ROM image plus its memory bank controller. Supports plain
// ROMs (type 0x00), MBC1 (0x01-0x03) per Pan Docs "MBC1", and MBC5
// (0x19-0x1E) per Pan Docs "MBC5". Other controllers arrive later in piece 4.
//
// The cartridge owns the ROM, the RAM and the header facts; which bank an
// address reaches is the controller's business, behind Mbc. The cartridge
// masks whatever bank number comes back against its own bank count, so a
// register wider than the cartridge needs no handling in the chip.
class Cartridge {
public:
    enum class Kind { RomOnly, Mbc1, Mbc5 };

    // Returns nullopt, with a message in *error, for images that are too small
    // or use a controller FourShades doesn't support yet.
    static std::optional<Cartridge> load(std::vector<u8> rom, std::string* error);

    // A cartridge is a value. Copying one produces an independent cartridge
    // in the same state, controller registers included; both of these are
    // out of line because Mbc is incomplete here.
    Cartridge(const Cartridge& other);
    Cartridge(Cartridge&& other) noexcept;
    Cartridge& operator=(const Cartridge& other);
    Cartridge& operator=(Cartridge&& other) noexcept;
    ~Cartridge();

    u8 read(u16 address) const;        // 0000-7FFF and A000-BFFF
    void write(u16 address, u8 value); // MBC registers and cartridge RAM

    Kind kind() const { return kind_; }
    bool headerChecksumOk() const { return headerChecksumOk_; }
    u8 headerChecksum() const { return rom_[0x014D]; }

    // Whether the header declares a battery behind the cartridge RAM, i.e.
    // whether that RAM is expected to survive a power cycle. The core only
    // reports it; what persistence means, and where it is kept, is somebody
    // else's business entirely.
    bool hasBattery() const { return hasBattery_; }

    // Cartridge RAM, in bank order, exactly as the hardware holds it. Empty
    // when the cartridge has none.
    const std::vector<u8>& ram() const { return ram_; }

    // Replaces cartridge RAM wholesale. Refuses, and changes nothing, unless
    // the size matches exactly: the header decides how much RAM this
    // cartridge has, and a caller cannot talk it into a different amount.
    bool setRam(const std::vector<u8>& bytes);

private:
    Cartridge() = default;
    // Points the controller at this cartridge's own RAM. Every copy and move
    // ends with it, because the vector it was holding belongs to the other
    // cartridge.
    void bindMbc();

    std::vector<u8> rom_;
    std::vector<u8> ram_;
    std::unique_ptr<Mbc> mbc_;
    Kind kind_ = Kind::RomOnly;
    bool headerChecksumOk_ = false;
    bool hasBattery_ = false;
    std::size_t romBanks_ = 2; // 16 KiB each, always a power of two
    std::size_t ramBanks_ = 0; // 8 KiB each
};

} // namespace fourshades
