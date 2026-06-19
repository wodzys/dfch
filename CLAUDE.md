# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

A DFHack plugin that hooks SDL2 rendering calls to translate Dwarf Fortress's on-screen English text into Chinese in real time. Uses Microsoft Detours for function hooking and TTF fonts for Chinese glyph rendering.

## Build

```bash
# Requires: CMake, vcpkg, DFHack SDK, Visual Studio 2022
# SDL2_INCLUDE_DIRS must point to DFHack's bundled SDL headers
cmake --preset default .
cmake --build .
```

Dependencies via `3rdParty/vcpkg.json` (detours, spdlog, tomlplusplus). SDL2_ttf v2.24.0 is downloaded at configure time. The `dfhack_plugin()` CMake macro is provided by the DFHack SDK.

## Architecture

Plugin entry point is `dfch.cpp` — standard DFHack lifecycle. On enable, it installs SDL2 function hooks via Detours (`hooks.cpp`). All manager classes are singletons accessed via macros (`SCREENMANAGER`, `DICTIONARY`, `RULESETS`, `TTFMANAGER`, `LOGGERMANAGER`). Everything lives under `DFHack::DFCH::Hooks`.

### Source files (10 .cpp, 10 .h/.hpp, ~5500 lines total)

| File | Lines | Purpose |
|---|---|---|
| `screen_manager.cpp/h` | ~1532 / 521 | Frame pipeline orchestration, texture cache, rendering |
| `rulesets_manager.cpp/h` | ~1062 / 238 | TOML rewrite-rule engine with token matching |
| `sentence_detector.cpp/h` | ~350 / 153 | Character-level sentence/word grouping |
| `dict_manager.cpp/h` | ~317 / 82 | CSV key-value dictionary |
| `hooks.cpp/h` | ~275 / 30 | Detours attach/detach, hook entry points |
| `dfch.cpp` | ~224 | Plugin lifecycle (DFHack entry point) |
| `ttf_manager.cpp/h` | ~216 / 88 | SDL2_ttf runtime loading, font rendering |
| `sdl2_hooks.cpp/h` | ~49 / 145 | ~50 hooked SDL2 function pointers |
| `hook_common.h` | ~27 | Hook trampoline macros |
| `logger.cpp/h` | ~76 / 53 | spdlog async logger setup |
| `config.cpp/h` | ~57 / 44 | Runtime path resolution |

### Per-frame rendering hook pipeline

1. **`preSDLRenderPresent`** (`screen_manager.cpp`) — the main hook entry, called each frame
2. Read the game's screen buffer directly from DF memory (`gps.screen`, `gps.screen_top`)
3. **`SentenceDetector`** (`sentence_detector.cpp`) — char-level analysis grouping individual characters into sentences/words by position, case rules, and punctuation. Uses compile-time lookup tables (`g_isUpper`, `g_isWordChar`, etc.)
4. **Translation (2-tier priority)** in `ScreenManager::processTranslations()`:
   - `DICTIONARY.tryTranslate()` — exact CSV match with digit normalization (digits → 0 → `{d}`)
   - `RULESETS.translate()` — TOML rewrite-rule engine for composite text (material+item combinations). Supports `@placeholder` tokens (`{@ph}`) that capture arbitrary text and pass it through as-is, `#builtin` tokens (`#digits`) for built-in matching, and `%replacer` tokens (`{%item_designation:ns}`) that strip markers and delegate inner text translation
   - No word-level fallback (intentional — avoids low-quality partial translations)
5. **Color span processing** — `processWordsColor()` performs phrase-level color lookup via `wordTranslate()`, then merges adjacent spans with identical colors into contiguous runs for efficient rendering
6. **`TTFManager`** (`ttf_manager.cpp`) — dynamically loads SDL2_ttf.dll at runtime (not linked), renders translated text to SDL surfaces/textures
7. Custom textures blended over the original game surface via LRU texture cache

### Key managers

| Manager | Role |
|---|---|
| ScreenManager | Orchestrates the per-frame pipeline: screen buffer processing (blocked-transpose `filterScreenUpdate` reads column-major DF buffers, writes row-major output), translation dispatch, color span merging, multi-line texture generation, texture cache, and rendering. cp437→ASCII mapping covers game tiles, and M/F symbols (cp437 11/12). ~1532 lines |
| DictManager | CSV key-value dictionary. `tryTranslate()`: digit normalization + exact match. `wordTranslate()`: per-word lookup (used for color spans in rendering, not translation). Thread-safe with `shared_mutex`. Auto-collects untranslated text (FIFO, max 2000). Duplicate CSV keys are reported at `warn` level during load |
| RulesetsManager | TOML-based recursive rewrite rules with tokenization, cross-reference resolution, and per-identifier LRU memoization cache (max 100/identifier). Token types: `Literal` (plain text), `Reference` (`{::ns::rule}`, cross-namespace), `Placeholder` (`{@name}`, `@` prefix, local capture), `Builtin` (`{#digits}`, `#` prefix, inline matching via `is_builtin()`), and `Replacer` (`{%item_designation:ns}`, `%` prefix, special processing via `resolve_replacer()`). `@placeholder` tokens capture arbitrary text and pass it through as-is. At load time, `analyze_from_root()` runs a full DFS from `"::"` (no visited-set pruning — all paths explored) to: (1) detect cyclic rules via back-edge on `in_stack`/`path_stack`/`edge_stack`, stored in `cyclic_rule_signatures_` for O(log n) hot-path skip; (2) print simple leaf paths — `parent_simple` propagates per-rule (single-token source rules are "simple delegation", multi-token rules break the chain), leaf namespaces with all 1-token pure-literal rules print `[SimpleLeaf]`; (3) track deepest path with `[Depth]` logging. References are followed during DFS by extracting `base_ns[:qualifier]` from `{ref:base_ns[:qualifier]}` format. `match_item_designation()` implements `%item_designation` — uses byte-level pre-filter + full-string validation for item designation marker matching (quality, wear, fire, ownership, etc.), strips markers, delegates inner text to referenced namespace, then wraps result back. Helper functions: `ci_char_equal` (case-insensitive char compare), `is_placeholder` (`@` prefix check), `is_builtin` (`#` prefix check), `find_literal_position` (substring search with case-insensitive comparator) |
| TTFManager | Runtime SDL2_ttf.dll loading, font matching by pixel height (binary search), text→surface rendering |
| LoggerManager | spdlog async logger with rotating file sink (10MB×3). Separate logger for untranslated text collection. Uses `debug` level for translation details, `info` for lifecycle, `warn`/`error` for failures |

### Config

`config.h/cpp` — runtime path resolution via `Core::getHackPath()` (no compile-time hardcoded paths):
- `getDataPath()` → `<hack>/data/dfch` (lazy, cached static)
- `getDFHackPath()` → DFHack installation root (parent of `hack/`)
- `getConfig()`, `getFontFile()`, `getLogFile()`, `getDictFile()` all resolve through `getDataPath()`
- Config file (`dfch_config.txt`) is loaded lazily on first access; `reloadConfig()` forces re-read

### SDL2 hook layer

- `hook_common.h` — macros for declaring hook trampolines (`HOOK_FUNC`, `ORIG_FUNC`, `ATTACH_HOOK`, `DETACH_HOOK`)
- `sdl2_hooks.h/cpp` — ~50 hooked SDL2 function pointers in `SDL2Functions` struct (`g_sdl2` global)
- `hooks.cpp` — Detours attach/detach. `HOOK_FUNC(SDL_RenderPresent)` calls ScreenManager. `dfhooks_sdl_event` hook filters events. Frame timing tracked in `dfch_proc_elapsed_us` / `df_frame_elapsed_us`

### Initialization order

`Hooks::init()`:
1. `LOGGERMANAGER.init()` — init spdlog async logger (path resolved via `Config::getLogFile()`)
2. `g_sdl2.loadFunc()` — runtime load ~50 SDL2 function pointers
3. `SCREENMANAGER.init()`:
   a. `TTFMANAGER.init()` — load SDL2_ttf.dll (path via `Config::getDFHackPath()`), init TTF, load font
   b. `DICTIONARY.init()` — load CSV dicts from `data/dfch_dict_exact.csv` and `data/dfch_dict_word.csv`
   c. `RULESETS.init()` → `load_rule_sets()` — clear + load TOML rules from `data/rulesets/`
   d. `SENTENCEDETECTOR.init()`

Shutdown reverses: SentenceDetector → Rulesets → Dict → TTF.

## Data files

All under `data/` (installed to `hack/data/dfch/`):

| File | Purpose |
|---|---|
| `dfch_dict_exact.csv` | Exact-match translations (`"key","value","align"` CSV) |
| `dfch_dict_word.csv` | Word-level translations (used for color spans) |
| `dfch_dict_untrans.csv` | Auto-collected untranslated texts |
| `dfch_config.txt` | `[KEY:VALUE]` format: `FONT_FILE`, `LOG_FILE`, `DICT_EXACT`, `DICT_WORD` |
| `rulesets/` | TOML rule files for RulesetsManager. Directory tree maps to `::ns::subns` identifiers. `index.toml` is the root ruleset. Supports `@placeholder` tokens (`{@ph}`), `#builtin` tokens (`#digits`), and `%replacer` tokens (`{%item_designation:ns}`) |
| `fonts/*.ttf` | Chinese fonts (MapleMonoNL-CN: Bold, Regular, Light) |

### Rulesets directory structure

All rules live under `rulesets/zh-Hans/` (185 TOML files across 12 subdirectories):

| Subdirectory | Files | Purpose |
|---|---|---|
| (root) | 16 | Core: `index.toml`, `color.toml`, `name.toml`, `skills.toml`, `tasks.toml`, `thought.toml`, `tiles.toml`, `workshop.toml`, etc. |
| `adv/` | 11 | Adventure mode: attack, climb, direction, getmenu, mount, etc. |
| `creatures/` | 3 | Creature names and castes |
| `dwarf_language/` | 5 | Dwarf language: adjectives, names, verbs |
| `english_name/` | 5 | English name equivalents |
| `gems/` | 2 | Gem details |
| `health/` | 5 | Body parts, conditions, item get |
| `items/` | 80+ | Every item type: weapon, armor, food, drink, plant, gem, book, furniture, etc. |
| `materials/` | 3 | Material jobs and states |
| `menu/` | 14 | Workshop menus: forge, kiln, smelt, weave, craft, etc. |
| `plants/` | 4 | Plant names, growths, seeds |
| `positions/` | 3 | Noble positions |
| `psychology/` | 5 | Needs, thoughts, memories, events |

## CI

GitHub Actions workflows in `.github/workflows/`:
- `build-plugin-windows.yml` — Build plugin with MSVC
- `build.yml` — General build workflow
- `watch-dfhack-release.yml` — Monitor upstream DFHack releases

## Hotkeys

| Key | Command | Action |
|---|---|---|
| `Ctrl-Alt-L` | `dfch save_untrans` | Flush collected untranslated texts to log |
| `Ctrl-Alt-R` | `dfch reload_dicts` | Reload CSV dicts + TOML rulesets, clear texture cache |
| `Ctrl-Alt-K` | `dfch show_ch` | Toggle translation display on/off |

## Logging

Use `LOGGERMANAGER.getLogger()->debug/info/warn/error("fmt {}", arg)`. Never `printf` or `std::cout` in new code (hooks.cpp attach/detach `printf` calls are intentionally kept). Translation detail logs use `debug` level; loading progress uses `info`; failures use `error`; `[Cycle]` uses `error`; `[SimpleLeaf]` and `[Depth]` use `debug`.
