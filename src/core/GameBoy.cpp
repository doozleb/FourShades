#include "core/GameBoy.h"

#include "core/Interrupts.h"

#include <utility>

namespace fourshades {

GameBoy::GameBoy(Cartridge cartridge) : cart_(std::move(cartridge)), cpu_(*this) {
    Registers& r = cpu_.regs;
    r.a = 0x01;
    r.setF(cart_.headerChecksum() != 0 ? 0xB0 : 0x80);
    r.setBc(0x0013);
    r.setDe(0x00D8);
    r.setHl(0x014D);
    r.sp = 0xFFFE;
    r.pc = 0x0100;
    timer_.setCounter(0xABCC); // DIV reads 0xAB
}

void GameBoy::tick() {
    ++cycles_;
    const u16 before = timer_.counter();
    if (timer_.tick()) {
        if_ = static_cast<u8>(if_ | irq::Timer);
    }
    if (serial_.tick(before, timer_.counter())) {
        if_ = static_cast<u8>(if_ | irq::Serial);
    }
    if_ = static_cast<u8>(if_ | lcd_.tick());
    tickDma();
}

void GameBoy::tickDma() {
    dmaCopying_ = dmaActive_;
    if (dmaActive_) {
        u16 from = static_cast<u16>(dmaFrom_ + dmaIndex_);
        if (from >= 0xE000) {
            from = static_cast<u16>(from - 0x2000); // Pan Docs: sources above DFFF
        }
        oam_[static_cast<std::size_t>(dmaIndex_)] = peek(from);
        if (++dmaIndex_ == 0xA0) {
            dmaActive_ = false;
        }
    }
    if (dmaStartDelay_ > 0 && --dmaStartDelay_ == 0) {
        dmaActive_ = true; // a restart replaces a transfer in progress
        dmaIndex_ = 0;
        dmaFrom_ = dmaSource_;
    }
}

// DMA timing: the M-cycle after the FF46 write is a start-up cycle and isn't
// blocked; the CPU is blocked in each of the 160 M-cycles that copy a byte,
// the one copying byte 159 included (hardware-verified test ROMs read OAM in
// that M-cycle and see 0xFF, and see the data one M-cycle later); blocked
// reads return 0xFF; on a restart the old transfer keeps copying, so keeps
// blocking, through the new one's start-up cycle.
bool GameBoy::dmaBlocks(u16 address) const {
    // On DMG the CPU sees only FF00-FFFF while bytes are being copied.
    return dmaCopying_ && address < 0xFF00;
}

u8 GameBoy::read(u16 address) {
    tick();
    return dmaBlocks(address) ? 0xFF : peek(address);
}

void GameBoy::write(u16 address, u8 value) {
    tick();
    if (!dmaBlocks(address)) {
        writeMemory(address, value);
    }
}

void GameBoy::idle() {
    tick();
}

u8 GameBoy::peek(u16 address) const {
    if (address < 0x8000) return cart_.read(address);
    if (address < 0xA000) return vram_[address - 0x8000];
    if (address < 0xC000) return cart_.read(address);
    if (address < 0xE000) return wram_[address - 0xC000];
    if (address < 0xFE00) return wram_[address - 0xE000];
    if (address < 0xFEA0) return oam_[address - 0xFE00];
    if (address < 0xFF00) return 0x00;
    if (address < 0xFF80) return readIo(address);
    if (address < 0xFFFF) return hram_[address - 0xFF80];
    return ie_;
}

void GameBoy::writeMemory(u16 address, u8 value) {
    if (address < 0x8000) {
        cart_.write(address, value);
    } else if (address < 0xA000) {
        vram_[address - 0x8000] = value;
    } else if (address < 0xC000) {
        cart_.write(address, value);
    } else if (address < 0xE000) {
        wram_[address - 0xC000] = value;
    } else if (address < 0xFE00) {
        wram_[address - 0xE000] = value;
    } else if (address < 0xFEA0) {
        oam_[address - 0xFE00] = value;
    } else if (address < 0xFF00) {
        // unusable area: writes are ignored
    } else if (address < 0xFF80) {
        writeIo(address, value);
    } else if (address < 0xFFFF) {
        hram_[address - 0xFF80] = value;
    } else {
        ie_ = value;
    }
}

u8 GameBoy::readIo(u16 address) const {
    switch (address) {
    case 0xFF00: return static_cast<u8>(0xC0 | joypadSelect_ | 0x0F); // no buttons held
    case 0xFF01:
    case 0xFF02: return serial_.read(address);
    case 0xFF04:
    case 0xFF05:
    case 0xFF06:
    case 0xFF07: return timer_.read(address);
    case 0xFF0F: return static_cast<u8>(if_ | 0xE0);
    case 0xFF46: return dmaRegister_;
    default:
        if (address >= 0xFF40 && address <= 0xFF4B) {
            return lcd_.read(address);
        }
        return 0xFF; // not implemented yet (sound is piece 5)
    }
}

void GameBoy::writeIo(u16 address, u8 value) {
    switch (address) {
    case 0xFF00: joypadSelect_ = static_cast<u8>(value & 0x30); break;
    case 0xFF01:
    case 0xFF02: serial_.write(address, value); break;
    case 0xFF04:
    case 0xFF05:
    case 0xFF06:
    case 0xFF07: timer_.write(address, value); break;
    case 0xFF0F: if_ = static_cast<u8>(value & 0x1F); break;
    case 0xFF46:
        dmaRegister_ = value;
        dmaSource_ = static_cast<u16>(value << 8);
        dmaStartDelay_ = 1;
        break;
    default:
        if (address >= 0xFF40 && address <= 0xFF4B) {
            lcd_.write(address, value);
        }
        break;
    }
}

} // namespace fourshades
