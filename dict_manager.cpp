
#include "dict_manager.h"
#include "config.h"
#include "logger.h"

#include <fstream>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <ranges>
#include <regex>
#include <string_view>

namespace DFHack {
namespace DFCH {
namespace Hooks {
    DictManager::DictManager() {
        // dict.reserve(30000);
        dict_exact.max_load_factor(0.75);
        dict_exact.reserve(MAX_DICT_EXACT_SIZE);   // number of lines in DICT_EXACT
        // dict_fuzzy.reserve(10);
        dict_word.max_load_factor(0.75);
        dict_word.reserve(1000);    // number of lines in DICT_WORD
        dict_alignment.max_load_factor(0.75);
        dict_alignment.reserve(1000);
    }

    bool DictManager::init() {
        // Dictionary files can be loaded from configuration during initialization
        if (!loadDicts()) {
            return false;
        }

        initialized = true;
        return true;
    }

    void DictManager::shutdown() {
        // Clean up resources
        // LOGGERMANAGER.getLogger()->info("DictManager shutdown");
        initialized = false;
    }

    void DictManager::screenChanged(std::string screen_name) {
        current_screen_name_dev = std::move(screen_name);

        if (initialized && current_screen_name_dev.find("screen") != std::string::npos) {
            current_screen_name = current_screen_name_dev;
            // Save untranslated text (internally locked and non-empty checked)
            flushUntranslatedEntries();
        }
    }

    bool DictManager::loadDicts() {
        auto result = loadDictFile(DFCH::Config::getDictFile("DICT_EXACT"));
        if (result.translation.empty()) {
            LOGGERMANAGER.getLogger()->warn("Dictionary file loaded but contains no valid entries. Old dictionary retained.");
            return false;
        }
        // Atomically update dictionaries under write lock
        {
            std::unique_lock<std::shared_mutex> lock(dict_mutex_);
            dict_exact = std::move(result.translation);
            dict_alignment = std::move(result.alignment);
            // dict_fuzzy remains unchanged (not loaded here)
        }
        result = loadDictFile(DFCH::Config::getDictFile("DICT_WORD"));
        if (!result.translation.empty()) {
            {
                std::unique_lock<std::shared_mutex> lock(dict_mutex_);
                dict_word = std::move(result.translation);
            }
        }
        return true;
    }

    void DictManager::reloadDicts() {
        if (!loadDicts()) {
            LOGGERMANAGER.getLogger()->error("DictManager::reloadDicts: loadDicts failed");
        }
    }

    void DictManager::clearDicts() {
        std::unique_lock<std::shared_mutex> lock(dict_mutex_);
        dict_exact.clear();
        dict_word.clear();
        // dict_fuzzy.clear();
        dict_alignment.clear();
    }

    DictManager::DictLoadResult DictManager::loadDictFile(const std::filesystem::path& dictPath) {
        DictLoadResult result;
        result.translation.max_load_factor(0.75);
        result.translation.reserve(MAX_DICT_EXACT_SIZE);
        result.alignment.reserve(100);

        std::ifstream dictFile(dictPath);
        if (!dictFile.is_open()) {
            LOGGERMANAGER.getLogger()->error("Failed to open dictionary file: {}", dictPath.string());
            return result;
        }

        std::string line;
        while (std::getline(dictFile, line)) {
            // Skip empty lines and lines not enclosed in quotes
            if ((line.length() < 5) || line.front() != '"' || line.back() != '"') {
                LOGGERMANAGER.getLogger()->warn("Invalid line format: {}", line);
                continue;
            }
            // Remove surrounding quotes
            std::string content = line.substr(1, line.size() - 2);
            std::vector<std::string_view> parts;
            size_t start = 0;
            size_t pos;

            // Split by "\",\" manually (avoid regex overhead and ranges split issues)
            while ((pos = content.find("\",\"", start)) != std::string::npos) {
                parts.emplace_back(content.data() + start, pos - start);
                start = pos + 3; // skip "\",\""
            }
            parts.emplace_back(content.data() + start, content.size() - start);

            if (parts.size() < 2 || parts[0].empty()) {
                continue;
            }

            std::string key(parts[0]);
            std::string value(parts[1]);

            // Handle alignment and escape sequences
            if (parts.size() >= 3 && !parts[2].empty()) {
                char alignment = parts[2][0];
                result.alignment.emplace(key, static_cast<unsigned char>(alignment));
                if (alignment == 's' && !value.empty()) {
                    // Replace escape sequences
                    size_t pos_nl;
                    while ((pos_nl = value.find("\\n")) != std::string::npos) {
                        value.replace(pos_nl, 2, "\n");
                    }
                    size_t pos_bel;
                    while ((pos_bel = value.find("\\a")) != std::string::npos) {
                        value.replace(pos_bel, 2, "\a");
                    }
                }
            }

            auto [it, inserted] = result.translation.emplace(std::move(key), std::move(value));
            if (!inserted) {
                LOGGERMANAGER.getLogger()->warn("Duplicate key in dictionary \"{}\": \"{}\"",
                    dictPath.string(), it->first);
            }
        }

        LOGGERMANAGER.getLogger()->info("Dictionary[\"{}\"] loaded: {} entries, {} alignment rules.",
           dictPath.string(), result.translation.size(), result.alignment.size());
        return result;
    }

    bool DictManager::tryTranslate(const std::string& text, std::string& out_translation, unsigned char& out_alignment) {
        // std::string text_tmp_storage;  // 仅在需要时分配
        // const std::string& text_ref = has_digit
        //     ? (text_tmp_storage = text, text_tmp_storage)  // 拷贝并后续归一化
        //     : text;
        std::vector<std::string> numbers;
        bool has_digit = std::any_of(text.begin(), text.end(), ::isdigit);
        if (has_digit) {
            std::string text_tmp = text;
            // static const std::regex pattern_digit(R"(\b\d+(?:/\d+)?\b)");
            // static const std::regex pattern_digit(R"(\b\d+\b)");
            static const std::regex pattern_digit(R"(\b(\d+)(?=%|\b))");
            numbers.reserve(5);
            for (auto i = std::sregex_iterator(text.begin(), text.end(), pattern_digit); i != std::sregex_iterator(); ++i) {
                numbers.push_back(i->str());
            }
            for (const auto& number : numbers) {
                size_t int_pos = text_tmp.find(number);
                if (int_pos != std::string::npos) {
                    text_tmp.replace(int_pos, number.length(), "0");
                }
            }
            // LOGGERMANAGER.getLogger()->debug("text_tmp: {}, numbers: {}", text_tmp, numbers[0]);
            if (findTransInExactDict(text_tmp, out_translation, out_alignment)) {
                for (const auto& number : numbers) {
                    size_t int_pos = out_translation.find("{d}");
                    if (int_pos != std::string::npos) {
                        out_translation.replace(int_pos, 3, number);
                    }
                }
                // LOGGERMANAGER.getLogger()->debug("out_translation: {}", out_translation);
                return true;
            }
        } else {
            if (findTransInExactDict(text, out_translation, out_alignment)) {
                return true;
            }
        }

        // Record untranslated text (with exclusive lock)
        {
            std::lock_guard<std::mutex> lock(untrans_mutex_);
            dict_untrans_set.insert(text);
            dict_untrans_queue.push_back(text);
            if (dict_untrans_queue.size() > MAX_UNTRANS_SIZE) {
                std::string oldest = std::move(dict_untrans_queue.front());
                dict_untrans_queue.pop_front();
                dict_untrans_set.erase(oldest);
            }
        }

        return false;
    }

    bool DictManager::wordTranslate(const std::string& text, std::string& out_translation) {
        std::shared_lock<std::shared_mutex> lock(dict_mutex_);
        auto dict_it = dict_word.find(text);
        if (dict_it != dict_word.end()) {
            out_translation = dict_it->second;
            return true;
        }
        return false;
    }

    bool DictManager::findTransInExactDict(const std::string& text, std::string& out_translation, unsigned char& out_alignment) const {
        std::shared_lock<std::shared_mutex> lock(dict_mutex_);
        out_alignment = 'l';
        auto dict_it = dict_exact.find(text);
        if (dict_it != dict_exact.end()) {
            out_translation = dict_it->second;
            auto align_it = dict_alignment.find(text);
            if (align_it != dict_alignment.end()) {
                out_alignment = align_it->second;
            }
            return true;
        }
        return false;
    }

    void DictManager::flushUntranslatedEntries() {
        // Extract untranslated text (avoid holding the lock during I/O)
        std::deque<std::string> untrans_to_save;
        {
            std::lock_guard<std::mutex> lock(untrans_mutex_);
            if (dict_untrans_queue.empty()) {
                return;
            }
            untrans_to_save = std::move(dict_untrans_queue); // Move the entire queue out
            dict_untrans_set.clear(); // Synchronously clear the set (keep both in sync)
        }

        // Use dedicated untrans logger
        auto untrans_logger = LOGGERMANAGER.getUntransLogger();
        if (!untrans_logger) {
            return;
        }
        untrans_logger->info("#[----------{}----------]", current_screen_name_dev);
        for (const auto& text : untrans_to_save) {
            if (std::all_of(text.begin(), text.end(), ::isdigit)) {
                continue;
            }
            if (text.find("FPS") != std::string::npos) {
                continue;
            }
            untrans_logger->info("{}", text);
        }
    }

} // namespace Hooks
} // namespace DFCH
} // namespace DFHack


// // Reference code for loading multiple dictionaries
// void DictManager::loadDicts() {
//     // Step 1: 加载所有字典文件到临时容器
//     std::unordered_map<std::string, std::string> temp_exact;
//     std::unordered_map<std::string, unsigned char> temp_alignment;

//     // 预分配空间（可选）
//     temp_exact.reserve(3000);
//     temp_alignment.reserve(500);

//     // Step 2: 按优先级顺序加载多个文件
//     // 后加载的会覆盖先加载的（适合“补丁”场景）
//     std::vector<std::string> dictKeys = {
//         "DICT_BASE",      // 基础字典（低优先级）
//         "DICT_EXACT",     // 精确匹配字典
//         "DICT_MENU",      // 菜单字典
//         "DICT_HELP"       // 帮助字典（高优先级）
//     };

// // 假设 Config 有一个方法返回字典文件列表（按优先级）
// auto dictFileKeys = DFCH::Config::getDictFileKeys(); // returns std::vector<std::string>

//     for (const auto& key : dictKeys) {
//         auto result = loadDictFile(DFCH::Config::getDictFile(key));
//         // 合并 exact
//         for (auto&& [k, v] : std::move(result.exact)) {
//             temp_exact.emplace(std::move(k), std::move(v));
//         }
//         // 合并 alignment（后加载的覆盖先加载的）
//         for (auto&& [k, a] : std::move(result.alignment)) {
//             temp_alignment.emplace(std::move(k), a);
//         }
//     }

//     // Step 3: 原子替换（加锁）
//     {
//         std::unique_lock<std::shared_mutex> lock(dict_mutex_);
//         dict_exact = std::move(temp_exact);
//         dict_alignment = std::move(temp_alignment);
//         // dict_fuzzy 保持不变（或按需清空/加载）
//     }

//     LOGGERMANAGER.getLogger()->info("All dictionaries loaded. Total entries: {}, alignment rules: {}",
//            dict_exact.size(), dict_alignment.size());
// }
