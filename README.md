# LatiBot

C++ port of a Discord bot (originally written in Java), built with CMake + MSVC
as C++20. It uses [DPP (D++)](https://dpp.dev/) for the Discord API, built from
source as a git submodule, and Conan 2 for the remaining dependencies.

The `java-reference/` folder holds the original Java source purely for
reference during the port; it is not part of the C++ build.

## Prerequisites

| Tool | Version used | Notes |
|---|---|---|
| Windows | 10 (1903+) / 11 | x64. 1903+ is needed for C++20 `<chrono>` time zones |
| [winget](https://learn.microsoft.com/windows/package-manager/winget/) | any recent | used to install MSVC Build Tools |
| MSVC Build Tools | VS 2022 (v143 toolset) | C++ compiler, installed via winget below |
| [Git](https://git-scm.com/) | any recent | needed for the submodules (and by DPP's CMake) |
| [CMake](https://cmake.org/) | >= 3.21 | required for the `TARGET_RUNTIME_DLLS` generator expression |
| [Conan](https://conan.io/) | 2.x | dependency manager |

If any are missing: `winget install Git.Git`, `winget install Kitware.CMake`,
and `pip install conan` (or `winget install --id Conan.Conan`).

Optional, for the analysis and sanitizer builds:

| Tool | Install | Used by |
|---|---|---|
| AddressSanitizer component | VS Installer → Build Tools → Individual components → **C++ AddressSanitizer** | the `asan` and `fuzz` presets |
| Ninja | bundled with the Build Tools CMake component, or `winget install Ninja-build.Ninja` | the `ninja-tidy` preset |
| clang-tidy / clang-format | bundled with the Build Tools LLVM component | linting and formatting |
| OpenCppCoverage | `winget install OpenCppCoverage.OpenCppCoverage` | local coverage reports |

## 1. Clone with submodules

DPP and DECtalk live in `third_party/` as git submodules:

```powershell
git clone --recursive <repo-url>
# or, in an existing clone:
git submodule update --init --recursive
```

## 2. Install MSVC Build Tools

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --source winget `
  --accept-package-agreements --accept-source-agreements `
  --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
```

This installs the Visual C++ build tools workload (compiler, linker, Windows
SDK) without the full Visual Studio IDE. Verify it afterwards:

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -all -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
```

This should print the BuildTools install path.

## 3. Set up the Conan profile

```powershell
conan profile detect --force
```

This writes `~/.conan2/profiles/default`. **Edit it and set
`compiler.cppstd=20`**. The detected default is usually `14`, and this project
is C++20:

```
[settings]
arch=x86_64
build_type=Release
compiler=msvc
compiler.cppstd=20
compiler.runtime=dynamic
compiler.version=194
os=Windows
```

The default `conancenter` remote is all that's needed.

## 4. Install dependencies

MSVC is a multi-config toolchain, so install both configurations into the same
`build/generators` folder:

```powershell
conan install . --build=missing -s build_type=Release
conan install . --build=missing -s build_type=Debug
```

This provides OpenSSL, zlib and opus (DPP's dependencies), SQLite with FTS5,
CTRE, and Catch2 for the tests. The first run can take a few minutes; packages
are cached in `~/.conan2/p/` afterwards.

## 5. Configure and build

```powershell
cmake --preset msvc
cmake --build build --config Debug
cmake --build build --config Release
```

The first build compiles DPP from source, which takes a while (10+ minutes,
longer for Release). Later builds only recompile what changed.

Binaries land in `build\bin\Debug\` and `build\bin\Release\`. A post-build step
copies `dpp.dll` and the other runtime DLLs next to `LatiBot.exe`, so it runs
without extra `PATH` setup.

## Running

The bot reads its token from the `DISCORD_BOT_TOKEN` environment variable:

```powershell
$env:DISCORD_BOT_TOKEN = "your-token-here"
.\build\bin\Release\LatiBot.exe
```

## Testing

Unit tests use [Catch2 v3](https://github.com/catchorg/Catch2) and run through
CTest:

```powershell
ctest --preset debug      # or: ctest --test-dir build -C Debug --output-on-failure
```

In VS Code, the CMake Tools extension lists every test case individually in the
**Testing** sidebar.

Tests live in `tests/`, and the bot's code is in the `latibot_core` static
library so both `LatiBot.exe` and `latibot_tests.exe` link the same code.
Tags (`[url]`, `[db]`, `[golden]`, `[live]`) select subsets:

```powershell
.\build\bin\Debug\latibot_tests.exe "[text]"
```

`[live]` tests need a test bot token in `LATIBOT_TEST_TOKEN` and are excluded
from the test presets.

### Build presets

| Preset | What it's for |
|---|---|
| `msvc` | the normal Debug/Release build |
| `asan` | our targets with `/fsanitize=address` (needs the ASan component) |
| `fuzz` | libFuzzer targets in `tests/fuzz` (needs the ASan component) |
| `ninja-tidy` | generates `compile_commands.json` for clang-tidy |

```powershell
# AddressSanitizer
cmake --preset asan; cmake --build build-asan --config Debug; ctest --preset asan

# Fuzzing (runs until stopped; -max_total_time=60 for a short run)
cmake --preset fuzz; cmake --build build-fuzz --config Debug
.\build-fuzz\bin\Debug\fuzz_text.exe -max_total_time=60

# clang-tidy: needs a Ninja-flavoured dependency install and a shell with
# the MSVC environment loaded
conan install . --build=missing -s build_type=Debug -c tools.cmake.cmaketoolchain:generator=Ninja
cmd /c "build\Debug\generators\conanbuild.bat && cmake --preset ninja-tidy && cmake --build build-tidy"
clang-tidy -p build-tidy (Get-ChildItem -Recurse src -Filter *.cpp).FullName
```

## Project layout

```
CMakeLists.txt      top-level build definition (also configures the DPP submodule)
CMakePresets.json   msvc / asan / fuzz / ninja-tidy presets
conanfile.py        Conan recipe: openssl, zlib, opus, sqlite3, ctre, catch2
cmake/              warnings, sanitizers and shared helpers
src/main.cpp        entry point
src/core/           the bot itself, built as the latibot_core static library
tests/              Catch2 tests, mocks, fixtures and fuzz targets
docs/porting/       porting plans (the implementation follows Stage 2 v4)
docs/ideas/         parked ideas
third_party/DPP     submodule: DPP v10.1.6, built from source
third_party/dectalk submodule: DECtalk (develop branch); not wired into the build yet
build/              build output (git-ignored)
data/               runtime database, backups and import files (git-ignored)
```

The original Java bot lives in `java-reference/` locally. It is deliberately
**not** tracked in git: it is large and contains personal data.

## Notes / gotchas

- **Why DPP is built from source:** it enables C++20 coroutines, and it avoids
  the outdated OpenSSL 1.1.1 / zlib 1.2.11 binaries DPP bundles for Windows.
  `CMakeLists.txt` sets `CONAN_EXPORTED=ON` so DPP uses the Conan-provided
  packages instead.
- **Voice support is forced on** (`HAVE_OPUS_OPUS_H`, `OPUS_LIBRARIES`). DPP only
  auto-detects opus on Windows when it uses its bundled binaries. Configure
  output should include `VOICE support will be enabled`.
- **`WITH_OPENSSL3` on the `hpke` target:** DPP hardcodes `OPENSSL_VERSION` to
  1.1.1f on Windows, which makes its `mlspp` dependency pick an OpenSSL 1.1
  code path that doesn't compile against OpenSSL 3. Recheck this when
  upgrading DPP.
- **"CMAKE_CXX_STANDARD ... modified to 17" warnings** during configure are
  expected. DPP sets 17 in its own scope but compiles the `dpp` target as C++20.
- **`CMAKE_CONFIGURATION_TYPES` is limited to `Debug;Release`.** Without that,
  the Visual Studio generator also expects `MinSizeRel`/`RelWithDebInfo`, which
  Conan hasn't installed.
- **`CMakeUserPresets.json` is generated by Conan and machine-specific.** It's
  git-ignored and recreated by steps 4/5. Our own `CMakePresets.json` is
  checked in and points at the toolchain file Conan generates.
- **DPP's headers are marked as system headers** (`SYSTEM TRUE` on the `dpp`
  target, plus `/external:W0`). Our code builds with `/W4 /WX`, and DPP's
  public headers produce C4251/C4100 warnings that we can't fix from here.
- **Warnings are errors by default** for our targets
  (`-DLATIBOT_WARNINGS_AS_ERRORS=OFF` turns that off while experimenting).
- **To upgrade DPP:** `git -C third_party/DPP fetch --depth 1 origin tag vX.Y.Z`,
  check out that tag, commit the submodule change, then rebuild.
- **To add a dependency:** add it to `requirements()` in `conanfile.py`, re-run
  both `conan install` commands, then reconfigure.
