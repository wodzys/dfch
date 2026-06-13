
#include "ttf_manager.h"
#include "config.h"
#include "logger.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

namespace DFHack {
namespace DFCH {
namespace Hooks {

    bool TTFManager::init() {
        if (!LoadSDL2TTF()) {
            LOGGERMANAGER.getLogger()->error("TTFManager::init: LoadSDL2TTF failed");
            return false;
        }
        if (!InitTTFFuncHandle()) {
            LOGGERMANAGER.getLogger()->error("TTFManager::init: InitTTFFuncHandle failed");
            return false;
        }
        if (!TTFInit()) {
            LOGGERMANAGER.getLogger()->error("TTFManager::init: TTFInit failed");
            return false;
        }
        if (!LoadFont(Config::getFontFile().string(), 20)) {
            LOGGERMANAGER.getLogger()->error("TTFManager::init: LoadFont failed");
            return false;
        }
        return true;
    }

    void TTFManager::shutdown() {
        FreeFont();
        TTFQuit();
        // FreeTTFFuncHandle();
        CloseSDL2TTF();
    }

    bool TTFManager::LoadSDL2TTF() {
        if (sdl2_ttf_handle) {
            return true;
        }
        const auto sdl2_ttf_dll_path = (Config::getDFHackPath() / "SDL2_ttf.dll").string();
        sdl2_ttf_handle = LoadLibrary(sdl2_ttf_dll_path.c_str());
        if (!sdl2_ttf_handle) {
            LOGGERMANAGER.getLogger()->error("LoadLibrary:{} failed: {}", sdl2_ttf_dll_path, GetLastError());
            return false;
        }
        return true;
    }

    void TTFManager::CloseSDL2TTF() {
        if (sdl2_ttf_handle) {
            FreeLibrary(sdl2_ttf_handle);
            sdl2_ttf_handle = nullptr;
        }
    }

    bool TTFManager::InitTTFFuncHandle() {
        #define INIT_ORIG(fn) INIT_ORIG_FUNC(fn, fn, sdl2_ttf_handle)
        FOR_EACH_ORIG_TTF_FUNC(INIT_ORIG)
        #undef INIT_ORIG

        return true;
    }

    // void TTFManager::FreeTTFFuncHandle() {
    //     ORIG_FUNC(TTF_Init) = nullptr;
    //     ORIG_FUNC(TTF_Quit) = nullptr;
    //     ORIG_FUNC(TTF_OpenFont) = nullptr;
    //     ORIG_FUNC(TTF_CloseFont) = nullptr;
    //     ORIG_FUNC(TTF_FontHeight) = nullptr;
    //     ORIG_FUNC(TTF_RenderUTF8_Solid) = nullptr;
    //     ORIG_FUNC(TTF_RenderUTF8_Blended) = nullptr;
    //     ORIG_FUNC(TTF_RenderUTF8_Blended_Wrapped) = nullptr;
    //     ORIG_FUNC(TTF_RenderUTF8_Shaded) = nullptr;
    // }

    bool TTFManager::TTFInit() {
        if (initialized) {
            return true;
        }
        if (ORIG_FUNC(TTF_Init)() == -1) {
            LOGGERMANAGER.getLogger()->error("TTF_Init failed");
            return false;
        }
        initialized = true;
        return true;
    }

    void TTFManager::TTFQuit() {
        ORIG_FUNC(TTF_Quit)();
        initialized = false;
    }

    bool TTFManager::LoadFont(const std::string& font_file, int ptsize) {
        FreeFont();

        font_handle = ORIG_FUNC(TTF_OpenFont)(font_file.c_str(), ptsize);
        if (!font_handle) {
            LOGGERMANAGER.getLogger()->error("TTF_OpenFont:{} failed", font_file);
            return false;
        }
        return true;
    }

    bool TTFManager::ReloadFont(int new_height_px) {
        if (new_height_px == pre_height_px) {
            return true;
        }
        TTF_Font* new_font = LoadFontMatchingHeight(Config::getFontFile().string(), new_height_px);
        if (!new_font) {
            LOGGERMANAGER.getLogger()->error("LoadFontMatchingHeight:{} {} failed", Config::getFontFile().string(), new_height_px);
            return false;
        }
        FreeFont();
        font_handle = new_font;
        pre_height_px = new_height_px;

        return true;
    }

    void TTFManager::FreeFont() {
        if (font_handle) {
            ORIG_FUNC(TTF_CloseFont)(font_handle);
            font_handle = nullptr;
        }
    }

    SDL_Surface* TTFManager::RenderSolidText(const std::string& str, SDL_Color color_fg) {
        return ORIG_FUNC(TTF_RenderUTF8_Solid)(font_handle, str.c_str(), color_fg);
    }

    SDL_Surface* TTFManager::RenderBlendedText(const std::string& str, SDL_Color color_fg) {
        return ORIG_FUNC(TTF_RenderUTF8_Blended)(font_handle, str.c_str(), color_fg);
    }

    SDL_Surface* TTFManager::RenderBlendedTextWrapped(const std::string& str, SDL_Color color_fg, Uint32 wrapLength) {
        return ORIG_FUNC(TTF_RenderUTF8_Blended_Wrapped)(font_handle, str.c_str(), color_fg, wrapLength);
    }

    SDL_Surface* TTFManager::RenderShadedText(const std::string& str, SDL_Color color_fg, SDL_Color color_bg) {
        return ORIG_FUNC(TTF_RenderUTF8_Shaded)(font_handle, str.c_str(), color_fg, color_bg);
    }

    int TTFManager::GetTextWidth(const std::string& str) {
        int w=0, h=0;
        ORIG_FUNC(TTF_SizeUTF8)(font_handle, str.c_str(), &w, &h);
        return w;
    }

    TTF_Font* TTFManager::LoadFontMatchingHeight(const std::string& font_file, int target_height_px, int style, int min_size, int max_size) {
        if (target_height_px <= 0) {
            return nullptr;
        }

        // Limit font size range to avoid infinite loop
        min_size = std::max(1, min_size);
        max_size = std::max(min_size, max_size);

        int best_size = -1;
        int best_height = -1; // Record actual_height
        TTF_Font* best_font = nullptr;

        // Binary search for the best font size
        int low = min_size, high = max_size;
        while (low <= high) {
            int mid = (low + high) / 2;

            TTF_Font* font = ORIG_FUNC(TTF_OpenFont)(font_file.c_str(), mid);
            if (!font) {
                // Failed to load font at this size, try smaller font sizes
                high = mid - 1;
                continue;
            }

            // TTF_SetFontStyle(font, style);
            int actual_height = ORIG_FUNC(TTF_FontHeight)(font);

            if (actual_height <= target_height_px) {
                if (actual_height > best_height) {
                    // Found a closer match
                    if (best_font) ORIG_FUNC(TTF_CloseFont)(best_font);
                    best_font = font;
                    best_height = actual_height;
                    best_size = mid;
                } else {
                    ORIG_FUNC(TTF_CloseFont)(font);
                }
                // Continue trying larger font sizes (possibly closer to target)
                low = mid + 1;
            } else {
                // Too large, must reduce font size
                ORIG_FUNC(TTF_CloseFont)(font);
                high = mid - 1;
            }
        }

        // If binary search fails (e.g., all attempts fail), fall back to min_size
        if (!best_font) {
            best_font = ORIG_FUNC(TTF_OpenFont)(font_file.c_str(), min_size);
            if (best_font) {
                best_height = ORIG_FUNC(TTF_FontHeight)(best_font);
                best_size = min_size;
            }
        }

        LOGGERMANAGER.getLogger()->info("LoadFontMatchingHeight:{} {} {} {}", font_file, target_height_px, best_height, best_size);
        return best_font;
    }

} // namespace Hooks
} // namespace DFCH
} // namespace DFHack
