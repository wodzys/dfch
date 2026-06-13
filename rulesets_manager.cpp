
#include "rulesets_manager.h"
#include "config.h"
#include "logger.h"

#include <algorithm>
#include <cctype>
#include <functional>

#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>


namespace DFHack {
namespace DFCH {
namespace Hooks {
    RulesetsManager::RulesetsManager() {
    }

    bool RulesetsManager::init() {
        if (!load_rule_sets()) {
            return false;
        }
        initialized = true;
        return true;
    }

    void RulesetsManager::shutdown() {
        initialized = false;
        rulesets_.clear();
        memo_cache_.clear();
        cyclic_rule_signatures_.clear();
    }

    void RulesetsManager::print_cache_stats() const {
        size_t total_entries = 0;
        for (const auto& [id, inner] : memo_cache_) total_entries += inner.size();
        LOGGERMANAGER.getLogger()->info("[Cache] {} identifiers, {} entries (LRU max {}/identifier)",
            memo_cache_.size(), total_entries, LruMemoMap::DEFAULT_MAX);
    }

    // -------------------------------------------------------------------------
    // 从根出发的图分析（循环检测 + simple 叶子路径）
    // -------------------------------------------------------------------------

    /// 从 "::" 出发做单次 DFS，同时完成两件事：
    ///
    ///   1. 循环检测：通过 path_stack 检测 back-edge，发现循环时标记规则签名
    ///      到 cyclic_rule_signatures_ 并直接打印循环路径。
    ///
    ///   2. simple 叶子路径：沿 DFS 路径传播 parent_simple 标志。
    ///      当 namespace 所有规则都是 1-token 且 max_refs==0（纯字面叶子）时，
    ///      直接打印 ":: → ... → leaf" 路径，供后续扁平化到 CSV 参考。
    ///      simple 路径结构：一连串 max_refs=1 的委派节点，最终落到 max_refs=0 的叶子。
    void RulesetsManager::analyze_from_root() {
        cyclic_rule_signatures_.clear();

        if (!rulesets_.contains("::")) {
            LOGGERMANAGER.getLogger()->warn("[Analyze] root \"::\" not found, skip");
            return;
        }

        std::set<std::string> visited;       // 已完成探索的 namespace
        std::set<std::string> in_stack;      // 当前路径上的 namespace
        std::vector<std::string> path_stack; // 当前路径（有序）

        // edge_stack[i] = 从 path_stack[i] 到 path_stack[i+1] 所经过的规则签名
        struct Edge { std::string from_ns; Tokens rule_source; };
        std::vector<Edge> edge_stack;

        size_t global_max_depth = 0;
        std::string deepest_id;

        // parent_simple: 从根到当前 namespace 的路径上所有祖先都是 simple
        std::function<std::string(const std::string&, bool)> dfs =
            [&](const std::string& current, bool parent_simple) -> std::string {
            if (visited.contains(current)) return "";

            // back-edge：current 已在当前路径上 → 发现循环
            if (in_stack.contains(current)) {
                auto stack_it = std::find(path_stack.begin(), path_stack.end(), current);
                if (stack_it != path_stack.end()) {
                    size_t idx = stack_it - path_stack.begin();
                    for (size_t i = idx; i < edge_stack.size(); ++i) {
                        cyclic_rule_signatures_.insert({edge_stack[i].from_ns, edge_stack[i].rule_source});
                    }
                    // 打印循环路径
                    std::string cycle_path = current;
                    cycle_path.reserve(256);
                    for (size_t i = idx + 1; i < path_stack.size(); ++i)
                        cycle_path += " → " + path_stack[i];
                    cycle_path += " → " + current + " (back)";
                    LOGGERMANAGER.getLogger()->warn("[Cycle] {}", cycle_path);
                }
                return current;  // 返回 cycle target
            }

            in_stack.insert(current);
            path_stack.push_back(current);

            // 深度追踪
            if (path_stack.size() > global_max_depth) {
                global_max_depth = path_stack.size();
                deepest_id = current;
            }

            auto it = rulesets_.find(current);
            if (it != rulesets_.end()) {
                // 检查当前 namespace 所有规则的 source pattern 是否都只有 1 个 token
                // 1-token 规则：纯字面（如 "barrel"）或单次委派（如 "{A}"），不涉及多 token 组合（如 "{A}{B}" 或 "{A} literal"）
                bool all_single_token = true;
                size_t max_refs = 0;
                for (const auto& [orig_tokens, trans_tokens] : it->second) {
                    if (orig_tokens.size() == 1 && trans_tokens.size() == 1
                        && orig_tokens[0].type == Type::Literal && orig_tokens[0].value.empty()
                        && trans_tokens[0].type == Type::Literal && trans_tokens[0].value.empty())
                        continue;

                    if (orig_tokens.size() != 1) {
                        all_single_token = false;
                        break;
                    }
                    if (orig_tokens[0].type == Type::Reference) max_refs = 1;
                }

                bool is_simple = parent_simple && all_single_token;
                if (is_simple && max_refs == 0) {
                    std::string path_str;
                    path_str.reserve(256);
                    for (size_t i = 0; i < path_stack.size(); ++i) {
                        if (i > 0) path_str += " → ";
                        path_str += path_stack[i];
                    }
                    LOGGERMANAGER.getLogger()->info("[SimpleLeaf] {}", path_str);
                }

                // 遍历所有规则的引用，将 effective 传递给子节点
                std::string cycle_target;
                for (const auto& [orig_tokens, _] : it->second) {
                    for (const auto& token : orig_tokens) {
                        if (token.type != Type::Reference) continue;
                        if (token.value[0] == '%') continue;

                        edge_stack.push_back({current, orig_tokens});
                        std::string target = dfs(token.value, is_simple);
                        edge_stack.pop_back();

                        if (!target.empty()) {
                            cyclic_rule_signatures_.insert({current, orig_tokens});
                            if (target != current) cycle_target = target;
                        }
                    }
                }

                path_stack.pop_back();
                in_stack.erase(current);
                visited.insert(current);
                return (cycle_target == current) ? "" : cycle_target;
            }

            path_stack.pop_back();
            in_stack.erase(current);
            visited.insert(current);
            return "";
        };

        dfs("::", true);  // 根 "::" 本身视为在 simple 路径起点

        if (!cyclic_rule_signatures_.empty()) {
            LOGGERMANAGER.getLogger()->info("[Analyze] {} cyclic rule(s) detected",
                cyclic_rule_signatures_.size());
        }
        if (global_max_depth > 0) {
            LOGGERMANAGER.getLogger()->info("[Analyze] deepest chain: {}, depth {}",
                deepest_id, global_max_depth);
        }

        LOGGERMANAGER.getLogger()->info("[Analyze] {} namespaces reachable from root",
            visited.size());
    }

    bool RulesetsManager::load_rule_sets() {
        rulesets_.clear();
        memo_cache_.clear();

        try {
            auto ruleset_dir = Config::getDataPath() / "rulesets/zh-Hans";
            if (std::filesystem::exists(ruleset_dir)) {
                load_from_dir(ruleset_dir);
                LOGGERMANAGER.getLogger()->info("RulesetsManager loaded: {} rulesets", rulesets_.size());
            } else {
                LOGGERMANAGER.getLogger()->info("RulesetsManager: no rulesets directory, skipping");
            }
        } catch (const std::exception& e) {
            LOGGERMANAGER.getLogger()->error("RulesetsManager init failed: {}", e.what());
            return false;
        }
        return true;
    }

    /// 翻译输入文本，返回翻译结果。
    ///
    /// 根级别采用迭代链式匹配策略：
    /// 根规则通常为单引用直通规则（如 "{materials}" = "{materials}"）。
    /// 复合输入（如 "Pig iron bars"）需要多次迭代才能完全消费：
    ///   第 1 轮: {materials} 匹配 "Pig iron" → 生铁，剩余 " bars"
    ///   第 2 轮: {items} 链匹配 " bars" → 锭，剩余 ""
    ///
    /// 每轮选择最优匹配的优先级：
    ///   1. 完整匹配 > 部分匹配
    ///   2. 剩余文本最短
    ///   3. 权重最低
    ///
    /// @param text 待翻译的英文文本
    /// @return     翻译后的中文文本，无法翻译时返回 std::nullopt
    std::optional<std::string> RulesetsManager::translate(const std::string& text) const {
        std::string full_translation;
        std::string remaining = text;
        size_t max_iterations = 10; // 防止异常输入导致无限循环

        while (!remaining.empty() && max_iterations-- > 0) {
            auto results = find_translations(remaining, false);

            if (results.empty()) break;

            // 三阶段排序：完整匹配优先 → 剩余文本最短 → 权重最低
            auto best = std::ranges::min_element(results,
                [](const std::shared_ptr<const ResultTree>& a, const std::shared_ptr<const ResultTree>& b) {
                    bool a_full = a->remaining.empty(), b_full = b->remaining.empty();
                    if (a_full != b_full) return a_full;
                    if (a->remaining.size() != b->remaining.size())
                        return a->remaining.size() < b->remaining.size();
                    return a->weight() < b->weight();
                });

            full_translation += (*best)->translated;
            std::string new_remaining = (*best)->remaining;

            // 无进展时退出，防止死循环
            if (new_remaining == remaining) break;
            remaining = std::move(new_remaining);
        }

        if (full_translation.empty()) return std::nullopt;
        return full_translation;
    }

    /// 递归遍历目录，收集 .toml 文件并按排序顺序加载。
    ///
    /// 目录条目排序确保确定性加载顺序，匹配 Rust 参考实现 sorted_paths.sort() 的行为。
    ///
    /// @param base         规则集根目录
    /// @param curr         当前遍历目录
    /// @param visited_root 记录已访问的根文件（确保唯一）
    void RulesetsManager::parse_dir(const std::filesystem::path& base, const std::filesystem::path& curr,
                std::optional<std::string>& visited_root) {
        LOGGERMANAGER.getLogger()->info("Loading rulesets from: {}", curr.string());

        // 收集并排序目录条目，确保确定性加载顺序（匹配 Rust sorted_paths.sort()）
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(curr)) {
            paths.push_back(entry.path());
        }
        std::sort(paths.begin(), paths.end());

        for (const auto& path : paths) {
            if (std::filesystem::is_directory(path)) {
                parse_dir(base, path, visited_root);
            } else if (path.extension() == ".toml") {
                parse_file(base, path, visited_root);
            }
        }
    }

    /// 解析单个 TOML 规则集文件。
    ///
    /// 加载流程：
    ///   1. 读取 [base] 字段（可选），验证与文件路径的一致性
    ///   2. 遍历 [[rulesets]] 数组，为每个规则集构建规则
    ///   3. 解析 [rulesets.rules] 表，将每条 key = value 转换为 (Tokens, Tokens)
    ///   4. 校验规则的有效性（重复引用、翻译模板中引用未在原文出现）
    ///
    /// @param base         规则集根目录
    /// @param path         当前 TOML 文件路径
    /// @param visited_root 记录已访问的根文件
    /// @throws std::runtime_error 文件格式错误或校验失败时抛出
    void RulesetsManager::parse_file(const std::filesystem::path& base, const std::filesystem::path& path,
                    std::optional<std::string>& visited_root) {
        auto result = toml::parse_file(path.u8string());

        if (!result) {
            LOGGERMANAGER.getLogger()->error("TOML parsing failed in {}: {}", path.string(), result.error().description());
            return;
        }

        const toml::table& toml_data = result.table();

        // [base] 字段（可选）：无 base 的文件为根文件，全局只能有一个
        std::optional<std::string> file_base = toml_data["base"].value<std::string>();

        if (!file_base.has_value()) {
            if (visited_root.has_value()) {
                throw std::runtime_error("Multiple root bases found: " + visited_root.value() +
                                        " and " + path.string());
            }
            visited_root = path.string();
        }

        std::string base_namespace = file_base.value_or("");

        // 验证 base_namespace 与文件路径一致，防止 TOML 作者误写 base 字段
        {
            auto relative = std::filesystem::relative(path, base);

            std::vector<std::string> parts;
            for (const auto& component : relative) {
                parts.push_back(component.string());
            }

            // 去除文件名中的 .toml 后缀
            if (!parts.empty() && parts.back().ends_with(".toml")) {
                parts.back() = parts.back().substr(0, parts.back().size() - 5);
            }

            // index.toml 是目录的默认规则文件，不贡献路径段
            if (!parts.empty() && parts.back() == "index") {
                parts.pop_back();
            }

            // 用 :: 连接各路径段作为期望的命名空间
            std::string expected;
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) expected += "::";
                expected += parts[i];
            }

            if (base_namespace != expected) {
                throw std::runtime_error(
                    "Base namespace mismatch in " + path.string() +
                    ": expected \"" + expected + "\", found \"" + base_namespace + "\""
                );
            }
        }

        // [[rulesets]] 数组
        if (!toml_data.contains("rulesets")) {
            return; // 允许空文件（无规则集）
        }
        auto rulesets_array = toml_data["rulesets"].as_array();
        if (!rulesets_array) {
            throw std::runtime_error("rulesets must be an array in " + path.string());
        }

        for (const auto& ruleset_entry : *rulesets_array) {
            if (!ruleset_entry.is_table()) {
                throw std::runtime_error("Non-table entry in rulesets array in " + path.string());
            }
            const auto& tbl = *ruleset_entry.as_table();

            std::string name_suffix;
            bool optional = false;

            // [rulesets.name]（可选）
            name_suffix = tbl["name"].value_or("");

            // [rulesets.optional]（默认 false）
            optional = tbl["optional"].value_or(false);

            // 构建完整的命名空间标识符
            std::string identifier = "::";
            if (!base_namespace.empty()) {
                identifier += base_namespace;
                if (!name_suffix.empty()) {
                    identifier += "::" + name_suffix;
                }
            } else {
                if (!name_suffix.empty()) {
                    identifier += name_suffix;
                }
            }

            validate_identifier_format(identifier);

            auto& rules = rulesets_[identifier];

            // optional 规则集：插入空匹配回退规则
            if (optional) {
                rules.emplace_back(
                    Tokens{Token{Type::Literal, ""}},
                    Tokens{Token{Type::Literal, ""}}
                );
            }

            // [rulesets.rules] 表
            if (!tbl.contains("rules")) {
                continue; // 允许无规则
            }
            auto rules_table = tbl["rules"].as_table();
            if (!rules_table) {
                throw std::runtime_error("rules must be a table in " + path.string());
            }

            for (const auto& [orig_str, trans_val] : *rules_table) {
                if (auto trans_node = trans_val.as_string()) {
                    std::string trans_str = trans_node->get();

                    // 解析原文和译文为 Token 序列
                    Tokens orig_tokens = parse_tokens(base_namespace, std::string(orig_str));
                    Tokens trans_tokens = parse_tokens(base_namespace, trans_str);

                    // 校验：原文中不能有重复引用
                    std::set<std::string> orig_refs;
                    for (const auto& tok : orig_tokens) {
                        if (tok.type == Type::Reference) {
                            if (!orig_refs.insert(tok.value).second) {
                                throw std::runtime_error(
                                    "Original entry \"" + std::string(orig_str) +
                                    "\" has duplicate reference: " + tok.value
                                    );
                            }
                        }
                    }

                    // 校验：译文中的引用必须全部出现在原文中
                    for (const auto& tok : trans_tokens) {
                        if (tok.type == Type::Reference) {
                            if (orig_refs.find(tok.value) == orig_refs.end()) {
                                throw std::runtime_error(
                                    "Translated reference \"" + tok.value +
                                    "\" not found in original: " + std::string(orig_str)
                                    );
                            }
                        }
                    }

                    // 插入规则（保持插入顺序，匹配 Rust IndexMap 行为）
                    rules.emplace_back(std::move(orig_tokens), std::move(trans_tokens));
                }
            }
        }
    }

    /// 验证所有规则中的非 Replacer 引用是否指向已加载的规则集。
    ///
    /// 遍历所有规则的所有 original Token，确保每个 Reference
    /// （% Replacer 除外）在 rulesets_ 中都能找到对应的规则集。
    ///
    /// @throws std::runtime_error 引用无法解析时抛出
    void RulesetsManager::validate_references() const {
        for (const auto& [name, ruleset] : rulesets_) {
            for (const auto& [orig, _] : ruleset) {
                for (const auto& token : orig) {
                    if (token.type == Type::Reference && token.value[0] != '%') {
                        if (!rulesets_.contains(token.value))
                            throw std::runtime_error("Reference not found: " + token.value);
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // 翻译核心
    // -------------------------------------------------------------------------

    /// 获取所有可能的翻译结果。
    /// @param partial_match true 时返回部分匹配结果，false 时仅返回完整匹配
    std::vector<std::shared_ptr<const RulesetsManager::ResultTree>> RulesetsManager::find_translations(const std::string& text, bool partial_match) const {
        auto results = resolve_namespace(text, "::", 0);
        if (partial_match) return results;
        std::vector<std::shared_ptr<const ResultTree>> full;
        for (auto& r : results)
            if (r->remaining.empty()) full.push_back(std::move(r));
        return full;
    }

    /// 在指定标识符命名空间内递归求解所有可能的翻译结果。
    ///
    /// 算法步骤：
    ///   0. 如果是 Replacer 引用（% 前缀），委托给 resolve_replacer
    ///   1. 检查记忆化缓存，命中则直接返回
    ///   2. 查找当前标识符对应的规则集
    ///   3. 遍历每条规则（OR 分支），逐 Token 匹配（AND 序列）
    ///      - Literal: 大小写不敏感前缀匹配
    ///      - Reference: 递归调用 resolve_namespace，绑定子结果
    ///   4. 对每个成功匹配的 Candidate 构建 ResultTree
    ///   5. 缓存结果到 memo_cache_（形成 DAG 共享边）
    ///
    /// 循环引用：加载时由 analyze_from_root() 预检测，
    /// 翻译时仅通过 cyclic_rule_signatures_ 做 O(log n) 查表跳过。
    ///
    /// @param text       待翻译的剩余文本
    /// @param identifier 当前命名空间标识符
    /// @param level      递归深度（仅用于调试输出）
    /// @return           所有可能的翻译结果列表
    std::vector<std::shared_ptr<const RulesetsManager::ResultTree>> RulesetsManager::resolve_namespace(
        const std::string& text,
        const std::string& identifier,
        size_t level
    ) const {
        // === 0. 处理 Replacer 引用（% 前缀）===
        if (!identifier.empty() && identifier[0] == '%') {
            return resolve_replacer(text, identifier, level);
        }

        // === 1. 记忆化检查 — 两级异构查找，0 次临时字符串分配 ===
        if (auto outer_it = memo_cache_.find(identifier); outer_it != memo_cache_.end()) {
            if (auto* cached = outer_it->second.find(text)) {
                return *cached;
            }
        }


        // === 2. 查找规则集 ===
        auto ruleset_it = rulesets_.find(identifier);
        if (ruleset_it == rulesets_.end()) return {};
        const auto& rules = ruleset_it->second;

        std::vector<std::shared_ptr<const ResultTree>> all_results;

        // === 3. 遍历每条规则（OR 分支）===
        for (const auto& [orig_tokens, trans_tokens] : rules) {

            // 跳过加载时已确认的循环规则（O(log n) 查表）
            if (!cyclic_rule_signatures_.empty() && cyclic_rule_signatures_.contains({identifier, orig_tokens})) continue;

            // === 3.1 逐 Token 匹配（AND 序列）—— 生成 Candidate 列表 ===
            std::vector<Candidate> candidates{Candidate({}, text)};

            for (const auto& token : orig_tokens) {
                if (candidates.empty()) break;
                std::vector<Candidate> next_candidates;

                for (auto& candidate : candidates) {
                    if (token.type == Type::Literal) {
                        // 字面文本：大小写不敏感前缀匹配
                        std::string_view pattern = token.value;
                        std::string_view remaining_text = candidate.remaining;
                        if (matches_literal(remaining_text, pattern)) {
                            std::string new_remaining(remaining_text.substr(pattern.size()));
                            next_candidates.emplace_back(std::move(candidate.results), std::move(new_remaining));
                        }
                    } else {
                        auto sub_results = resolve_namespace(candidate.remaining, token.value, level + 1);

                        // 为每个子结果创建新的 Candidate 分支
                        for (auto& sub_result : sub_results) {
                            std::string remaining_copy = sub_result->remaining;
                            auto new_results = candidate.results;
                            new_results.emplace_back(sub_result->identifier, std::move(sub_result));
                            next_candidates.emplace_back(std::move(new_results), std::move(remaining_copy));
                        }
                    }
                }
                candidates = std::move(next_candidates);
            }

            // === 3.2 为每个存活的 Candidate 构建 ResultTree ===
            for (auto& candidate : candidates) {
                std::string matched = text.substr(0, text.size() - candidate.remaining.size());

                std::string translated_str = build_translated(trans_tokens, candidate.results);

                auto result = std::make_shared<const ResultTree>(
                    identifier,
                    std::move(matched),
                    std::move(translated_str),
                    std::move(candidate.remaining),
                    std::move(candidate.results)
                );
                all_results.push_back(std::move(result));
            }
        }

        // === 4. 缓存结果 — 形成 DAG 边，共享子问题可复用 ===
        memo_cache_[identifier].insert(text, all_results);
        return all_results;
    }

    /// 将 Replacer 引用（% 前缀标识符）解析为翻译结果。
    ///
    /// 支持两种 Replacer：
    ///   - item_designation: 匹配物品标记（品质符号、磨损标记、括号等），
    ///     剥离标记后委托到引用命名空间翻译内部文本，最后恢复标记包装。
    ///   - number: 匹配输入开头的连续 ASCII 数字
    ///
    /// 完整的 Replacer 实现（品质符号匹配、磨损标记等）见
    /// reference/replacer/item_designation.rs 中的 Rust 代码。
    std::vector<std::shared_ptr<const RulesetsManager::ResultTree>> RulesetsManager::resolve_replacer(
        const std::string& text,
        const std::string& identifier,
        size_t level
    ) const {
        // 解析格式：%name:base_ns 或 %name:base_ns:config
        size_t first_colon = identifier.find(':');
        if (first_colon == std::string::npos) return {};

        std::string replacer_name = identifier.substr(1, first_colon - 1);
        std::string rest = identifier.substr(first_colon + 1);

        // 提取 base_ns 和可选的 config
        size_t second_colon = rest.find(':');
        std::string base_ns = (second_colon != std::string::npos)
            ? rest.substr(0, second_colon) : rest;
        std::string config = (second_colon != std::string::npos)
            ? rest.substr(second_colon + 1) : std::string{};

        if (replacer_name == "item_designation") {
            std::string reference = config.empty()
                ? to_canonical_identifier(base_ns, "")
                : to_canonical_identifier(config, base_ns);
            std::vector<std::shared_ptr<const ResultTree>> results;

            // 对每种可能的物品标记（无标记、"("、"{...}"、"$...$" 等），
            // 剥离标记 → 翻译内部文本 → 恢复标记
            for (const auto& [prefix, suffix] : match_item_designation(text)) {
                std::string inner_text = text.substr(prefix.size());
                auto inner_results = resolve_namespace(inner_text, reference, level + 1);

                for (auto& inner : inner_results) {
                    // 内部翻译的剩余文本必须以预期后缀开头
                    if (!inner->remaining.starts_with(suffix)) continue;

                    std::string remaining = inner->remaining.substr(suffix.size());
                    std::string matched = text.substr(0, text.size() - remaining.size());
                    std::string translated = prefix + inner->translated + suffix;

                    BindingMap children;
                    children.emplace_back("item", inner);

                    results.push_back(std::make_shared<const ResultTree>(
                        identifier,
                        std::move(matched),
                        std::move(translated),
                        std::move(remaining),
                        std::move(children)
                    ));
                }
            }
            return results;
        }

        if (replacer_name == "number") {
            // 匹配文本开头的连续 ASCII 数字（Rust NumberReplacer 的简化移植）
            std::vector<std::shared_ptr<const ResultTree>> results;
            std::string matched;
            for (char ch : text) {
                if (!std::isdigit(static_cast<unsigned char>(ch))) break;
                matched.push_back(ch);
            }
            if (!matched.empty()) {
                results.push_back(std::make_shared<const ResultTree>(
                    identifier,
                    matched,
                    matched,  // 数字直通，不做翻译
                    text.substr(matched.size()),
                    BindingMap{}
                ));
            }
            return results;
        }

        // 未知 Replacer 类型：返回空结果
        return {};
    }

    // -------------------------------------------------------------------------
    // Designation 标记匹配（物品标记：品质、磨损、括号等）
    // -------------------------------------------------------------------------

    /// 物品标记模式列表（从最外层到最内层）
    const std::vector<std::vector<std::pair<std::string, std::string>>>& RulesetsManager::designation_patterns() {
        static const std::vector<std::vector<std::pair<std::string, std::string>>> patterns = {
            // 1. not owned
            {{"$", "$"}},
            // 2. on fire
            {{"‼", "‼"}},
            // 3. wear（XX 必须在 X 之前，否则 X 会提前匹配）
            {{"XX", "XX"}, {"X", "X"}, {"x", "x"}},
            // 4. off site（异地物品 — 正是 (iron anvil) 中的括号）
            {{"(", ")"}},
            // 5. unclaimed
            {{"{", "}"}},
            // 6. quality
            {{"-", "-"}, {"+", "+"}, {"*", "*"}, {"=", "="}, {"☼", "☼"}},
            // 7. decor
            {{"«", "»"}},
            // 8. magic
            {{"◄", "►"}},
            // 9. quality (again)
            {{"-", "-"}, {"+", "+"}, {"*", "*"}, {"=", "="}, {"☼", "☼"}},
        };
        return patterns;
    }

    /// 匹配输入文本中的物品标记（designation markers）。
    ///
    /// 从输入两端同时扫描，按从外到内的顺序匹配所有标记层级，
    /// 返回所有可能的 (前缀, 后缀) 对。始终包含 ("", "") 表示无标记。
    ///
    /// 算法：
    ///   1. 找出所有候选：第一个字符匹配某级前缀、最后一个字符匹配对应后缀的位置
    ///   2. 对每个候选，尝试从外到内剥离所有层级的标记
    ///   3. 返回所有成功剥离的 (前缀, 后缀) 组合
    std::set<std::pair<std::string, std::string>> RulesetsManager::match_item_designation(const std::string& input) {
        std::set<std::pair<std::string, std::string>> results;
        results.insert({"", ""});

        if (input.size() <= 2) return results;

        // 构建 UTF-8 字符位置信息
        struct CharPos { size_t offset; size_t len; };
        std::vector<CharPos> chars;
        size_t offset = 0;
        while (offset < input.size()) {
            unsigned char c = static_cast<unsigned char>(input[offset]);
            size_t clen = 1;
            if      (c >= 0xF0) clen = 4;
            else if (c >= 0xE0) clen = 3;
            else if (c >= 0xC0) clen = 2;
            chars.push_back({offset, clen});
            offset += clen;
        }

        if (chars.size() <= 2) return results;

        // 累积字节偏移：slens[i] = 第 i 个字符之后的字节位置
        std::vector<size_t> slens;
        for (size_t i = 0; i < chars.size(); ++i)
            slens.push_back(chars[i].offset + chars[i].len);

        auto char_at = [&](size_t idx) -> std::string_view {
            return std::string_view(input).substr(chars[idx].offset, chars[idx].len);
        };

        const auto& patterns = designation_patterns();
        std::string_view first_ch = char_at(0);
        std::string_view input_sv = input;

        // --- 第 1 步：查找候选 ---
        std::vector<std::string_view> candidates;
        for (const auto& pattern_pairs : patterns) {
            for (const auto& [pl, pr] : pattern_pairs) {
                if (pl.empty()) continue;

                // 比较前缀的第一个 Unicode 字符
                unsigned char c0 = static_cast<unsigned char>(pl[0]);
                size_t pl_first_len = 1;
                if      (c0 >= 0xF0) pl_first_len = 4;
                else if (c0 >= 0xE0) pl_first_len = 3;
                else if (c0 >= 0xC0) pl_first_len = 2;
                if (std::string_view(pl.data(), pl_first_len) != first_ch) continue;

                // 获取后缀的最后一个 Unicode 字符
                size_t pr_last_off = 0;
                if (!pr.empty()) {
                    size_t pos = pr.size() - 1;
                    while (pos > 0 && (static_cast<unsigned char>(pr[pos]) & 0xC0) == 0x80)
                        --pos;
                    pr_last_off = pos;
                }
                std::string_view pr_last(pr.data() + pr_last_off, pr.size() - pr_last_off);

                // 扫描位置 >= 2，寻找匹配的最后字符
                for (size_t i = 2; i < chars.size(); ++i) {
                    if (char_at(i) == pr_last)
                        candidates.push_back(input_sv.substr(0, slens[i]));
                }
                break;  // 每个层级只使用第一个匹配的模式
            }
        }

        // --- 第 2 步：剥离标记 ---
        for (auto candidate : candidates) {
            size_t wrapper_len = 0;
            std::string_view remaining = candidate;

            for (const auto& pattern_pairs : patterns) {
                for (const auto& [pl, pr] : pattern_pairs) {
                    if (pl.empty()) continue;
                    if (remaining.size() < pl.size() + pr.size()) continue;

                    if (remaining.substr(0, pl.size()) == pl &&
                        remaining.substr(remaining.size() - pr.size(), pr.size()) == pr) {
                        remaining = remaining.substr(pl.size(),
                            remaining.size() - pl.size() - pr.size());
                        wrapper_len += pl.size();
                        break;
                    }
                }
            }

            results.insert({
                std::string(candidate.substr(0, wrapper_len)),
                std::string(candidate.substr(candidate.size() - wrapper_len, wrapper_len))
            });
        }

        return results;
    }

    // -----------------------------------------------------------------------------
    // 标识符处理
    // -----------------------------------------------------------------------------

    /// 将字符串解析为 Token 序列。
    ///
    /// 使用正则 `{([^{}]+)}` 识别引用标记：
    ///   - 花括号内的内容 → Reference Token（经 to_canonical_identifier 规范化）
    ///   - 花括号外的内容 → Literal Token
    ///
    /// @param base_ns 当前文件的命名空间，用于相对引用展开
    /// @param input   待解析的字符串
    /// @return        解析后的 Token 序列
    /// @throws std::runtime_error 括号不匹配时抛出
    RulesetsManager::Tokens RulesetsManager::parse_tokens(const std::string& base_ns, const std::string& input) {
        // 校验括号是否成对出现
        int lbrace = 0, rbrace = 0;
        for (char c : input) {
            if (c == '{') ++lbrace;
            else if (c == '}') ++rbrace;
        }
        if (lbrace != rbrace)
            throw std::runtime_error("Mismatched braces in token string");

        // 收集所有分割位置。0 在最前，input.size() 在最后，
        // 中间每次匹配推入 start/end 且 regex 迭代器保证递增，
        // 整个序列天然有序，无需 sort，只需去重
        std::vector<size_t> positions{0};
        for (std::sregex_iterator it(input.begin(), input.end(), token_split_regex()), end;
            it != end; ++it) {
            positions.push_back(it->position());
            positions.push_back(it->position() + it->length());
        }
        positions.push_back(input.size());
        positions.erase(std::unique(positions.begin(), positions.end()), positions.end());

        Tokens tokens;
        for (size_t i = 0; i < positions.size() - 1; ++i) {
            size_t l = positions[i], r = positions[i + 1];
            std::string chunk = input.substr(l, r - l);
            if (!chunk.empty() && chunk.front() == '{' && chunk.back() == '}') {
                std::string inner = chunk.substr(1, chunk.size() - 2);
                std::string ref = to_canonical_identifier(inner, base_ns);
                validate_identifier_format(ref);
                tokens.emplace_back(Token{Type::Reference, std::move(ref)});
            } else if (!chunk.empty()) {
                tokens.emplace_back(Token{Type::Literal, std::move(chunk)});
            }
        }

        // 注意：不在此处检查 original 中的重复引用；
        // 该检查由 parse_file 中调用方负责（见 reference/translator.rs 的 duplicate detection）

        return tokens;
    }

    /// 将标识符转换为规范形式。
    ///
    /// 处理三种形式：
    ///   - "::prefix"         → 绝对引用，原样返回
    ///   - "%replacer:ns"     → Replacer 引用，插入 base_ns
    ///   - "bare_name"        → 相对引用，展开为 "::base_ns::bare_name"
    ///
    /// @param identifier 原始标识符
    /// @param base_ns    当前文件的基础命名空间
    /// @return           规范化后的标识符字符串
    std::string RulesetsManager::to_canonical_identifier(const std::string& identifier, const std::string& base_ns) const {
        if (identifier.starts_with("::")) {
            return identifier;
        }
        if (identifier.starts_with("%")) {
            size_t colon = identifier.find(':', 1);
            if (colon == std::string::npos) {
                std::string result;
                result.reserve(identifier.size() + 1 + base_ns.size());
                result += identifier;
                result += ':';
                result += base_ns;
                return result;
            } else {
                std::string result;
                result.reserve((colon + 1) + base_ns.size() + (identifier.size() - colon));
                result.append(identifier, 0, colon + 1);
                result += base_ns;
                result.append(identifier, colon);
                return result;
            }
        }
        if (base_ns.empty()) {
            std::string result;
            result.reserve(2 + identifier.size());
            result += "::";
            result += identifier;
            return result;
        }
        std::string result;
        result.reserve(4 + base_ns.size() + identifier.size());
        result += "::";
        result += base_ns;
        result += "::";
        result += identifier;
        return result;
    }

    /// 校验标识符格式的合法性。
    ///
    /// - Replacer 标识符（% 前缀）必须包含 ':' 分隔符
    /// - 普通标识符不能以 "::" 结尾（根 "::" 除外）
    /// - 普通标识符中不能出现连续三个及以上的冒号
    ///
    /// @throws std::runtime_error 格式不合法时抛出
    void RulesetsManager::validate_identifier_format(const std::string& identifier) {
        if (identifier.starts_with("%")) {
            if (identifier.find(':') == std::string::npos) {
                throw std::runtime_error("Replacer identifier must contain ':' separating name and config");
            }
            // Replacer 标识符允许任意后续格式，不做进一步校验
            return;
        }

        if (identifier != "::" && identifier.ends_with("::"))
            throw std::runtime_error("Identifier cannot end with '::'");

        std::sregex_iterator it(identifier.begin(), identifier.end(), consecutive_colons_regex()), end;
        while (it != end) {
            if (it->str().size() != 2)
                throw std::runtime_error("Identifier contains invalid colon sequence");
            ++it;
        }
    }

    /// 大小写不敏感的前缀匹配（位运算替代 std::tolower，编译器可 SIMD 展开）
    /// @return input 以 pattern 开头时返回 true
    bool RulesetsManager::matches_literal(std::string_view input, std::string_view pattern) {
        if (input.size() < pattern.size()) return false;
        return std::equal(pattern.begin(), pattern.end(), input.begin(),
            [](unsigned char a, unsigned char b) {
                if (a >= 'A' && a <= 'Z') a |= 0x20;
                if (b >= 'A' && b <= 'Z') b |= 0x20;
                return a == b;
            });
    }

    /// 根据翻译模板和绑定结果构建最终翻译文本。
    ///
    /// 遍历 trans_tokens：
    ///   - Literal → 直接拼接其值
    ///   - Reference → 查找 bindings 中对应的子结果，拼接其 translated 字段
    ///
    /// @throws std::runtime_error 引用未绑定时抛出
    std::string RulesetsManager::build_translated(const Tokens& trans_tokens, const BindingMap& bindings) {
        std::string s;
        for (const auto& t : trans_tokens) {
            if (t.type == Type::Literal) {
                s += t.value;
            } else {
                auto it = std::find_if(bindings.begin(), bindings.end(),
                    [&](const auto& p) { return p.first == t.value; });
                if (it == bindings.end()) {
                    throw std::runtime_error("Unbound reference: " + t.value);
                }
                s += it->second->translated;
            }
        }
        return s;
    }

} // namespace Hooks
} // namespace DFCH
} // namespace DFHack
