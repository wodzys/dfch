// sdl2_hooks.h
#pragma once

#include "hook_common.h"

#include <SDL.h>
#include <SDL_syswm.h>

namespace DFHack {
namespace DFCH {
namespace Hooks {

// 函数指针类型
/* SDL.dll hook function */
typedef void(SDLCALL* SDL_GetWindowSize)(SDL_Window * window, int* w, int* h);
typedef void(SDLCALL* SDL_RenderPresent)(SDL_Renderer * renderer);
typedef int (SDLCALL * SDL_PollEvent)(SDL_Event * event);

typedef int (SDLCALL * SDL_RenderCopy)(SDL_Renderer * renderer, SDL_Texture * texture, const SDL_Rect * srcrect, const SDL_Rect * dstrect);
typedef SDL_Texture * (SDLCALL * SDL_CreateTextureFromSurface)(SDL_Renderer * renderer, SDL_Surface * surface);
typedef void (SDLCALL * SDL_FreeSurface)(SDL_Surface* surface);
typedef void (SDLCALL * SDL_DestroyTexture)(SDL_Texture * texture);
typedef SDL_Surface* (SDLCALL * SDL_CreateRGBSurfaceWithFormat)(Uint32 flags, int width, int height, int depth, Uint32 format);
typedef Uint32 (SDLCALL * SDL_MapRGBA)(const SDL_PixelFormat * format, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
typedef int (SDLCALL * SDL_FillRect)(SDL_Surface * dst, const SDL_Rect * rect, Uint32 color);
typedef int (SDLCALL * SDL_UpperBlit)(SDL_Surface * src, const SDL_Rect * srcrect, SDL_Surface * dst, SDL_Rect * dstrect);
typedef int (SDLCALL * SDL_SetTextureScaleMode)(SDL_Texture * texture, SDL_ScaleMode scaleMode);

// ---- Extended SDL2 functions (reserved) ----
typedef int (SDLCALL * SDL_RenderSetViewport)(SDL_Renderer * renderer, const SDL_Rect * rect);
typedef int (SDLCALL * SDL_RenderSetClipRect)(SDL_Renderer * renderer, const SDL_Rect * rect);
typedef void (SDLCALL * SDL_RenderGetScale)(SDL_Renderer * renderer, float *scaleX, float *scaleY);
typedef SDL_bool (SDLCALL * SDL_RenderIsClipEnabled)(SDL_Renderer * renderer);
typedef void (SDLCALL * SDL_RenderGetViewport)(SDL_Renderer * renderer, SDL_Rect * rect);
typedef void (SDLCALL * SDL_RenderGetClipRect)(SDL_Renderer * renderer, SDL_Rect * rect);
typedef int (SDLCALL * SDL_RenderGeometryRaw)(SDL_Renderer *renderer, SDL_Texture *texture, const float *xy, int xy_stride,
                       const SDL_Color *color, int color_stride, const float *uv, int uv_stride,
                       int num_vertices, const void *indices, int num_indices, int size_indices);
typedef SDL_Texture * (SDLCALL * SDL_CreateTexture)(SDL_Renderer * renderer, Uint32 format, int access, int w, int h);
typedef int (SDLCALL * SDL_UpdateTexture)(SDL_Texture * texture, const SDL_Rect * rect, const void *pixels, int pitch);
typedef int (SDLCALL * SDL_SetTextureBlendMode)(SDL_Texture * texture, SDL_BlendMode blendMode);
typedef Uint32 (SDLCALL * SDL_GetWindowID)(SDL_Window * window);
typedef Uint32 (SDLCALL * SDL_GetWindowFlags)(SDL_Window * window);
typedef void (SDLCALL * SDL_GetWindowPosition)(SDL_Window * window,int *x, int *y);
typedef int (SDLCALL * SDL_GetWindowDisplayIndex)(SDL_Window * window);
typedef SDL_bool (SDLCALL * SDL_GetWindowWMInfo)(SDL_Window * window, SDL_SysWMinfo * info);
typedef SDL_Window * (SDLCALL * SDL_GetKeyboardFocus)(void);
typedef SDL_Window * (SDLCALL * SDL_GetMouseFocus)(void);
typedef const Uint8* (SDLCALL * SDL_GetKeyboardState)(int *numkeys);
typedef SDL_bool (SDLCALL * SDL_GetRelativeMouseMode)(void);
typedef int (SDLCALL * SDL_CaptureMouse)(SDL_bool enabled);
typedef void (SDLCALL * SDL_WarpMouseInWindow)(SDL_Window * window, int x, int y);
typedef Uint32 (SDLCALL * SDL_GetGlobalMouseState)(int *x, int *y);
typedef char * (SDLCALL * SDL_GetClipboardText)(void);
typedef int (SDLCALL * SDL_SetClipboardText)(const char *text);
typedef SDL_Cursor* (SDLCALL * SDL_CreateSystemCursor)(SDL_SystemCursor id);
typedef void (SDLCALL * SDL_FreeCursor)(SDL_Cursor * cursor);
typedef void (SDLCALL * SDL_SetCursor)(SDL_Cursor * cursor);
typedef int (SDLCALL * SDL_ShowCursor)(int toggle);
typedef Uint64 (SDLCALL * SDL_GetPerformanceCounter)(void);
typedef Uint64 (SDLCALL * SDL_GetPerformanceFrequency)(void);
typedef int (SDLCALL * SDL_GetDisplayDPI)(int displayIndex, float * ddpi, float * hdpi, float * vdpi);
typedef int (SDLCALL * SDL_GetRendererOutputSize)(SDL_Renderer * renderer, int *w, int *h);
typedef void (SDLCALL * SDL_SetTextInputRect)(const SDL_Rect *rect);
typedef const char* (SDLCALL * SDL_GetCurrentVideoDriver)(void);
typedef int (SDLCALL * SDL_OpenURL)(const char *url);
typedef void (SDLCALL * SDL_GetVersion)(SDL_version * ver);
typedef void (SDLCALL * SDL_GL_GetDrawableSize)(SDL_Window * window, int *w, int *h);
typedef SDL_bool (SDLCALL * SDL_SetHint)(const char *name, const char *value);
typedef void (SDLCALL * SDL_free)(void *mem);
// ---- End extended SDL2 functions ----


#define FOR_EACH_ORIG_SDL_FUNC(X) \
    X(SDL_GetWindowSize) \
    X(SDL_RenderPresent) \
    X(SDL_PollEvent) \
    X(SDL_RenderCopy) \
    X(SDL_CreateTextureFromSurface) \
    X(SDL_FreeSurface) \
    X(SDL_DestroyTexture) \
    X(SDL_CreateRGBSurfaceWithFormat) \
    X(SDL_MapRGBA) \
    X(SDL_FillRect) \
    X(SDL_UpperBlit) \
    X(SDL_SetTextureScaleMode) \
    X(SDL_RenderSetViewport) \
    X(SDL_RenderSetClipRect) \
    X(SDL_RenderGetScale) \
    X(SDL_RenderIsClipEnabled) \
    X(SDL_RenderGetViewport) \
    X(SDL_RenderGetClipRect) \
    X(SDL_RenderGeometryRaw) \
    X(SDL_CreateTexture) \
    X(SDL_UpdateTexture) \
    X(SDL_SetTextureBlendMode) \
    X(SDL_GetWindowID) \
    X(SDL_GetWindowFlags) \
    X(SDL_GetWindowPosition) \
    X(SDL_GetWindowDisplayIndex) \
    X(SDL_GetWindowWMInfo) \
    X(SDL_GetKeyboardFocus) \
    X(SDL_GetMouseFocus) \
    X(SDL_GetKeyboardState) \
    X(SDL_GetRelativeMouseMode) \
    X(SDL_CaptureMouse) \
    X(SDL_WarpMouseInWindow) \
    X(SDL_GetGlobalMouseState) \
    X(SDL_GetClipboardText) \
    X(SDL_SetClipboardText) \
    X(SDL_CreateSystemCursor) \
    X(SDL_FreeCursor) \
    X(SDL_SetCursor) \
    X(SDL_ShowCursor) \
    X(SDL_GetPerformanceCounter) \
    X(SDL_GetPerformanceFrequency) \
    X(SDL_GetDisplayDPI) \
    X(SDL_GetRendererOutputSize) \
    X(SDL_SetTextInputRect) \
    X(SDL_GetCurrentVideoDriver) \
    X(SDL_OpenURL) \
    X(SDL_GetVersion) \
    X(SDL_GL_GetDrawableSize) \
    X(SDL_SetHint) \
    X(SDL_free)

// #define DECLARE_ORIG(fn) DECLARE_ORIG_FUNC(fn, fn)
// FOR_EACH_ORIG_SDL_FUNC(DECLARE_ORIG)
// #undef DECLARE_ORIG

// bool LoadSDL2Functions();

struct SDL2Functions {
    #define DEFINE_ORIG(fn) DEFINE_ORIG_FUNC(fn, fn)
    FOR_EACH_ORIG_SDL_FUNC(DEFINE_ORIG)
    #undef DEFINE_ORIG

    bool loadFunc();
};

extern SDL2Functions g_sdl2;

} // namespace Hooks
} // namespace DFCH
} // namespace DFHack
