#include "core/GameBoy.h"

#include "core/Interrupts.h"

#include <array>
#include <cstddef>
#include <utility>

namespace fourshades {
namespace {

// The VRAM a DMG boot ROM leaves behind, reproduced without running one.
//
// Pan Docs (Power Up Sequence): "The monochrome boot ROMs read the logo from
// the header, unpack it into VRAM, and then start slowly scrolling it down."
// The unpacking is a bit-doubling routine: it walks the 48 bytes of the
// cartridge header's logo area (0x0104-0x0133) and, for each nibble, doubles
// every one of its four bits into two, so a nibble becomes eight pixels across
// and each source byte becomes four screen rows. Each doubled byte is written
// twice, one address apart from the next, which places it in bit plane 0 of two
// consecutive tile rows - two pixels down as well as two across. Plane 1 is
// left at zero, so the logo is drawn in colour 1, which BGP = 0xFC shades
// black.
//
// The result starts at 0x8010 and runs to 0x818F: two header bytes per tile,
// 24 tiles, 0x01-0x18, twelve across and two down. Tile 0x19 follows at 0x8190
// and holds the (R) glyph, which is the boot ROM's own data rather than the
// cartridge's. Then the background map gets the entries that lay the 24 tiles
// out, 0x01-0x0C at 0x9904-0x990F and 0x0D-0x18 at 0x9924-0x992F, with the
// glyph at 0x9910 beside the top row. Everything else in VRAM stays zero: the
// boot ROM clears all of it first.
//
// The logo is read from the cartridge in the slot, never from a copy kept here,
// because that is where the hardware reads it: the boot ROM unpacks the
// header's bytes and only afterwards compares them against its own copy (and
// hangs if they differ, which is a thing FourShades does not model - it never
// runs a boot ROM, so every cartridge reaches 0x0100). A cartridge whose logo
// area holds something else therefore leaves something else in VRAM.
void unpackHeaderLogoIntoVram(Ppu& ppu, const Cartridge& cart) {
    u16 dest = 0x8010;
    for (u16 header = 0x0104; header < 0x0134; ++header) {
        const u8 byte = cart.read(header);
        for (const int shift : {4, 0}) { // the high nibble first, then the low
            u8 doubled = 0;
            for (int bit = 3; bit >= 0; --bit) {
                const bool set = ((byte >> (shift + bit)) & 0x01) != 0;
                doubled = static_cast<u8>((doubled << 2) | (set ? 0x03 : 0x00));
            }
            ppu.powerOnVramWrite(dest, doubled);
            ppu.powerOnVramWrite(static_cast<u16>(dest + 2), doubled);
            dest = static_cast<u16>(dest + 4);
        }
    }
    static constexpr std::array<u8, 8> kTrademarkGlyph{
        0x3C, 0x42, 0xB9, 0xA5, 0xB9, 0xA5, 0x42, 0x3C,
    };
    for (const u8 row : kTrademarkGlyph) {
        ppu.powerOnVramWrite(dest, row);
        dest = static_cast<u16>(dest + 2);
    }
    for (int tile = 0; tile < 24; ++tile) {
        // Twelve entries from 0x9904, then twelve more a map row below.
        const u16 entry = static_cast<u16>(0x9904 + (tile < 12 ? tile : tile + 20));
        ppu.powerOnVramWrite(entry, static_cast<u8>(tile + 1));
    }
    ppu.powerOnVramWrite(0x9910, 0x19);
}

} // namespace

GameBoy::GameBoy(Cartridge cartridge) : cart_(std::move(cartridge)), cpu_(*this) {
    Registers& r = cpu_.regs;
    r.a = 0x01;
    r.setF(cart_.headerChecksum() != 0 ? 0xB0 : 0x80);
    r.setBc(0x0013);
    r.setDe(0x00D8);
    r.setHl(0x014D);
    r.sp = 0xFFFE;
    r.pc = 0x0100;
    // DIV reads 0xAB (Pan Docs). Pan Docs doesn't give the phase below it;
    // hardware sees DIV turn 0xAC in the 14th M-cycle from the fetch at
    // 0x0100, which puts the counter at 0xABC8 before that fetch (0xABCC,
    // the value often quoted, is the counter just after it).
    timer_.setCounter(0xABC8);
    unpackHeaderLogoIntoVram(ppu_, cart_);
}

// A button changing state is asynchronous to the CPU: it is a pin moving, not
// a bus cycle, so no time passes here. The interrupt is requested on the
// high-to-low edge only (Pan Docs, INT $60), which is why Joypad reports it
// rather than this deciding from the level.
void GameBoy::setButtons(u8 pressed) {
    if (joypad_.setButtons(pressed)) {
        if_ = static_cast<u8>(if_ | irq::Joypad);
    }
}

void GameBoy::tick() {
    ++cycles_;
    // The CPU is the only thing that can enter or leave STOP mode, so the
    // machine learns about it here, at the top of every M-cycle. Pan Docs
    // (Reducing Power Consumption) calls STOP mode "VERY low power standby
    // mode"; the PPU is the part of that this models, and it is the part
    // anyone can see. What the timer, the serial port and the sound chip do
    // while the machine is stopped is still not modelled: they keep
    // advancing. See docs/known-divergences.md, the STOP entry.
    ppu_.setClockStopped(cpu_.state() == Cpu::State::Stopped);
    const u16 before = timer_.counter();
    if (timer_.tick()) {
        if_ = static_cast<u8>(if_ | irq::Timer);
    }
    if (serial_.tick(before, timer_.counter())) {
        if_ = static_cast<u8>(if_ | irq::Serial);
    }
    if_ = static_cast<u8>(if_ | ppu_.tick());
    apu_.tick(timer_);
    // The cartridge's own clock, if it has one. Nothing above or below
    // depends on it, and nothing it does depends on them.
    cart_.tick();
    tickDma();
}

void GameBoy::tickDma() {
    dmaCopying_ = dmaActive_;
    if (dmaActive_) {
        u16 from = static_cast<u16>(dmaFrom_ + dmaIndex_);
        // Pan Docs lists DMA sources $00-$DF only. Mapping E000 and above
        // onto C000 and above, the way the echo area does, was inferred when
        // this was written. Piece 4 settled it on 2026-09-23: once this core
        // gained the bank controller the hardware-verified DMA source test
        // needs, that test agreed with this mapping exactly as it stands, no
        // change here, and the DMA group came out whole. Not a divergence --
        // Pan Docs says nothing this contradicts -- so there is no entry in
        // docs/known-divergences.md for it.
        if (from >= 0xE000) {
            from = static_cast<u16>(from - 0x2000);
        }
        dmaCurrentSource_ = from;
        ppu_.dmaWriteOam(dmaIndex_, peek(from));
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
//
// Which bus is blocked: Pan Docs says "On DMG, during OAM DMA, the CPU can
// access only HRAM (memory at $FF80-$FFFE)" (OAM DMA Transfer: OAM DMA bus
// conflicts). Nine hardware-verified test ROMs run an OAM DMA from VRAM and,
// while it copies, execute an instruction from ROM or from WRAM's echo whose
// operand or stack bytes fall in OAM; they expect the ROM/WRAM accesses to
// succeed and only the OAM ones to read 0xFF, which per-bus blocking
// explains and the Pan Docs sentence above does not (see
// docs/known-divergences.md, "OAM DMA bus conflicts", resolved 2026-09-11).
// So FourShades blocks only OAM (FE00-FEFF), always, and whichever bus the
// DMA is currently reading from: the video bus (VRAM, 8000-9FFF) if the
// source lies there, otherwise the external bus (ROM 0000-7FFF, cartridge
// RAM A000-BFFF, WRAM C000-DFFF and its echo E000-FDFF). I/O, HRAM and IE
// stay reachable throughout.
bool GameBoy::dmaBlocks(u16 address) const {
    if (!dmaCopying_) {
        return false;
    }
    if (address >= 0xFE00 && address < 0xFF00) {
        return true; // OAM (and the unusable FEA0-FEFF stretch on the same bus)
    }
    const bool sourceIsVideo = dmaCurrentSource_ >= 0x8000 && dmaCurrentSource_ < 0xA000;
    if (sourceIsVideo) {
        return address >= 0x8000 && address < 0xA000; // video bus: VRAM
    }
    return address < 0x8000 || (address >= 0xA000 && address < 0xFE00); // external bus
}

// What the CPU's data bus carries for `address` right now: the DMA's and the
// PPU's locks both apply. The M-cycle this read belongs to has already
// been stepped before this is called.
u8 GameBoy::busRead(u16 address) const {
    if (dmaBlocks(address)) {
        return 0xFF;
    }
    if (address >= 0x8000 && address < 0xA000) {
        return ppu_.vramRead(address);
    }
    if (address >= 0xFE00 && address < 0xFEA0) {
        return ppu_.oamRead(address);
    }
    if (address >= 0xFEA0 && address < 0xFF00) {
        // Pan Docs: this area reads 0xFF while OAM is blocked, 0x00 otherwise.
        return ppu_.oamBlocked() ? 0xFF : 0x00;
    }
    return peek(address);
}

// An access anywhere in FE00-FEFF while the PPU is scanning OAM corrupts the
// row it is reading, whether or not the PPU's lock lets the access itself
// through: the address and the read/write line reach OAM regardless. The PPU
// is told about every such M-cycle, its own mode decides whether anything
// comes of it (Ppu::oamBusAccess).
u8 GameBoy::read(u16 address) {
    tick();
    ppu_.oamBusAccess(address, Ppu::Kind::Read);
    return busRead(address);
}

void GameBoy::write(u16 address, u8 value) {
    tick();
    ppu_.oamBusAccess(address, Ppu::Kind::Write);
    if (!dmaBlocks(address)) {
        writeMemory(address, value);
    }
}

void GameBoy::idle() {
    tick();
}

std::optional<u8> GameBoy::haltedCycle(u16 address) {
    tick();
    if (pendingInterrupts() == 0) {
        return std::nullopt;
    }
    // The byte the CPU latches when it leaves HALT comes off the same bus as
    // any other read, so the PPU's VRAM and OAM locks apply to it too, and so
    // does the OAM corruption bug.
    ppu_.oamBusAccess(address, Ppu::Kind::Read);
    return busRead(address);
}

u8 GameBoy::peek(u16 address) const {
    if (address < 0x8000) return cart_.read(address);
    if (address < 0xA000) return ppu_.peekVram(address);
    if (address < 0xC000) return cart_.read(address);
    if (address < 0xE000) return wram_[address - 0xC000];
    if (address < 0xFE00) return wram_[address - 0xE000];
    if (address < 0xFEA0) return ppu_.peekOam(address);
    if (address < 0xFF00) return 0x00;
    if (address < 0xFF80) return readIo(address);
    if (address < 0xFFFF) return hram_[address - 0xFF80];
    return ie_;
}

void GameBoy::writeMemory(u16 address, u8 value) {
    if (address < 0x8000) {
        cart_.write(address, value);
    } else if (address < 0xA000) {
        ppu_.vramWrite(address, value);
    } else if (address < 0xC000) {
        cart_.write(address, value);
    } else if (address < 0xE000) {
        wram_[address - 0xC000] = value;
    } else if (address < 0xFE00) {
        wram_[address - 0xE000] = value;
    } else if (address < 0xFEA0) {
        ppu_.oamWrite(address, value);
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
    case 0xFF00: return joypad_.read();
    case 0xFF01:
    case 0xFF02: return serial_.read(address);
    case 0xFF04:
    case 0xFF05:
    case 0xFF06:
    case 0xFF07: return timer_.read(address);
    case 0xFF0F: return static_cast<u8>(if_ | 0xE0);
    case 0xFF46: return dmaRegister_;
    default:
        if (address >= 0xFF10 && address <= 0xFF3F) {
            return apu_.read(address);
        }
        if (address >= 0xFF40 && address <= 0xFF4B) {
            return ppu_.read(address);
        }
        return 0xFF; // not implemented yet
    }
}

void GameBoy::writeIo(u16 address, u8 value) {
    switch (address) {
    case 0xFF00:
        // Selecting a group that already has a button held drives its line
        // low inside this M-cycle, so the IF bit has to land now, the same
        // way a STAT write's does below.
        if (joypad_.write(value)) {
            if_ = static_cast<u8>(if_ | irq::Joypad);
        }
        break;
    case 0xFF01:
    case 0xFF02: serial_.write(address, value); break;
    case 0xFF04:
    case 0xFF05:
    case 0xFF06:
    case 0xFF07:
        timer_.write(address, value);
        // Clearing the system counter can drop the bit the sound frame
        // sequencer hangs off, and this write lands after the APU has already
        // been ticked for this M-cycle, so it has to ask again.
        apu_.counterWritten(timer_);
        break;
    case 0xFF0F: if_ = static_cast<u8>(value & 0x1F); break;
    case 0xFF46:
        dmaRegister_ = value;
        dmaSource_ = static_cast<u16>(value << 8);
        dmaStartDelay_ = 1;
        break;
    default:
        if (address >= 0xFF10 && address <= 0xFF3F) {
            apu_.write(address, value);
        } else if (address >= 0xFF40 && address <= 0xFF4B) {
            // A STAT write, or switching the LCD on, can raise the STAT level
            // line inside this very M-cycle, so the IF bit has to land now
            // rather than on the next tick.
            if_ = static_cast<u8>(if_ | ppu_.write(address, value));
        }
        break;
    }
}

} // namespace fourshades
