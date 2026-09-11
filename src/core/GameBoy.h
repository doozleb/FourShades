#pragma once

#include "core/Bus.h"
#include "core/Cartridge.h"
#include "core/Cpu.h"
#include "core/LcdTiming.h"
#include "core/Serial.h"
#include "core/Timer.h"
#include "core/Types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace fourshades {

// The whole machine except the screen. It is the CPU's Bus: every read,
// write or idle first advances the timer, serial port, LCD timing and OAM
// DMA by one M-cycle, then performs the access.
class GameBoy final : public Bus {
public:
    // Starts in the state a DMG (revisions A-C) boot ROM leaves behind, at
    // PC=0x0100 (Pan Docs "Power Up Sequence"). No boot ROM is run.
    explicit GameBoy(Cartridge cartridge);

    GameBoy(const GameBoy&) = delete;
    GameBoy& operator=(const GameBoy&) = delete;

    Cpu& cpu() { return cpu_; }
    const Cpu& cpu() const { return cpu_; }
    void step() { cpu_.step(); }

    // What a debugger would see: no time passes and no DMA blocking applies.
    u8 peek(u16 address) const;
    // Bytes sent over the serial port, in order.
    const std::vector<u8>& serialOutput() const { return serial_.sent(); }
    // M-cycles since power-on.
    std::uint64_t cycles() const { return cycles_; }

    u8 read(u16 address) override;
    void write(u16 address, u8 value) override;
    void idle() override;
    u8 pendingInterrupts() override { return static_cast<u8>(ie_ & if_ & 0x1F); }
    void acknowledgeInterrupt(int bit) override { if_ = static_cast<u8>(if_ & ~(1 << bit)); }

private:
    void tick();
    void tickDma();
    bool dmaBlocks(u16 address) const;
    u8 readIo(u16 address) const;
    void writeIo(u16 address, u8 value);
    void writeMemory(u16 address, u8 value);

    Cartridge cart_;
    Timer timer_;
    Serial serial_;
    LcdTiming lcd_;
    std::array<u8, 0x2000> vram_{};
    std::array<u8, 0x2000> wram_{};
    std::array<u8, 0xA0> oam_{};
    std::array<u8, 0x7F> hram_{};
    u8 ie_ = 0x00;
    u8 if_ = 0x01;          // bits 0-4; reads OR in 0xE0
    u8 joypadSelect_ = 0x00; // P1 bits 4-5
    u8 dmaRegister_ = 0xFF;
    u16 dmaSource_ = 0;
    int dmaStartDelay_ = 0; // M-cycles until a requested DMA begins
    bool dmaActive_ = false;
    bool dmaCopying_ = false; // a byte was copied in the current M-cycle
    int dmaIndex_ = 0;      // next byte to copy, 0-159
    u16 dmaFrom_ = 0;
    std::uint64_t cycles_ = 0;
    Cpu cpu_; // last: it holds a reference to this Bus
};

} // namespace fourshades
