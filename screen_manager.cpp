
#include "screen_manager.h"
#include "dict_manager.h"
#include "rulesets_manager.h"
#include "sentence_detector.h"
#include "ttf_manager.h"
#include "logger.h"

#include "DFHackVersion.h"

#include "df/renderer.h"
#include "df/enabler.h"
#include "df/graphic.h"
#include "df/init.h"

#include <cstdio>
#include <cctype>
#include <string>
#include <ranges>
#include <regex>
#include <string_view>
#include <unordered_set>
#include <algorithm>
#include <format>
#include <sstream>

namespace DFHack {
namespace DFCH {
namespace Hooks {

    constexpr uint32_t BIT1 = 1;
    constexpr uint32_t BIT2 = 2;
    constexpr uint32_t BIT3 = 4;
    constexpr uint32_t BIT4 = 8;
    constexpr uint32_t BIT5 = 16;
    constexpr uint32_t BIT6 = 32;
    constexpr uint32_t BIT7 = 64;
    constexpr uint32_t BIT8 = 128;
    constexpr uint32_t BIT9 = 256;

    constexpr uint32_t SCREENTEXPOS_FLAG_TOP_OF_TEXT = BIT4;
    constexpr uint32_t SCREENTEXPOS_FLAG_BOTTOM_OF_TEXT = BIT5;

    ScreenManager::ScreenManager() {
        // Max tiles: 13000
        screen_bak.reserve(13000*8);
        screen_top_bak.reserve(13000*8);
        screentexpos_bak.reserve(13000);
        screentexpos_top_bak.reserve(13000);
        screentexpos_lower_bak.reserve(13000);
        screentexpos_top_lower_bak.reserve(13000);
        screentexpos_flag_bak.reserve(13000);
        screentexpos_top_flag_bak.reserve(13000);

        screen_char_buffer.reserve(13000);
        screen_char_buffer_old.reserve(13000);

        normalTranslations.reserve(200);
        freshTranslations.reserve(200);
        customTranslations.reserve(100);
    }

    ScreenManager::~ScreenManager() {
        df_sdl_renderer = nullptr;
        df_sdl_window = nullptr;
        screen_tmp = nullptr;
        screen_top_tmp = nullptr;
        screentexpos_tmp = nullptr;
        screentexpos_top_tmp = nullptr;
        // screentexpos_lower_tmp = nullptr;
        // screentexpos_top_lower_tmp = nullptr;
    }

    void ScreenManager::init() {
        printVersionInfo();
        if(checkDFPointer()) {
            df_sdl_renderer = reinterpret_cast<SDL_Renderer*>(df::global::enabler->renderer->get_renderer());
            if (!df_sdl_renderer) {
                // Try get df_sdl_renderer at preSDLRenderPresent function
                LOGGERMANAGER.getLogger()->error("ScreenManager::init: df_sdl_renderer is null");
            }
            df_sdl_window = reinterpret_cast<SDL_Window*>(df::global::enabler->renderer->get_window());
            if (!df_sdl_window) {
                // Try get df_sdl_window at onSDLGetWindowSize function
                LOGGERMANAGER.getLogger()->error("ScreenManager::init: df_sdl_window is null");
            }
        }
        if (!TTFMANAGER.init()) {
            return;
        }
        if (!DICTIONARY.init()) {
            return;
        }
        if (!RULESETS.init()) {
            return;
        }
        SENTENCEDETECTOR.init();

        initialized = true;
    }

    void ScreenManager::shutdown() {
        initialized = false;
        SENTENCEDETECTOR.shutdown();
        RULESETS.shutdown();
        DICTIONARY.shutdown();
        TTFMANAGER.shutdown();
    }
    void ScreenManager::screenChanged(std::string screen_name) {
        if (!initialized) {
            return;
        }
        DICTIONARY.screenChanged(screen_name);
        current_screen_name_dev = screen_name;
        LOGGERMANAGER.getLogger()->info("screen_name: {}", screen_name);
        if (screen_name.find("screen") == std::string::npos) {
            return;
        }
        current_screen_name = screen_name;
        screen_state_changed_flag = true;
    }

    void ScreenManager::doCommand(const std::vector<std::string>& commands) {
        if (!initialized) {
            return;
        }
        std::string cmd_str;
        for (const auto& cmd : commands) {
            cmd_str += cmd;
            cmd_str += ",";
        }
        LOGGERMANAGER.getLogger()->info("doCommand: {}", cmd_str);
        std::string main_cmd = commands[0];
        if (main_cmd == "save_untrans") {
            DICTIONARY.flushUntranslatedEntries();
        } else if (main_cmd == "reload_dicts") {
            DICTIONARY.reloadDicts();
            RULESETS.load_rule_sets();
            dict_reloaded_flag = true;
        } else if (main_cmd == "show_ch") {
            screen_show_trans_flag = !screen_show_trans_flag;
        }
    }

    // Backup code: void ScreenManager::preDFhooksPreRender() {
    // }

    void ScreenManager::onSDLGetWindowSize() {
        // #if 0
        if (current_screen_name == "screen_initial_prep") {
            return;
        }
        if (!initialized || !checkDFPointer()) {
            return;
        }
        // #0: prepare screen info
        if (!updateScreenInfo()) {
            return;
        }
        if (screen_size_changed_flag) {
            printScreenInfo();

            screen_bak.resize(tile_size*8);
            screen_top_bak.resize(tile_size*8);
            screentexpos_bak.resize(tile_size);
            screentexpos_top_bak.resize(tile_size);
            screentexpos_lower_bak.resize(tile_size);
            screentexpos_top_lower_bak.resize(tile_size);
            screentexpos_flag_bak.resize(tile_size);
            screentexpos_top_flag_bak.resize(tile_size);

            screen_char_buffer.resize(tile_size);
            screen_char_buffer_old.resize(tile_size);
            if (!TTFMANAGER.ReloadFont(dispy_z_adjusted)) {
                return;
            }
        }

        if (!screen_buffers_swapped_flag) {
            swapScreen();

            screen_buffers_swapped_flag = true;
        }

        // if (screen_show_trans_flag) {
        //     // std::fill(screentexpos_anchored_bak.begin(), screentexpos_anchored_bak.end(), 0);
        //     std::fill(screentexpos_bak.begin(), screentexpos_bak.end(), 0);
        // }
        // #1: filter screen buffer, detect source sentences
        // screen_char_buffer = std::move(filterScreen());
        // std::vector<unsigned char> filtered_screen = filterScreen();
        // std::memcpy(screen_char_buffer.data(), reinterpret_cast<const char*>(filtered_screen.data()), filtered_screen.size());

        filterScreenUpdate(screen_char_buffer);

        if (screen_size_changed_flag || (memcmp(screen_char_buffer_old.data(), screen_char_buffer.data(), (tile_size-dimx*3)) != 0)) {
            screen_char_buffer_old = screen_char_buffer;
            screen_contents_changed_flag = true;
        }

        if (screen_size_changed_flag || dict_reloaded_flag || (screen_contents_changed_flag && (current_screen_name == "screen_title"))) {
            textTextureCache.clear();
        }
        // #2: process translations
        if (screen_contents_changed_flag) {
            LOGGERMANAGER.getLogger()->info("onSDLGetWindowSize.top_in_use: {}, textTextureCache.size(): {}", top_in_use ? "true" : "false", textTextureCache.size());
            printScreenBuffer(screen_char_buffer);
            // TODO: multi thread
        }
        if (screen_contents_changed_flag || dict_reloaded_flag) {
            processTranslations();
        }

        // #3: create textures
        if (screen_show_trans_flag) {
            // Reset original English text positions to space characters
            clearTranslationAreas();
        }

        if (screen_contents_changed_flag || dict_reloaded_flag) {
            createNormalTextures();
            createCustomTextures();
        }
        createFreshTextures();

        if (current_screen_name == "screen_title") {
            if (detectDisplayedVersion()) {
                if (screen_contents_changed_flag) {
                    LOGGERMANAGER.getLogger()->info("displayed_version: {}", displayed_version);
                }
                addCHFlag();
            }
        }
        addPerformanceInfo();
        // TODO: add new func to show current screen name ahead of FPS?
        // TODO: add new func to show current mouse position behind the FPS

        dict_reloaded_flag = false;
        // #endif
    }

    // *** End of a render loop *** //
    bool ScreenManager::preSDLRenderPresent(SDL_Renderer * renderer) {
        bool ret = true;
        if (current_screen_name == "screen_initial_prep") {
            return ret;
        }
        if (!initialized || !checkDFPointer()) {
            return ret;
        }
        if (screen_buffers_swapped_flag) {
            restoreScreen();
            screen_buffers_swapped_flag = false;
        }

        if (screen_show_trans_flag) {
            // Try to render translations in this loop
            renderTextures(renderer);
        }

        if (current_screen_name == "screen_title") {
            showCHFlag(renderer);
        }
        if (screen_state_changed_flag) {
            ret = false;
        }

        screen_contents_changed_flag = false;
        screen_size_changed_flag = false;
        screen_state_changed_flag = false;

        ret = true; // TODO: tmp, just test purpose
        return ret;
    }

    bool ScreenManager::checkDFPointer() {
        if (!df::global::gps || !df::global::gps->screen || !df::global::gps->screentexpos || 
            !df::global::gps->screen_top || !df::global::gps->screentexpos_top || !df::global::gps->screentexpos_top_lower) {
            return false;
        }
        if (!df::global::enabler || !df::global::enabler->renderer) {
            return false;
        }
        auto& renderer = *df::global::enabler->renderer;
        if (!renderer.screen || !renderer.screentexpos || !renderer.screen_top || !renderer.screentexpos_top) {
            // backup code, not used
            // !df::global::enabler->renderer->screen_old || !df::global::enabler->renderer->screentexpos_old || 
            // !df::global::enabler->renderer->screen_top_old || !df::global::enabler->renderer->screentexpos_top_old
            //  renderer.screen_top renderer.screentexpos_top renderer.screentexpos_top_lower
            return false;
        }
        if (!df::global::init) {
            return false;
        }
        return true;
    }

    bool ScreenManager::updateScreenInfo() {
        dimx = df::global::gps->dimx;
        dimy = df::global::gps->dimy;
        dispx = df::global::gps->screen_pixel_x;
        dispy = df::global::gps->screen_pixel_y;
        dispx_z = df::global::gps->tile_pixel_x;
        dispy_z = df::global::gps->tile_pixel_y;
        top_in_use = df::global::gps->top_in_use;
        origin_x = (dispx - dispx_z * dimx) / 2;
        origin_y = (dispy - dispy_z * dimy) / 2;
        dispy_z_adjusted = static_cast<int>(dispy_z * textHeightScale + 0.5);

        if ((dimx <= 0) || (dimy <= 0) || (dispx <= 0) || (dispy <= 0) || (dispx_z <= 0) || (dispy_z <= 0)) {
            return false;
        }

        int origin_y_bias = ((dispy_z_adjusted - dispy_z) + 1) / 2;
        // origin_y = (origin_y > origin_y_bias) ? (origin_y - origin_y_bias) : 0;
        origin_y -= origin_y_bias;

        size_t new_tile_size = static_cast<size_t>(dimx*dimy);
        if(tile_size != new_tile_size) {
            tile_size = new_tile_size;
            screen_size_changed_flag = true;
        }
        return true;
    }

    std::vector<unsigned char> ScreenManager::filterScreen() {
        // const unsigned char* screen, const long* screentexpos, const unsigned char* screen_top, const long* screentexpos_top, const long* screentexpos_top_lower
        // screen_bak, screentexpos_bak, screen_top_bak, screentexpos_top_bak, screentexpos_top_lower_bak

        std::vector<unsigned char> result(tile_size);
        for (size_t y = 0; y < dimy; y++) {
            bool screentexpos_lower_pre1 = false;
            bool screentexpos_lower_pre2 = false;
            for (size_t x = 0; x < dimx; x++) {
                const size_t tile_row_major = y * dimx + x;
                const size_t tile_col_major = x * dimy + y;
                const size_t offset = tile_col_major * 8;
                unsigned char current_char = ' ';

                // if (top_in_use && screentexpos_top[tile_col_major] == 0) {
                // if (top_in_use && (screentexpos_top_lower[tile_col_major] != 0)) {
                if (top_in_use && (screentexpos_top_lower_bak[tile_col_major] != 0)) {
                    // const unsigned char* s_top = screen_top + offset;
                    // unsigned char ch_top = s_top[0];
                    const unsigned char ch_top = screen_top_bak[offset];
                    // if (valid_chars[ch_top]) {
                    //     current_char = ch_top;
                    // }
                    current_char = cp437_to_simple_ascii[ch_top];
                } else {
                // if (screentexpos[tile_col_major] == 0) {
                    // const unsigned char* s = screen + offset;
                    // const unsigned char s = screen_bak[offset];
                    // unsigned char ch = s[0];
                    const unsigned char ch = screen_bak[offset];
                    // if (valid_chars[ch]) {
                    //     current_char = ch;
                    // }
                    current_char = cp437_to_simple_ascii[ch];
                // }
                    // // TODO[12/28]: also screentexpos_bak
                    // if (screentexpos_lower_pre2 == screentexpos_lower_pre1 && screentexpos_lower_pre1 != (screentexpos_lower_bak[tile_col_major] == 0 ? false : true) && current_char == ' ') {
                    //     current_char = '|';
                    // }
                    // screentexpos_lower_pre2 = screentexpos_lower_pre1;
                    // screentexpos_lower_pre1 = (screentexpos_lower_bak[tile_col_major] == 0) ? false : true;
                }
                // result[tile_row_major] = static_cast<const char>(current_char);
                result[tile_row_major] = current_char;
            }
        }

        return result;
    }

    void ScreenManager::filterScreenUpdate(std::span<char> out_buffer) {
        // Blocked-transpose: read column-major DF screen buffers, write row-major to out_buffer.
        // Each tile block fits entirely in L1 cache — inner loops reuse cache-warm data.
        // Block row/column strides are defined separately so they can be tuned independently
        // for screen dimensions (e.g. dimx >> dimy prefers wider blocks).
        constexpr size_t BLOCK_ROWS = 16;
        constexpr size_t BLOCK_COLS = 16;

        // Column-major input strides: consecutive columns are dimy apart, rows within a column are adjacent.
        const size_t col_stride = dimy;
        const size_t row_stride = 1;

        for (size_t by = 0; by < dimy; by += BLOCK_ROWS) {
            const size_t y_end = (by + BLOCK_ROWS < dimy) ? (by + BLOCK_ROWS) : dimy;
            for (size_t bx = 0; bx < dimx; bx += BLOCK_COLS) {
                const size_t x_end = (bx + BLOCK_COLS < dimx) ? (bx + BLOCK_COLS) : dimx;
                for (size_t y = by; y < y_end; ++y) {
                    const size_t dst_base = y * dimx;
                    for (size_t x = bx; x < x_end; ++x) {
                        const size_t tile_col_major = x * col_stride + y * row_stride;
                        const size_t offset = tile_col_major * 8;

                        const unsigned char current_char = (top_in_use && (screentexpos_top_lower_bak[tile_col_major] != 0))
                            ? cp437_to_simple_ascii[screen_top_bak[offset]]
                            : cp437_to_simple_ascii[screen_bak[offset]];

                        out_buffer[dst_base + x] = static_cast<char>(current_char);
                    }
                }
            }
        }
    }

    void ScreenManager::swapScreen() {
        std::copy(df::global::gps->screen, df::global::gps->screen + tile_size*8, screen_bak.begin());
        std::copy(df::global::gps->screen_top, df::global::gps->screen_top + tile_size*8, screen_top_bak.begin());
        std::copy(df::global::gps->screentexpos, df::global::gps->screentexpos + tile_size, screentexpos_bak.begin());
        std::copy(df::global::gps->screentexpos_top, df::global::gps->screentexpos_top + tile_size, screentexpos_top_bak.begin());
        std::copy(df::global::gps->screentexpos_lower, df::global::gps->screentexpos_lower + tile_size, screentexpos_lower_bak.begin());
        std::copy(df::global::gps->screentexpos_top_lower, df::global::gps->screentexpos_top_lower + tile_size, screentexpos_top_lower_bak.begin());
        std::copy(df::global::gps->screentexpos_flag, df::global::gps->screentexpos_flag + tile_size, screentexpos_flag_bak.begin());
        std::copy(df::global::gps->screentexpos_top_flag, df::global::gps->screentexpos_top_flag + tile_size, screentexpos_top_flag_bak.begin());

        screen_tmp = df::global::gps->screen;
        screen_top_tmp = df::global::gps->screen_top;
        screentexpos_tmp = df::global::gps->screentexpos;
        screentexpos_top_tmp = df::global::gps->screentexpos_top;
        // screentexpos_lower_tmp = df::global::gps->screentexpos_lower;
        // screentexpos_top_lower_tmp = df::global::gps->screentexpos_top_lower;
        df::global::gps->screen = screen_bak.data();
        df::global::gps->screen_top = screen_top_bak.data();
        df::global::gps->screentexpos = screentexpos_bak.data();
        df::global::gps->screentexpos_top = screentexpos_top_bak.data();
        // df::global::gps->screentexpos_lower = screentexpos_lower_bak.data();
        // df::global::gps->screentexpos_top_lower = screentexpos_top_lower_bak.data();
        auto& renderer = *df::global::enabler->renderer;
        renderer.screen = df::global::gps->screen;
        renderer.screen_top = df::global::gps->screen_top;
        renderer.screentexpos = df::global::gps->screentexpos;
        renderer.screentexpos_top = df::global::gps->screentexpos_top;
        // renderer.screentexpos_lower = df::global::gps->screentexpos_lower;
        // renderer.screentexpos_top_lower = df::global::gps->screentexpos_top_lower;
    }
    // Reset the screen buffer to its original state
    void ScreenManager::restoreScreen() {
        df::global::gps->screen = screen_tmp;
        df::global::gps->screen_top = screen_top_tmp;
        df::global::gps->screentexpos = screentexpos_tmp;
        df::global::gps->screentexpos_top = screentexpos_top_tmp;
        // df::global::gps->screentexpos_lower = screentexpos_lower_tmp;
        // df::global::gps->screentexpos_top_lower = screentexpos_top_lower_tmp;
        auto& renderer = *df::global::enabler->renderer;
        renderer.screen = df::global::gps->screen;
        renderer.screen_top = df::global::gps->screen_top;
        renderer.screentexpos = df::global::gps->screentexpos;
        renderer.screentexpos_top = df::global::gps->screentexpos_top;
        // renderer.screentexpos_lower = df::global::gps->screentexpos_lower;
        // renderer.screentexpos_top_lower = df::global::gps->screentexpos_top_lower;
        screen_tmp = nullptr;
        screen_top_tmp = nullptr;
        screentexpos_tmp = nullptr;
        screentexpos_top_tmp = nullptr;
        // screentexpos_lower_tmp = nullptr;
        // screentexpos_top_lower_tmp = nullptr;
    }

    bool ScreenManager::detectDisplayedVersion() {
        const size_t displayed_version_pos = static_cast<size_t>(dimx*(dimy-2));
        const unsigned char version_str_len = sizeof(displayed_version) - 1;

        if ((displayed_version_pos + version_str_len < tile_size) &&
            (screen_char_buffer[displayed_version_pos] == 'v') &&
            (screen_char_buffer[displayed_version_pos+3] == '.')) {
            for (unsigned char i = 0; i < version_str_len; i++) {
                displayed_version[i] = screen_char_buffer[displayed_version_pos + i];
            }
            return true;
        }
        return false;
    }

    void ScreenManager::addCHFlag() {
        static constexpr char kChineseFlagText[] = " -- Chinese";
        static constexpr int kChineseFlagLen = sizeof(kChineseFlagText) - 1;
        static constexpr int kVersionStrLen = sizeof(displayed_version) - 1;

        const SDL_Color kTextFg = {255, 255, 153}; // pale yellow
        const SDL_Color kTextBg = {0, 0, 0};       // black

        const int y = dimy - 2;
        for (int i = 0; i < kChineseFlagLen; ++i) {
            const int x = kVersionStrLen + i;
            addChar2Screen(kChineseFlagText[i], x, y, &kTextFg, &kTextBg);
        }
    }

    void ScreenManager::addPerformanceInfo() {
        std::string localization_time_percent_text = std::format("{:2}% (f/10)", localization_time_percent);
        int text_x = 56;
        int text_y = dimy - 1;
        SDL_Color text_fg = {0,0,0};
        SDL_Color text_bg = {113,187,176};

        for (auto c : localization_time_percent_text) {
            addChar2Screen(static_cast<unsigned char>(c), text_x, text_y, &text_fg, &text_bg);
            text_x++;
        }
    }

    void ScreenManager::showCHFlag(SDL_Renderer * sdl_render) {
        const std::string ch_str = "CH汉化";
        int x_ch_flag = 1;
        int y_ch_flag = -3 + dimy;
        SDL_Rect renderRect;
        SDL_Color text_fg = {192,192,192};
        SDL_Color text_bg = {0,0,0};

        renderRect.x = x_ch_flag * dispx_z + origin_x;
        renderRect.y = y_ch_flag * dispy_z + origin_y;

        if (auto multi_asset = textTextureCache.get(ch_str)) {
            if (!multi_asset->lines.empty() && multi_asset->lines[0].texture) {
                const auto& line_asset = multi_asset->lines[0];
                renderRect.w = line_asset.w;
                renderRect.h = line_asset.h;
                g_sdl2.ORIG_FUNC(SDL_RenderCopy)(sdl_render, line_asset.texture.get(), nullptr, &renderRect);
                return;
            }
        }
        SDL_Surface* textSurface = TTFMANAGER.RenderBlendedText(ch_str, text_fg);
        // SDL_Surface* textSurface = TTFMANAGER.RenderShadedText(str, text_fg, text_bg);
        // SDL_Surface* textSurface = TTFMANAGER.RenderSolidText(str, text_fg);
        if (!textSurface) {
            LOGGERMANAGER.getLogger()->error("textSurface generation error on string {}", ch_str);
            return;
        }

        // Convert Surface to Texture
        SDL_Texture *textTexture = g_sdl2.ORIG_FUNC(SDL_CreateTextureFromSurface)(sdl_render, textSurface);
        if (!textTexture) {
            LOGGERMANAGER.getLogger()->error("textTexture: Failed to create textTexture");
            g_sdl2.ORIG_FUNC(SDL_FreeSurface)(textSurface);
            return;
        }

        int surface_h = textSurface->h;
        int surface_w = textSurface->w;
        g_sdl2.ORIG_FUNC(SDL_FreeSurface)(textSurface);

        // Calculate width to maintain original aspect ratio
        renderRect.w = surface_w;
        renderRect.h = surface_h;

        g_sdl2.ORIG_FUNC(SDL_RenderCopy)(sdl_render, textTexture, NULL, &renderRect);

        if (screen_size_changed_flag) {
            LOGGERMANAGER.getLogger()->debug("textSurfacesize: w: {}, h: {}, renderRect.w: {}, renderRect.h: {}", surface_w, surface_h, renderRect.w, renderRect.h);
        }

        MultiLineTextureAsset multi_asset;
        multi_asset.lines.emplace_back(renderRect.w, renderRect.h, SDLTexturePtr(textTexture, SDLTextureDeleter{}));
        textTextureCache.put(ch_str, std::move(multi_asset));
    }

    void ScreenManager::updatePerformanceStatistics(std::int64_t proc_us, std::int64_t frame_us) {
        // localization_time_percent = proc_us;
        if (frame_us == 0) return;
        float local_time_percent = static_cast<float>(proc_us * 100) * 10 / frame_us;
        // Exponential Moving Average
        if (smoothed_value == 0.0f) {
            smoothed_value = local_time_percent;
        } else {
            smoothed_value = FRAME_SMOOTH_ALPHA * local_time_percent +
                                (1.0f - FRAME_SMOOTH_ALPHA) * smoothed_value;
        }
        localization_time_percent = static_cast<uint64_t>(smoothed_value);
    }

    void ScreenManager::processTranslations() {
        std::string translation;
        unsigned char alignment = 'l';
        normalTranslations.clear();
        freshTranslations.clear();
        customTranslations.clear();

        auto detection_results = SENTENCEDETECTOR.detectSentences(screen_char_buffer, dimy, dimx);
        LOGGERMANAGER.getLogger()->debug("Translation detection_results: {} sentences.", detection_results.sentences.size());

        // TODO[12/12]: using size_t instead of int
        for (auto& sentence : detection_results.sentences) {
            const int start_x = static_cast<int>(sentence.start_col);
            const int start_y = static_cast<int>(sentence.start_row);
            const int row_span = static_cast<int>(sentence.end_row - sentence.start_row + 1);
            const int col_span = static_cast<int>(sentence.end_col - sentence.start_col + 1);

            if (sentence.content.empty() || row_span <= 0 || col_span <= 0) continue;

            uint32_t text_flag = getTextFlag(start_x, start_y);
            // 感觉需要加上，但是为什么之前没有出现问题
            translation.clear();

            // 2-tier translation: exact match → rulesets fallback
            if (DICTIONARY.tryTranslate(sentence.content, translation, alignment)) {
                // Only for log, not used in rendering, to replace special characters in log
                // translation = std::regex_replace(translation, std::regex("\n"), R"(\n)");   // for printing translated text
                // translation = std::regex_replace(translation, std::regex("\a"), R"(\a)");   // for printing translated text
                // exact match
            } else if (auto ruleset_result = RULESETS.translate(sentence.content)) {
                translation = std::move(*ruleset_result);
                alignment = 'l';
            }

            if (!translation.empty()) {
                if (text_flag & SCREENTEXPOS_FLAG_TOP_OF_TEXT) {
                    alignment = 't';
                } else if (text_flag & SCREENTEXPOS_FLAG_BOTTOM_OF_TEXT) {
                    // Skip bottom of tab text
                    translation.clear();
                    alignment = 't';
                }

                std::string default_fg;
                std::vector<ColorSpan> color_spans;
                std::vector<size_t> break_points;
                if ((alignment == 'l') && !translation.empty()) {
                    std::tie(default_fg, color_spans) = processWordsColor(sentence.words, translation);
                    if (row_span > 1) {
                        break_points = computeLineBreaks(translation, color_spans, col_span);
                    }
                    if (!color_spans.empty() || !break_points.empty()) {
                        alignment = 's';
                    }
                }

                // For logging - start
                std::ostringstream oss;
                for (size_t i = 0; i < sentence.words.size(); ++i) {
                    if (i > 0) oss << ' ';
                    oss << sentence.words[i].content;
                }
                std::string wordsStr = oss.str();

                LOGGERMANAGER.getLogger()->debug("Trans[{:2d},{:2d}],[{:2d},{:2d}],[{:2d},{:2d}][{}]:<{}>-<{}>+{}.", start_x, start_y, sentence.end_col, sentence.end_row, row_span, col_span, alignment, sentence.content, translation, wordsStr);
                // For logging - end

                if (alignment == 's') {
                    customTranslations.emplace_back(start_x, start_y, row_span, col_span, std::move(sentence.content), std::move(translation), alignment,
                                                    std::move(default_fg), std::move(color_spans), std::move(break_points));
                } else if (alignment == 't' || alignment == 'b' || alignment == 'd') {
                    freshTranslations.emplace_back(start_x, start_y, row_span, col_span, std::move(sentence.content), std::move(translation), alignment);
                } else {
                    normalTranslations.emplace_back(start_x, start_y, row_span, col_span, std::move(sentence.content), std::move(translation), alignment);
                }
            } else {
                // TODO[12/08]: log unprocessed sentences and words with color
                std::ostringstream oss;
                for (size_t i = 0; i < sentence.words.size(); ++i) {
                    if (i > 0) oss << ' ';
                    oss << sentence.words[i].content;
                }
                std::string wordsStr = oss.str();

                LOGGERMANAGER.getLogger()->debug("UnTrans[{:2d},{:2d}],[{:2d},{:2d}]:<{}>+{}.", sentence.start_col, sentence.start_row, sentence.end_col, sentence.end_row, sentence.content, wordsStr);
            }
        }
        LOGGERMANAGER.getLogger()->debug("Translation {}+{}+{} processing completed.", normalTranslations.size(), freshTranslations.size(), customTranslations.size());
    }

    std::pair<std::string, std::vector<ScreenManager::ColorSpan>> ScreenManager::processWordsColor(const std::vector<WordData>& words, const std::string& sentence_trans) {
        std::vector<std::string> colors;
        colors.reserve(words.size());
        for (const auto& word : words) {
            SDL_Color fg = getForegroundColor(word.start_col, word.start_row);
            colors.emplace_back(colorToHex(fg));
        }

        auto [isSingleColor, default_fg] = isSingleColorAndGetMostFrequent(colors);
        if (isSingleColor) {
            return {std::move(default_fg), {}};
        }

        // Step 3: 构建需要着色的翻译词列表（仅非默认色）
        std::vector<std::pair<std::string, std::string>> trans_colors;
        trans_colors.reserve(words.size());

        for (size_t i = 0; i < words.size(); ++i) {
            if (colors[i] == default_fg) {
                continue;
            }
            std::string word_trans;
            if (DICTIONARY.wordTranslate(words[i].content, word_trans) && !word_trans.empty()) {
                LOGGERMANAGER.getLogger()->debug("processWordsColor WordColor[{}]: {}, {}, {}", i, words[i].content, word_trans, colors[i]);
                trans_colors.emplace_back(std::move(word_trans), std::move(colors[i]));
            }
        }

        if (trans_colors.empty()) {
            return {std::move(default_fg), {}}; // 无有效彩色项
        }

        size_t searchPos = 0;
        std::vector<ColorSpan> color_spans;
        color_spans.reserve(trans_colors.size());

        for (auto [word_text, word_color] : trans_colors) {
            size_t pos = sentence_trans.find(word_text, searchPos);
            if (pos == std::string::npos) {
                continue; // 未找到，跳过（不影响其他词）
            }
            color_spans.emplace_back(pos, pos + word_text.length(), std::move(word_color));
            LOGGERMANAGER.getLogger()->debug("processWordsColor ColorSpan[{}]: {}, {}", color_spans.size(), pos, pos + word_text.length());
            // searchPos = pos + word_text.length();    // 避免中文翻译词序与英文词序不一致
        }

        // 确保 color_spans 按 start_idx 升序排列，generateMultiLineTextureAsset 的遍历逻辑依赖此顺序
        std::sort(color_spans.begin(), color_spans.end(),
            [](const ColorSpan& a, const ColorSpan& b) { return a.start_idx < b.start_idx; });

        LOGGERMANAGER.getLogger()->debug("processWordsColor ColorSpan: {}, color_spans.size: {}", sentence_trans, color_spans.size());
        return {std::move(default_fg), std::move(color_spans)};
    }

    std::vector<size_t> ScreenManager::computeLineBreaks(const std::string& sentence_trans, const std::vector<ColorSpan>& protectedRanges, size_t col_span) {
        auto trans_text_width_px = TTFMANAGER.GetTextWidth(sentence_trans);
        int limit_line_width_px = col_span * dispx_z;
        if(trans_text_width_px <= limit_line_width_px) {
            return {};
        }
        float char_width_px = static_cast<float>(trans_text_width_px) / sentence_trans.length();
        int limit_line_char_count = limit_line_width_px / char_width_px;
        if (limit_line_char_count <= 4) return {};
        LOGGERMANAGER.getLogger()->debug("computeLineBreaks limit_line_char_count: {}", limit_line_char_count);

        auto ranges = protectedRanges;
        std::sort(ranges.begin(), ranges.end(), [](const ColorSpan& a, const ColorSpan& b) {
            return a.start_idx < b.start_idx;
        });

        // === Step 1: 计算 breaks ===
        std::vector<size_t> breaks;
        size_t pos = 0;
        size_t rangeIdx = 0;
        size_t n = sentence_trans.size();

        while (pos < n) {
            size_t lineEnd = pos + limit_line_char_count;
            if (lineEnd >= n) break;
            size_t safeEnd = lineEnd;
            while (safeEnd > pos && isContinuationByte(static_cast<unsigned char>(sentence_trans[safeEnd]))) {
                --safeEnd;
            }
            while (rangeIdx < ranges.size()) {
                const auto& range = ranges[rangeIdx];
                // 1. 如果保护区域完全在 safeEnd 左侧（已过），跳过
                if (range.end_idx <= safeEnd) {
                    ++rangeIdx;
                    continue;
                }
                // 2. 如果保护区域起始位置 >= safeEnd，后续都更远，停止
                if (range.start_idx >= safeEnd) {
                    break;
                }
                // 3. 此时：range 与 [pos, safeEnd) 有交集
                if (range.end_idx > safeEnd) {
                    // 保护区域跨过 safeEnd → 必须提前行尾到其起点
                    safeEnd = range.start_idx;
                    break; // 找到最近的冲突，无需继续
                } else {
                    // 保护区域完全包含在 [pos, safeEnd) 内
                    ++rangeIdx;
                    continue;
                }
            }
            breaks.push_back(safeEnd);
            LOGGERMANAGER.getLogger()->debug("computeLineBreaks break: {}", safeEnd);
            pos = safeEnd;
        }

        return breaks;
    }

    std::pair<bool, std::string> ScreenManager::isSingleColorAndGetMostFrequent(const std::vector<std::string>& colors) {
        if (colors.empty()) {
            return {true, ""}; // 空序列视为“只有一种颜色”的退化情况
        }

        std::unordered_map<std::string, int> count;
        for (const auto& color : colors) {
            ++count[color];
        }

        // 判断是否只有一种颜色
        if (count.size() == 1) {
            return {true, colors[0]}; // 或 count.begin()->first
        }

        // 多种颜色：找最频繁的（频次相同时取原始序列中第一个达到最大频次的）
        int maxCount = 0;
        std::string outMostFrequentColor;
        for (const auto& color : colors) {
            if (count[color] > maxCount) {
                maxCount = count[color];
                outMostFrequentColor = color;
            }
        }

        return {false, outMostFrequentColor};
    }

    void ScreenManager::renderTextures(SDL_Renderer *renderer) {
        if (!renderer) return;
        renderNormalCachedTextures(renderer);
        renderFreshCachedTextures(renderer);
        renderCustomCachedTextures(renderer);
    }

    void ScreenManager::clearTranslationAreas() {
        clearTextAreaContainer(normalTranslations);
        clearTextAreaContainer(freshTranslations);
        clearTextAreaContainer(customTranslations);
    }

    void ScreenManager::addChar2Screen(const unsigned char c, const int x, const int y, const SDL_Color* fg_color, const SDL_Color* bg_color) {
        if (x < 0 || x >= dimx || y < 0 || y >= dimy) {
            return;
        }
        const size_t tile_col_major = static_cast<size_t>(x * dimy + y);
        const size_t offset = static_cast<size_t>(tile_col_major * 8);

        auto& buffer = getActiveBuffer(tile_col_major);

        buffer[offset + 0] = c;

        // Only modify foreground color when fg_color pointer is non-null
        if (fg_color) {
            buffer[offset + 1] = static_cast<unsigned char>(fg_color->r);
            buffer[offset + 2] = static_cast<unsigned char>(fg_color->g);
            buffer[offset + 3] = static_cast<unsigned char>(fg_color->b);
        }

        // Only modify background color when bg_color pointer is non-null
        if (bg_color) {
            buffer[offset + 4] = static_cast<unsigned char>(bg_color->r);
            buffer[offset + 5] = static_cast<unsigned char>(bg_color->g);
            buffer[offset + 6] = static_cast<unsigned char>(bg_color->b);
        }

        // if (top_in_use && (screentexpos_top_lower_bak[tile_col_major] != 0)) {
        //     screentexpos_top_bak[tile_col_major] = 0;
        // } else {
        //     screentexpos_bak[tile_col_major] = 0;
        // }
    }

    void ScreenManager::createNormalTextures() {
        // Create SDL_Texture for each translation result
        int render_rect_w;
        int render_rect_h;
        for (const auto& pending_trans : normalTranslations) {
            if (pending_trans.target_text.empty()) {
                continue;
            }

            // Try: Reduce key length
            // std::string source_key = std::format("{}:{}:{}", pending_trans.source_text, pending_trans.start_x, pending_trans.start_y);
            std::string_view source_text = pending_trans.source_text;
            if (source_text.size() > kMaxKeyLength) source_text = source_text.substr(source_text.size() - kMaxKeyLength);
            std::string source_key = std::format("{}{}{}", source_text, pending_trans.start_x+pending_trans.start_y, pending_trans.col_span);

            SDL_Color text_fg = getForegroundColor(pending_trans.start_x, pending_trans.start_y);

            if (auto multi_asset = textTextureCache.get(source_key); multi_asset && areColorsEqual(multi_asset->default_fg, text_fg)) {
                continue;
            }

            SDL_Texture* textTexture = createTextTexture(pending_trans.target_text, text_fg, render_rect_w, render_rect_h);
            if (!textTexture) {
                continue;
            }
            MultiLineTextureAsset multi_asset;
            multi_asset.lines.emplace_back(
                render_rect_w, render_rect_h,
                SDLTexturePtr(textTexture, SDLTextureDeleter{})
            );
            multi_asset.default_fg = text_fg;
            textTextureCache.put(std::move(source_key), std::move(multi_asset));
            // LOGGERMANAGER.getLogger()->debug("Created texture for translation: '{}' -> '{}'", pending_trans.source_text, pending_trans.target_text);
        }
        // LOGGERMANAGER.getLogger()->debug("Translation textures creation completed. Total textures created: {:zu}", normalTextures.size());
    }

    void ScreenManager::createFreshTextures() {
        // Create SDL_Texture for each translation result
        int render_rect_w;
        int render_rect_h;
        for (auto& pending_trans : freshTranslations) {
            if (pending_trans.target_text.empty()) {
                continue;
            }

            // Try: Reduce key length
            std::string_view source_text = pending_trans.source_text;
            if (source_text.size() > kMaxKeyLength) source_text = source_text.substr(source_text.size() - kMaxKeyLength);
            std::string source_key = std::format("{}{}{}", source_text, pending_trans.start_x+pending_trans.start_y, pending_trans.col_span);

            SDL_Color text_fg = getForegroundColor(pending_trans.start_x, pending_trans.start_y);

            // if (textTextureCache.get(source_key) && areColorsEqual(pending_trans.fg_color, text_fg) && !screen_contents_changed_flag) {
            //     continue;
            // }
            if (auto multi_asset = textTextureCache.get(source_key); multi_asset && areColorsEqual(multi_asset->default_fg, text_fg)) {
                continue;
            }
            // Update color information for fresh translations
            // pending_trans.fg_color = std::move(text_fg);

            SDL_Texture* textTexture = createTextTexture(pending_trans.target_text, text_fg, render_rect_w, render_rect_h);
            if (!textTexture) {
                continue;
            }
            MultiLineTextureAsset multi_asset;
            multi_asset.lines.emplace_back(
                render_rect_w, render_rect_h,
                SDLTexturePtr(textTexture, SDLTextureDeleter{})
            );
            multi_asset.default_fg = text_fg;
            textTextureCache.put(std::move(source_key), std::move(multi_asset));
        }
    }

    void ScreenManager::createCustomTextures() {
        for (const auto& pending_trans : customTranslations) {
            if (pending_trans.target_text.empty()) {
                continue;
            }

            // Try: Reduce key length
            // std::string source_key = std::format("{}:{}:{}", pending_trans.source_text, pending_trans.start_x, pending_trans.start_y);
            std::string_view source_text = pending_trans.source_text;
            if (source_text.size() > kMaxKeyLength) source_text = source_text.substr(source_text.size() - kMaxKeyLength);
            std::string source_key = std::format("{}{}{}", source_text, pending_trans.start_x+pending_trans.start_y, pending_trans.col_span);

            SDL_Color default_fg;
            if (!pending_trans.default_fg.empty()) {
                default_fg = parseColorHex(pending_trans.default_fg);
            } else {
                default_fg = getForegroundColor(pending_trans.start_x, pending_trans.start_y);
            }
            // === 1. Try to fetch from cache ===
            if (auto multi_asset = textTextureCache.get(source_key); multi_asset && areColorsEqual(multi_asset->default_fg, default_fg)) {
                continue;
            }
            // === 2. Generate new texture asset ===
            // SDL_Color default_fg = getForegroundColor(pending_trans.start_x, pending_trans.start_y);
            MultiLineTextureAsset multi_asset;
            if (!pending_trans.default_fg.empty()) {
                // default_fg = parseColorHex(pending_trans.default_fg);
                multi_asset = generateMultiLineTextureAsset(pending_trans.target_text, default_fg, pending_trans.color_spans, pending_trans.break_points);
            } else {
                // default_fg = getForegroundColor(pending_trans.start_x, pending_trans.start_y);
                multi_asset = generateMultiLineTextureAsset(pending_trans.target_text, default_fg);
            }
            multi_asset.default_fg = default_fg;
            if (!multi_asset.empty()) {
                textTextureCache.put(std::move(source_key), std::move(multi_asset));
            } else {
                LOGGERMANAGER.getLogger()->warn("Failed to generate texture for: {}", source_key);
            }
        }
    }

    SDL_Color ScreenManager::getForegroundColor(const int x, const int y) const {
        if (x < 0 || x >= dimx || y < 0 || y >= dimy) {
            return SDL_Color{0, 0, 0, 0}; // Return black transparent as the default value
        }
        const size_t tile_col_major = static_cast<size_t>(x * dimy + y);
        const size_t offset = static_cast<size_t>(tile_col_major * 8);

        const auto& buffer = getActiveBuffer(tile_col_major);

        return SDL_Color{
            buffer[offset + 1],
            buffer[offset + 2],
            buffer[offset + 3],
            255
        };
    }

    uint32_t ScreenManager::getTextFlag(const int x, const int y) const {
        if (x < 0 || x >= dimx || y < 0 || y >= dimy) {
            return 0;
        }
        const size_t tile_col_major = static_cast<size_t>(x * dimy + y);

        if (top_in_use && (screentexpos_top_lower_bak[tile_col_major] != 0)) {
            return screentexpos_top_flag_bak[tile_col_major];
        } else {
            return screentexpos_flag_bak[tile_col_major];
        }
    }

    // Backup code
    // bool ScreenManager::isConfirmButtonText(const int x, const int y) const {
    //     if (x < 0 || x >= dimx || y < 0 || y >= dimy) {
    //         return 0;
    //     }
    //     const size_t tile_col_major = static_cast<size_t>(x * dimy + y);

    //     // texpos_confirm_intro_button[4] is the middle texture of the button
    //     if (top_in_use && (screentexpos_top_lower_bak[tile_col_major] != 0)) {
    //         return (screentexpos_top_lower_bak[tile_col_major] == df::global::init->texpos_confirm_intro_button[4]);
    //     } else {
    //         return (screentexpos_lower_bak[tile_col_major] == df::global::init->texpos_confirm_intro_button[4]);
    //     }
    // }

    SDL_Texture* ScreenManager::createTextTexture(const std::string& text, SDL_Color color_fg, int& out_w_px, int& out_h_px) {
        // Backup code: SDL_Surface* textSurface = TTFMANAGER.RenderSolidText(text, color_fg);
        SDL_Surface* textSurface = TTFMANAGER.RenderBlendedText(text, color_fg);
        if (!textSurface) {
            LOGGERMANAGER.getLogger()->error("Text surface generation error on string: {}", text);
            return nullptr;
        }

        SDL_Texture* textTexture = g_sdl2.ORIG_FUNC(SDL_CreateTextureFromSurface)(df_sdl_renderer, textSurface);
        if (!textTexture) {
            LOGGERMANAGER.getLogger()->error("Failed to create text texture");
            g_sdl2.ORIG_FUNC(SDL_FreeSurface)(textSurface);
            return nullptr;
        }

        out_w_px = textSurface->w;
        out_h_px = textSurface->h;

        g_sdl2.ORIG_FUNC(SDL_FreeSurface)(textSurface);
        return textTexture;
    }

    ScreenManager::MultiLineTextureAsset ScreenManager::generateMultiLineTextureAsset(const std::string& target_text, SDL_Color default_fg) {
        MultiLineTextureAsset asset;

        // Split into multiple lines by '\n'
        std::vector<std::string> lines;
        if (target_text.find('\n') != std::string::npos) {
            for (const auto& part : std::ranges::views::split(target_text, '\n')) {
                lines.emplace_back(part.begin(), part.end());
            }
        } else {
            lines.push_back(target_text);
        }

        for (const auto& line : lines) {
            if (line.empty()) continue;

            SDL_Surface* surf = nullptr;
            int width = 0;
            int height = 0;

            if (line.find('\a') != std::string::npos) {
                surf = createCombinedLineSurface(line, default_fg);
            } else {
                surf = TTFMANAGER.RenderBlendedText(line, default_fg);
            }

            if (!surf) continue;
            width = surf->w;
            height = surf->h;

            SDL_Texture* tex = g_sdl2.ORIG_FUNC(SDL_CreateTextureFromSurface)(df_sdl_renderer, surf);
            g_sdl2.ORIG_FUNC(SDL_FreeSurface)(surf);
            if (!tex) continue;

            asset.lines.emplace_back(width, height, SDLTexturePtr(tex, SDLTextureDeleter{}));
        }
        return asset;
    }

    ScreenManager::MultiLineTextureAsset ScreenManager::generateMultiLineTextureAsset(const std::string& target_text, SDL_Color default_fg,
        const std::vector<ColorSpan>& color_spans, const std::vector<size_t>& break_points) {
        MultiLineTextureAsset asset;

        // Split into multiple lines by '\n'
        std::vector<std::pair<std::string, size_t>> lines;
        if (!break_points.empty()) {
            size_t prev_break_pos = 0;
            size_t current_break_pos = 0;
            for (size_t break_pos : break_points) {
                current_break_pos = break_pos;
                size_t line_length = current_break_pos - prev_break_pos;
                lines.emplace_back(target_text.substr(prev_break_pos, line_length), prev_break_pos);
                prev_break_pos = break_pos;
            }
            if (prev_break_pos < target_text.length()) {
                lines.emplace_back(target_text.substr(prev_break_pos), prev_break_pos);
            }
        } else {
            lines.emplace_back(target_text, 0);
        }

        size_t color_span_idx = 0;
        for (const auto& [line, line_start_pos] : lines) {
            if (line.empty()) continue;

            SDL_Surface* line_surface = nullptr;
            int line_width = 0;
            int line_height = 0;

            if (!color_spans.empty()) {
                size_t current_line_end = line_start_pos + line.length();
                std::vector<SDL_Surface*> segment_surfaces;
                size_t current_line_pos = 0;
                int combined_width = 0;
                while (color_span_idx < color_spans.size()) {
                    const auto& color_span = color_spans[color_span_idx];
                    size_t line_span_start = color_span.start_idx - line_start_pos;
                    size_t line_span_end = color_span.end_idx - line_start_pos;
                    if (line_span_start >= current_line_end) break;
                    if (current_line_pos != line_span_start) {
                        std::string text_segment = line.substr(current_line_pos, line_span_start - current_line_pos);
                        SDL_Surface* segment_surface = TTFMANAGER.RenderBlendedText(text_segment, default_fg);
                        if (segment_surface) {
                            segment_surfaces.push_back(segment_surface);
                            combined_width += segment_surface->w;
                        }
                    }

                    std::string text_segment = line.substr(line_span_start, color_span.end_idx - color_span.start_idx);
                    SDL_Surface* segment_surface = TTFMANAGER.RenderBlendedText(text_segment, parseColorHex(color_span.color_hex));
                    if (segment_surface) {
                        segment_surfaces.push_back(segment_surface);
                        combined_width += segment_surface->w;
                    }
                    current_line_pos = line_span_end;
                    color_span_idx++;
                }
                if (current_line_pos < current_line_end) {
                    std::string text_segment = line.substr(current_line_pos, current_line_end - current_line_pos);
                    SDL_Surface* segment_surface = TTFMANAGER.RenderBlendedText(text_segment, default_fg);
                    if (segment_surface) {
                        segment_surfaces.push_back(segment_surface);
                        combined_width += segment_surface->w;
                    }
                }
                line_surface = combineSegmentSurfaces(segment_surfaces, combined_width);
            } else {
                line_surface = TTFMANAGER.RenderBlendedText(line, default_fg);
            }

            if (!line_surface) continue;
            line_width = line_surface->w;
            line_height = line_surface->h;

            SDL_Texture* line_texture = g_sdl2.ORIG_FUNC(SDL_CreateTextureFromSurface)(df_sdl_renderer, line_surface);
            g_sdl2.ORIG_FUNC(SDL_FreeSurface)(line_surface);
            if (!line_texture) continue;

            asset.lines.emplace_back(line_width, line_height, SDLTexturePtr(line_texture, SDLTextureDeleter{}));
        }

        return asset;
    }

    SDL_Surface* ScreenManager::createCombinedLineSurface(const std::string& line, SDL_Color default_fg) {
        auto [segment_surfaces, total_width] = generateSegmentSurfaces(line, default_fg);
        return combineSegmentSurfaces(segment_surfaces, total_width);
    }
    std::pair<std::vector<SDL_Surface*>, int> ScreenManager::generateSegmentSurfaces(const std::string& line, SDL_Color default_fg) {
        if (line.empty()) {
            return {{}, 0};
        }

        // === 1. Split into color segments by '\a' ===
        std::vector<std::string> segments;
        segments.reserve(8);
        for (const auto& part : std::ranges::views::split(line, '\a')) {
            segments.emplace_back(part.begin(), part.end());
        }

        // === 2. Render each segment ===
        std::vector<SDL_Surface*> segment_surfaces;
        int total_width = 0;
        int max_height = 0;
        for (const auto& seg : segments) {
            if (seg.empty()) continue;

            SDL_Color fg = default_fg;
            std::string text_to_render = seg;

            // === 3. Try to parse #RRGGBB color ===
            if (seg[0] == '#' && seg.size() >= 7) {
                bool valid_color = true;
                for (int i = 1; i <= 6; ++i) {
                    if (!std::isxdigit(static_cast<unsigned char>(seg[i]))) {
                        valid_color = false;
                        break;
                    }
                }
                if (valid_color) {
                    fg.r = static_cast<Uint8>(HexCharToInt(seg[1]) * 16 + HexCharToInt(seg[2]));
                    fg.g = static_cast<Uint8>(HexCharToInt(seg[3]) * 16 + HexCharToInt(seg[4]));
                    fg.b = static_cast<Uint8>(HexCharToInt(seg[5]) * 16 + HexCharToInt(seg[6]));
                    fg.a = 255;
                    text_to_render = seg.substr(7);
                    if (text_to_render.empty()) continue;
                }
            }

            // === 4. Render segment text ===
            SDL_Surface* surf = TTFMANAGER.RenderBlendedText(text_to_render, fg);
            if (!surf) {
                LOGGERMANAGER.getLogger()->warn("Failed to render segment: {}", text_to_render);
                continue;
            }

            segment_surfaces.push_back(surf);
            total_width += surf->w;
            if (surf->h > max_height) {
                max_height = surf->h;
                LOGGERMANAGER.getLogger()->debug("Segment height: {} (max_height: {})", surf->h, max_height);
            }
        }

        return {segment_surfaces, total_width};
    }

    SDL_Surface* ScreenManager::combineSegmentSurfaces(std::vector<SDL_Surface*>& segment_surfaces, int total_width) {
        if (segment_surfaces.empty()) {
            return nullptr;
        }
        int max_height = segment_surfaces[0]->h;
        // === Create combined surface ===
        SDL_Surface* combined = g_sdl2.ORIG_FUNC(SDL_CreateRGBSurfaceWithFormat)(
            0, total_width, max_height, 32, SDL_PIXELFORMAT_RGBA8888
        );
        if (!combined) {
            for (auto* surf : segment_surfaces) {
                g_sdl2.ORIG_FUNC(SDL_FreeSurface)(surf);
            }
            return nullptr;
        }

        // Transparent background
        Uint32 transparent = g_sdl2.ORIG_FUNC(SDL_MapRGBA)(combined->format, 0, 0, 0, 0);
        g_sdl2.ORIG_FUNC(SDL_FillRect)(combined, nullptr, transparent);

        // === Blit each segment (vertically centered) ===
        int x_offset = 0;
        for (SDL_Surface* src : segment_surfaces) {
            int y_offset = (max_height - src->h) / 2;
            SDL_Rect dst_rect{x_offset, y_offset, src->w, src->h};
            g_sdl2.ORIG_FUNC(SDL_UpperBlit)(src, nullptr, combined, &dst_rect);
            x_offset += src->w; // ← 直接使用 src->w
            g_sdl2.ORIG_FUNC(SDL_FreeSurface)(src); // Free after blit
        }

        return combined;
    }

    void ScreenManager::renderNormalCachedTextures(SDL_Renderer *renderer) {
        SDL_Rect renderRect;
        for (auto& pending_trans : normalTranslations) {
            if (pending_trans.target_text.empty()) {
                continue; // Skip if target text is empty
            }
            renderRect.x = pending_trans.start_x * dispx_z + origin_x;
            renderRect.y = pending_trans.start_y * dispy_z + origin_y;

            // Try: Reduce key length
            // std::string source_key = std::format("{}:{}:{}", pending_trans.source_text, pending_trans.start_x, pending_trans.start_y);
            std::string_view source_text = pending_trans.source_text;
            if (source_text.size() > kMaxKeyLength) source_text = source_text.substr(source_text.size() - kMaxKeyLength);
            std::string source_key = std::format("{}{}{}", source_text, pending_trans.start_x+pending_trans.start_y, pending_trans.col_span);

            if (auto multi_asset = textTextureCache.get(source_key)) {
                if (!multi_asset->lines.empty() && multi_asset->lines[0].texture) {
                    const auto& line_asset = multi_asset->lines[0];
                    renderRect.w = line_asset.w;
                    renderRect.h = line_asset.h;
                    if ((pending_trans.alignment == 'c')) {
                        renderRect.x += (((pending_trans.col_span * dispx_z) - renderRect.w) + 1) / 2;
                    }
                    g_sdl2.ORIG_FUNC(SDL_RenderCopy)(renderer, line_asset.texture.get(), nullptr, &renderRect);
                }
            }
        }
    }

    void ScreenManager::renderFreshCachedTextures(SDL_Renderer *renderer) {
        SDL_Rect renderRect;
        for (const auto& pending_trans : freshTranslations) {
            if (pending_trans.target_text.empty()) {
                continue; // Skip if target text is empty
            }
            renderRect.x = pending_trans.start_x * dispx_z + origin_x;
            renderRect.y = pending_trans.start_y * dispy_z + origin_y;

            // Try: Reduce key length
            // std::string source_key = std::format("{}:{}:{}", pending_trans.source_text, pending_trans.start_x, pending_trans.start_y);
            std::string_view source_text = pending_trans.source_text;
            if (source_text.size() > kMaxKeyLength) source_text = source_text.substr(source_text.size() - kMaxKeyLength);
            std::string source_key = std::format("{}{}{}", source_text, pending_trans.start_x+pending_trans.start_y, pending_trans.col_span);

            if (auto multi_asset = textTextureCache.get(source_key)) {
                if (!multi_asset->lines.empty() && multi_asset->lines[0].texture) {
                    const auto& line_asset = multi_asset->lines[0];
                    renderRect.w = line_asset.w;
                    renderRect.h = line_asset.h;
                    // Handle vertical and horizontal center alignment issues
                    if (pending_trans.alignment == 'd') {
                        renderRect.x += (((pending_trans.col_span * dispx_z) - renderRect.w) + 1) / 2;
                    } else if (pending_trans.alignment == 't') {
                        renderRect.x += (((pending_trans.col_span * dispx_z) - renderRect.w) + 1) / 2;
                        renderRect.y += static_cast<int>((dispy_z + 1) / 2);  // TODO: dispy_z * tabTextHeightShiftScale
                    }
                    g_sdl2.ORIG_FUNC(SDL_RenderCopy)(renderer, line_asset.texture.get(), nullptr, &renderRect);
                }
            }
        }
    }

    void ScreenManager::renderCustomCachedTextures(SDL_Renderer *renderer) {
        for (const auto& pending_trans : customTranslations) {
            if (pending_trans.target_text.empty()) {
                continue;
            }
            // === 3. Calculate base coordinates ===
            int start_x_px = pending_trans.start_x * dispx_z + origin_x;
            int start_y_px = pending_trans.start_y * dispy_z + origin_y;

            // Try: Reduce key length
            // std::string source_key = std::format("{}:{}:{}", pending_trans.source_text, pending_trans.start_x, pending_trans.start_y);
            std::string_view source_text = pending_trans.source_text;
            if (source_text.size() > kMaxKeyLength) source_text = source_text.substr(source_text.size() - kMaxKeyLength);
            std::string source_key = std::format("{}{}{}", source_text, pending_trans.start_x+pending_trans.start_y, pending_trans.col_span);

            if (auto multi_asset = textTextureCache.get(source_key)) {
                if (!multi_asset->lines.empty()) {
                    // // === 4. Vertical centering: based on actual line count vs row_span ===
                    // int actual_lines = static_cast<int>(multi_asset->lineCount());
                    // int reserved_height = pending_trans.row_span * dispy_z_adjusted;
                    // int used_height = actual_lines * dispy_z_adjusted;
                    // int y_offset = std::max(0, (reserved_height - used_height) / 2);
                    // int current_y = start_y_px + y_offset;
                    int current_y = start_y_px;

                    // === 5. Render each line ===
                    for (size_t i = 0; i < multi_asset->lineCount(); ++i) {
                        const auto& line = multi_asset->lines[i];
                        if (!line.texture) continue; // safety check

                        SDL_Rect renderRect{
                            start_x_px,          // x: left-align each line
                            current_y,           // y: current line y coordinate
                            line.w,              // w: from cache
                            line.h               // h: from cache
                        };

                        g_sdl2.ORIG_FUNC(SDL_RenderCopy)(renderer, line.texture.get(), nullptr, &renderRect);

                        current_y += line.h;
                    }
                }
            }
        }
    }

    void ScreenManager::printScreenBuffer(const std::vector<char>& screen_buffer) const {
        std::string output;
        output.reserve(dimx * dimy + dimy);
        for (int y = 0; y < dimy; y++) {
            const char* buffer = screen_buffer.data() + y * dimx;
            output.append(buffer, dimx);
            output.push_back('\n');
        }
        LOGGERMANAGER.getLogger()->debug("{}", output);
    }

    void ScreenManager::printVersionInfo() const {
        LOGGERMANAGER.getLogger()->info("Dwarf Fortess version: {}, DFHack version: {}", Version::df_version(), Version::dfhack_version());
    }

    void ScreenManager::printScreenInfo() const {
        LOGGERMANAGER.getLogger()->info("ScreenManager: dispx:{}, dispy:{}, dimx:{}, dimy:{}, dispx_z:{}, dispy_z:{}, origin_x:{}, origin_y:{}",
               dispx, dispy, dimx, dimy, dispx_z, dispy_z, origin_x, origin_y);
    }
}
}
}

// Backup code
    // // Render a single-line string (possibly containing \a#RRGGBB) into a merged SDL_Surface
    // // Returns {surface, total_width}; on failure returns {nullptr, 0}
    // SDL_Surface* ScreenManager::createCombinedLineSurface(const std::string& line, SDL_Color default_fg) {
    //     if (line.empty()) {
    //         return nullptr;
    //     }

    //     // === 1. Split into color segments by '\a' ===
    //     std::vector<std::string> segments;
    //     segments.reserve(8); // Pre-allocate to avoid frequent reallocations
    //     for (const auto& part : std::ranges::views::split(line, '\a')) {
    //         segments.emplace_back(part.begin(), part.end());
    //     }

    //     // === 2. Render each segment, collect surfaces and widths ===
    //     std::vector<SDL_Surface*> segment_surfaces;
    //     std::vector<int> segment_widths;
    //     int total_width = 0;
    //     int max_height = 0;

    //     for (const auto& seg : segments) {
    //         if (seg.empty()) {
    //             continue;
    //         }

    //         SDL_Color fg = default_fg;
    //         std::string text_to_render = seg;

    //         // === 3. Try to parse #RRGGBB color (7 chars: # + 6 hex) ===
    //         if (seg[0] == '#' && seg.size() >= 7) {
    //             bool valid_color = true;
    //             for (int i = 1; i <= 6; ++i) {
    //                 if (!std::isxdigit(static_cast<unsigned char>(seg[i]))) {
    //                     valid_color = false;
    //                     break;
    //                 }
    //             }
    //             if (valid_color) {
    //                 // Safely parse RGB
    //                 fg.r = static_cast<Uint8>(HexCharToInt(seg[1]) * 16 + HexCharToInt(seg[2]));
    //                 fg.g = static_cast<Uint8>(HexCharToInt(seg[3]) * 16 + HexCharToInt(seg[4]));
    //                 fg.b = static_cast<Uint8>(HexCharToInt(seg[5]) * 16 + HexCharToInt(seg[6]));
    //                 fg.a = 255; // Fully opaque
    //                 text_to_render = seg.substr(7);
    //                 if (text_to_render.empty()) {
    //                     continue; // Skip if color marker is followed by empty text
    //                 }
    //             }
    //         }

    //         // === 4. Render the segment text ===
    //         SDL_Surface* surf = TTFMANAGER.RenderBlendedText(text_to_render, fg);
    //         if (!surf) {
    //             LOGGERMANAGER.getLogger()->warn("Failed to render segment: {}", text_to_render);
    //             continue;
    //         }

    //         segment_surfaces.push_back(surf);
    //         segment_widths.push_back(surf->w);
    //         total_width += surf->w;
    //         if (surf->h > max_height) {
    //             max_height = surf->h;
    //             LOGGERMANAGER.getLogger()->debug("Segment height: {} (max_height: {})", surf->h, max_height);
    //         }
    //     }

    //     // === 5. Return failure if no valid segments ===
    //     if (segment_surfaces.empty()) {
    //         return nullptr;
    //     }

    //     // // === 6. Create target Surface (transparent background, RGBA8888) ===
    //     SDL_Surface* combined = g_sdl2.ORIG_FUNC(SDL_CreateRGBSurfaceWithFormat)(
    //         0, total_width, max_height, 32, SDL_PIXELFORMAT_RGBA8888
    //     );
    //     if (!combined) {
    //         // Clean up temporary surfaces if combined creation fails
    //         for (auto* surf : segment_surfaces) {
    //             g_sdl2.ORIG_FUNC(SDL_FreeSurface)(surf);
    //         }
    //         return nullptr;
    //     }

    //     // Fill transparent background
    //     Uint32 transparent = g_sdl2.ORIG_FUNC(SDL_MapRGBA)(combined->format, 0, 0, 0, 0);
    //     g_sdl2.ORIG_FUNC(SDL_FillRect)(combined, nullptr, transparent);

    //     // === 7. Blit each segment to combined surface (vertically centered) ===
    //     int x_offset = 0;
    //     for (size_t i = 0; i < segment_surfaces.size(); ++i) {
    //         SDL_Surface* src = segment_surfaces[i];
    //         int y_offset = (max_height - src->h) / 2; // Vertically center

    //         SDL_Rect dst_rect{x_offset, y_offset, src->w, src->h};
    //         g_sdl2.ORIG_FUNC(SDL_UpperBlit)(src, nullptr, combined, &dst_rect);

    //         x_offset += segment_widths[i];
    //         g_sdl2.ORIG_FUNC(SDL_FreeSurface)(src); // Free temporary surface after blit
    //     }

    //     return combined;
    // }