// SPDX-License-Identifier: MIT
// Algorithm & rule logic referenced from:
// https://github.com/DFI18n/dfi18n/tree/develop/crates/rule_based_translator
// Original License: MIT
// This implementation is independently rewritten in C++ with performance
// optimizations and C++ idiom improvements.
// Copyright (c) 2026 0x53an

#pragma once

#include <filesystem>
#include <list>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace DFHack {
namespace DFCH {
namespace Hooks {

    class RulesetsManager {
    public:
        static RulesetsManager& getInstance() {
            static RulesetsManager instance;
            return instance;
        }

        bool init();
        void shutdown();

        void print_cache_stats() const;

        bool load_rule_sets();
        void load_from_dir(const std::filesystem::path& dir) {
            std::optional<std::string> visited_root;
            parse_dir(dir, dir, visited_root);
            validate_references();
            analyze_from_root();
        }

        std::optional<std::string> translate(const std::string& text) const;

    private:
        // =====================================================================
        // 1. 构造
        // =====================================================================
        RulesetsManager();
        RulesetsManager(const RulesetsManager&) = delete;
        RulesetsManager& operator=(const RulesetsManager&) = delete;
        RulesetsManager(RulesetsManager&&) = delete;
        ~RulesetsManager() = default;

        // =====================================================================
        // 2. 嵌套类型（按依赖从底到顶）
        // =====================================================================

        enum class Type { Literal, Reference };

        struct Token {
            Type type;
            std::string value;
            auto operator<=>(const Token&) const = default;
        };

        using Tokens = std::vector<Token>;

        struct RuleSignature {
            std::string identifier;
            Tokens rule;
            auto operator<=>(const RuleSignature&) const = default;
        };

        struct ResultTree;
        using BindingMap = std::vector<std::pair<std::string, std::shared_ptr<const ResultTree>>>;

        struct ResultTree {
            std::string identifier;
            std::string matched;
            std::string translated;
            std::string remaining;
            BindingMap children;

            ResultTree(std::string id, std::string mat,
                    std::string trans, std::string rem,
                    BindingMap kids
            ) : identifier(std::move(id)), matched(std::move(mat)),
                translated(std::move(trans)), remaining(std::move(rem)), children(std::move(kids)) {}

            ResultTree(const ResultTree&) = default;
            ResultTree(ResultTree&&) = default;
            ResultTree& operator=(const ResultTree&) = delete;
            ResultTree& operator=(ResultTree&&) = delete;

            [[nodiscard]] size_t weight() const {
                size_t w = matched.empty() ? 0 : 1;
                for (const auto& [_, child] : children) w += child->weight();
                return w;
            }
        };

        struct TransparentHash {
            using is_transparent = void;
            size_t operator()(std::string_view sv) const noexcept {
                return std::hash<std::string_view>{}(sv);
            }
        };

        struct TransparentEqual {
            using is_transparent = void;
            bool operator()(std::string_view a, std::string_view b) const noexcept {
                return a == b;
            }
        };

        struct Candidate {
            BindingMap results;
            std::string remaining;

            Candidate(BindingMap res, std::string rem
            ) : results(std::move(res)), remaining(std::move(rem)) {}

            Candidate() = default;
            Candidate(const Candidate&) = default;
            Candidate(Candidate&&) = default;
            Candidate& operator=(const Candidate&) = delete;
            Candidate& operator=(Candidate&&) = default;
        };

        struct LruMemoMap {
            using Value = std::vector<std::shared_ptr<const ResultTree>>;
            using List   = std::list<std::pair<std::string, Value>>;
            using Map    = std::unordered_map<std::string, List::iterator,
                                            TransparentHash, TransparentEqual>;

            static constexpr size_t DEFAULT_MAX = 100;

            List lru_list;
            Map cache_map;
            size_t max_entries;

            LruMemoMap(size_t max = DEFAULT_MAX) : max_entries(max) {}

            Value* find(std::string_view text) {
                auto it = cache_map.find(text);
                if (it == cache_map.end()) return nullptr;
                lru_list.splice(lru_list.begin(), lru_list, it->second);
                return &it->second->second;
            }

            void insert(std::string text, Value value) {
                auto it = cache_map.find(text);
                if (it != cache_map.end()) {
                    lru_list.splice(lru_list.begin(), lru_list, it->second);
                    it->second->second = std::move(value);
                    return;
                }
                if (lru_list.size() >= max_entries) {
                    cache_map.erase(lru_list.back().first);
                    lru_list.pop_back();
                }
                lru_list.emplace_front(std::move(text), std::move(value));
                cache_map.emplace(lru_list.front().first, lru_list.begin());
            }

            size_t size() const { return lru_list.size(); }
        };

        using MemoOuterMap = std::unordered_map<
            std::string,
            LruMemoMap,
            TransparentHash,
            TransparentEqual
        >;

        using RuleSet = std::vector<std::pair<Tokens, Tokens>>;
        using RuleSets = std::unordered_map<std::string, RuleSet>;

        // =====================================================================
        // 3. 成员变量
        // =====================================================================
        bool initialized = false;
        RuleSets rulesets_;
        mutable MemoOuterMap memo_cache_;
        std::set<RuleSignature> cyclic_rule_signatures_;

        // =====================================================================
        // 4. 函数声明（按调用链：加载 → 翻译 → Token → 工具）
        // =====================================================================

        // TOML 加载
        void parse_dir(const std::filesystem::path& base, const std::filesystem::path& curr, std::optional<std::string>& visited_root);
        void parse_file(const std::filesystem::path& base, const std::filesystem::path& path, std::optional<std::string>& visited_root);
        void validate_references() const;
        void analyze_from_root();

        // 翻译核心
        std::vector<std::shared_ptr<const ResultTree>> find_translations(const std::string& text, bool partial_match) const;
        std::vector<std::shared_ptr<const ResultTree>> resolve_namespace(const std::string& text, const std::string& identifier, size_t level) const;
        std::vector<std::shared_ptr<const ResultTree>> resolve_replacer(const std::string& text, const std::string& identifier, size_t level) const;
        static const std::vector<std::vector<std::pair<std::string, std::string>>>& designation_patterns();
        static std::set<std::pair<std::string, std::string>> match_item_designation(const std::string& input);

        // Token 处理
        Tokens parse_tokens(const std::string& base_ns, const std::string& input);
        std::string to_canonical_identifier(const std::string& identifier, const std::string& base_ns) const;
        void validate_identifier_format(const std::string& identifier);

        // 工具
        static bool matches_literal(std::string_view input, std::string_view pattern);
        static std::string build_translated(const Tokens& trans_tokens, const BindingMap& bindings);
        static const std::regex& token_split_regex() {
            static const std::regex re(R"(\{([^\{\}]+)\})");
            return re;
        }
        static const std::regex& consecutive_colons_regex() {
            static const std::regex re(":+");
            return re;
        }
    };

    #define RULESETS DFHack::DFCH::Hooks::RulesetsManager::getInstance()

} // namespace Hooks
} // namespace DFCH
} // namespace DFHack
