# Modules and the build system (v2)

This supersedes [Module_Plan_v1.md](Module_Plan_v1.md), which stays as it is,
with your answers. v1 holds the longer reasoning for the module design; this
version keeps that short and adds three things:
- what your answers needed designed: flattened migrations and config
  sections;
- a review of the build system, with improvements;
- the decisions those raise.

The compiler stays MSVC. The code moved from C++20 to **C++23** on
2026-10-03 (`eb0eb67`, `a5cf7e9`), ahead of MSVC 14.52's stable `/std:c++23`
in November (§9.1). The GCC and C++26 idea was looked into and dropped; §9
keeps a short record of what was found.

Apart from C++23, nothing in the code has changed. The open decisions are
**D15–D31**, in §7, each with a recommendation.

---

## Contents

0. [What changed since v1](#0-what-changed-since-v1)
1. [In short](#1-in-short)
2. [The modules, as settled](#2-the-modules-as-settled)
3. [The database: flattened schemas](#3-the-database-flattened-schemas)
4. [Configuration: a section per module](#4-configuration-a-section-per-module)
5. [The build system](#5-the-build-system)
6. [Order of work](#6-order-of-work)
7. [Open decisions](#7-open-decisions)
8. [Risks](#8-risks)
9. [The language standard: C++26 dropped, C++23 done](#9-the-language-standard-c26-dropped-c23-done)
- [Appendix A: changes to v1's file list](#appendix-a-changes-to-v1s-file-list)

---

## 0. What changed since v1

| Area | v1 | v2 |
|---|---|---|
| Modules (D1, D2) | Proposed | **Settled:** core, voice (built with dectalk or music), dectalk, music, llm, triggers, nicknames, midnight, links, linkstats |
| URL replacement's module name (D13) | `urlrepl` | **`links`**. Its commands stay `/urlrepl` and `/urltoggle` (D26). |
| Database (D3) | A frozen baseline of 1–15, then per-module versions | **Flattened:** each module creates its tables as they are today, as its version 1. Migrations 1–15 stay only to bring an old database up to date, **marked for deletion** until you say (§3). |
| `/voice lab`, `list`, `remove` (D4) | Move to `/tts` | **Settled:** `/tts voices lab`, `list` and `remove` (D27 confirms the names) |
| Config (D8) | Sections per module | **Settled.** Old flat keys warn and are **marked for removal**. Each section's keys are declared once, in a table (§4). |
| README checks (D12) | Commands and panel view names | **Settled**, and widened to config keys, which the tables in §4 make listable |
| D5–D7, D9–D11, D14 | Proposed | **Settled** as recommended |
| Build system | Only the module targets | **Reviewed** (§5). The main finding: DPP is compiled from source in every build folder, up to five times on one machine, and on every CI run. |
| `util/process` | Core | **Music**, its only user (Appendix A) |
| Language | C++20 | **C++23, done** (§9.1). It makes `std::expected` and `std::move_only_function` available to the module work (D31). |

---

## 1. In short

- **The module design from v1 stands**, with your answers applied.
- **Two pieces of new design came out of your answers:**
  - **Flattened schemas (§3).** Each module creates today's tables directly.
    The old migrations only bring an old database up to date, then it is
    *adopted*. A test proves the two routes give the same schema.
  - **Config sections (§4).** Each key is declared once, with its member,
    description and range. That one declaration drives reading, rejecting
    unknown keys, writing the defaults, and the README check.
- **The build system works, but pays a lot for DPP.** DPP is built from
  source by our CMake, so each build folder compiles it again:
  `build` (Debug and Release), `build-asan`, `build-fuzz` and `build-tidy`.
  CI compiles it on every run; its own comment calls this the slowest part.
- **Most of the other awkward parts trace back to that, or to the Visual
  Studio generator's multi-config layout:**
  - three different `conan install`s;
  - a separate folder and shell setup just for clang-tidy;
  - `latibot_map_conan_configs`;
  - the forced `CMAKE_CONFIGURATION_TYPES`.
- **The recommended improvements, in order of payoff (§5.2):**
  - Package DPP with Conan, from the submodule, so it is built once per
    configuration and cached in CI.
  - Link DPP statically, which removes `dpp.dll`.
  - Use Ninja Multi-Config, so clang-tidy reads the main build and its
    folder and extra Conan install go away.
  - Add workflow presets: one command per job.
  - Add an install step that writes the server folder, so the bot you run
    isn't the one you're rebuilding.
  - Add a Conan lockfile.
  - Add interface targets for flags, which come with the module split.
  - Measure, then decide: a precompiled header for DPP, and ASan in CI.
- **The build system goes first** (§6). Every step of the module split then
  builds faster, and the module CMake is written once, on the new setup.

---

## 2. The modules, as settled

```text mermaid
flowchart TB
    core["<b>core</b><br/>/ping /say /status /shutdown /goodbye /bots /logs"]
    voice["<b>voice</b> (built with dectalk or music)<br/>/join /leave /voice start|stop|grace"]
    dectalk["<b>dectalk</b><br/>/speak /tts /chat<br/>/tts voices lab|list|remove"]
    music["<b>music</b><br/>/music"]
    llm["<b>llm</b><br/>/llm /memory"]
    triggers["<b>triggers</b><br/>/trigger"]
    nicknames["<b>nicknames</b><br/>/nickname /nicknames"]
    midnight["<b>midnight</b><br/>/midnight"]
    links["<b>links</b><br/>/urlrepl /urltoggle"]
    linkstats["<b>linkstats</b><br/>/linkstats"]

    voice --> core
    dectalk --> voice
    music --> voice
    llm --> core
    triggers --> core
    nicknames --> core
    midnight --> core
    links --> core
    linkstats --> links
    llm -. "speech capability (optional)" .-> dectalk
```

Settled, as in v1:
- **Interface:** `module`, `host` and `capabilities` (v1 §4).
- **How modules find each other:** the three kinds of dependency, and the
  `speech` capability (v1 §5).
- **Layout:** `src/core/{include,src,tests}` and
  `src/modules/<name>/{include,src,tests}` (D5, D6; v1 §6).
- **Tests:** one test executable per module (D6).
- **Startup:** a generated module list (D7).
- **Data:** a compiled-out module's data is kept (D11).
- **The build matrix:** run locally before core changes, with a core-only
  CI job (D10).
- **Scope:** build time only (D14).

---

## 3. The database: flattened schemas

Your answer to D3: flatten the migrations away, keep the old ones for now,
and mark them for deletion.

### 3.1 What each module owns

Each module's **version 1** is its tables exactly as they are after
migration 15. It is written as one set of `CREATE` statements, with the
`ALTER`s folded in.

| Module | Tables, views and triggers | From migrations |
|---|---|---|
| core | `guild_settings`, `allowed_bots` | 1, 3 |
| triggers | `triggers` (with `respond_to_bots` and `message_flags`), `trigger_responses` | 2, 3, 9 |
| nicknames | `nickname_history` | 4 |
| midnight | `midnight_messages` (with `message_flags`) | 5, 9 |
| links | `url_rules`, `url_opt_outs`, `known_mirrors`, `replacement_messages` (with `kind`), `replacement_links` | 6, 12 |
| linkstats | `reactions`, `reaction_log`, `emojis`, `emoji_aliases`, `backfill_progress`, `emoji_images`, `emoji_copies`, `emote_reactions`, the view `counted_reactions` | 7, 8, 13, 14 |
| dectalk | `tts_voices` | 10 |
| llm | `llm_usage`, `llm_documents`, `llm_memory`, the FTS5 table `llm_memory_search` and its three triggers, `llm_blacklist`, `llm_triggers`, `llm_aliases` | 11, 15 |

Migration 9, which altered two modules' tables at once, disappears.

One table crosses a module line. Linkstats writes image posts into links'
`replacement_messages`, with `kind = 'image'`. That's allowed because
linkstats requires links and goes through links' public
`replacement_store`.

### 3.2 What happens at startup

A new table, `schema_versions(module TEXT PRIMARY KEY, version INTEGER)`,
records each module's version.

| The database found | What happens |
|---|---|
| Has `schema_versions` | Each built module applies its own steps above the version recorded. This is the normal case from now on. |
| Brand new, with no tables | Each built module creates its version 1 and records it. The old migrations never run. |
| Old, at `user_version` 1–15 | **Transitional, marked for deletion.** The old migrations bring it to 15, unedited. Then *adoption* creates `schema_versions` and records version 1 for every module named in §3.1, built or not, since their tables already exist. `user_version` stays at 15 and is never read again. |

Two consequences:
- **A module built in later** finds no row and creates its tables, unless
  adoption already recorded it.
- **Once you say the old migrations can go**, they and the adoption step
  are deleted. A database still below 15 is then refused, with a message
  saying which bot version to run first. Your live database will long since
  have been adopted.

### 3.3 Proving the flattening is exact

A wrong flattened schema would only show up later, on the live database. So
a test builds two databases and compares them:
- one through migrations 1–15;
- one from every module's version 1.

They must match on:
- every table's columns, in order: `PRAGMA table_xinfo`, which covers name,
  type, default, `NOT NULL` and key;
- every index and its columns;
- every foreign key;
- `WITHOUT ROWID`;
- every view, trigger and virtual table, compared by its normalised SQL.

Column order matters. `ALTER TABLE ADD COLUMN` appends at the end, so the
flattened `CREATE TABLE` lists those columns last too. The test lives as
long as the old migrations do (D28).

---

## 4. Configuration: a section per module

Your answer to D8: sections, with a warning for old keys that's marked for
removal.

### 4.1 The shape

```json
{
  "log_level": "info",
  "database_path": "data/bot.db",
  "backup_directory": "data/backups",
  "backups_to_keep": 7,
  "backup_interval_minutes": 360,
  "trusted_guilds": [],
  "trusted_users": [],

  "nicknames": { "track_changes": true },
  "linkstats": { "emoji_copy_min_uses": 1 },
  "llm": { "provider": "anthropic", "model": "claude-haiku-4-5", "spend_cap_daily_usd": 2.0,
           "spend_cap_monthly_usd": 20.0, "tool_rounds": 4 },
  "music": { "ytdlp_path": "", "ffmpeg_path": "", "deno_path": "", "pot_provider_path": "",
             "pot_provider_port": 4416 }
}
```

| Old flat key | New place |
|---|---|
| `track_nicknames` | `nicknames.track_changes` |
| `emoji_copy_min_uses` | `linkstats.emoji_copy_min_uses` |
| `llm_provider`, `llm_model`, `llm_tool_rounds` | `llm.provider`, `llm.model`, `llm.tool_rounds` |
| `spend_cap_daily_usd`, `spend_cap_monthly_usd` | `llm.` the same names |
| `ytdlp_path`, `ffmpeg_path`, `deno_path`, `pot_provider_path`, `pot_provider_port` | `music.` the same names |

How each kind of key is handled:
- **An old flat key** is still read. It logs one warning naming its new
  place. The mapping table carries a `// remove after: you say so` marker,
  like the old migrations.
- **A section for a module that isn't built** gets one warning, then is
  ignored.
- **An unknown key inside a built module's section** stops startup, as now.

**D25** approves the names.

### 4.2 Each key declared once

**Today** a key such as `pot_provider_port` is written in four places:
1. the field in `bootstrap`;
2. the `known_keys` array, whose size is also bumped by hand;
3. its reader, with its own range check;
4. `default_json()`, which `bootstrap.cpp`'s comment asks to be kept in step
   by hand.

It is then written again in `config.example.json` and the docs.

**With sections**, each module has a plain struct and a table that declares
each key once, next to the member it fills:

```cpp
// A sketch.
struct music_config {
    std::filesystem::path ytdlp_path;
    // ...
    int pot_provider_port = 4416;
};

inline const config::section<music_config> music_section{"music", {
    config::key("ytdlp_path", &music_config::ytdlp_path, "Where yt-dlp is. Empty: beside the bot, then PATH."),
    // ...
    config::key("pot_provider_port", &music_config::pot_provider_port, "bgutil's PO token provider's port.",
                config::range{1, 65535}),
}};
```

One generic reader in the core uses the table to:
- read each key as its member's type;
- check ranges;
- refuse keys the table doesn't have;
- write a section's defaults, from a default-constructed struct.

So `default_json()` and `config.example.json` come from the tables, and a
test checks the example file is current. The README check (D12) lists each
module's keys from the same table.

The same pattern fits the per-server settings each module keeps as `*_key`,
default and clamp-function trios, such as `music_volume`, the voice grace
and the speech limits. The LLM's `setting_spec` table already works this
way. Each module adopts it when it moves (D29).

---

## 5. The build system

### 5.1 What it does today, and what that costs

| # | Finding | Where | Cost |
|---|---|---|---|
| B1 | **DPP is compiled by our CMake in every build folder:** `build` (Debug and Release), `build-asan`, `build-fuzz`, `build-tidy`. | `add_subdirectory(third_party/DPP)` in `CMakeLists.txt` | Up to five full DPP builds on one machine. A fresh folder, or a CMake change that rebuilds everything, waits on DPP. |
| B2 | **CI compiles DPP on every run.** The Conan cache holds OpenSSL, zlib, opus, SQLite, CTRE and Catch2, but not DPP. | `ci.yml`: "DPP itself is built from source by our CMake and is not cached; that is the slowest part of a cold run" | Every push pays for DPP, twice: Debug and Release. |
| B3 | **Three different `conan install`s:** Debug and Release in the multi-config layout, plus a single-config Ninja Debug for clang-tidy, which also needs a shell with `vcvars` loaded. | `CMakePresets.json` descriptions, `DevEnvSetup.ps1` | Setup steps you have to remember, or a script to keep working. |
| B4 | **clang-tidy needs its own folder**, because the Visual Studio generator writes no `compile_commands.json`. | `ninja-tidy` preset | B1 and B3 again, just for linting. |
| B5 | **Workarounds for the multi-config layout:** `CMAKE_CONFIGURATION_TYPES` is forced to Debug and Release, and `latibot_map_conan_configs` silences dozens of `IMPORTED_LOCATION` errors from VS Code's CMake Tools. | `CMakeLists.txt`, `cmake/helpers.cmake`, README notes | Code and notes that exist only to keep the generator quiet. |
| B6 | **The bot runs out of the build folder** (`build\bin\<config>`), so building while it runs fails on the locked executable. | README "Starting it" | A rule to remember: don't build Debug while the bot runs. |
| B7 | **Deploying means copying files by hand** from `build\bin\Release`: `LatiBot.exe`, `dpp.dll`, `dectalk.dll`, `dtalk_us.dic`, plus the setup script and its readme. | README "On another machine" | Easy to miss one. |
| B8 | **Flags are applied per target** by `latibot_target_warnings()`, `latibot_target_sanitizers()` and `latibot_copy_runtime_dlls()`. | `src/CMakeLists.txt`, `tests/CMakeLists.txt` | Every new target must remember all three, and the module split adds about 20 targets. |
| B9 | **Running ASan is three chained commands** (configure, build, test), in a VS Code shell task. | `.vscode/tasks.json` | Small, but repeated for each job. |
| B10 | **Dependency versions are pinned, but their dependencies and recipe revisions aren't.** CI's cache key is the hash of `conanfile.py`. | `conanfile.py`, `ci.yml` | A recipe update upstream can change a build without any change here. |
| B11 | **ASan builds turn off the STL's container checks**, because Conan's Catch2 isn't instrumented. | `cmake/warnings.cmake` | Overflows inside `std::vector` and `std::string` go unseen. |

What works well and stays:
- Conan for dependencies.
- DPP and DECtalk as submodules.
- `cmake/dectalk.cmake`.
- Presets as the source of truth.
- Warnings as errors.
- The `tools/` scripts.
- Not pinning a Visual Studio version.
- Parallel compiles: Conan's toolchain already sets `/MP`, which the
  generated projects show.

### 5.2 Improvements

| # | Improvement | Fixes | Cost | Recommended |
|---|---|---|---|---|
| I1 | **DPP as a Conan package, built from our submodule.** A small recipe in the repo (`conan/dpp/conanfile.py`) builds `third_party/DPP` with today's settings: Conan's OpenSSL, zlib and opus; voice on; DPP's formatters; the `WITH_OPENSSL3` fix. Conan then builds DPP once per configuration and every build folder reuses it. CI caches it with the other packages. | B1, B2 | About 80 lines of recipe. Upgrading DPP becomes: move the submodule, then bump the recipe's version. ASan builds don't instrument DPP today either, so sharing it changes nothing there. | **Yes** (D15) |
| I2 | **DPP linked statically.** DPP's CMake has a static build (`dppstatic`). | B7: no `dpp.dll` to copy or forget, and no `latibot_copy_runtime_dlls` for it | Check that it links cleanly on MSVC. DECtalk stays a DLL: it's built from the untouched sources as a DLL, and its dictionary sits beside it. | **Yes, if it links cleanly** (D16) |
| I3 | **Ninja Multi-Config generator** instead of Visual Studio's | B3, B4: `compile_commands.json` from the main build, so the `ninja-tidy` preset, its folder and its Conan install go away. Usually faster to build and to decide nothing changed. | It needs the Visual Studio developer environment. VS Code's CMake Tools sets that up from the preset; the `tools/` scripts already load it for clang-tidy; CI adds one setup step. Two things to check first: that CMake writes `compile_commands.json` for a multi-config Ninja build, and that CMake Tools loads the environment. If either fails, keep the Visual Studio generator and the rest still applies. | **Yes, after those two checks** (D17) |
| I4 | **Workflow presets**: `cmake --workflow --preset asan` configures, builds and tests in one command. | B9 | Presets file version 6, CMake 3.25 or newer, so `cmake_minimum_required` goes from 3.21 to 3.25. | **Yes** (D18) |
| I5 | **An install step.** `cmake --install` writes a complete server folder: `LatiBot.exe`, `dectalk.dll`, `dtalk_us.dic`, `Install-Dependencies.ps1` and its readme. It replaces the `latibot_deploy_files` target and the README's list of files to copy. | B6, B7 | Small. The running bot moves to its own folder; moving `config.json`, `.env` and `data/` there is your call (D19). | **Yes** (D19) |
| I6 | **Interface targets for flags**: `latibot::warnings`, `latibot::sanitizers`, and the runtime-DLL copy. `latibot_module()` links them, so a new target can't forget them. | B8 | Comes with the module split's CMake (v1 §6.3). | **Yes** |
| I7 | **A Conan lockfile** (`conan.lock`), committed. CI's cache key becomes its hash. | B10 | `conan lock create` when changing a dependency. | **Yes** (D20) |
| I8 | **A precompiled header for DPP** (`<dpp/dpp.h>`), shared by our targets | Compile time of our own code | clang-tidy has to ignore MSVC's PCH flags. Worth it only if it saves a real share of a clean build. | **Measure first** (D22) |
| I9 | **ASan in CI**, now that DPP is cached | Catches memory errors on every push, not only when you run the task | One more CI job, about as long as a Debug one. It needs the runner image to have MSVC's ASan component. | **Yes, if the image has it** (D21) |
| I10 | **Dependencies built with ASan** too: a custom Conan setting gives instrumented Catch2 and DPP | B11: the STL's container checks come back | Separate dependency binaries for ASan, and a custom setting to maintain | **Not now** (D23) |

Not recommended:
- **Linking the C runtime statically**, so the server needs no Visual C++
  Redistributable. `Install-Dependencies.ps1` already installs the
  Redistributable, and every dependency would have to be rebuilt to match.
- **Unity builds.** They hide missing includes, which the module boundaries
  are meant to expose.

### 5.3 What the presets become

| Preset | Folder | For | Workflow |
|---|---|---|---|
| `default` | `build` | Debug and Release (Ninja Multi-Config), with `compile_commands.json` | `debug`, `release`: build and test |
| `asan` | `build-asan` | ASan Debug | `asan`: build and test |
| `fuzz` | `build-fuzz` | libFuzzer targets | build only; fuzzers run by hand |
| `core-only` | `build-core` | Every module off (D10) | build and test |

> For organization, lets but all the build folders in a parent 'build' dir. So the build folders are instead like: default: 'build/build/', asan: 'build/build-asan', and etc.

`ninja-tidy` is gone. clang-tidy reads `build/compile_commands.json`.

With I1, a new folder compiles only our own code. DPP comes from Conan's
cache.

---

## 6. Order of work

Every phase ends building, with ASan tests passing and clang-tidy clean.
Nothing is pushed.

| Phase | What | Notes |
|---|---|---|
| **1. Untie the knots** | v1 Phase 1: K1–K9, and `/tts voices` | Inside today's library. Changes no behaviour, apart from the three renamed commands. |
| **2. The build system** | I1 (DPP via Conan), I2 (static DPP), I3 (Ninja Multi-Config, after its checks), I4 (workflow presets), I5 (install step), I7 (lockfile), and CI with the cached DPP and, if possible, ASan (I9). `DevEnvSetup.ps1`, `.vscode/tasks.json`, the README and `docs/testing/README.md` follow. | Behaviour unchanged. Measure a clean build before and after. |
| **3. The module interface** | v1 Phase 2, plus:<br>• the flattened schemas, `schema_versions` and adoption (§3);<br>• the config sections and key tables (§4);<br>• midnight as the first module. | |
| **4. Folders and targets** | v1 Phase 3, on the new build system: `latibot_module()`, with its interface targets (I6) and generated module list | Moves in commits of their own |
| **5. The other modules, one per step** | nicknames, triggers, links, linkstats, voice, dectalk, music, llm. Each brings its settings declarations (§4.2), its README and the sync test. | `bot.cpp` shrinks with each |
| **6. Matrix and docs** | v1 Phase 5, plus the `core-only` CI job | |
| **When you say** | Delete migrations 1–15, the adoption step, the flattening test and the old flat config keys | Marked in the code: `// remove after: you say so` |

**Build system before modules (D24).** Each module step rebuilds a lot,
and every rebuild is faster with DPP cached. The module CMake is then
written once, on Ninja, workflow presets and interface targets, rather than
on today's setup and again after.

---

## 7. Open decisions

### Build system

| # | Question | Recommended | Alternatives |
|---|---|---|---|
| **D15** | How is DPP built? | **As a Conan package, from our submodule, with a recipe in the repo** (I1) | ConanCenter's `dpp` recipe, though its version and options may not match ours; or as now |
| **D16** | DPP static or a DLL? | **Static, if it links cleanly** (I2) | A DLL, as now |
| **D17** | Generator? | **Ninja Multi-Config, after checking compile commands and CMake Tools** (I3) | Visual Studio, as now |
| **D18** | Workflow presets, and CMake 3.25 as the minimum? | **Yes** (I4) | Separate commands, as now |
| **D19** | The install step, and where the live bot runs from | **Install to a folder outside `build/`, such as `out/LatiBot/`, ignored by Git, and run the live bot from there.** Moving `config.json`, `.env` and `data/` there is your call; until then it can keep running from the repo root. | Keep running from `build\bin\<config>` |
| **D20** | A Conan lockfile? | **Yes** (I7) | Version pins only, as now |
| **D21** | ASan in CI? | **Yes, if the runner image has MSVC's ASan component** (I9) | Keep it local |
| **D22** | A precompiled header for DPP? | **Measure a clean build with and without it; adopt it if it saves more than about a quarter** (I8) | Adopt it straight away; or never |
| **D23** | Instrumented dependencies for ASan? | **Not now** (I10) | Now |
| **D24** | Build system before modules? | **Yes** (§6) | Modules first |

> D15: Sure, lets make it a conan package built from the submodule that keeps the current build options.

> D16: Sure, static if links clean is fine.

> D17: Yes, lets switch to ninja if no issues.

> D18: sure.

> D19: yes, that would be better. I will move the config.json and the .env myself.

> D20: Yes, create lock file.

> D21: Sure

> D22: if it saves time on the compile then sure.

> D23: Sure, it can wait.

> D24: Yes.

### Module follow-ups

| # | Question | Recommended | Alternatives |
|---|---|---|---|
| **D25** | Config section names and key moves | **As in the table in §4.1** | Your names |
| **D26** | The `links` module's commands | **Keep `/urlrepl` and `/urltoggle`; only the module is renamed** | `/links ...` |
| **D27** | The custom voice commands | **`/tts voices lab`, `/tts voices list`, `/tts voices remove`** | `/tts voice ...`, or another name |
| **D28** | Flattened schema checks | **The comparison test (§3.3) for as long as the old migrations live; afterwards, a database below 15 is refused with a message** | No comparison test |
| **D29** | Key tables for config and per-server settings | **Yes: a key is declared once, with its member, description and range (§4.2)** | Hand-written readers per section, as now |
| **D30** | `util/process` to music, its only user | **Yes** | Leave it in the core |
| **D31** | `ports::result` → `std::expected`, and `background_task` (v1 §4.5) on `std::move_only_function`, now that the code is C++23 | **Yes: `std::expected` as one mechanical commit in Phase 1; `move_only_function` where Phase 3 needs it** | Keep `ports::result` |

> D25: Yes.

> D26: lets rename the command to links.

> D27: sure.

> D28: sure.

> D29: sure.

> D30: sure.

> D31: sure. 

---

## 8. Risks

v1's risks (§12 there) stand. These are new:

| Risk | How it's handled |
|---|---|
| The DPP recipe builds DPP differently from today: voice off, the wrong OpenSSL, no formatters | Phase 2 compares the configure output: "VOICE support will be enabled", the OpenSSL found, and the flags. The whole test suite runs against the packaged DPP before anything else moves. |
| Static DPP misbehaves on Windows | Found in Phase 2 by the full test suite. The fallback is the DLL, which changes one recipe option. |
| Ninja doesn't get the Visual Studio environment in some shell | The two checks in I3 come first. CMake Tools, the `tools/` scripts and CI each set it up explicitly. |
| A flattened schema differs from the migrated one | The comparison test (§3.3), before any database is adopted. |
| Adoption runs on the live database | It only adds `schema_versions` and rows in it. The backup timer already keeps copies, and the first start can be done right after a fresh backup. |

---

## 9. The language standard: C++26 dropped, C++23 done

Looked into on 2026-10-02, and dropped the same day. Kept here so the
findings don't need finding again:

- **Reflection (P2996) is only in GCC 16**, behind `-std=c++26 -freflection`.
  GCC 16 also has annotations, `template for` and contracts. MSYS2 ships it
  for Windows (UCRT64, 16.2). MSVC has no support; Clang has it only in
  forks.
- **The cost was the tooling, not the language.** On Windows, GCC means
  MinGW, which has:
  - no AddressSanitizer;
  - no libFuzzer;
  - no MSVC debugger.

  And clang-tidy, clangd and possibly clang-format can't handle reflection
  syntax.
- **Smaller traps found:**
  - libstdc++ looks for time zone data relative to its DLL, which affects
    `/midnight`;
  - GCC 15's C23 default rejects DECtalk's K&R-style C;
  - MinGW's runtime DLLs;
  - `env.cpp` choosing by `_MSC_VER`.
- **Where reflection would have earned its place:**
  - config sections;
  - per-server settings;
  - enum names;
  - slash-command options as structs.

  §4.2's key tables get most of the first two without it.
- **Worth revisiting** when MSVC, or a Clang that clang-tidy builds on,
  ships reflection.

### 9.1 C++23, done

On 2026-10-03:

| Commit | What |
|---|---|
| `eb0eb67` | Our code compiles as C++23. CMake and Conan would both ask MSVC for `/std:c++latest`, which adds C++26 draft features, so `CMakeLists.txt` asks for `/std:c++23preview` up to MSVC 14.51 and `/std:c++23` from 14.52, chosen by compiler version. A compiler older than 19.43 is refused at configure. DPP stays C++20 and the Conan packages stay `cppstd=20`. |
| `a5cf7e9` | clang-tidy's `readability-container-contains` covers strings in C++23 mode: 274 `find(x) != npos` checks became `contains(x)`. Four `std::iota` became `std::ranges::iota`, and `decode_board` was split under the complexity limit. |
| `dd936f3` | The fuzz build linked again. It had been broken since `2b899c3`, unrelated to C++23. |

All 938 tests pass under ASan, and clang-tidy reports nothing in `src/` or
`tests/`.

When MSVC 14.52 is installed, the switch moves to `/std:c++23` by itself.
What to remember:
- `std::stacktrace` needs a `.pdb` beside a Release build to name
  functions.
- Don't use `import std;`: CMake's support is experimental, and clang-tidy
  can't read MSVC's module files.
- Consteval propagation and constexpr `<cmath>` only arrive with 14.52.

---

## Appendix A: changes to v1's file list

v1's Appendix A stands, with these changes:

| File | v1 | v2 |
|---|---|---|
| `util/process`, `tests/support/test_child.cpp`, `process_test.cpp` | core | **music** (priv), if D30 |
| everything under "urlrepl" | urlrepl | **links** |
| `commands/voice`'s `lab`, `list`, `remove` | → dectalk, `/tts voices` | the same, settled |
| `db/migrations` | core: baseline 1–15 | core: the runner, `schema_versions`, adoption, and 1–15 **marked for deletion**. Each module's version 1 lives in the module. |
| `config/bootstrap` | split | core keys only, through the core's key table, plus the old-key mapping, marked for removal |
| new: `config/section` (key tables, the generic reader, default writer) | — | core, pub |
| new: `conan/dpp/conanfile.py` | — | the DPP recipe (I1) |
| `CMakePresets.json` | unchanged | the presets of §5.3, with workflows |
| `cmake/helpers.cmake`, `cmake/warnings.cmake` | unchanged | interface targets (I6); `latibot_map_conan_configs` goes if Ninja Multi-Config no longer needs it |
| `src/CMakeLists.txt` `latibot_deploy_files` | unchanged | replaced by `install()` rules (I5) |
