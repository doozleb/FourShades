#include "app/Input.h"

namespace app {

fourshades::u8 buttonMask(const bool* state, int numKeys) {
    if (state == nullptr) {
        return 0x00;
    }
    fourshades::u8 mask = 0x00;
    for (const Binding& binding : kBindings) {
        const int index = static_cast<int>(binding.key);
        if (index < numKeys && state[index]) {
            mask = static_cast<fourshades::u8>(mask | binding.button);
        }
    }
    return mask;
}

} // namespace app
