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
| MSVC Build Tools | VS 2026 (v145 toolset, MSVC 14.51) | C++ compiler, installed via winget below. Matches what GitHub's `windows-latest` runner ships, so CI and local builds use the same compiler. VS 2022 also works: no preset pins a Visual Studio version |
| [Git](https://git-scm.com/) | any recent | needed for the submodules (and by DPP's CMake) |
| [CMake](https://cmake.org/) | >= 3.21 | required for the `TARGET_RUNTIME_DLLS` generator expression |
| [Conan](https://conan.io/) | 2.x | dependency manager |

If any are missing: `winget install Git.Git`, `winget install Kitware.CMake`,
and `pip install conan` (or `winget install --id Conan.Conan`).

The Build Tools install in step 2 already includes AddressSanitizer, Ninja and
clang-tidy/clang-format. One more is optional:

| Tool | Install | Used by |
|---|---|---|
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
winget install --id Microsoft.VisualStudio.BuildTools --source winget `
  --accept-package-agreements --accept-source-agreements `
  --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --add Microsoft.VisualStudio.Component.VC.ASAN --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset"
```

This installs the C++ build tools (compiler, linker, Windows SDK, CMake,
Ninja) without the Visual Studio IDE, plus AddressSanitizer for the `asan` and
`fuzz` presets and clang-tidy/clang-format for linting.

**`Microsoft.VisualStudio.Component.VC.Tools.x86.x64` has to be listed
explicitly.** In VS 2026 the VCTools *workload* alone no longer pulls in the
x64 compiler component. Without it, `cl.exe` is on disk but the instance is
not registered as having a C++ compiler, and CMake fails with
`could not find any instance of Visual Studio`.

Verify afterwards:

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -all -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
```

This should print the BuildTools install path. If you add components later,
run the installer **elevated**: `--passive` and `--quiet` refuse to prompt for
elevation and exit with code 5007 instead.

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
compiler.version=195
os=Windows
```

`compiler.version=195` is the v145 toolset that ships with VS 2026, and it is
what the CI runner detects too. ConanCenter has no prebuilt binaries for it
yet, so the first `conan install` builds the dependencies from source (about
ten minutes); afterwards they come from `~/.conan2/p/`.

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

## 6. In VS Code

Install the recommended extensions when prompted (`.vscode/extensions.json`),
then **select a configure preset once**: the CMake status bar, or
`CMake: Select Configure Preset` → **msvc**.

That selection is what makes IntelliSense work. CMake Tools supplies cpptools
the include paths, defines and compiler for every file, and it only does so
after it has configured, so before the first selection every `#include` is
underlined in red while the command line builds perfectly.
`.vscode/c_cpp_properties.json` has a fallback for that gap, but it cannot
tell which build of a Conan package this project links, so it is a stopgap
rather than the answer.

## Running

Secrets come from the environment only ([plan §5.1](docs/porting/Porting_Plan_Final.md)): `DISCORD_BOT_TOKEN`,
and optionally `ANTHROPIC_API_KEY` / `OPENAI_API_KEY` for later phases.
Either set them in the shell:

```powershell
$env:DISCORD_BOT_TOKEN = "your-token-here"
.\build\bin\Release\LatiBot.exe
```

or copy [.env.example](.env.example) to `.env` (git-ignored) and fill it in —
`LatiBot.exe` loads it on startup if present, without overriding a variable
the shell already set. Run it from the repo root so it finds both `.env` and
`config.json`.

In VS Code, **F5** does both: `.vscode/launch.json` builds the executable and
runs it from the repo root, so the same `.env` applies.

The bot needs two **privileged intents**, both enabled for the application at
*Discord Developer Portal → your app → Bot → Privileged Gateway Intents*:

- **Message Content.** Without it Discord delivers guild messages with an empty
  `content`, and everything that reads a message — the goodbye phrase, the
  triggers — goes quiet while the slash commands keep working.
- **Server Members.** Without it no nickname change is ever seen. Set
  `"track_nicknames": false` in `config.json` to turn nickname tracking off
  and stop the bot asking for this one.

An intent the application was not granted is not a warning: Discord refuses the
gateway outright and the bot reconnects in a loop. The log says which toggle to
go and find when that happens.

### Logging

Lines go to **stderr**, one per message, timestamped first so they stay
greppable and sort chronologically:

```
2026-09-23T18:01:34Z [info] latios (42) ran /trigger add pattern="420" in guild 999
```

The level is whichever of these is set, last one winning:

| | Level | |
|---|---|---|
| Build default | `debug` in a Debug build, `info` in Release | nothing to configure |
| `config.json` | `"log_level": "debug"` | `trace`, `debug`, `info`, `warn`, `error`, `off` |
| Environment | `LATIBOT_LOG_LEVEL=debug` | also works from `.env`; overrides the file |

`LATIBOT_LOG_LEVEL` is the one to reach for while the bot is running badly,
since it needs no file edit and applies before the configuration is even read.

**What each level is for.** `info` is the running record: every command with
who ran it and what they passed, every reply the bot posts by itself, startup,
shutdown, schema changes and guilds joined. `debug` adds why — which stage
wanted what, which trigger matched and why it stayed quiet, what a panel button
decoded to, how long a command took. `trace` adds the content of every message
the bot sees, and DPP's own gateway chatter.

To keep the output to one file:

```powershell
.\build\bin\Release\LatiBot.exe 2> latibot.log
```

**Colour.** In a terminal, arguments are coloured by their type — numbers,
`true` and `false`, Discord ids, durations — along with the timestamp, the
level, and the `[dpp]` tag on lines forwarded from DPP. The text around them
stays the terminal's own colour. Callers do nothing for this: the logger sees
each argument's type and picks the colour itself, which is why ids are logged
as the snowflake rather than `id.str()`.

| `LATIBOT_LOG_COLOR` | |
|---|---|
| `auto` (default) | colour when stderr is a terminal, so a redirected log stays plain |
| `never` | no colour; `off`, `false` and `0` work too |
| `always` | colour even into a pipe or a file |

The widely used `NO_COLOR` convention is honoured as well, and an explicit
`LATIBOT_LOG_COLOR=always` overrides it. Both work from `.env`.

The colours themselves live in code: `palette` in
[src/core/util/log.hpp](src/core/util/log.hpp) says what each one is, and
`log_style` in the same file says which type gets which. Colouring another
type is one specialisation of `log_style`.

### What it does so far

Phases 1 to 3 are done: the framework, the features that keep records, and URL
replacement with its reaction statistics.
[docs/features/](docs/features/README.md) documents all of this properly —
options, replies and edge cases.

| Command | What it does |
|---|---|
| `/ping` | round trip and gateway latency |
| `/say` | post as the bot, optionally as a reply |
| `/status` | set the bot's presence |
| `/join`, `/leave` | voice channel, following you or a named user |
| `/shutdown` | stop the bot |
| `/goodbye` | show, change or turn off the phrase that stops the bot |
| `/trigger` | `add`, `edit`, `remove`, `list`, `panel` — automatic replies |
| `/bots` | `allow`, `deny`, `list` — which other bots the bot may hear |
| `/nickname` | change somebody's nickname, and record who did it |
| `/nicknames` | every nickname somebody has had here, paginated |
| `/midnight` | `list`, `add`, `edit`, `remove`, `toggle` — a message at midnight |
| `/urlrepl` | `list`, `set`, `remove`, `test`, `panel` — which links get a working preview |
| `/urltoggle` | have your own links left alone, or not |
| `/linkstats` | `top`, `user`, `emojis`, `alias`, `recompute` — reactions on replaced links |

Without being asked: an administrator saying the goodbye phrase stops the bot,
trigger patterns get weighted replies with a per-channel cooldown, links to
sites with poor previews are posted again on a mirror that previews properly,
reactions on those are counted, every nickname change is recorded with
whoever made it, each midnight message posts once per local day, the database
backs itself up on a schedule, and anything the bot lacks permission to do is
reported per server at startup as a warning rather than an error.

URL replacement watches for Discord to actually build the preview rather than
guessing from a timer, tries each mirror twice before moving on, and when none
works leaves a **Retry** button instead of deleting its message. Every link in a
message is handled, spoilers stay spoilered, and each of the Java bot's bugs
here has a test written against it.

Other bots are ignored unless `/bots allow` says otherwise, and even then a
trigger only answers one if it was added with `bots:true`. Hearing and answering
are separate on purpose: the first is a server-wide decision, the second belongs
to each trigger.

Nickname attribution is the part Discord's own audit log gets wrong: it records
the bot for anything the bot did. The history records the person instead, and
says **unknown** rather than guessing when nobody can be named. Dropping the
Java bot's `nicknames.json` into `data/` imports years of history, timezones
and all; its `UrlReplacements.txt` in the same place becomes each server's URL
rules, once. `/linkstats recompute` then reads years of channel history back
into the reaction statistics.

**Still to come** — [the plan](docs/porting/Porting_Plan_Final.md), and
[what each feature should do](docs/features/Planned.md):

| Phase | Features |
|---|---|
| 4 | DECtalk speech, `/speak`, custom voices, voice sessions |
| 5 | the LLM: replies, memory, personality, advanced triggers |

## Testing

Unit tests use [Catch2 v3](https://github.com/catchorg/Catch2) and run through
CTest:

```powershell
ctest --preset debug      # or: ctest --test-dir build -C Debug --output-on-failure
```

In VS Code, the TestMate C++ extension puts the whole suite under one node in
the **Testing** sidebar, grouped by component tag and then by source file, and
rebuilds the Debug tests before running them.

[docs/testing/](docs/testing/README.md) covers the strategy, the tag
conventions and the VS Code setup;
[docs/testing/Test_Catalog.md](docs/testing/Test_Catalog.md) lists every test
by component and is generated by `tools/Update-TestCatalog.ps1`.

Tests live in `tests/`, and the bot's code is in the `latibot_core` static
library so both `LatiBot.exe` and `latibot_tests.exe` link the same code.
Each test carries one component tag (`[db]`, `[config]`, `[commands]`,
`[discord]`, `[ports]`, `[log]`, `[util]`) plus optional traits (`[coro]`,
`[threads]`, `[fs]`), which select subsets:

```powershell
.\build\bin\Debug\latibot_tests.exe "[db]"           # one component
.\build\bin\Debug\latibot_tests.exe "[config]~[fs]"  # config, minus file I/O
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
.\build-fuzz\bin\Debug\fuzz_url_scan.exe -max_total_time=60      # also fuzz_text, fuzz_legacy_parser

# clang-tidy and clang-format, through the scripts in tools/
pwsh tools/Invoke-ClangTidy.ps1                  # src/
pwsh tools/Invoke-ClangTidy.ps1 -IncludeTests    # src/ and tests/
pwsh tools/Invoke-ClangFormat.ps1 -Check         # report, change nothing
```

The clang-tidy script needs a Ninja-flavoured dependency install once, since
the Visual Studio generator cannot produce `compile_commands.json`. It says so
if the install is missing rather than starting a long build on its own:

```powershell
conan install . --build=missing -s build_type=Debug -c tools.cmake.cmaketoolchain:generator=Ninja
```

### VS Code tasks

`.vscode/tasks.json` wraps the common ones, so they are available from
**Run Task** with clickable output: build and test per configuration, the
AddressSanitizer run, clang-tidy over `src/` or a single file, clang-format,
the test catalog generator, the Conan install and a short fuzz run. They call
the same commands as above; nothing is exclusive to the editor.

## Project layout

```
CMakeLists.txt      top-level build definition (also configures the DPP submodule)
CMakePresets.json   msvc / asan / fuzz / ninja-tidy presets
conanfile.py        Conan recipe: openssl, zlib, opus, sqlite3, ctre, catch2
cmake/              warnings, sanitizers and shared helpers
tools/              catalog generator, clang-tidy and clang-format wrappers
.vscode/            tasks, IntelliSense and the grouped test tree
src/main.cpp        entry point
src/core/           the bot itself, built as the latibot_core static library
tests/              Catch2 tests, mocks, fixtures and fuzz targets
docs/features/      what the bot does, and what it will do
docs/porting/       the porting plan (Porting_Plan_Final.md) and its drafts
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
- **Conan's OpenSSL ships no CA bundle.** Its `OPENSSLDIR` is an empty
  directory in the package cache, so every TLS handshake fails verification,
  which DPP reports as `Malformed HTTP response` — the request never gets far
  enough to have a status. `src/core/util/ca_certificates.cpp` fixes this at
  startup by exporting the Windows root store to `data/ca-bundle.pem` and
  setting `SSL_CERT_FILE`, which is what OpenSSL's default verify paths read.
  The roots therefore stay whatever Windows Update says they are, and nothing
  has to be vendored or installed alongside. Set `SSL_CERT_FILE` yourself to
  override it.
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
- **No preset names a Visual Studio version.** CMake picks the installed one.
  Pinning it is what broke the first CI run: GitHub's Windows image moved from
  VS 2022 to VS 2026, and a pinned generator cannot find an instance.
- **The `asan` preset disables the STL's container annotations**
  (`_DISABLE_STL_ANNOTATION`). Conan's Catch2 is not instrumented, and mixing
  annotated and unannotated objects fails to link with `LNK2038`. The cost is
  overflow detection *inside* std containers; everything else ASan checks
  still works. It also copies `clang_rt.asan_dynamic-x86_64.dll` next to the
  binaries, since MSVC links that runtime dynamically and it is not on `PATH`.
- **clang-tidy must be Clang 20 or newer** for MSVC 14.51's headers; older
  ones stop at `error STL1000: Unexpected compiler version`. The copy in the
  VS 2026 install (LLVM 22) is new enough; the one in VS 2022 is not, so use
  the 2026 path:
  `& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools\VC\Tools\Llvm\x64\bin\clang-tidy.exe"`.
- **`IMPORTED_LOCATION not set for imported target "CONAN_LIB::…_RELEASE"
  configuration "Debug"`** (and the reverse), dozens of times, during a
  configure of `build/`. This comes from CMake 4.4 answering the codemodel
  query VS Code's CMake Tools leaves in `build/.cmake/api/v1/query/`: to
  describe every target for both configurations it asks each of Conan's
  per-configuration libraries where it lives in the *other* configuration.
  The generated projects are still correct and configure exits 0, but the
  first `cmake --build` after a `CMakeLists.txt` edit can finish without
  compiling newly added files. Build again. A fresh build folder without the
  query configures cleanly.
- **To upgrade DPP:** `git -C third_party/DPP fetch --depth 1 origin tag vX.Y.Z`,
  check out that tag, commit the submodule change, then rebuild.
- **To add a dependency:** add it to `requirements()` in `conanfile.py`, re-run
  both `conan install` commands, then reconfigure.
