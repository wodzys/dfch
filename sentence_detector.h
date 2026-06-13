#pragma once

#include <vector>
#include <string>
#include <cctype>
#include <sstream>    // For string optimization
#include <algorithm>  // For sort function
#include <span>       // For std::span
#include <cstddef>
#include <array>
#include <unordered_set>

namespace DFHack {
namespace DFCH {
namespace Hooks {
    struct WordData {
        std::string content;
        size_t start_row = 0;
        size_t start_col = 0;
        size_t end_row = 0;
        size_t end_col = 0;
        WordData() = default;                               // Default constructor
        WordData(WordData&&) noexcept = default;            // Move constructor
        WordData& operator=(WordData&&) noexcept = default; // Move assignment operator
        WordData(const WordData&) = default;                // Copy constructor
        WordData& operator=(const WordData&) = default;     // Copy assignment operator
        void reuse() {
            content.clear();    // Clear content, retain memory
            start_row = 0;
            start_col = 0;
            end_row = 0;
            end_col = 0;
        }
    };
    class SentenceDetector {
    public:
        static SentenceDetector& getInstance() {
            static SentenceDetector instance;
            return instance;
        }
        // TODO[12/11]: update content to text
        struct SentenceData {
            std::string content;
            size_t start_row = 0;
            size_t start_col = 0;
            size_t end_row = 0;
            size_t end_col = 0;
            size_t end_col_t = 0;
            char prevChar_t = ' ';
            std::vector<WordData> words;
            SentenceData() = default;
            SentenceData(SentenceData&&) noexcept = default;            // Move constructor
            SentenceData& operator=(SentenceData&&) noexcept = default; // Move assignment operator
            SentenceData(const SentenceData&) = default;                // Copy constructor
            SentenceData& operator=(const SentenceData&) = default;     // Copy assignment operator
            void reuse() {
                content.clear();    // Clear content, retain memory
                words.clear();      // Clear word list
                start_row = 0;
                start_col = 0;
                end_row = 0;
                end_col = 0;
                resetTemp();
            }
            void resetTemp() {
                end_col_t = 0;
                prevChar_t = ' ';
            }
        };

        struct ProcessResult {
            std::vector<SentenceData> sentences;
            void reuse() {
                sentences.clear();          // Clear sentence list
            }
        };


        void init();
        void shutdown();
        ProcessResult detectSentences(std::span<const char> screenData, const size_t rows, const size_t cols) const;
        // ProcessResult detectSentences(std::span<const char> screenData, const size_t rows, const size_t cols);

    private:
        SentenceDetector() = default;
        SentenceDetector(const SentenceDetector&) = delete;
        SentenceDetector& operator=(const SentenceDetector&) = delete;
        SentenceDetector(SentenceDetector&&) = delete;
        ~SentenceDetector() = default;

        std::pair<SentenceData, SentenceData> splitSentenceIfInvalidStart(SentenceData&& sent) const;
        std::unordered_set<std::string> prefixTextHoverable = {"?: ", "/: ", "Enter: ", "Alt+s: ", "Ctrl+h: ", "Ctrl+d: ", "Ctrl+g: "};
        // // Utility function: Check if character is a sentence terminator
        // inline bool isSentenceTerminator(char ch) const { return ch == '.' || ch == '!' || ch == '?'; }
        // // Utility function: Check if character is a word character, a-z, A-Z, 0-9, -, '
        // // inline bool isWordChar(char ch) const { return isalnum(ch) || ch == '-' || ch == '\''; }
        // // Utility function: Check if character is a word character, a-z, A-Z
        // inline bool isWordChar(char ch) const { return isalpha(ch); }
        static constexpr std::array<bool, 256> g_isUpper = []() consteval {
            std::array<bool, 256> lut{};
            for (int i = 'A'; i <= 'Z'; ++i) lut[i] = true;
            return lut;
        }();
        static constexpr std::array<bool, 256> g_isSentenceEnd = []() consteval {
            std::array<bool, 256> lut{};
            lut['.'] = true;
            lut['!'] = true;
            lut['?'] = true;
            return lut;
        }();
        static constexpr std::array<bool, 256> g_isWordBoundaryPunct = []() consteval {
            std::array<bool, 256> lut{};
            lut['.'] = true;
            lut[','] = true;
            lut[';'] = true;
            lut[':'] = true;
            lut['!'] = true;
            lut['?'] = true;
            return lut;
        }();
        static constexpr std::array<bool, 256> g_isWordChar = []() consteval {
            std::array<bool, 256> lut{};
            for (int i = 'a'; i <= 'z'; ++i) lut[i] = true;
            for (int i = 'A'; i <= 'Z'; ++i) lut[i] = true;
            for (int i = '0'; i <= '9'; ++i) lut[i] = true;
            // lut['_'] = true;
            return lut;
        }();
        static constexpr std::array<bool, 256> g_isLowerOrDigit = []() consteval {
            std::array<bool, 256> lut{};
            for (int i = 'a'; i <= 'z'; ++i) lut[i] = true;
            for (int i = '0'; i <= '9'; ++i) lut[i] = true;
            return lut;
        }();
        static constexpr std::array<bool, 256> g_isAlpha = []() consteval {
            std::array<bool, 256> lut{};
            for (int i = 'a'; i <= 'z'; ++i) lut[i] = true;
            for (int i = 'A'; i <= 'Z'; ++i) lut[i] = true;
            return lut;
        }();
        static constexpr std::array<bool, 256> g_notSentenceStart = []() consteval {
            std::array<bool, 256> lut{};
            lut['?'] = true;
            lut['/'] = true;
            lut[':'] = true;
            return lut;
        }();
    };

    #define SENTENCEDETECTOR DFHack::DFCH::Hooks::SentenceDetector::getInstance()
} // namespace Hooks
} // namespace DFCH
} // namespace DFHack
