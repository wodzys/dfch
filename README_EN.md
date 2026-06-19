# Dwarf Fortress Chinese Translation Plugin (df-chinese)

> **[中文文档](README.md)** — 本文件的中文版本

A Chinese localization plugin for *Dwarf Fortress* built on the DFHack framework, providing real-time on-screen text translation and rendering.

## About

This plugin runs as a DFHack extension, implementing real-time game UI localization through four core technologies: direct memory reading, SDL2 rendering hooking, a two-tier translation engine, and TTF font rendering. See [Technical Architecture](#technical-architecture) below for details.

## Features

- **Real-time Translation**: Automatically detects and replaces on-screen English text with Chinese on every frame
- **Two-Tier Translation System**: CSV exact-match dictionary + TOML rule engine (recursive rewriting, cross-namespace references, `@placeholder` capture, cycle detection, LRU memoization cache)
- **Sentence-Level Text Detection**: Char-level analysis combining position, case and punctuation rules to group characters into sentences/words
- **TTF Chinese Font Rendering**: Dynamic SDL2_ttf.dll loading, font matching by pixel height
- **Color Preservation**: Retains original text color information, supports dynamic color updates in real time
- **Untranslated Text Collection**: Automatically collects unmatched text (FIFO, max 2000 entries) for one-click export

## Technical Architecture

### Per-Frame Rendering Pipeline

```
Screen Buffer (DF Memory: gps.screen / gps.screen_top)
       │
       ▼
SentenceDetector.detectSentences()    Char-level detection → sentence/word grouping
       │
       ▼
ScreenManager::processTranslations()
  ├─ 1. DICTIONARY.tryTranslate()     CSV exact match (with digit normalization)
  └─ 2. RULESETS.translate()          TOML rule engine (recursive rewrite + @placeholder + memoization cache)
       │
       ▼
TTFManager::RenderBlendedText()        Chinese TTF rendering → SDL_Surface → SDL_Texture
       │
       ▼
LRU Texture Cache (500 entries)       Reuse on hit, regenerate on miss
       │
       ▼
SDL_RenderCopy (via g_sdl2 hooks)     Chinese texture composited onto game display
```

### Module Architecture

| Module | File(s) | Responsibility |
|--------|---------|----------------|
| **Plugin Entry** | `dfch.cpp` | DFHack lifecycle management, command handling, hotkey binding |
| **Hook Management** | `hooks.cpp/h`, `sdl2_hooks.cpp/h`, `hook_common.h` | Detours attach/detach, runtime loading of ~50 SDL2 function pointers |
| **ScreenManager** | `screen_manager.cpp/h` | Core dispatcher: screen buffer processing, translation dispatch, texture creation and caching, rendering overlay |
| **DictManager** | `dict_manager.cpp/h` | CSV dictionary management: exact match + digit normalization + word-level lookup, thread-safe |
| **RulesetsManager** | `rulesets_manager.cpp/h` | TOML rule engine: recursive rewriting, `@placeholder` capture, cross-namespace reference resolution, DFS cycle detection, LRU memoization cache |
| **SentenceDetector** | `sentence_detector.cpp/h` | Char-level text detection: compile-time lookup tables, sentence grouping by position/case/punctuation |
| **TTFManager** | `ttf_manager.cpp/h` | SDL2_ttf runtime loading, font matching, text→Surface rendering |
| **LoggerManager** | `logger.cpp/h` | spdlog async logger: rotating file (10MB×3) + separate untranslated text log |
| **Config** | `config.cpp/h` | Config file parsing, path management |

### Initialization Order

```
Hooks::init()
  ├─ LOGGERMANAGER.init()
  ├─ g_sdl2.loadFunc()              Runtime load of ~50 SDL2 function pointers
  └─ SCREENMANAGER.init()
       ├─ 1. TTFMANAGER.init()      Dynamically load SDL2_ttf.dll, init TTF, load font
       ├─ 2. DICTIONARY.init()      Load CSV dictionaries (dfch_dict_exact.csv + dfch_dict_word.csv)
       ├─ 3. RULESETS.init()        Load TOML ruleset directory (data/rulesets/)
       └─ 4. SENTENCEDETECTOR.init()

Shutdown reverses: SentenceDetector → Rulesets → Dict → TTF
```

## Project Structure

```
├── CMakeLists.txt              # CMake build file
├── dfch.cpp                    # Plugin main entry point
├── hooks.cpp / hooks.h         # SDL hook management
├── sdl2_hooks.cpp / sdl2_hooks.h  # SDL2 function pointer runtime loading
├── hook_common.h               # Hook macros
├── screen_manager.cpp / .h     # Screen management and rendering dispatch
├── dict_manager.cpp / .h       # CSV dictionary management
├── rulesets_manager.cpp / .h   # TOML rule engine
├── sentence_detector.cpp / .h  # Sentence detector
├── ttf_manager.cpp / .h        # TTF font management
├── logger.cpp / .h             # Log management
├── config.cpp / .h             # Config management
├── 3rdParty/                   # Third-party dependencies
│   ├── vcpkg.json              # vcpkg dependency manifest (detours, spdlog, tomlplusplus)
│   └── SDL2_ttf/               # SDL2_ttf download cache
└── data/                       # Runtime data files
    ├── dfch_config.txt         # Config file [KEY:VALUE] format
    ├── dfch_dict_exact.csv     # Exact-match dictionary (key,value,align)
    ├── dfch_dict_word.csv      # Word-level dictionary
    ├── dfch_dict_untrans.csv   # Auto-collected untranslated text
    ├── fonts/                  # Chinese font files (MapleMonoNL-CN Bold/Regular/Light, SIL OFL 1.1)
    └── rulesets/               # TOML rulesets (zh-Hans/)
```

## Installation

1. Ensure Dwarf Fortress and DFHack are installed
2. Build the plugin or use a pre-built binary
3. Copy `dfch.plug.dll` to DFHack's `plugins/` directory
4. Copy all files from `data/` to `<DF>/hack/data/dfch/`
5. Place `SDL2_ttf.dll` in the game root directory (same level as `Dwarf Fortress.exe`)
6. Launch the game and run `enable dfch` in the DFHack console

## Usage

1. Launch Dwarf Fortress with DFHack
2. In the DFHack command line, enter `enable dfch` to start the plugin
3. The plugin will automatically detect and translate on-screen text

### Hotkeys

| Hotkey | Command | Action |
|--------|---------|--------|
| `Ctrl-Alt-L` | `dfch save_untrans` | Export collected untranslated text to log |
| `Ctrl-Alt-R` | `dfch reload_dicts` | Reload CSV dictionaries + TOML rulesets, clear texture cache |
| `Ctrl-Alt-K` | `dfch show_ch` | Toggle Chinese translation display on/off |

## Configuration

Edit `data/dfch_config.txt` (`[KEY:VALUE]` format). All paths are resolved at runtime via `Core::getHackPath()` to relative paths under `<hack>/data/dfch/`:

| Key | Description | Default |
|-----|-------------|---------|
| `FONT_FILE` | TTF font path (relative to data/dfch/) | `fonts/MapleMonoNL-CN-Bold.ttf` |
| `LOG_FILE` | Log file path | `logs/dfch.log` |
| `DICT_EXACT` | Exact-match dictionary file | `dfch_dict_exact.csv` |
| `DICT_WORD` | Word-level dictionary file | `dfch_dict_word.csv` |

## Dictionary Format

Dictionary files use CSV format with three fields:

| Column | Description |
|--------|-------------|
| First | Original English text |
| Second | Chinese translation; supports `\n` newlines and `\a#FFFFFFtext\a` color tags |
| Third | Control character: empty=left-align, `c`=center, `b`=real-time update, `s`=contains escaped chars, `d`=`b`+`c` |

Example:
```csv
"Continue Playing","继续游戏",
"DFHack Launcher","DFHack启动器","c"
"Hello World","你好 \a#FF0000世界\a","s"
```

## TOML Ruleset Format

Ruleset files are located under `data/rulesets/zh-Hans/` in TOML format. The directory tree maps to `::ns::subns` namespace identifiers, with `index.toml` as the root ruleset.

### Token Types

| Token | Syntax | Description |
|-------|--------|-------------|
| **Literal** | Plain text | Case-insensitive prefix match |
| **Reference** | `{::ns::rule}` | Recursively references rules in other namespaces |
| **Builtin** | `{#digits}` | Built-in replacers (e.g., `#digits` matches leading ASCII digits) |
| **Placeholder** | `{@name}` | Captures arbitrary text and passes it through as-is in the output |
| **Replacer** | `{%name:ns}` | Special-processing token (e.g., `%item_designation`): strips markers, delegates inner text translation to the specified namespace, then restores markers around the result |

### Placeholder Example

```toml
# Captures fortress name and preserves it in the output
"A new chapter of dwarven history begins here at this place, {@ph}. Strike the earth!" = "矮人历史的新篇章在这里{@ph}开始书写. 开山掘地！"

# Multiple placeholders
"A Dwarven Outpost: You have arrived. After a journey from the Mountainhomes into the forbidding wilderness beyond, your harsh trek has finally ended. Your party of seven is to make an outpost for the glory of all of {@ph}." = "一座矮人前哨: 你们终于抵达了。历经从群山家园出发, 穿越险恶荒野的漫长旅程, 艰苦的跋涉至此告一段落。你们这支七人队伍, 将在此建立一座前哨, 为{@ph}全体子民赢得荣光。"
```

Placeholders start with `@` and only apply within the current rule (not resolved as namespace references). If followed by a Literal token, that Literal acts as a delimiter for capture boundaries; if the last token, it consumes all remaining text.

### Replacer

Replacers start with `%` and perform special preprocessing on the matched text before translation. Currently supported replacers:

| Replacer | Format | Description |
|----------|--------|-------------|
| `%item_designation` | `{%item_designation:ns}` or `{%item_designation:ns:subns}` | Matches item designation markers (quality `-` `+` `*` `=` `☼`, wear `x` `X` `XX`, fire `‼`, ownership `$`, off-site `()`, unclaimed `{}`, decor `<>`, magic `◄►`), strips them, delegates the inner text to the specified namespace for translation, then wraps the translation back with markers |

## Building

DFHack plugins must be compiled within the DFHack source tree. See the [DFHack Compilation Guide — Windows](https://docs.dfhack.org/en/stable/docs/dev/compile/Compile.html#windows) for environment setup.

Additional dependencies (vcpkg manifest): detours, spdlog, tomlplusplus; SDL2_ttf is downloaded during CMake configuration.

### Build Steps

1. Place the plugin directory into the DFHack source tree at `plugins/df_chinese/`
2. In the DFHack repository, edit `build/win64/DF_PATH.txt` to point to the game installation path
3. Open **x64 Native Tools Command Prompt for VS 2022**, then run:

```batch
cd build\win64

generate-MSVC-gui.bat    :: CMake configuration (generates VC2022 project)
build-debug.bat          :: Build
install-debug.bat        :: Install to game directory (includes data/ and SDL2_ttf.dll)
```

> You can also use `build-release.bat` + `install-release.bat` for Release builds.

## Acknowledgments

### Algorithm Reference

The rule-based translation algorithm (RulesetsManager) is inspired by and references the design of:

- [DFI18n/dfi18n](https://github.com/DFI18n/dfi18n) — Source path: `crates/rule_based_translator`, License: MIT

This module is an independent C++ reimplementation with performance optimizations (LRU memoization cache, compile-time lookup tables, heterogeneous lookup) and idiomatic C++ improvements. Development was assisted by [Claude Code](https://claude.ai/code) and [DeepSeek](https://www.deepseek.com/); all code has been manually reviewed.

### Translation Data

Translation data in the TOML rulesets is copyright of the [Dwarf Fortress Chinese Wiki](https://dfzh.huijiwiki.com/) translation group, licensed under [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/deed.en).

### Third-Party Libraries

| Library | License |
|---------|---------|
| [spdlog](https://github.com/gabime/spdlog) | MIT |
| [toml++](https://github.com/marzer/tomlplusplus) | MIT |
| [Microsoft Detours](https://github.com/microsoft/Detours) | MIT |
| [SDL2_ttf](https://github.com/libsdl-org/SDL_ttf) | zlib |
| [Maple Mono NF CN](https://github.com/subframe7536/maple-font) | SIL Open Font License 1.1 |

## License

The source code (C++ source files) of this project is released under the **MIT** license.

Copyright (c) 2026 0x53an

Different components of this project are licensed under different terms:

- `src/` — MIT
- `data/rulesets/` — CC BY-NC 4.0 (Dwarf Fortress Chinese Wiki translation group)
- `data/fonts/` — SIL Open Font License 1.1 (Maple Mono Project Authors)

See the `LICENSE` file in each subdirectory for details.
