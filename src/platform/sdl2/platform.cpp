#include "kr32/platform/window.h"

#include <SDL.h>

#include <cstdio>

namespace kr32::platform {

bool smoke_test() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_version version{};
    SDL_GetVersion(&version);
    std::printf("SDL %u.%u.%u\n",
                static_cast<unsigned>(version.major),
                static_cast<unsigned>(version.minor),
                static_cast<unsigned>(version.patch));

    SDL_Quit();
    return version.major == 2;
}

} // namespace kr32::platform
