
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

    /// 从 "::" 出发做全量 DFS（无缓存，每条路径完整探索），同时完成三件事：
    ///
    ///   设计决策：不使用 visited 集合剪枝（即同一 namespace 可能被多条路径重复访问）。
    ///   规则集图规模有限（实际 ~585 节点，边数可控），全量遍历的开销在加载期可接受。
    ///   保留 visited 仅用于统计可达节点数，不用于控制流程 —— 确保每条 distinct 路径的
    ///   simple 属性都被独立评估，每条循环路径都被完整发现。
    ///
    ///   1. 循环检测：通过 in_stack 检测 back-edge，发现循环时标记规则签名
    ///      到 cyclic_rule_signatures_ 并打印循环路径。
    ///
    ///   2. simple 叶子路径：parent_simple 在规则粒度沿 DFS 传播。
    ///      source 为单 token 的规则是"简单委派"，不切断 simple 链。
    ///      当 namespace 所有规则都是 1-token 纯字面时打印 [SimpleLeaf]。
    ///
    ///   3. 深度追踪：每次深入新节点时记录最深路径。
    void RulesetsManager::analyze_from_root() {
        cyclic_rule_signatures_.clear();

        if (!rulesets_.contains("::")) {
            LOGGERMANAGER.getLogger()->warn("[Analyze] root \"::\" not found, skip");
            return;
        }

        std::set<std::string> in_stack;      // 当前路径上的 namespace（循环检测）
        std::set<std::string> visited;       // 所有已探索的 namespace（纯统计）
        std::vector<std::string> path_stack; // 当前路径（有序）

        // edge_stack[i] = 从 path_stack[i] 到 path_stack[i+1] 所经过的规则签名
        struct Edge { std::string from_ns; Tokens rule_source; };
        std::vector<Edge> edge_stack;

        size_t global_max_depth = 0;
        std::string deepest_id;

        // parent_simple: 从根到当前 namespace 的路径上所有祖先都是 simple
        std::function<std::string(const std::string&, bool)> dfs =
            [&](const std::string& current, bool parent_simple) -> std::string {

            // back-edge：current 已在当前路径上 → 发现循环
            if (in_stack.contains(current)) {
                auto stack_it = std::find(path_stack.begin(), path_stack.end(), current);
                // in_stack 与 path_stack 始终同步，current 必在 path_stack 中
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
                LOGGERMANAGER.getLogger()->error("[Cycle] {}", cycle_path);
                return current;  // 返回 cycle target
            }

            in_stack.insert(current);
            path_stack.push_back(current);

            // 深度追踪
            if (path_stack.size() > global_max_depth) {
                global_max_depth = path_stack.size();
                deepest_id = current;
                std::string depth_path;
                depth_path.reserve(256);
                for (size_t i = 0; i < path_stack.size(); ++i) {
                    if (i > 0) depth_path += " → ";
                    depth_path += path_stack[i];
                }
                LOGGERMANAGER.getLogger()->debug("[Depth]{}: {}", global_max_depth, depth_path);
            }

            auto it = rulesets_.find(current);
            if (it != rulesets_.end()) {
                // [SimpleLeaf] 检查（namespace 级别）：所有规则 source 都是单 token 且无引用
                {
                    bool all_single_token = true;
                    bool has_ref = false;
                    for (const auto& [orig_tokens, trans_tokens] : it->second) {
                        if (orig_tokens.size() == 1 && trans_tokens.size() == 1
                            && orig_tokens[0].type == Type::Literal && orig_tokens[0].value.empty()
                            && trans_tokens[0].type == Type::Literal && trans_tokens[0].value.empty())
                            continue;

                        if (orig_tokens.size() != 1) {
                            all_single_token = false;
                            break;
                        }
                        if (orig_tokens[0].type == Type::Reference) has_ref = true;
                    }
                    if (parent_simple && all_single_token && !has_ref) {
                        std::string path_str;
                        path_str.reserve(256);
                        for (size_t i = 0; i < path_stack.size(); ++i) {
                            if (i > 0) path_str += " → ";
                            path_str += path_stack[i];
                        }
                        LOGGERMANAGER.getLogger()->debug("[SimpleLeaf] {}", path_str);
                    }
                }

                // 逐规则遍历引用 — parent_simple 在规则粒度判断：
                // 单 token source 的规则是"简单委派"，不影响 child 的 simple 链；
                // 多 token source 的规则是"复杂匹配"，切断 simple 链。
                std::string cycle_target;
                for (const auto& [orig_tokens, trans_tokens] : it->second) {
                    // 判断当前这条规则是否"简单"（source 只有 1 个 token）
                    bool is_optional_empty = (orig_tokens.size() == 1 && trans_tokens.size() == 1
                        && orig_tokens[0].type == Type::Literal && orig_tokens[0].value.empty()
                        && trans_tokens[0].type == Type::Literal && trans_tokens[0].value.empty());
                    bool rule_is_single = is_optional_empty || (orig_tokens.size() == 1);
                    bool child_simple = parent_simple && rule_is_single;

                    for (const auto& token : orig_tokens) {
                        if (token.type != Type::Reference) continue;
                        if (token.value[0] == '@' || token.value[0] == '#') continue;

                        // 决定要跟踪的目标 namespace。
                        // - 普通引用：直接使用 token.value（已由 parse_file 规范化）
                        // - % 引用：格式为 %replacer:base_ns[:qualifier]（已规范化）
                        //   从中提取目标 namespace ::base_ns[::qualifier]
                        std::string ref_target;
                        if (token.value[0] == '%') {
                            auto first_colon = token.value.find(':');
                            if (first_colon == std::string::npos) continue;       // 无冒号（不应出现）
                            auto second_colon = token.value.find(':', first_colon + 1);
                            if (second_colon == std::string::npos) {
                                // %replacer:base_ns → 目标为 ::base_ns
                                std::string base = token.value.substr(first_colon + 1);
                                if (base.empty()) continue;                      // %replacer: → 跳过
                                ref_target = "::" + base;
                            } else {
                                // %replacer:base_ns:qualifier → 目标为 ::base_ns::qualifier
                                std::string base = token.value.substr(first_colon + 1, second_colon - first_colon - 1);
                                std::string qualifier = token.value.substr(second_colon + 1);
                                if (qualifier.starts_with("::"))
                                    ref_target = qualifier;                      // 已是绝对路径
                                else
                                    ref_target = "::" + base + "::" + qualifier;
                            }
                        } else {
                            ref_target = token.value;
                        }

                        edge_stack.push_back({current, orig_tokens});
                        std::string target = dfs(ref_target, child_simple);
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

        LOGGERMANAGER.getLogger()->info("[Analyze] {} namespaces reachable from root", visited.size());
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
    /// 单次递归匹配：从根命名空间 :: 出发，由一条完整规则消费全部输入。
    /// 跨域组合词（如 "Pig iron bars"）的翻译应由 TOML 规则本身覆盖
    /// （例如在 ::items 中定义 "{::materials} bars" = "{::materials}锭"），
    /// 而非由引擎通过多轮迭代动态拼凑。单规则完整匹配也保证了 build_translated
    /// 可以按需调整子翻译的语序，而不受左到右拼接约束。
    ///
    /// @param text 待翻译的英文文本
    /// @return     翻译后的中文文本，无法翻译时返回 std::nullopt
    std::optional<std::string> RulesetsManager::translate(const std::string& text) const {
        auto results = find_translations(text, false);

        if (results.empty()) return std::nullopt;

        auto best = std::ranges::min_element(results,
            [](const std::shared_ptr<const ResultTree>& a, const std::shared_ptr<const ResultTree>& b) {
                return a->weight() < b->weight();
            });

        // LOGGERMANAGER.getLogger()->debug("\n========== All %zu Translation Results for \"%s\" ==========\n", results.size(), text.c_str());
        // for (size_t i = 0; i < results.size(); ++i) {
        //     LOGGERMANAGER.getLogger()->debug("--- Result #%zu (weight=%zu) ---\n", i, results[i]->weight());
        //     print_translation_path(*results[i]);
        // }
        // LOGGERMANAGER.getLogger()->debug("======================================================\n\n");

        return (*best)->translated;
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

                    // 校验：Placeholder（@ 前缀）后的 token 必须是 Literal
                    for (size_t i = 0; i < orig_tokens.size(); ++i) {
                        if (orig_tokens[i].type == Type::Reference && is_placeholder(orig_tokens[i].value)) {
                            if (i + 1 < orig_tokens.size() && orig_tokens[i + 1].type != Type::Literal) {
                                throw std::runtime_error(
                                    "Placeholder '" + orig_tokens[i].value +
                                    "' must be followed by a Literal token in: " + std::string(orig_str));
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
                    if (token.type == Type::Reference && token.value[0] != '%' && token.value[0] != '@' && token.value[0] != '#') {
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
        // === 0. 记忆化检查 — 两级异构查找，0 次临时字符串分配 ===
        if (auto outer_it = memo_cache_.find(identifier); outer_it != memo_cache_.end()) {
            if (auto* cached = outer_it->second.find(text)) {
                return *cached;
            }
        }

        // === 1. 处理 Replacer 引用（% 前缀）===
        if (!identifier.empty() && identifier[0] == '%') {
            auto results = resolve_replacer(text, identifier, level);
            memo_cache_[identifier].insert(text, results);
            return results;
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

            for (size_t ti = 0; ti < orig_tokens.size(); ++ti) {
                const auto& token = orig_tokens[ti];
                if (candidates.empty()) break;
                std::vector<Candidate> next_candidates;

                if (token.type == Type::Literal) {
                    for (auto& candidate : candidates) {
                        // 字面文本：大小写不敏感前缀匹配
                        std::string_view pattern = token.value;
                        std::string_view remaining_text = candidate.remaining;
                        if (matches_literal(remaining_text, pattern)) {
                            std::string new_remaining(remaining_text.substr(pattern.size()));
                            next_candidates.emplace_back(std::move(candidate.results), std::move(new_remaining));
                        }
                    }
                } else if (is_placeholder(token.value)) {
                    // Placeholder（@ 前缀）：捕获文本并原样穿透输出。
                    // 加载时已保证后面若有 token 则必为 Literal，见 parse_file()。
                    for (auto& candidate : candidates) {
                        if (ti + 1 < orig_tokens.size()) {
                            // 情况 A：后面还有 Literal（unique delimiter，只需首次匹配）
                            const auto& next_lit = orig_tokens[ti + 1];
                            auto pos = find_literal_position(
                                candidate.remaining, next_lit.value);
                            if (pos) {
                                std::string captured = candidate.remaining.substr(0, *pos);
                                std::string rem = candidate.remaining.substr(
                                    *pos + next_lit.value.size());
                                auto new_results = candidate.results;
                                new_results.emplace_back(token.value,
                                    std::make_shared<const ResultTree>(
                                        token.value, captured, captured, "", BindingMap{}));
                                next_candidates.emplace_back(
                                    std::move(new_results), std::move(rem));
                            }
                        } else {
                            // 情况 B：最后一个 token → 消费全部剩余文本
                            auto new_results = candidate.results;
                            new_results.emplace_back(token.value,
                                std::make_shared<const ResultTree>(
                                    token.value, candidate.remaining,
                                    candidate.remaining, "", BindingMap{}));
                            next_candidates.emplace_back(
                                std::move(new_results), "");
                        }
                    }
                    // 仅在确有候选存活时跳过已消费的 Literal；
                    // 若 delimiter 未找到（所有候选死于 placeholder），则不推进 ti，
                    // 由循环顶部 candidates.empty() break 安全退出。
                    if (ti + 1 < orig_tokens.size() && !next_candidates.empty()) ti++;
                } else if (is_builtin(token.value)) {
                    // Builtin token（# 前缀）：内联匹配，不递归。
                    // #digits: 匹配首部连续 ASCII 数字，原样穿透。
                    for (auto& candidate : candidates) {
                        std::string matched;
                        for (char ch : candidate.remaining) {
                            if (!std::isdigit(static_cast<unsigned char>(ch))) break;
                            matched.push_back(ch);
                        }
                        if (matched.empty()) continue;

                        std::string remaining = candidate.remaining.substr(matched.size());
                        auto new_results = candidate.results;
                        new_results.emplace_back(token.value,
                            std::make_shared<const ResultTree>(
                                token.value, matched, matched, remaining, BindingMap{}));
                        next_candidates.emplace_back(
                            std::move(new_results), std::move(remaining));
                    }
                } else {
                    for (auto& candidate : candidates) {
                        // 引用 Token：递归求解子目标（AND 绑定）
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
    /// 当前仅支持 item_designation：
    ///   匹配物品标记（品质符号、磨损标记、括号等），
    ///   剥离标记后委托到引用命名空间翻译内部文本，最后恢复标记包装。
    ///
    /// 完整的 Replacer 实现见 reference/replacer/item_designation.rs 中的 Rust 代码。
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
            {{"<", ">"}},
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

        const auto& patterns = designation_patterns();
        std::string_view input_sv = input;
        char first_ch = input[0];

        // --- 第 1 步：查找候选 ---
        // 使用首/尾字节匹配作为启发式预过滤：
        // - pl[0] 与 input[0] 比较：designation 前缀均为单字节 ASCII（+ - * = < …）
        // - pr.back() 与 input[i] 比较：后缀末字节匹配是宽松的启发式，可能对多字节
        //   UTF-8 字符（如 ☼ ◄ ►）产生多余候选，但第 2 步的全串验证会过滤掉误匹配。
        std::vector<std::string_view> candidates;
        for (const auto& pattern_pairs : patterns) {
            for (const auto& [pl, pr] : pattern_pairs) {
                if (pl.empty() || pr.empty()) continue;
                if (pl[0] != first_ch) continue;

                for (size_t i = 2; i < input.size(); ++i) {
                    if (input[i] == pr.back())
                        candidates.push_back(input_sv.substr(0, i + 1));
                }
                break;
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

            if (wrapper_len == 0) continue;
            results.insert({
                std::string(candidate.substr(0, wrapper_len)),
                std::string(candidate.substr(candidate.size() - wrapper_len, wrapper_len))
            });
        }
        // 存在真实标记时移除空标记，调用方可直接遍历无需判断
        if (results.size() > 1)
            results.erase({"", ""});

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
        if (identifier.starts_with("@")) {
            return identifier;  // Placeholder: local to rule, no namespace scoping
        }
        if (identifier.starts_with("#")) {
            return identifier;  // Builtin token: processed inline, no namespace scoping
        }
        if (identifier.starts_with("%")) {
            size_t colon = identifier.find(':', 1);
            if (colon == std::string::npos) {
                return identifier;      // Replacer: no namespace scoping
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
        if (identifier.starts_with("@")) {
            return;     // Placeholder: local name, no format requirements
        }
        if (identifier.starts_with("#")) {
            return;     // Builtin token: no format requirements
        }
        if (identifier.starts_with("%")) {
            return;     // Replacer: 无需校验格式，resolve_replacer 处理未知类型时返回空
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

    /// 大小写不敏感字符相等比较（位运算替代 std::tolower）。
    /// 作为 matches_literal 和 find_literal_position 的共用 comparator。
    bool RulesetsManager::ci_char_equal(unsigned char a, unsigned char b) {
        if (a >= 'A' && a <= 'Z') a |= 0x20;
        if (b >= 'A' && b <= 'Z') b |= 0x20;
        return a == b;
    }

    /// 大小写不敏感的前缀匹配，委托 ci_char_equal 逐字符比较。
    /// @return input 以 pattern 开头时返回 true
    bool RulesetsManager::matches_literal(std::string_view input, std::string_view pattern) {
        if (input.size() < pattern.size()) return false;
        return std::equal(pattern.begin(), pattern.end(), input.begin(), ci_char_equal);
    }

    /// 检查标识符是否为 Placeholder（@ 前缀）。
    /// Placeholder 没有对应规则集，用于捕获任意文本并原样穿透输出。
    bool RulesetsManager::is_placeholder(std::string_view identifier) {
        return !identifier.empty() && identifier[0] == '@';
    }

    /// 检查标识符是否为内建 token（# 前缀）。
    /// 内建 token 没有对应规则集，由引擎在 token 循环中内联处理。
    bool RulesetsManager::is_builtin(std::string_view identifier) {
        return !identifier.empty() && identifier[0] == '#';
    }

    /// 在文本中查找字面量的第一处大小写不敏感出现位置。
    /// 使用 std::search + ci_char_equal；placeholder 的后续 Literal
    /// 是 unique delimiter，只需首次匹配即可。
    /// @param text    待搜索的文本
    /// @param literal 要查找的字面量模式
    /// @return        匹配起始字节偏移，未找到返回 std::nullopt
    std::optional<size_t> RulesetsManager::find_literal_position(
        std::string_view text, std::string_view literal)
    {
        if (literal.empty()) return 0;
        auto it = std::search(text.begin(), text.end(),
                              literal.begin(), literal.end(), ci_char_equal);
        if (it == text.end()) return std::nullopt;
        return it - text.begin();
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

    /// 递归打印翻译路径（调试用）。
    /// 从根节点开始，逐层打印每条规则的匹配和翻译信息。
    void RulesetsManager::print_translation_path(const ResultTree& node, int indent) {
        std::string pad(indent * 2, ' ');
        printf("%s[%s]\n", pad.c_str(), node.identifier.c_str());
        printf("%s  matched   : \"%s\"\n", pad.c_str(), node.matched.c_str());
        printf("%s  translated: \"%s\"\n", pad.c_str(), node.translated.c_str());
        if (!node.remaining.empty())
            printf("%s  remaining : \"%s\"\n", pad.c_str(), node.remaining.c_str());
        printf("%s  weight    : %zu\n", pad.c_str(), node.weight());
        for (const auto& [ref_id, child] : node.children) {
            printf("%s  ref: %s\n", pad.c_str(), ref_id.c_str());
            print_translation_path(*child, indent + 1);
        }
    }

} // namespace Hooks
} // namespace DFCH
} // namespace DFHack
