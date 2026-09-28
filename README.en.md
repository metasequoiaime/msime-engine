# Metasequoia IME Engine

[中文 README](README.md) · [Website](https://msime.app)

<!-- badges:start -->
[![CI](https://img.shields.io/github/actions/workflow/status/metasequoiaime/MSIME-Engine/ci.yml?branch=main&label=CI)](https://github.com/metasequoiaime/MSIME-Engine/actions/workflows/ci.yml)
[![CodeQL](https://img.shields.io/github/actions/workflow/status/metasequoiaime/MSIME-Engine/codeql.yml?branch=main&label=CodeQL)](https://github.com/metasequoiaime/MSIME-Engine/actions/workflows/codeql.yml)
[![License](https://img.shields.io/github/license/metasequoiaime/MSIME-Engine)](LICENSE)
[![Stars](https://img.shields.io/github/stars/metasequoiaime/MSIME-Engine?style=flat)](https://github.com/metasequoiaime/MSIME-Engine/stargazers)
<!-- badges:end -->

The shared C++ conversion engine behind Metasequoia IME. The Windows, macOS, iOS and Linux frontends all link this; each supplies its own UI and text injection, and none of them reimplement conversion.

This repository also holds the things the frontends must agree on: the cross-process contracts, the dictionary build pipeline and its source data, the helpcode tables, and the shared voice module.

| Directory | Purpose |
| --- | --- |
| `core/`, `quanpin/`, `shuangpin/`, `schemes/`, `providers/` | Input sessions and candidate lookup |
| `contracts/` | The authoritative IPC wire format, dictionary format and WebView message schema |
| `dictionary/` | Dictionary source data and the build scripts that produce the shipped databases |
| `helpcode/` | Helpcode (形码) tables and generators, including six bundled schemes |
| `voice/` | Recording, WAV encoding, recognition and text cleanup; builds independently |

New platforms integrate through `<metasequoia/session.h>`. See [runtime architecture](docs/runtime-architecture.md).

## Build

CI builds this on Ubuntu, macOS and Windows; the commands below are what it runs. Dependencies are CMake 3.25+, a C++17 compiler, Boost, fmt, spdlog and SQLite3.

```bash
git clone --recursive https://github.com/metasequoiaime/MSIME-Engine.git
cd MSIME-Engine
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure --timeout 20
```

On Windows, pass `-DCMAKE_TOOLCHAIN_FILE` for vcpkg; on macOS, `-DCMAKE_PREFIX_PATH="$(brew --prefix)"`. The Chinese README has the exact per-platform invocations.

`ctest` runs 15 root targets covering segmentation, the IPC and punctuation contracts, candidate removal, sessions and the local query modes. Voice targets are registered separately when the optional Voice build is enabled. `tests/` additionally holds a separate Windows-only project that CI does **not** build — add new tests to the targets registered in the root `CMakeLists.txt` so they actually run.

## Contract rules

The IPC wire format, opcodes, voice framing and WebView message definitions live in `contracts/` and are the single source of truth. Consumers include the same headers; do not restate them on either side. Dictionary naming is defined by `contracts/dictionary/format.json`, used by both the Python builders and the C++ readers.

Cross-repository conventions are in the [organisation AGENTS.md](https://github.com/metasequoiaime/.github/blob/main/AGENTS.md).

## Data and licensing

Dictionaries are built from `dictionary/` and published as `dict-*` releases, which the frontends pin by tag and digest. Per-source licensing is tracked in [NOTICE.md](NOTICE.md), including sources whose terms are still unresolved — read it before redistributing the built databases.

## Licence

GPL-3.0. Vendored `googlepinyinime-rev` and `utfcpp` keep their upstream licences.
