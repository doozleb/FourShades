#include "core/Timer.h"

namespace fourshades {

bool Timer::tick() {
    reloading_ = false;
    bool interrupt = false;
    if (overflowed_) {
        overflowed_ = false;
        tima_ = tma_;
        reloading_ = true;
        interrupt = true;
    }
    const bool before = input();
    counter_ = static_cast<u16>(counter_ + 4);
    if (before && !input()) {
        increment();
    }
    return interrupt;
}

u8 Timer::read(u16 address) const {
    switch (address) {
    case 0xFF04: return hi(counter_);
    case 0xFF05: return tima_;
    case 0xFF06: return tma_;
    default: return static_cast<u8>(tac_ | 0xF8);
    }
}

void Timer::write(u16 address, u8 value) {
    switch (address) {
    case 0xFF04: {
        const bool before = input();
        counter_ = 0;
        if (before && !input()) {
            increment();
        }
        break;
    }
    case 0xFF05:
        if (!reloading_) {
            tima_ = value;
            overflowed_ = false; // a write in cycle A cancels the reload
        }
        break;
    case 0xFF06:
        tma_ = value;
        if (reloading_) {
            tima_ = value;
        }
        break;
    default: {
        const bool before = input();
        tac_ = static_cast<u8>(value | 0xF8);
        if (before && !input()) {
            increment();
        }
        break;
    }
    }
}

bool Timer::input() const {
    static constexpr int kBit[4] = {9, 3, 5, 7};
    return (tac_ & 0x04) != 0 && ((counter_ >> kBit[tac_ & 0x03]) & 1) != 0;
}

void Timer::increment() {
    if (tima_ == 0xFF) {
        tima_ = 0x00;
        overflowed_ = true;
    } else {
        ++tima_;
    }
}

} // namespace fourshades
