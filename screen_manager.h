// screen_manager.h
#pragma once

#include "hook_common.h"
#include "sdl2_hooks.h"

#include <SDL.h>

#include <span>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#include <array>
// #include <optional>
#include <unordered_map>
#include <list>
#include <utility>
#include <functional> // for std::hash (optional, for custom hash if needed)

namespace DFHack {
namespace DFCH {
namespace Hooks {
    // Safely compute proportional width: w_new = (w_old * h_new + h_old/2) / h_old
    // Scaling calculation that preserves aspect ratio
    #define SAFE_SCALE_WIDTH(w_old, h_old, h_new) \
        ((h_old) > 0 ? ((static_cast<int64_t>(w_old) * (h_new) + (h_old) / 2) / (h_old)) : (w_old))

    struct WordData;
    class ScreenManager {
    public:
        static ScreenManager& getInstance() {
            static ScreenManager instance;
            return instance;
        }

        void init();
        void shutdown();
        void screenChanged(std::string screen_name);
        void doCommand(const std::vector<std::string>& commands);
        void updatePerformanceStatistics(std::int64_t proc_us, std::int64_t frame_us);
        // void preDFhooksPreRender();
        void onSDLGetWindowSize();
        bool preSDLRenderPresent(SDL_Renderer * renderer);
    private:
        ScreenManager();
        ScreenManager(const ScreenManager&) = delete;
        ScreenManager& operator=(const ScreenManager&) = delete;
        ScreenManager(ScreenManager&&) = delete;
        ~ScreenManager();

        struct SDLTextureDeleter {
            void operator()(SDL_Texture* t) const noexcept {
                if (t) g_sdl2.ORIG_FUNC(SDL_DestroyTexture)(t);
            }
        };
        using SDLTexturePtr = std::unique_ptr<SDL_Texture, SDLTextureDeleter>;

        // LRUCache: Least Recently Used Cache
        struct TextureAsset {
            int w,h;
            SDLTexturePtr texture;

            TextureAsset(int width, int height, SDLTexturePtr tex)
                : w(width), h(height), texture(std::move(tex)) {}

            TextureAsset(TextureAsset&&) = default;
            TextureAsset& operator=(TextureAsset&&) = default;
            TextureAsset(const TextureAsset&) = delete;
            TextureAsset& operator=(const TextureAsset&) = delete;
        };
        // TODO: std::variant<TextureAsset, MultiLineTextureAsset>
        struct MultiLineTextureAsset {
            std::vector<TextureAsset> lines; // 每行一个 TextureAsset
            SDL_Color default_fg;
            MultiLineTextureAsset() = default;
            // 支持移动语义（LRUCache 需要）
            MultiLineTextureAsset(MultiLineTextureAsset&&) = default;
            MultiLineTextureAsset& operator=(MultiLineTextureAsset&&) = default;
            MultiLineTextureAsset(const MultiLineTextureAsset&) = delete;
            MultiLineTextureAsset& operator=(const MultiLineTextureAsset&) = delete;

            bool empty() const noexcept { return lines.empty(); }
            size_t lineCount() const noexcept { return lines.size(); }
            // Backup code:
            // const TextureAsset* getLine(size_t i) const noexcept {
            //     return (i < lines.size()) ? &lines[i] : nullptr;
            // }
        };
        template <typename Key, typename Value,
            typename Hash = std::hash<Key>,
            typename KeyEqual = std::equal_to<Key>
        >
        class LRUCache {
        private:
            using KeyValuePair = std::pair<Key, Value>;
            using ListIterator = typename std::list<KeyValuePair>::iterator;

            template<typename K>
            void _put_impl(K&& key, Value value) {
                auto it = cache_map_.find(key);
                if (it != cache_map_.end()) {
                    // key already exists: update value and move to tail
                    it->second->second = std::move(value);
                    cache_list_.splice(cache_list_.end(), cache_list_, it->second);
                } else {
                    // need to insert new key-value
                    if (cache_list_.size() >= capacity_) {
                        // evict the oldest
                        auto& oldest = cache_list_.front();
                        cache_map_.erase(oldest.first);
                        cache_list_.pop_front();
                    }
                    // emplace_back supports perfect forwarding
                    cache_list_.emplace_back(std::forward<K>(key), std::move(value));
                    cache_map_[cache_list_.back().first] = std::prev(cache_list_.end());
                }
            }

            size_t capacity_;
            std::list<KeyValuePair> cache_list_; // 双向链表：尾部为最近使用
            std::unordered_map<Key, ListIterator, Hash, KeyEqual> cache_map_;
        public:
            // 构造函数：指定缓存容量
            explicit LRUCache(size_t capacity = 100) : capacity_(capacity) {
                if (capacity_ == 0) capacity_ = 100;
                cache_map_.reserve(capacity_);
            }

            Value* get(const Key& key) {
                auto it = cache_map_.find(key);
                if (it == cache_map_.end()) {
                    return nullptr;
                }

                // 将访问的节点移到链表尾部（标记为最近使用）
                cache_list_.splice(cache_list_.end(), cache_list_, it->second);
                return &(it->second->second);
            }

            // 1. Accepts lvalue reference (copies key)
            void put(const Key& key, Value value) {
                _put_impl(key, std::move(value));
            }

            // 2. Accepts rvalue reference (move key)
            void put(Key&& key, Value value) {
                _put_impl(std::move(key), std::move(value));
            }

            // Get current cache size (for debugging)
            size_t size() const {
                return cache_list_.size();
            }

            // Whether cache is empty
            bool empty() const {
                return cache_list_.empty();
            }

            void clear() {
                cache_list_.clear();
                cache_map_.clear();
            }
        };

        struct TextTranslationData {
            int start_x;        // (tiles)
            int start_y;        // (tiles)
            int row_span;       // (tiles)
            int col_span;       // (tiles)
            std::string source_text;
            std::string target_text;
            unsigned char alignment;     // alignment (l: left-aligned, c: centered)
            TextTranslationData(int start_x, int start_y, int row_span, int col_span, const std::string& source_text, const std::string& target_text, unsigned char align = 'l')
                : start_x(start_x), start_y(start_y), row_span(row_span), col_span(col_span), source_text(source_text), target_text(target_text), alignment(align) {}
            TextTranslationData(int sx, int sy, int rs, int cs, std::string&& source, std::string&& target, unsigned char align = 'l')
                : start_x(sx), start_y(sy), row_span(rs), col_span(cs), source_text(std::move(source)), target_text(std::move(target)), alignment(align) {}

            TextTranslationData(TextTranslationData&&) = default;
            TextTranslationData& operator=(TextTranslationData&&) = default;
        };

        // struct TextTranslationDataFresh : public TextTranslationData {
        //     SDL_Color fg_color = {0, 0, 0, 255};
        //     using TextTranslationData::TextTranslationData;
        // };

        struct ColorSpan {
            // [start, end)
            size_t start_idx;
            size_t end_idx;
            std::string color_hex;
        };
        struct TextTranslationDataSpecial : public TextTranslationData {
            using TextTranslationData::TextTranslationData;
            std::string default_fg;
            std::vector<ColorSpan> color_spans;
            std::vector<size_t> break_points;
            TextTranslationDataSpecial(
                int start_x, int start_y, int row_span, int col_span,
                std::string source_text, std::string target_text,
                unsigned char align,
                std::string default_fg,
                std::vector<ColorSpan> color_spans = {},
                std::vector<size_t> break_points = {}
            ) : TextTranslationData(start_x, start_y, row_span, col_span,
                                    std::move(source_text), std::move(target_text), align),
                default_fg(std::move(default_fg)),
                color_spans(std::move(color_spans)),
                break_points(std::move(break_points))
            {}
        };

        size_t tile_size = 0;
        int dispx = 0, dispy = 0, dimx = 0, dimy = 0;
        // We may shrink or enlarge dispx/dispy in response to zoom requests. dispx/y_z are the size we actually display tiles at.
        int dispx_z = 0, dispy_z = 0;
        // Viewport origin
        int origin_x = 0, origin_y = 0;
        bool top_in_use = false;

        const float textHeightScale = 1.2f;             // Height scaling factor for single-line text
        const float tabTextHeightShiftScale = 0.5f;     // Height shift scaling factor for tabbed text
        static constexpr std::size_t kMaxKeyLength = 32;
        int dispy_z_adjusted = 0;

        bool initialized = false;
        bool screen_state_changed_flag = false;
        bool screen_size_changed_flag = false;
        bool screen_contents_changed_flag = false;
        bool screen_buffers_swapped_flag = false;
        bool screen_show_trans_flag = false;
        bool dict_reloaded_flag = false;

        std::string current_screen_name;
        std::string current_screen_name_dev;
        char displayed_version[7] = "v50.00";
        std::int64_t localization_time_percent = 0;
        float smoothed_value = 0.0f;
        static constexpr float FRAME_SMOOTH_ALPHA = 0.1f;   // 0.1 ~ 0.3：较平滑
        // Precompute valid character lookup table for faster matching
        // // std::array<bool, 256> valid_chars;
        // static constexpr std::array<bool, 256> valid_chars = []() consteval {
        //     std::array<bool, 256> lut = {};
        //     for (int i = 'a'; i <= 'z'; ++i) lut[i] = true;
        //     for (int i = 'A'; i <= 'Z'; ++i) lut[i] = true;
        //     for (int i = '0'; i <= '9'; ++i) lut[i] = true;
        //     const unsigned char specials[] = {',', '.', '+', '-', '=', ':', '<', '>', 
        //                             '(', ')', '/', '%', '?', '\'', '!', '"', '`', '[',
        //                             ']', '&'};
        //     for (unsigned char c : specials) {
        //         lut[c] = true;
        //     }
        //     return lut;
        // }();

        static constexpr std::array<unsigned char, 256> cp437_to_simple_ascii = []() consteval {
            std::array<unsigned char, 256> lut = {};

            // 1. 默认：所有字节先映射为空格
            for (int i = 0; i < 256; ++i) {
                lut[i] = static_cast<unsigned char>(' ');
            }

            // 2. 保留原始合法 ASCII 字符（a-z, A-Z, 0-9, 特殊符号）
            auto keep = [&](int c) {
                lut[static_cast<unsigned char>(c)] = static_cast<unsigned char>(c);
            };

            for (int i = 'a'; i <= 'z'; ++i) keep(i);
            for (int i = 'A'; i <= 'Z'; ++i) keep(i);
            for (int i = '0'; i <= '9'; ++i) keep(i);

            const unsigned char specials[] = {',', '.', '+', '-', '=', ':', '<', '>', 
                                    '(', ')', '/', '%', '?', '\'', '!', '"', '`', '[',
                                    ']', '&', '#', '*'};
            for (unsigned char c : specials) {
                keep(c);
            }

            // 3. 添加 CP437 重音字符 → 简化 ASCII 的映射（根据你原始函数）
            auto map_char = [&](int cp437_code, char simple) {
                lut[static_cast<unsigned char>(cp437_code)] = static_cast<unsigned char>(simple);
            };

            // u / U
            map_char(129, 'u');  // ü
            map_char(150, 'u');  // û
            map_char(151, 'u');  // ù
            map_char(163, 'u');  // ú

            map_char(154, 'U');  // Ü

            // y / n / N
            map_char(152, 'y');  // ÿ
            map_char(164, 'n');  // ñ
            map_char(165, 'N');  // Ñ

            // a
            map_char(131, 'a');  // â
            map_char(132, 'a');  // ä
            map_char(133, 'a');  // à
            map_char(134, 'a');  // å
            map_char(145, 'a');  // æ — 小写合字 ae
            map_char(160, 'a');  // á
            
            // A
            map_char(142, 'A');  // Å
            map_char(143, 'A');  // Å
            map_char(146, 'A');  // Æ — 大写合字 AE

            // e
            map_char(130, 'e');  // é
            map_char(136, 'e');  // ê
            map_char(137, 'e');  // ë
            map_char(138, 'e');  // è

            // E
            map_char(144, 'E');  // É

            // i
            map_char(139, 'i');  // ï
            map_char(140, 'i');  // î
            map_char(141, 'i');  // ì
            map_char(161, 'i');  // í

            // o
            map_char(147, 'o');  // ô
            map_char(148, 'o');  // ö
            map_char(149, 'o');  // ò
            map_char(162, 'o');  // ó

            // O
            map_char(153, 'O');  // Ö

            // C / c
            map_char(128, 'C');  // Ç
            map_char(135, 'c');  // ç

            map_char(240, '=');  // ≡
            map_char(225, 'g');  // Γ

            return lut;
        }();

        unsigned char * screen_tmp = nullptr;
        unsigned char * screen_top_tmp = nullptr;
        long * screentexpos_tmp = nullptr;
        long * screentexpos_top_tmp = nullptr;
        // long * screentexpos_lower_tmp = nullptr;
        // long * screentexpos_top_lower_tmp = nullptr;

        std::vector<unsigned char> screen_bak;
        std::vector<unsigned char> screen_top_bak;
        std::vector<long> screentexpos_bak;
        std::vector<long> screentexpos_top_bak;
        std::vector<long> screentexpos_lower_bak;        // debug purpose
        std::vector<long> screentexpos_top_lower_bak;
        std::vector<uint32_t> screentexpos_flag_bak;
        std::vector<uint32_t> screentexpos_top_flag_bak;

        SDL_Renderer * df_sdl_renderer = nullptr;
        SDL_Window * df_sdl_window = nullptr;

        std::vector<char> screen_char_buffer;
        std::vector<char> screen_char_buffer_old;

        std::vector<TextTranslationData> normalTranslations;
        std::vector<TextTranslationData> freshTranslations;
        std::vector<TextTranslationDataSpecial> customTranslations;     // 用于多行文本(\n)和带有颜色信息的文本(\e)

        LRUCache<std::string, MultiLineTextureAsset> textTextureCache{500};

        // Process SentenceDetector results and match translations
        void processTranslations();
        // bool processWordsColor(const std::vector<WordData>& words, std::string& sentence_trans);
        std::pair<std::string, std::vector<ColorSpan>> processWordsColor(const std::vector<WordData>& words, const std::string& sentence_trans);
        std::vector<size_t> computeLineBreaks(const std::string& sentence_trans, const std::vector<ColorSpan>& protectedRanges, size_t col_span);

        // Reset text render area to space characters
        void clearTranslationAreas();
        // Render translation textures to the screen
        void renderTextures(SDL_Renderer *renderer);
        void renderNormalCachedTextures(SDL_Renderer *renderer);
        void renderFreshCachedTextures(SDL_Renderer *renderer);
        void renderCustomCachedTextures(SDL_Renderer *renderer);

        bool checkDFPointer();
        bool updateScreenInfo();
        // Extract valid characters from screen array
        std::vector<unsigned char> filterScreen();
        // Blocked-transpose version: reads column-major input, writes row-major to out_buffer.
        // out_buffer must be at least tile_size bytes. BLOCK_ROWS/BLOCK_COLS are sized to keep
        // each tile block within L1 cache.
        void filterScreenUpdate(std::span<char> out_buffer);
        void swapScreen();
        void restoreScreen();
        bool detectDisplayedVersion();
        void addCHFlag();
        void addPerformanceInfo();
        void showCHFlag(SDL_Renderer * sdl_render);
        void addChar2Screen(const unsigned char c, const int x, const int y, const SDL_Color* fg_color = nullptr, const SDL_Color* bg_color = nullptr);
        // Create SDL_Texture based on translation results
        void createNormalTextures();
        void createFreshTextures();
        void createCustomTextures();
        std::pair<bool, std::string> isSingleColorAndGetMostFrequent(const std::vector<std::string>& colors);
        SDL_Color getForegroundColor(const int x, const int y) const;
        uint32_t getTextFlag(const int x, const int y) const;
        // bool isConfirmButtonText(const int x, const int y) const;

        const std::vector<unsigned char>& getActiveBuffer(int tile_col_major) const {
            return (top_in_use && (screentexpos_top_lower_bak[tile_col_major] != 0)) ? screen_top_bak : screen_bak;
        }
        std::vector<unsigned char>& getActiveBuffer(int tile_col_major) {
            return (top_in_use && (screentexpos_top_lower_bak[tile_col_major] != 0)) ? screen_top_bak : screen_bak;
        }

        SDL_Texture* createTextTexture(const std::string& text, SDL_Color color_fg, int& out_w_px, int& out_h_px);
        MultiLineTextureAsset generateMultiLineTextureAsset(const std::string& target_text, SDL_Color default_fg);
        MultiLineTextureAsset generateMultiLineTextureAsset(const std::string& target_text, SDL_Color default_fg,
        const std::vector<ColorSpan>& color_spans, const std::vector<size_t>& break_points);
        SDL_Surface* createCombinedLineSurface(const std::string& line, SDL_Color default_fg);
        std::pair<std::vector<SDL_Surface*>, int> generateSegmentSurfaces(const std::string& line, SDL_Color default_fg);
        SDL_Surface* combineSegmentSurfaces(std::vector<SDL_Surface*>& segment_surfaces, int total_width);

        void printScreenBuffer(const std::vector<char>& screen_buffer) const;
        void printScreenInfo() const;
        void printVersionInfo() const;

        inline bool isContinuationByte(unsigned char b) {
            return (b & 0xC0) == 0x80;
        }
        // inline static auto c2i = [](char c) -> int {
        //     return (c >= '0' && c <= '9') ? c - '0' : 
        //         (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 
        //         (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
        // };
        inline int HexCharToInt(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return 0;
        }
        inline SDL_Color parseColorHex(const std::string& hex) {
            SDL_Color c = {0, 0, 0, 255};
            if (hex.length() >= 6) {
                c.r = static_cast<Uint8>(HexCharToInt(hex[1]) * 16 + HexCharToInt(hex[2]));
                c.g = static_cast<Uint8>(HexCharToInt(hex[3]) * 16 + HexCharToInt(hex[4]));
                c.b = static_cast<Uint8>(HexCharToInt(hex[5]) * 16 + HexCharToInt(hex[6]));
            }
            return c;
        }
        static std::string colorToHex(const SDL_Color& c) {
            static const char hex[] = "0123456789ABCDEF";
            std::string s = "#FFFFFF";
            s[1] = hex[(c.r >> 4) & 0xF];
            s[2] = hex[c.r & 0xF];
            s[3] = hex[(c.g >> 4) & 0xF];
            s[4] = hex[c.g & 0xF];
            s[5] = hex[(c.b >> 4) & 0xF];
            s[6] = hex[c.b & 0xF];
            return s;
        }


        /**
         * Resets the screen area occupied by all elements in the text area container
         * 
         * Elements in the container must support the following member variables:
         * - start_x: Text starting column coordinate
         * - start_y: Text starting row coordinate
         * - row_span: Number of rows occupied by the text
         * - col_span: Number of columns occupied by the text
         */
        template<typename Container>
        void clearTextAreaContainer(const Container& container) {
            for (const auto& text_element : container) {
                for (int row = 0; row < text_element.row_span; ++row) {
                    int y = text_element.start_y + row;
                    for (int col = 0; col < text_element.col_span; ++col) {
                        int x = text_element.start_x + col;
                        addChar2Screen(' ', x, y);
                    }
                }
            }
        }

        bool areColorsEqual(const SDL_Color& color1, const SDL_Color& color2) const {
            return (color1.r == color2.r && 
                    color1.g == color2.g && 
                    color1.b == color2.b);
                    // color1->a == color2->a
        }

        // // Generic memory release function
        // template<typename T>
        // void releaseMemory(T*& ptr) {
        //     if (ptr) {
        //         delete[] ptr;
        //         ptr = nullptr;
        //     }
        // }

    };

    #define SCREENMANAGER DFHack::DFCH::Hooks::ScreenManager::getInstance()
} // namespace Hooks
} // namespace DFCH
} // namespace DFHack
