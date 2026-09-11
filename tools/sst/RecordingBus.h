#pragma once

#include "core/Bus.h"
#include "sst/SstTypes.h"

#include <vector>

namespace sst {

// A flat 64 KB memory that logs every cycle. One instance is reused across
// all 500,000 tests, so reset() only clears the addresses a test touched
// instead of zeroing 64 KB each time.
class RecordingBus final : public fourshades::Bus {
public:
    RecordingBus() : memory_(0x10000, 0) {}

    // Test setup: set memory without logging a cycle.
    void poke(u16 address, u8 value) {
        memory_[address] = value;
        touched_.push_back(address);
    }

    u8 peek(u16 address) const { return memory_[address]; }

    const std::vector<Cycle>& log() const { return log_; }

    void reset() {
        for (const u16 address : touched_) {
            memory_[address] = 0;
        }
        touched_.clear();
        log_.clear();
    }

    u8 read(u16 address) override {
        const u8 value = memory_[address];
        log_.push_back({address, value, CycleKind::Read});
        return value;
    }

    void write(u16 address, u8 value) override {
        poke(address, value);
        log_.push_back({address, value, CycleKind::Write});
    }

    // Address and value aren't modelled on idle cycles; the comparator checks
    // only their kind (see the design spec, "Match the test model").
    void idle() override { log_.push_back({0, 0, CycleKind::Idle}); }

private:
    std::vector<u8> memory_;
    std::vector<u16> touched_;
    std::vector<Cycle> log_;
};

} // namespace sst
