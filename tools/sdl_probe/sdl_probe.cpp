// Proves SDL3 links and reports its version. Piece 3b groundwork: this is
// not part of the emulator (src/core stays SDL-free) and not part of the
// window itself, just evidence that the vendored SDL3 is wired correctly.
#include <SDL3/SDL.h>

#include <cstdio>

int main() {
    const int version = SDL_GetVersion();
    std::printf("SDL3 %d.%d.%d\n",
                SDL_VERSIONNUM_MAJOR(version),
                SDL_VERSIONNUM_MINOR(version),
                SDL_VERSIONNUM_MICRO(version));
    return 0;
}
