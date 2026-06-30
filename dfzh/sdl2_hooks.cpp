// sdl2_loader.cpp
#include "sdl2_hooks.h"

namespace DFHack {
namespace DFCH {
namespace Hooks {

// #define DEFINE_ORIG(fn) DEFINE_ORIG_FUNC(fn, fn)
// FOR_EACH_ORIG_SDL_FUNC(DEFINE_ORIG)
// #undef DEFINE_ORIG

// bool LoadSDL2Functions() {
//     HMODULE sdl2_handle = GetModuleHandle(TEXT("SDL2.dll"));
//     if (!sdl2_handle) {
//         return false;
//     }

//     #define INIT_ORIG(fn) INIT_ORIG_FUNC(fn, fn, sdl2_handle)
//     FOR_EACH_ORIG_SDL_FUNC(INIT_ORIG)
//     #undef INIT_ORIG

//     if (!ORIG_FUNC(SDL_GetWindowSize) || !ORIG_FUNC(SDL_RenderPresent) || !ORIG_FUNC(SDL_PollEvent)) {
//         return false;
//     }

//     return true;
// }
SDL2Functions g_sdl2 = {};

bool SDL2Functions::loadFunc() {
    HMODULE sdl2_handle = GetModuleHandle(TEXT("SDL2.dll"));
    if (!sdl2_handle) {
        return false;
    }

    #define INIT_ORIG(fn) INIT_ORIG_FUNC(fn, fn, sdl2_handle)
    FOR_EACH_ORIG_SDL_FUNC(INIT_ORIG)
    #undef INIT_ORIG

    if (!ORIG_FUNC(SDL_GetWindowSize) || !ORIG_FUNC(SDL_RenderPresent) || !ORIG_FUNC(SDL_PollEvent)) {
        return false;
    }

    return true;
}

}
}
}
