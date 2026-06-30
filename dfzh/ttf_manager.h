#pragma once

#include "hook_common.h"

#include <SDL_ttf.h>

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace DFHack {
namespace DFCH {
namespace Hooks {
    typedef int (SDLCALL * TTF_Init)(void);
    typedef void (SDLCALL * TTF_Quit)(void);
    typedef TTF_Font * (SDLCALL * TTF_OpenFont)(const char *file, int ptsize);
    typedef void (SDLCALL * TTF_CloseFont)(TTF_Font *font);
    typedef int (SDLCALL * TTF_FontHeight)(const TTF_Font *font);
    typedef SDL_Surface * (SDLCALL * TTF_RenderUTF8_Solid)(TTF_Font *font, const char *text, SDL_Color fg);
    typedef SDL_Surface * (SDLCALL * TTF_RenderUTF8_Blended)(TTF_Font *font, const char *text, SDL_Color fg);
    typedef SDL_Surface * (SDLCALL * TTF_RenderUTF8_Blended_Wrapped)(TTF_Font *font, const char *text, SDL_Color fg, Uint32 wrapLength);
    typedef SDL_Surface * (SDLCALL * TTF_RenderUTF8_Shaded)(TTF_Font *font, const char *text, SDL_Color fg, SDL_Color bg);
    typedef int (SDLCALL * TTF_SizeUTF8)(TTF_Font *font, const char *text, int *w, int *h);
    typedef const char * (SDLCALL * TTF_GetError)(void);
    // SDL_SetTextureAlphaMod

    // TTF_GetError
    #define FOR_EACH_ORIG_TTF_FUNC(X) \
        X(TTF_Init) \
        X(TTF_Quit) \
        X(TTF_OpenFont) \
        X(TTF_CloseFont) \
        X(TTF_FontHeight) \
        X(TTF_RenderUTF8_Solid) \
        X(TTF_RenderUTF8_Blended) \
        X(TTF_RenderUTF8_Blended_Wrapped) \
        X(TTF_RenderUTF8_Shaded) \
        X(TTF_SizeUTF8)

    class TTFManager {
    public:
        static TTFManager& getInstance() {
            static TTFManager instance;
            return instance;
        }
        bool init();
        void shutdown();

        bool LoadFont(const std::string& font_file, int ptsize);
        bool ReloadFont(int new_height_px);
        void FreeFont();
        TTF_Font* LoadFontMatchingHeight(const std::string& font_file, int target_height_px, int style = 0, int min_size = 8, int max_size = 24);

        SDL_Surface* RenderSolidText(const std::string& str, SDL_Color color_fg);
        SDL_Surface* RenderBlendedText(const std::string& str, SDL_Color color_fg);
        SDL_Surface* RenderBlendedTextWrapped(const std::string& str, SDL_Color color_fg, Uint32 wrapLength=0);
        SDL_Surface* RenderShadedText(const std::string& str, SDL_Color color_fg, SDL_Color color_bg);
        int GetTextWidth(const std::string& str);

    private:
        TTFManager() = default;
        TTFManager(const TTFManager&) = delete;
        TTFManager& operator=(const TTFManager&) = delete;
        TTFManager(TTFManager&&) = delete;
        ~TTFManager() = default;

        int pre_height_px = 0;
        HMODULE sdl2_ttf_handle = nullptr;
        TTF_Font* font_handle = nullptr;
        bool initialized = false;

        #define DEFINE_ORIG(fn) DEFINE_ORIG_FUNC(fn, fn)
        FOR_EACH_ORIG_TTF_FUNC(DEFINE_ORIG)
        #undef DEFINE_ORIG

        bool LoadSDL2TTF();
        void CloseSDL2TTF();
        bool InitTTFFuncHandle();
        // void FreeTTFFuncHandle();
        bool TTFInit();
        void TTFQuit();
    };

    #define TTFMANAGER DFHack::DFCH::Hooks::TTFManager::getInstance()
} // namespace Hooks
} // namespace DFCH
} // namespace DFHack
