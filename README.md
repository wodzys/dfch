# Dwarf Fortress 中文汉化插件 (df-chinese)

一个为《Dwarf Fortress》（矮人要塞）开发的中文汉化插件，基于 DFHack 框架实现游戏界面文本的实时中文翻译与渲染。

## 项目简介

该插件作为 DFHack 插件运行，通过以下核心技术实现游戏界面汉化：

- **内存直读**：直接访问 Dwarf Fortress 的 `gps.screen` / `gps.screen_top` 内存缓冲区，提取游戏界面上的文本内容
- **SDL2 渲染拦截**：使用 Microsoft Detours 挂钩 SDL2 渲染函数（`SDL_RenderPresent` 等），在渲染管线中注入中文翻译纹理
- **两层级翻译引擎**：CSV 精确匹配词典 + TOML 规则引擎（递归重写），实现从简单词汇到复合文本的完整翻译覆盖
- **TTF 字体渲染**：运行时动态加载 SDL2_ttf，使用中文字体渲染翻译结果并通过 LRU 纹理缓存叠加到游戏画面

## 功能特点

- **实时文本翻译**：每帧自动检测游戏界面上的英文文本并替换为中文
- **两层级翻译系统**：
  - **一级 (Dictionary)**：CSV 精确匹配，支持数字归一化（digits → `{d}`），线程安全（读写锁）
  - **二级 (Rulesets)**：TOML 规则引擎，支持递归重写、跨命名空间引用、循环检测，带 LRU 记忆化缓存
- **句子级文本检测**：字符级分析结合位置、大小写和标点规则，将单字符组合为句子/词语
- **TTF 中文字体渲染**：动态加载 SDL2_ttf.dll，支持按像素高度匹配字体，无需链接时依赖
- **颜色保留**：翻译过程中保留原始文本的颜色信息，支持动态颜色实时更新
- **未翻译文本收集**：自动收集未匹配的文本（FIFO，上限 2000），可一键导出

## 技术架构

### 每帧渲染流程

```
屏幕缓冲区 (DF 内存: gps.screen / gps.screen_top)
       │
       ▼
SentenceDetector.detectSentences()   字符级检测 → 句子/词语分组
       │
       ▼
ScreenManager::processTranslations()
  ├─ 1. DICTIONARY.tryTranslate()    CSV 精确匹配（含数字归一化）
  └─ 2. RULESETS.translate()         TOML 规则引擎（递归重写 + 记忆化缓存）
       │
       ▼
TTFManager::RenderBlendedText()      中文 TTF 渲染 → SDL_Surface → SDL_Texture
       │
       ▼
LRU 纹理缓存 (500 条)               命中直接复用，未命中重新生成
       │
       ▼
SDL_RenderCopy (via g_sdl2 hooks)    中文纹理叠加到游戏画面
```

### 模块架构

| 模块 | 文件 | 职责 |
|------|------|------|
| **插件入口** | `dfch.cpp` | DFHack 生命周期管理、命令处理、快捷键绑定 |
| **Hook 管理** | `hooks.cpp/h`, `sdl2_hooks.cpp/h`, `hook_common.h` | Detours 挂钩/解挂，~50 个 SDL2 函数指针运行时加载 |
| **ScreenManager** | `screen_manager.cpp/h` | 核心调度器：屏幕缓冲区处理、翻译调度、纹理创建与缓存、渲染叠加 |
| **DictManager** | `dict_manager.cpp/h` | CSV 词典管理：精确匹配 + 数字归一化 + 单词级查询，线程安全 |
| **RulesetsManager** | `rulesets_manager.cpp/h` | TOML 规则引擎：递归重写、跨命名空间引用解析、循环检测、LRU 记忆化缓存 |
| **SentenceDetector** | `sentence_detector.cpp/h` | 字符级文本检测：编译期查找表，基于位置/大小写/标点的句子分组 |
| **TTFManager** | `ttf_manager.cpp/h` | SDL2_ttf 运行时加载、字体匹配（二分查找）、文本→Surface 渲染 |
| **LoggerManager** | `logger.cpp/h` | spdlog 异步日志：滚动文件（10MB×3）+ 未翻译文本独立日志 |
| **Config** | `config.cpp/h` | 配置文件解析、路径管理（懒加载） |

### 初始化顺序

```
Hooks::init()
  ├─ LOGGERMANAGER.init()
  ├─ g_sdl2.loadFunc()              运行时加载 ~50 个 SDL2 函数指针
  └─ SCREENMANAGER.init()
       ├─ 1. TTFMANAGER.init()      动态加载 SDL2_ttf.dll，初始化 TTF，加载字体
       ├─ 2. DICTIONARY.init()      加载 CSV 词典 (dfch_dict_exact.csv + dfch_dict_word.csv)
       ├─ 3. RULESETS.init()        加载 TOML 规则集目录 (data/rulesets/)
       └─ 4. SENTENCEDETECTOR.init()

Shutdown 逆序: SentenceDetector → Rulesets → Dict → TTF
```

## 项目结构

```
├── CMakeLists.txt              # CMake 构建文件
├── dfch.cpp                    # 插件主入口
├── hooks.cpp / hooks.h         # SDL 钩子管理
├── sdl2_hooks.cpp / sdl2_hooks.h  # SDL2 函数指针运行时加载
├── hook_common.h               # Hook 宏定义
├── screen_manager.cpp / .h     # 屏幕管理与渲染调度
├── dict_manager.cpp / .h       # CSV 词典管理
├── rulesets_manager.cpp / .h   # TOML 规则引擎
├── sentence_detector.cpp / .h  # 句子检测器
├── ttf_manager.cpp / .h        # TTF 字体管理
├── logger.cpp / .h             # 日志管理
├── config.cpp / .h             # 配置管理
├── 3rdParty/                   # 第三方依赖
│   ├── vcpkg.json              # vcpkg 依赖声明 (detours, spdlog, tomlplusplus)
│   └── SDL2_ttf/               # SDL2_ttf 下载缓存
└── data/                       # 运行时数据文件
    ├── dfch_config.txt         # 配置文件 [KEY:VALUE] 格式
    ├── dfch_dict_exact.csv     # 精确匹配词典 (key,value,align)
    ├── dfch_dict_word.csv      # 单词级词典
    ├── dfch_dict_untrans.csv   # 自动收集的未翻译文本
    ├── fonts/                  # 中文字体文件 (MapleMonoNL-CN, SIL OFL 1.1)
    └── rulesets/               # TOML 规则集 (zh-Hans/)
```

## 安装方法

1. 确保已安装 Dwarf Fortress 和 DFHack
2. 编译插件或使用预编译版本
3. 将编译后的 `dfch.plug.dll` 复制到 DFHack 的 `plugins/` 目录
4. 将 `data/` 目录中的所有文件复制到 `<DF>/hack/data/dfch/`
5. 将 `SDL2_ttf.dll` 放置到游戏根目录（与 `Dwarf Fortress.exe` 同级）
6. 启动游戏，在 DFHack 控制台输入 `enable dfch`

## 使用说明

1. 启动 Dwarf Fortress 和 DFHack
2. 在 DFHack 命令行中输入 `enable dfch` 启动插件
3. 插件会自动开始识别和翻译游戏界面上的文本

### 快捷键

| 快捷键 | 命令 | 功能 |
|--------|------|------|
| `Ctrl-Alt-L` | `dfch save_untrans` | 导出收集的未翻译文本到日志 |
| `Ctrl-Alt-R` | `dfch reload_dicts` | 重新加载 CSV 词典 + TOML 规则集，清空纹理缓存 |
| `Ctrl-Alt-K` | `dfch show_ch` | 切换中文翻译显示开/关 |

## 配置说明

编辑 `data/dfch_config.txt`（`[KEY:VALUE]` 格式）：

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `FONT_FILE` | TTF 字体文件路径（相对于 data/dfch/） | `fonts/MapleMonoNL-CN-Bold.ttf` |
| `LOG_FILE` | 日志文件路径 | `logs/dfch.log` |
| `DICT_EXACT` | 精确匹配词典文件 | `dfch_dict_exact.csv` |
| `DICT_WORD` | 单词级词典文件 | `dfch_dict_word.csv` |

## 词典格式说明

词典文件采用 CSV 格式，三个字段：

| 列 | 说明 |
|----|------|
| 第一列 | 英文原文 |
| 第二列 | 中文翻译，支持 `\n` 换行符和 `\a#FFFFFFHello\a` 颜色标记 |
| 第三列 | 控制字符：空=`l`(左对齐), `c`(居中), `b`(闪烁/实时更新), `s`(含转义字符), `d`=`b`+`c` |

示例：
```csv
"Continue Playing","继续游戏",
"DFHack Launcher","DFHack启动器","c"
"Hello World","你好 \a#FF0000世界\a","s"
```

## 构建

DFHack 插件需在 DFHack 源码树中编译，完整环境搭建参考：

- [DFHack 编译指南 — Windows](https://docs.dfhack.org/en/stable/docs/dev/compile/Compile.html#windows)

本插件额外依赖（vcpkg manifest 管理）：detours、spdlog、tomlplusplus；SDL2_ttf 在 CMake 配置时自动下载。

### 编译步骤

1. 将插件目录放置到 DFHack 源码的 `plugins/df_chinese/`
2. 在 DFHack 仓库中，编辑 `build/win64/DF_PATH.txt` 写入游戏安装路径
3. 打开 **x64 Native Tools Command Prompt for VS 2022**，依次执行：

```batch
cd build\win64

generate-MSVC-gui.bat    :: CMake 配置（生成 VC2022 工程）
build-debug.bat          :: 编译
install-debug.bat        :: 安装到游戏目录（含 data/ 和 SDL2_ttf.dll）
```

> 也可使用 `build-release.bat` + `install-release.bat` 编译 Release 版本。

## 致谢

### 算法参考

本项目的规则翻译算法（RulesetsManager）受以下开源项目启发并参考其设计：

- [DFI18n/dfi18n](https://github.com/DFI18n/dfi18n) — 源码路径: `crates/rule_based_translator`，许可协议: MIT

该模块在 C++ 中独立重新实现，并进行了性能优化（LRU 记忆化缓存、编译期查找表、heterogeneous lookup）与 C++ 惯用法改进。开发过程中由 [Claude Code](https://claude.ai/code) 与 [DeepSeek](https://www.deepseek.com/) 辅助完成，所有代码均经过人工审核。

### 翻译数据

TOML 规则集中的翻译数据版权归属于[矮人要塞中文维基](https://dfzh.huijiwiki.com/)翻译组，在 [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/deed.zh-hans) 协议下授权使用。

### 第三方库

| 库 | 许可 |
|----|------|
| [spdlog](https://github.com/gabime/spdlog) | MIT |
| [toml++](https://github.com/marzer/tomlplusplus) | MIT |
| [Microsoft Detours](https://github.com/microsoft/Detours) | MIT |
| [SDL2_ttf](https://github.com/libsdl-org/SDL_ttf) | zlib |
| [Maple Mono NF CN](https://github.com/subframe7536/maple-font) | SIL Open Font License 1.1 |

## 许可证

本项目源代码（C++ 源码文件）基于 **MIT** 许可证发布。

Copyright (c) 2026 0x53an

本项目的不同组件适用不同的许可协议：

- `src/` — MIT
- `data/rulesets/` — CC BY-NC 4.0（矮人要塞中文维基翻译组）
- `data/fonts/` — SIL Open Font License 1.1（Maple Mono Project Authors）

详见各子目录中的 `LICENSE` 文件。
