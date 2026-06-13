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

### Per-frame rendering hook pipeline

1. **`preSDLRenderPresent`** (`screen_manager.cpp`) — the main hook entry, called each frame
2. Read the game's screen buffer directly from DF memory (`gps.screen`, `gps.screen_top`)
3. **`SentenceDetector`** (`sentence_detector.cpp`) — char-level analysis grouping individual characters into sentences/words by position, case rules, and punctuation. Uses compile-time lookup tables (`g_isUpper`, `g_isWordChar`, etc.)
4. **Translation (2-tier priority)** in `ScreenManager::processTranslations()`:
   - `DICTIONARY.tryTranslate()` — exact CSV match with digit normalization (digits → 0 → `{d}`)
   - `RULESETS.translate()` — TOML rewrite-rule engine for composite text (material+item combinations)
   - No word-level fallback (intentional — avoids low-quality partial translations)
5. **`TTFManager`** (`ttf_manager.cpp`) — dynamically loads SDL2_ttf.dll at runtime (not linked), renders translated text to SDL surfaces/textures
6. Custom textures blended over the original game surface via LRU texture cache

### Key managers

| Manager | Role |
|---|---|
| ScreenManager | Orchestrates the per-frame pipeline: screen buffer processing, translation dispatch, texture creation, texture cache, and rendering. ~1700 lines |
| DictManager | CSV key-value dictionary. `tryTranslate()`: digit normalization + exact match. `wordTranslate()`: per-word lookup (used only for color spans in rendering, not translation). Thread-safe with `shared_mutex`. Auto-collects untranslated text (FIFO, max 2000). Duplicate CSV keys are reported at `warn` level during load |
| RulesetsManager | TOML-based recursive rewrite rules with tokenization, cross-reference resolution, and per-identifier LRU memoization cache (max 100/identifier). Replacers: `%number` (digit matching), `%item_designation` (delegates to sub-namespace). At load time, `analyze_from_root()` runs a single DFS from `"::"` to detect cyclic rules (back-edge via `in_stack`/`path_stack`/`edge_stack`, stored in `cyclic_rule_signatures_` for O(log n) hot-path skip) and print simple leaf paths (fully non-compositional chains ending at pure-literal namespaces) |
| TTFManager | Runtime SDL2_ttf.dll loading, font matching by pixel height (binary search), text→surface rendering |
| LoggerManager | spdlog async logger with rotating file sink (10MB×3). Separate logger for untranslated text collection. Uses `debug` level for translation details, `info` for lifecycle, `warn`/`error` for failures |

### SDL2 hook layer

- `hook_common.h` — macros for declaring hook trampolines (`HOOK_FUNC`, `ORIG_FUNC`, `ATTACH_HOOK`, `DETACH_HOOK`)
- `sdl2_hooks.h/cpp` — ~50 hooked SDL2 function pointers in `SDL2Functions` struct (`g_sdl2` global)
- `hooks.cpp` — Detours attach/detach. `HOOK_FUNC(SDL_RenderPresent)` calls ScreenManager for per-frame translation rendering. Frame timing tracked in `dfch_proc_elapsed_us` / `df_frame_elapsed_us`

### Initialization order

`Hooks::init()` → `ScreenManager::init()`:
1. `TTFMANAGER.init()` — load SDL2_ttf.dll, init TTF, load font
2. `DICTIONARY.init()` — load CSV dicts from `data/dfch_dict_exact.csv` and `data/dfch_dict_word.csv`
3. `RULESETS.init()` → `load_rule_sets()` — clear + load TOML rules from `data/rulesets/`
4. `SENTENCEDETECTOR.init()`

Shutdown reverses: SentenceDetector → Rulesets → Dict → TTF.

## Data files

All under `data/` (installed to `hack/data/dfch/`):

| File | Purpose |
|---|---|
| `dfch_dict_exact.csv` | Exact-match translations (`"key","value","align"` CSV) |
| `dfch_dict_word.csv` | Word-level translations |
| `dfch_dict_untrans.csv` | Auto-collected untranslated texts |
| `dfch_config.txt` | `[KEY:VALUE]` format: `FONT_FILE`, `LOG_FILE`, `DICT_EXACT`, `DICT_WORD` |
| `rulesets/` | TOML rule files for RulesetsManager. Directory tree maps to `::ns::subns` identifiers. `index.toml` is the root ruleset |
| `*.ttf/*.otf` | Chinese fonts (MapleMonoNL-CN, NotoSansMonoCJKsc, SourceHanSerifCN) |

## Hotkeys

| Key | Command | Action |
|---|---|---|
| `Ctrl-Alt-L` | `dfch save_untrans` | Flush collected untranslated texts to log |
| `Ctrl-Alt-R` | `dfch reload_dicts` | Reload CSV dicts + TOML rulesets, clear texture cache |
| `Ctrl-Alt-K` | `dfch show_ch` | Toggle translation display on/off |

## Logging

Use `LOGGERMANAGER.getLogger()->debug/info/warn/error("fmt {}", arg)`. Never `printf` or `std::cout` in new code (hooks.cpp attach/detach `printf` calls are intentionally kept). Translation detail logs use `debug` level; loading progress uses `info`; failures use `error`; `[Cycle]` uses `warn`; `[SimpleLeaf]` uses `info`.
