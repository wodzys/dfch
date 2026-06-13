
#include "sentence_detector.h"
#include "logger.h"

#include <iostream>
#include <map>
#include <string_view>

namespace DFHack {
namespace DFCH {
namespace Hooks {

    void SentenceDetector::init() {
    }

    void SentenceDetector::shutdown() {
    }

    // Core logic for sentence detection
    // SentenceDetector::ProcessResult SentenceDetector::detectSentences(std::span<const char> screenData, const size_t rows, const size_t cols) {
    SentenceDetector::ProcessResult SentenceDetector::detectSentences(std::span<const char> screenData, const size_t rows, const size_t cols) const {
        if (rows == 0 || cols == 0 || screenData.empty()) return {};
        const size_t max_idx = rows * cols;
        if (screenData.size() < max_idx) return {};

        bool parsingWord = false;
        bool parsingSentence = false;
        ProcessResult detectedSentences;
        SentenceData currentSentence;
        WordData currentWord;
        std::map<size_t, SentenceData> newlyPending;            // 当前行新产生的未完成句子
        std::map<size_t, SentenceData> carryOverSentences;      // 上一行遗留的、待处理的句子
        currentSentence.content.reserve(200);
        currentSentence.words.reserve(30);
        currentWord.content.reserve(10);

        auto extractContent = [screenData, cols](size_t row, size_t sc, size_t ec) {
            size_t start = row * cols + sc;
            size_t len = ec - sc + 1;
            return std::string_view(screenData.data() + start, len);
        };

        LOGGERMANAGER.getLogger()->info("detectSentences start: rows: {}, cols: {}", rows, cols);
        for (size_t row = 0; row < rows; ++row) {
            char prevChar = ' ';
            // Track the smallest column sentence to process in current row, from previous row
            auto activeIt = carryOverSentences.begin(); // map 自动按 key (start_col) 升序

            for (size_t col = 0; col < cols; ++col) {
                size_t idx = row * cols + col;
                if (idx >= max_idx) break;
                const char c = screenData[idx];
                // Get the next character - optimized version: remove intermediate variable, perform conditional check directly in the ternary expression
                const char nextChar = (col + 1 < cols) ? (screenData[idx + 1]) : ' ';
                // LOGGERMANAGER.getLogger()->debug("detectSentences start: m_rows: {}, m_cols: {}, c: {}, nextChar: {}", row, col, c, nextChar);

                if (((c == ' ') && (prevChar == ' ')) || ((c != ' ') && (prevChar != ' '))) {
                    prevChar = c;
                    continue;
                }
                while (activeIt != carryOverSentences.end() && col > activeIt->second.start_col) {
                    auto& sentence = activeIt->second;
                    if (sentence.content.back() == ' ') {
                        sentence.content.pop_back();
                        sentence.end_col = sentence.end_col - 1;
                    }
                    auto [prefix, suffix] = splitSentenceIfInvalidStart(std::move(sentence));
                    if (!suffix.content.empty()) {
                        detectedSentences.sentences.push_back(std::move(suffix));
                    }
                    if (!prefix.content.empty()) {
                        detectedSentences.sentences.push_back(std::move(prefix));
                    }
                    // detectedSentences.sentences.push_back(std::move(sentence));
                    carryOverSentences.erase(activeIt);
                    activeIt = carryOverSentences.begin();
                }
                // Process the smallest column sentence
                if (activeIt != carryOverSentences.end() && col == activeIt->second.start_col) {
                    auto& sentence = activeIt->second;
                    // if ((c == ' ') || parsingSentence || !isWordChar(c) || isupper(c) || !isupper(sentence.content.front())) {
                    if ((c == ' ') || parsingSentence || (!g_isLowerOrDigit[static_cast<unsigned char>(c)] && (sentence.content.size() <= 50)) || 
                        !g_isUpper[static_cast<unsigned char>(sentence.content.front())]) {
                        // Sentence completed, remove trailing space and add to results
                        if (sentence.content.back() == ' ') {
                            sentence.content.pop_back();
                            sentence.end_col = sentence.end_col - 1;
                        }
                        auto [prefix, suffix] = splitSentenceIfInvalidStart(std::move(sentence));
                        if (!suffix.content.empty()) {
                            detectedSentences.sentences.push_back(std::move(suffix));
                        }
                        if (!prefix.content.empty()) {
                            detectedSentences.sentences.push_back(std::move(prefix));
                        }
                        // detectedSentences.sentences.push_back(std::move(sentence));
                    } else {
                        parsingSentence = true;
                        currentSentence = std::move(sentence);
                    }
                    carryOverSentences.erase(activeIt);
                    activeIt = carryOverSentences.begin();
                }

                if ((c != ' ') && (prevChar == ' ')) {
                    parsingWord = true;
                    currentWord.start_row = row;
                    currentWord.start_col = col;
                    if ((currentSentence.end_col_t != 0) && ((currentSentence.end_col + 1) < col)) {
                        parsingSentence = false;
                        if (g_isSentenceEnd[static_cast<unsigned char>(currentSentence.prevChar_t)]) {
                            size_t end_col = currentSentence.end_col_t - 1;
                            currentSentence.end_row = row;
                            // currentSentence.end_col = (end_col > currentSentence.end_col) ? end_col : currentSentence.end_col;
                            currentSentence.content.append(extractContent(row, currentSentence.start_col, end_col));
                            detectedSentences.sentences.push_back(std::move(currentSentence));
                        } else {
                            size_t end_col = currentSentence.end_col_t;
                            currentSentence.end_row = row;
                            // currentSentence.end_col = (end_col > currentSentence.end_col) ? end_col : currentSentence.end_col;
                            currentSentence.content.append(extractContent(row, currentSentence.start_col, end_col));
                            currentSentence.resetTemp();
                            newlyPending.emplace(currentSentence.start_col, std::move(currentSentence));
                        }
                        currentSentence.reuse();
                    }
                    if (!parsingSentence) {
                        parsingSentence = true;
                        currentSentence.start_row = row;
                        currentSentence.start_col = col;
                    }
                } else {    // Branch: (c == ' ') && (prevChar != ' ')
                    parsingWord = false;
                    if (g_isWordBoundaryPunct[static_cast<unsigned char>(prevChar)]) {
                        currentWord.end_row = row;
                        currentWord.end_col = col - 2;
                    } else {
                        currentWord.end_row = row;
                        currentWord.end_col = col - 1;
                    }
                    currentWord.content.assign(extractContent(row, currentWord.start_col, currentWord.end_col));
                    currentSentence.words.push_back(std::move(currentWord));
                    currentWord.reuse();
                    // if (nextChar == ' ' || prevChar == ':') {        // 实验性功能：空格或冒号后接空格，句子结束
                    if (nextChar == ' ') {
                        if (currentSentence.end_col <= col) {
                            parsingSentence = false;
                            currentSentence.resetTemp();
                            if (g_isSentenceEnd[static_cast<unsigned char>(prevChar)]) {
                                size_t end_col = col - 1;
                                currentSentence.end_row = row;
                                currentSentence.end_col = (end_col > currentSentence.end_col) ? end_col : currentSentence.end_col;
                                currentSentence.content.append(extractContent(row, currentSentence.start_col, end_col));
                                detectedSentences.sentences.push_back(std::move(currentSentence));
                            } else {
                                size_t end_col = col;
                                currentSentence.end_row = row;
                                currentSentence.end_col = (end_col > currentSentence.end_col) ? end_col : currentSentence.end_col;
                                currentSentence.content.append(extractContent(row, currentSentence.start_col, end_col));
                                newlyPending.emplace(currentSentence.start_col, std::move(currentSentence));
                            }
                            currentSentence.reuse();
                        } else {
                            currentSentence.end_col_t = col;
                            currentSentence.prevChar_t = prevChar;
                        }
                    }
                }

                prevChar = c;       // Update previous character
            }

            while (activeIt != carryOverSentences.end()) {
                auto& sentence = activeIt->second;
                if (sentence.content.back() == ' ') {
                    sentence.content.pop_back();
                    sentence.end_col = sentence.end_col - 1;
                }
                auto [prefix, suffix] = splitSentenceIfInvalidStart(std::move(sentence));
                if (!suffix.content.empty()) {
                    detectedSentences.sentences.push_back(std::move(suffix));
                }
                if (!prefix.content.empty()) {
                    detectedSentences.sentences.push_back(std::move(prefix));
                }
                // detectedSentences.sentences.push_back(std::move(sentence));
                carryOverSentences.erase(activeIt);
                activeIt = carryOverSentences.begin();
            }

            // End of line processing
            if (parsingSentence) {
                if (parsingWord) {
                    parsingWord = false;
                    currentWord.end_row = row;
                    currentWord.end_col = cols - 1;
                    currentWord.content.assign(extractContent(row, currentWord.start_col, currentWord.end_col));
                    currentSentence.words.push_back(std::move(currentWord));
                    currentWord.reuse();
                }
                parsingSentence = false;
                if (currentSentence.end_col_t == 0) {
                    size_t end_col = cols - 1;
                    currentSentence.end_row = row;
                    currentSentence.end_col = (end_col > currentSentence.end_col) ? end_col : currentSentence.end_col;
                    currentSentence.content.append(extractContent(row, currentSentence.start_col, end_col));
                    newlyPending.emplace(currentSentence.start_col, std::move(currentSentence));
                } else {
                    if (g_isSentenceEnd[static_cast<unsigned char>(currentSentence.prevChar_t)]) {
                        size_t end_col = currentSentence.end_col_t - 1;
                        currentSentence.end_row = row;
                        // currentSentence.end_col = (end_col > currentSentence.end_col) ? end_col : currentSentence.end_col;
                        currentSentence.content.append(extractContent(row, currentSentence.start_col, end_col));
                        detectedSentences.sentences.push_back(std::move(currentSentence));
                    } else {
                        size_t end_col = currentSentence.end_col_t;
                        currentSentence.end_row = row;
                        // currentSentence.end_col = (end_col > currentSentence.end_col) ? end_col : currentSentence.end_col;
                        currentSentence.content.append(extractContent(row, currentSentence.start_col, end_col));
                        currentSentence.resetTemp();
                        newlyPending.emplace(currentSentence.start_col, std::move(currentSentence));
                    }
                }
                currentSentence.reuse();
            }

            // Prepare for next line
            carryOverSentences.clear();
            newlyPending.swap(carryOverSentences);
        }
        // Process unfinished sentences at end of line
        auto activeIt = carryOverSentences.begin();

        while (activeIt != carryOverSentences.end()) {
            auto& sentence = activeIt->second;
            if (sentence.content.back() == ' ') {
                sentence.content.pop_back();
                sentence.end_col = sentence.end_col - 1;
            }
            auto [prefix, suffix] = splitSentenceIfInvalidStart(std::move(sentence));
            if (!suffix.content.empty()) {
                detectedSentences.sentences.push_back(std::move(suffix));
            }
            if (!prefix.content.empty()) {
                detectedSentences.sentences.push_back(std::move(prefix));
            }
            // detectedSentences.sentences.push_back(std::move(sentence));
            carryOverSentences.erase(activeIt);
            activeIt = carryOverSentences.begin();
        }

        return detectedSentences;
    }

    std::pair<SentenceDetector::SentenceData, SentenceDetector::SentenceData> SentenceDetector::splitSentenceIfInvalidStart(SentenceData&& sent) const {
        SentenceData prefix;  // default-constructed as empty
        SentenceData suffix;  // default-constructed as empty
        // Only process single-line sentences
        if ((sent.start_row != sent.end_row) || sent.content.empty()) {
            suffix = std::move(sent);
            return {std::move(prefix), std::move(suffix)};
        }
        // // Exclude sentences that are entirely numeric
        // if(std::all_of(sent.content.begin(), sent.content.end(), ::isdigit)) {
        //     return {std::move(prefix), std::move(suffix)};
        // }
        // Exclude sentences that contain no alphabetic characters (using precomputed lookup table)
        if (std::none_of(sent.content.begin(), sent.content.end(), [](unsigned char c) {return g_isAlpha[c];})) {
            return {std::move(prefix), std::move(suffix)};
        }

        // if (!g_notSentenceStart[static_cast<unsigned char>(sent.content[0])]) {
        //     // No need to split
        //     suffix = std::move(sent);
        //     return {std::move(prefix), std::move(suffix)};
        // }

        // 2. Find the first space in the entire content (starting from index 0)
        // size_t firstSpacePos = sent.content.find(' ');
        size_t colonSpacePos = sent.content.find(": ");
        if (colonSpacePos == std::string::npos) {
            suffix = std::move(sent);
            return {std::move(prefix), std::move(suffix)};
        }

        // Check if there is *any alphabetic character* in the part after ": "
        size_t afterColonSpace = colonSpacePos + 2;
        if (afterColonSpace >= sent.content.size()) {
            // Nothing after ": " → don't split
            suffix = std::move(sent);
            return {std::move(prefix), std::move(suffix)};
        }

        // Exclude text that does not respond to mouse hover events
        std::string prefix_str(sent.content.data(), colonSpacePos + 2);
        if (!prefixTextHoverable.count(prefix_str)) {
            suffix = std::move(sent);
            return {std::move(prefix), std::move(suffix)};
        }

        // Check if any character from afterColonSpace onward is alphabetic
        if (!std::any_of(sent.content.begin() + afterColonSpace, sent.content.end(), [](unsigned char c) { return g_isAlpha[c]; })) {
            // No letter after ": " → don't split
            suffix = std::move(sent);
            return {std::move(prefix), std::move(suffix)};
        }

        // 3. Construct the prefix up to the first space
        prefix.start_row = sent.start_row;
        prefix.end_row = sent.start_row;
        prefix.start_col = sent.start_col;
        prefix.end_col = sent.start_col + static_cast<int>(colonSpacePos+1);
        // prefix.content.assign(sent.content.data(), colonSpacePos + 2);
        prefix.content = std::move(prefix_str);

        // 4. Construct the suffix starting from the character after the first space
        // size_t suffixStartIdx = colonSpacePos + 1;
        // if (suffixStartIdx < sent.content.size()) {
            suffix.start_row = sent.start_row;
            suffix.end_row = sent.start_row;
            suffix.start_col = sent.start_col + static_cast<int>(afterColonSpace);
            suffix.end_col = sent.start_col + static_cast<int>(sent.content.size() - 1);
            suffix.content.assign(sent.content.data() + afterColonSpace, sent.content.size() - afterColonSpace);
        // } // Otherwise, suffix remains default-constructed as empty

        // 5. Assign words: the first goes to prefix, the rest go to suffix
        if (!sent.words.empty()) {
            // prefix gets the first word
            prefix.words.reserve(1);
            prefix.words.push_back(std::move(sent.words[0]));

            // suffix gets the rest of the words (if any)
            if (sent.words.size() > 1) {
                suffix.words.reserve(sent.words.size() - 1);
                for (size_t i = 1; i < sent.words.size(); ++i) {
                    suffix.words.push_back(std::move(sent.words[i]));
                }
            }
        }

        return {std::move(prefix), std::move(suffix)};
    }

    // TODO[11/06]: Implement character-level detection logic for loop unrolling in detectSentences
    // void SentenceDetector::detectChar(const char c, const char nextChar, const char prevChar, const int row, const int col) {
    // }
}
}
}
