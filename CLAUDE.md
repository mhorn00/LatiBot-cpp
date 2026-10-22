# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

LatiBot is a Discord bot ported from Java to C++23 (MSVC on Windows, GCC 14+ on Linux), using DPP (built from the `third_party/DPP` submodule as a Conan package) and Conan 2 for everything else. `java-reference/` is the original Java source, kept only for reference during the port. It is not built.

## Build and test

Conan is not installed system-wide: it lives in `.venv`, at the version `requirements.txt` pins. `tools/DevEnvSetup.ps1` creates it, or do it by hand with `python -m venv .venv` and `.venv\Scripts\python -m pip install -r requirements.txt`. Activate it (`.venv\Scripts\Activate.ps1`) or call `.venv\Scripts\conan` directly. Scripts and tasks always use it by its path.

Dependencies have to be installed first, once per configuration. They go into `build/conan`, which every build folder shares:

```powershell
conan export conan/dpp
conan install . --build=missing -s build_type=Debug   -s compiler.cppstd=20 --lockfile-partial
conan install . --build=missing -s build_type=Release -s compiler.cppstd=20 --lockfile-partial
```

`-s compiler.cppstd=20` and `--lockfile-partial` are both required (the README explains why). On a fresh Windows machine, `tools/DevEnvSetup.ps1` does all of this.

On Windows, the Ninja presets need the Visual Studio developer environment: run them in a Developer PowerShell, or prefix the command with `build\conan\conanbuild.bat &&` in `cmd`.

```powershell
cmake --workflow --preset debug        # configure + build + test (Debug); also: release, asan, core-only
cmake --preset default                 # configure only -> build/build
cmake --build --preset debug           # build
ctest --preset debug                   # test (skips [live] tests)
ctest --preset debug -R "triggers: "   # one module's tests (ctest names are prefixed "<module>: ")
```

On Linux, use the `linux-*` twins of the presets (`linux-debug`, `linux-release`, `linux-asan`, `linux-core-only`), which build in `build/linux`.

- **Binaries** go to `build/build/bin/<Config>/`. There is one Catch2 executable per module, `latibot_<name>_tests`, plus `latibot_core_tests`. To run a single test, run the executable directly with a Catch2 filter: `latibot_core_tests.exe "test name"` or `"[tag]"`.
- **`[live]` tests** reach real programs or services. They are hidden (`[.]`) and filtered out of the presets; run them by tag.
- **Fuzzers** (`tests/fuzz`) build only with the `fuzz` preset.
- **Formatting:** `pwsh tools/Invoke-ClangFormat.ps1`. CI runs it with `-Check` and fails on unformatted files. The style is Google-based, with 4-space indents and 140 columns (`.clang-format`).
- **Lint:** `pwsh tools/Invoke-ClangTidy.ps1 [-IncludeTests] [-Path file] [-Fix]`. It runs locally only, not in CI.
- **Module matrix:** `pwsh tools/Test-ModuleMatrix.ps1 [-Only <module>]` builds and tests with every module on, with the core alone, and with each module left out in turn. Run it before changing `src/core/include/`, a capability, or a module that others require.

CI (`.github/workflows/ci.yml`) builds Debug, Release, ASan and core-only, on both Windows and Linux.

## Architecture

The bot is split into a **core** and **feature modules**, each a static library. The design and its reasoning are in `docs/modules/Module_Plan_Final.md`.

- `src/app/main.cpp`: the executable.
- `src/core/`: the host. Public headers are in `include/core/`, private ones in `src/`, and its tests in `tests/`. The core holds config, the SQLite database and migrations, the command registry, the message pipeline, panels/UI, logging, and the **ports** (`core/ports/`: `discord_gateway`, `http_client`, `clock`, ...), which are narrow interfaces over DPP so that tests can use the mocks in `tests/mocks/`.
- `src/modules/<name>/`: voice, dectalk, music, llm, triggers, nicknames, midnight, links, linkstats. Each is declared with `latibot_module(...)` (`cmake/modules.cmake`), has a switch `LATIBOT_WITH_<NAME>`, and is added in dependency order in `src/modules/CMakeLists.txt`. Voice has no switch: it is built when dectalk or music is.

**Module lifecycle** (`core/modules/module.hpp`). CMake generates `enabled_modules.cpp`, which calls each enabled module's `make_module(host&)` in dependency order. Startup then goes:

1. The bot applies each module's `schema()` (migrations recorded per module in `schema_versions`; shipped steps are never edited).
2. Every module's `offer(capability_registry&)` runs.
3. Every module's `start(host&)` runs. Here a module registers its slash commands, message stages, DPP listeners, timers, panels, intents and permissions through the host.

Modules are destroyed in reverse order.

**Modules depend on each other in only three ways:**
- **Requires:** links its public headers, and CMake refuses the configure if the required module is off (linkstats→links, dectalk/music→voice).
- **Capability:** an interface in `core/capabilities/` (`speech`, `link_replacements`) that one module offers and another looks up with `find<T>()`, which may return null.
- **Bridge library:** for an integration that needs both modules' own types.

A module may include only the core's public headers and those of the modules it requires. The include directories enforce this, so reaching into another module's `src/` does not compile, and each module's test executable sees only what that module can see.

**Message pipeline** (`src/core/docs/Message_Pipeline.md`, `core/events/stage_order.hpp`). Incoming messages pass through stages at named positions: stop (goodbye) 100, rewrite (links) 200, reply (triggers) 300, model (llm) 400. Two stages claiming the same position stop startup. Stages are pure decision functions that return actions (`send_message`, `stop_bot`, `background_task`), and the shell carries the actions out, which keeps the stages testable.

**Configuration.** `config.json`, in the working directory, has one section per module. Each key is declared once, and an unknown key or a wrong type stops startup. Secrets (`DISCORD_BOT_TOKEN`, `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`) come only from the environment or `.env`. Per-server settings live in the database (`guild_settings`).

## Tests that keep docs in sync

- Each module's `README.md` has a "What it owns" table. Rows marked ✓ (commands, panels, stages, events, timers, tables, config keys) are checked against what the module actually registers by `tests/<name>_module_test.cpp`, so update the README when you add any of those.
- A test keeps `config.example.json` identical to the defaults the bot writes.
- Feature specs live beside their code (`src/core/docs/`, `src/modules/<name>/docs/`). The user-facing command reference is `docs/User_Guide.md`.

## Conventions

- Trailing-return-type style (`auto f() -> T`), `snake_case` names, and plural namespaces (`latibot::modules`, `commands`, `ports`, `capabilities`).
- Log Discord IDs as the snowflake itself, not `id.str()`: the logger colours each argument by its type.
- Commit messages are `area: lowercase description in plain prose` (for example `llm: conversation mode`, `music: ...`, `build: ...`).
