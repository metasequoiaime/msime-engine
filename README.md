# Metasequoia IME Engine（水杉输入法引擎）

[English README](README.en.md)

> **本仓库已废弃。** 引擎代码、词库、协议和公共模块已经整合到 [MSIME 主仓库](https://github.com/metasequoiaime/msime)，不再在这里独立开发、构建或发布。新代码请提交到主仓库；本仓库仅保留历史记录。

<!-- badges:start -->
[![CI](https://img.shields.io/github/actions/workflow/status/metasequoiaime/MSIME-Engine/ci.yml?branch=main&label=CI)](https://github.com/metasequoiaime/MSIME-Engine/actions/workflows/ci.yml)
[![CodeQL](https://img.shields.io/github/actions/workflow/status/metasequoiaime/MSIME-Engine/codeql.yml?branch=main&label=CodeQL)](https://github.com/metasequoiaime/MSIME-Engine/actions/workflows/codeql.yml)
[![Release](https://img.shields.io/github/v/release/metasequoiaime/MSIME-Engine?include_prereleases&label=release)](https://github.com/metasequoiaime/MSIME-Engine/releases)
[![Downloads](https://img.shields.io/github/downloads/metasequoiaime/MSIME-Engine/total?label=downloads)](https://github.com/metasequoiaime/MSIME-Engine/releases)
[![License](https://img.shields.io/github/license/metasequoiaime/MSIME-Engine)](LICENSE)
[![Stars](https://img.shields.io/github/stars/metasequoiaime/MSIME-Engine?style=flat)](https://github.com/metasequoiaime/MSIME-Engine/stargazers)
<!-- badges:end -->

水杉输入法的公共仓库：输入引擎、词库构建、自定义词库、辅助码和公共语音模块。引擎现有头文件路径与 `MetasequoiaIme::Engine` 目标保持兼容，各平台前端继续独立维护。

| 目录 | 职责 |
|---|---|
| `core/`、`quanpin/` 等现有目录 | 输入会话和候选查询 |
| `contracts/` | 共享协议和词库格式 |
| `dictionary/` | 原 MSIME-Dict 的源数据和构建脚本 |
| `dictionary/custom/` | 原 CustomDict 的词条、翻译覆盖和可选专业包 |
| `helpcode/` | 原 HelpCode 的数据和生成脚本 |
| `voice/` | 公共语音录音、识别、文本处理；独立可选构建 |

新平台通过 `<metasequoia/session.h>` 接入；会话隔离、资源目录和兼容接口迁移见
[运行时架构](docs/runtime-architecture.md)。资源清单与校验权威为 [assets 契约](contracts/assets/README.md)。

迁移来源、历史与下游接入顺序见 [合仓说明](docs/consolidation.md)。

General IME engine shared by the Metasequoia IME frontends: [MSIME-Windows](https://github.com/metasequoiaime/MSIME-Windows) (through [its server/ component](https://github.com/metasequoiaime/MSIME-Windows/tree/develop/server)), [MSIME-Apple](https://github.com/metasequoiaime/MSIME-Apple) and [MSIME-Linux](https://github.com/metasequoiaime/MSIME-Linux).

## 构建与测试

CI 在 Ubuntu、macOS 和 Windows 三个平台上跑的就是下面这套，是当前唯一被持续验证的构建方式。

依赖：CMake 3.25+、支持 C++17 的编译器、Boost、fmt、spdlog、SQLite3。

```bash
git clone --recursive https://github.com/metasequoiaime/MSIME-Engine.git
cd MSIME-Engine
```

Ubuntu：

```bash
sudo apt-get install --no-install-recommends -y build-essential cmake libboost-dev libfmt-dev libspdlog-dev libsqlite3-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure --timeout 20
```

macOS：

```bash
brew install boost fmt spdlog
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build --parallel
ctest --test-dir build --output-on-failure --timeout 20
```

Windows：

```powershell
vcpkg install --triplet x64-windows-static
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --timeout 20
```

`ctest` 默认跑 15 个根工程测试目标：`segmentation_contract`、`windows_ipc_contract`、`punctuation_contract`、`platform_api`、`data_path`、`candidate_removal`、`engine_smoke`、`input_session`、`frequency_input_session`、`english_input_session`、`mixed_expressive_input_session`、`jianpin_input_session`、`temporary_input_session`、`local_modes`、`online_input_session`。Voice 目标在启用 Voice 的独立构建中另行注册。

## tests/ 下那套独立工程

`tests/CMakeLists.txt` 是一个**独立的 Windows-only 工程**，产出 `imetest`，源文件是 `tests/src/test_pinyin.cpp`。它**不被根 `CMakeLists.txt` 引入**，所以 CI 不构建也不运行它——`test_pinyin.cpp` 里的用例在 CI 中不会执行，往那个文件加测试不会被自动跑到。

它还要求本机装有 Boost，并使用 MSVC 专有编译选项。Boost 的位置按 `-DBoost_ROOT=...` → 环境变量 `Boost_ROOT` / `BOOST_ROOT` → scoop 默认布局（`%USERPROFILE%\scoop\apps\boost\current`）的顺序解析，vcpkg 走 `VCPKG_ROOT`，都不需要改文件。

```powershell
python .\tests\scripts\prepare_env.py  # 只生成 .clangd，供编辑器补全用；构建本身不需要它
cd .\tests\
.\scripts\llaunch.ps1
```

新增测试请优先加到根 `CMakeLists.txt` 已经登记的那些目标里，那些才会在三个平台上被跑到。

## 数据

引擎读取的词库由同仓 [dictionary/](dictionary/README.md) 构建：`msime.db`（全拼、五笔、快捷短语、日语词表）、`english.db`（英文候选与中英释义）、`others.db`（emoji、颜文字、符号）、`dict_japanese.dat`（日语整句模型）。

候选窗的中英翻译除了 `english.db` 的大表之外，还有一层人工覆盖：`english.db` 同目录下的 `custom_translations.txt`，格式为 `源<Tab>释义`，优先级高于大表（见 `english/english_dictionary.cpp`）。要纠正某条释义时改这里，不要动 ECDICT 生成的大表。

## 词库公共构建入口

```bash
python -m pip install -r dictionary/requirements.txt
python build_profile.py --profile desktop --fetch-references
python build_profile.py --profile desktop --verify
python build_profile.py --profile mobile --source dictionary/out/desktop/msime.db
python build_profile.py --profile mobile --verify
```

产物默认在 `dictionary/out/{desktop,mobile}/`，可用 `--output` 指定绝对路径。数据许可证逐项见 [NOTICE.md](NOTICE.md)。新词库由本仓 `dict-*` Release 发布，平台锁定来源提交及产物摘要；旧 MSIME-Dict Release 和对应历史锁继续保留。

## 跨仓约定

组织边界以 [组织 AGENTS.md](https://github.com/metasequoiaime/.github/blob/main/AGENTS.md) 为准。词库命名权威定义为 `contracts/dictionary/format.json`，同仓 Python 建库与 C++ 查询共同使用，禁止在消费端复制规则。

## 公共语音

[VoiceInput](voice/README.md) 提供 `MetasequoiaIme::Voice`、`VoiceCapture` 和可选 `VoiceWhisper`。Windows 与 Apple 可直接链接相同实现。根构建通过 `METASEQUOIA_IME_BUILD_VOICE=ON` 开启，也可独立构建 `voice/`。平台权限、快捷键、界面和上屏由宿主负责；macOS 示例见 `voice/examples/macos/`。

## 完整运行时资源包

从同一 Engine 提交构建桌面词库后，运行：

```bash
python build_profile.py --profile desktop --fetch-references
python build_assets.py
python build_assets.py --verify
```

默认生成 `build/assets/engine-assets-desktop.zip` 和同名 `.sha256` 校验文件。
可用 `--dictionary-dir` 指定已构建的 desktop 词库目录，`--output` 指定输出目录。
打包前校验词库格式和摘要，并要求词库来源提交与当前 Engine 提交一致。

包内根目录包含四个词库文件、`dict_pinyin.dat`、`custom_translations.txt`、
Mozc 授权文件和 `dictionary-manifest.json`；`helpcodes/` 包含六种运行时辅助码，
`licenses/` 保留 Engine、词库、辅助码和 Google 拼音的来源及许可声明。
`engine-assets-manifest.json` 记录逐文件大小、SHA-256、来源路径、Engine 提交和
Google 拼音子模块提交。辅助码的授权现状仍以随包的 `helpcode-NOTICE.md` 为准。

平台先按固定摘要校验 ZIP，再将数据文件安装到引擎数据目录，并配置辅助码目录。
`custom_translations.txt` 必须与 `english.db` 同目录。包不含可写用户学习数据
`user_dict.dat`，升级时应保留已有用户数据。历史 `pinyin.txt` 已内置到代码。
公共 Voice 没有必需的外置模型；Whisper 模型由宿主提供，导入的独立 Windows
程序的模型、图标和 HTML 不属于这个公共资源包。

`Build dictionaries` 在 push / PR 中构建并校验完整包，上传为
`engine-assets-desktop` artifact；手动发布时追加到现有 `dict-*` Release，原有
独立词库 assets 保留兼容。移动端继续使用 `build_profile.py --profile mobile`。
消费者应锁定已合入默认分支的来源提交及发布包摘要；此入口不会自动修改平台锁文件。

<!-- star-history:start -->
## Star History

<a href="https://star-history.com/#metasequoiaime/MSIME-Engine&Date">
  <img src="https://api.star-history.com/svg?repos=metasequoiaime/MSIME-Engine&type=Date" alt="Star History Chart" width="600">
</a>
<!-- star-history:end -->
