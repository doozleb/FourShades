#include "core/Cpu.h"

namespace fourshades {

// Task 13. Fetching the second byte keeps the cycle count honest meanwhile.
void Cpu::executeCb() {
    fetch8();
    unimplemented_ = true;
}

} // namespace fourshades
