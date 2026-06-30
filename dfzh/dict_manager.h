#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <deque>
#include <filesystem>
#include <unordered_set>
#include <shared_mutex>

namespace DFHack {
namespace DFCH {
namespace Hooks {

    class DictManager {
    public:
        static DictManager& getInstance() {
            static DictManager instance;
            return instance;
        }

        bool init();
        void shutdown();
        bool loadDicts();
        void reloadDicts();
        void clearDicts();
        void screenChanged(std::string screen_name);

        void flushUntranslatedEntries();
        // Find Chinese translation based on text
        // Return value: whether translation was found
        // out_translation: output translated text
        // out_alignment: l: left, c: center
        bool tryTranslate(const std::string& text, std::string& out_translation, unsigned char& out_alignment);
        bool wordTranslate(const std::string& text, std::string& out_translation);

    private:
        DictManager();
        DictManager(const DictManager&) = delete;
        DictManager& operator=(const DictManager&) = delete;
        DictManager(DictManager&&) = delete;
        ~DictManager() = default;

        struct DictLoadResult {
            std::unordered_map<std::string, std::string> translation;
            std::unordered_map<std::string, unsigned char> alignment;
        };

        bool initialized = false;

        std::string current_screen_name;
        std::string current_screen_name_dev;

        // Dictionary data storage
        static constexpr size_t MAX_DICT_EXACT_SIZE = 32420;
        std::unordered_map<std::string, std::string> dict_exact; // Store text -> text_translation (default left-aligned)
        // std::unordered_map<std::string, std::string> dict_fuzzy; // Store text -> text_translation (default left-aligned)
        std::unordered_map<std::string, std::string> dict_word;
        std::unordered_map<std::string, unsigned char> dict_alignment; // Store dictionary alignment information (l: left, c: center)

        // ===== Untranslated text storage (FIFO + deduplication + capacity limit) =====
        static constexpr size_t MAX_UNTRANS_SIZE = 2000;        // Keep at most 2000 unique texts
        std::unordered_set<std::string> dict_untrans_set;       // Fast deduplication
        std::deque<std::string>         dict_untrans_queue;     // FIFO queue (maintain insertion order)

        // // Load dictionary file and parse text,text_translation format
        DictLoadResult loadDictFile(const std::filesystem::path& dictPath);
        bool findTransInExactDict(const std::string& text, std::string& out_translation, unsigned char& out_alignment) const;

        // Mutexes for thread safety
        mutable std::shared_mutex dict_mutex_;     // protects dict_exact, dict_fuzzy, dict_alignment
        mutable std::mutex untrans_mutex_;         // protects dict_untrans
    };

    #define DICTIONARY DFHack::DFCH::Hooks::DictManager::getInstance()

} // namespace Hooks
} // namespace DFCH
} // namespace DFHack
