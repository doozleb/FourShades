#pragma once

#include "core/Types.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace fourshades {

// A memory bank controller: the chip on the cartridge that decides which ROM
// bank the CPU sees and what answers from A000-BFFF. Cartridge owns the ROM
// and the RAM and does the address decoding; everything that differs between
// one controller and the next lives behind this.
class Mbc {
public:
    virtual ~Mbc() = default;

    // Which ROM bank 0000-3FFF and 4000-7FFF currently see. The caller masks
    // the result against the cartridge's bank count, so a register wider than
    // the cartridge is not this chip's problem.
    virtual std::size_t romBank(u16 address) const = 0;

    // A000-BFFF. Returns nullopt when the window reads open bus (RAM
    // disabled, no RAM fitted, or a bank number that decodes to nothing),
    // which the caller turns into 0xFF.
    virtual std::optional<u8> readRam(u16 address) const = 0;
    virtual void writeRam(u16 address, u8 value) = 0;

    // 0000-7FFF: the control registers.
    virtual void writeControl(u16 address, u8 value) = 0;

    // One M-cycle. Only a controller with a timer does anything.
    virtual void tick() {}

    // --- Plumbing, not part of the banking interface ------------------
    //
    // A cartridge is a value: it is copied (a pristine image kept beside the
    // running machine) and moved (into the machine). Both leave the chip
    // pointing at the wrong vector, so the cartridge hands it the right one
    // afterwards. clone() carries the banking registers across a copy so a
    // copied cartridge is an independent cartridge in the same state.
    virtual std::unique_ptr<Mbc> clone() const = 0;
    void bindRam(std::vector<u8>& ram) { ram_ = &ram; }

protected:
    // The cartridge's RAM, owned by the cartridge. Null until bound; a
    // controller that addresses no RAM never looks at it.
    std::vector<u8>* ram_ = nullptr;
};

} // namespace fourshades
