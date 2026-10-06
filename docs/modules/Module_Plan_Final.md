# Modules and the build system: final plan

The plan to build from. It consolidates
[Module_Plan_v1.md](Module_Plan_v1.md) and
[Module_Plan_v2.md](Module_Plan_v2.md), which stay as they are with your
answers. They hold the longer reasoning and the options that were turned
down; this document holds what was decided.

Every decision is settled (§13). Progress is tracked in §12.

---

## Contents

1. [Goals](#1-goals)
2. [Where the code started](#2-where-the-code-started)
3. [The modules](#3-the-modules)
4. [What a module is](#4-what-a-module-is)
5. [Modules using each other](#5-modules-using-each-other)
6. [Folders, headers and CMake](#6-folders-headers-and-cmake)
7. [The database](#7-the-database)
8. [Configuration](#8-configuration)
9. [The build system](#9-the-build-system)
10. [Tests](#10-tests)
11. [Docs](#11-docs)
12. [Order of work and progress](#12-order-of-work-and-progress)
13. [Decisions](#13-decisions)
14. [Risks](#14-risks)
- [Appendix A: every file, and where it goes](#appendix-a-every-file-and-where-it-goes)

---

## 1. Goals

| # | Requirement |
|---|---|
| R1 | The bot compiles, and works, with any set of feature modules left out. |
| R2 | A module registers its own commands. |
| R3 | A module hooks into the core through callbacks where it needs to. |
| R4 | The core offers public headers for modules to use. |
| R5 | When two modules are both present, one can use the other. When either is absent, neither breaks. Example: the LLM speaking through DECtalk. |
| R6 | Additions and their tests are easier to review: a change to one feature touches one folder, with its tests beside it and a README saying everything it owns. |
| R7 | The build system is improved while it is being changed anyway. |

| # | Constraint |
|---|---|
| N1 | The live `bot.db` keeps working. Migrations 1–15 stay, unedited, until you say they can go (§7). |
| N2 | The `config.json` on the server keeps working. Old keys warn and are marked for removal (§8). |
| N3 | Every step lands building, with `ctest` passing under ASan and clang-tidy clean. |
| N4 | A build with every module on behaves as today, apart from the renamed commands (§3.3). |
| N5 | Modules are chosen at build time and linked in. Nothing is loaded at run time. |

Stays as it is:
- **Toolchain:** MSVC and C++23. The code moved to C++23 on 2026-10-03
  (`eb0eb67`), with `/std:c++23preview` until MSVC 14.52, then
  `/std:c++23`.
- **Scope:** modules are chosen at build time only; there are no per-server
  module switches (D14).

---

## 2. Where the code started

### 2.1 One library, one shell

Everything is in `latibot_core`, one static library of 197 files under
`src/core/`. Its folders are layers, not features. `latibot::bot` (`bot.cpp`,
about 1,300 lines, 65 members) wires every feature to every Discord event by
hand.

### 2.2 The knots

These are the places where one feature reaches into another, from the
`#include` graph. Each is untied before anything is switched off.

| # | From → to | Why | Becomes |
|---|---|---|---|
| K1 | `events/message_pipeline.hpp` → `events/url_rules.hpp` | The action list is a fixed `variant<send_message, stop_bot, replace_links, ask_llm>`. | Generic actions (§4.5) |
| K2 | `config/bootstrap.cpp` → `llm/models.hpp` | `llm_model` is checked while `config.json` is read. | The LLM checks its own section (§8) |
| K3 | `llm/advanced_triggers.hpp` → `events/triggers.hpp` | Advanced triggers reuse the triggers' matching. | Matching moves to `util/match` |
| K4 | `llm/responder.cpp` → speech queue, sanitizer, PCM, TTS port, `commands/speak.hpp` | Spoken replies | The `speech` capability (§5.3) |
| K5 | `llm/prompt.cpp` → `audio/voice_params.hpp` | The prompt lists DECtalk's voices. | `speech::guide_for_model` |
| K6 | `llm/stage.cpp` → `events/voice_sessions.hpp` | Whether replies are spoken | `speech::speaks_in` |
| K7 | `commands/music.cpp` → `commands/speak.hpp` | `plan_speak` decides where music plays. | Moves to voice |
| K8 | `commands/voice.cpp` → `commands/basic.hpp`, `commands/voice_lab.hpp` | `/voice` mixes sessions with DECtalk's custom voices. | Custom voices move to `/tts voices` |
| K9 | `bot::on_voice_state` → speech queue, music player, mixer, auto-leave | Leaving voice tidies four features at once. | Voice's `on_left` hook |

---

## 3. The modules

```text mermaid
flowchart TB
    core["<b>core</b><br/>/ping /say /status /shutdown /goodbye /bots /logs"]
    voice["<b>voice</b> (built with dectalk or music)<br/>/join /leave /voice start|stop|grace"]
    dectalk["<b>dectalk</b><br/>/speak /tts /chat<br/>/tts voices lab|list|delete"]
    music["<b>music</b><br/>/music"]
    llm["<b>llm</b><br/>/llm /memory"]
    triggers["<b>triggers</b><br/>/trigger"]
    nicknames["<b>nicknames</b><br/>/nickname /nicknames"]
    midnight["<b>midnight</b><br/>/midnight"]
    links["<b>links</b><br/>/links /urltoggle"]
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

Solid arrows are **requires**: the module is built against, linked with and
switched on together with what it points at. The dotted arrow is an
optional **capability**: used when both modules are present, absent
otherwise.

### 3.1 What each owns

| Module | Owns | Requires | Uses if present |
|---|---|---|---|
| **core** | `/ping` `/say` `/status` `/shutdown` `/goodbye` `/bots` `/logs`; the pipeline and the goodbye phrase; panel routing; permission warnings; the log channel; backups; the module host | — | — |
| **voice** | `/join` `/leave` `/voice start`, `stop`, `grace`; the mixer; voice sessions; auto-leave. It has no switch of its own: it is built when dectalk or music is. | core | — |
| **dectalk** | `/speak` `/tts` `/chat`; `/tts voices lab`, `list`, `delete`; DECtalk itself, which is only built with this module | voice | — |
| **music** | `/music`; yt-dlp, ffmpeg, cookies, the PO token provider; `util/process` | voice | — |
| **llm** | `/llm` `/memory`; answering, advanced triggers, memory, aliases, spend | core | speech |
| **triggers** | `/trigger`; trigger replies | core | — |
| **nicknames** | `/nickname` `/nicknames`; history, audit attribution, the Java import, the Server Members intent | core | — |
| **midnight** | `/midnight`; the daily message | core | — |
| **links** | `/links` (was `/urlrepl`) and `/urltoggle`; replacing links, watching previews, Retry, the Java rules import | core | — |
| **linkstats** | `/linkstats`; reaction counts, image posts, emote messages, the recompute, emoji copies | links | — |

### 3.2 Why the core holds what it holds

- **`/ping`, `/say`, `/status`, `/shutdown` and `/goodbye`** are small, and
  a bot without them isn't one you can run.
- **`/bots` and the allowlist** decide whether a bot's message reaches the
  pipeline at all, which every module that reads messages relies on.
- **The log channel and `/logs`** are how you run the bot.
- **The pipeline, panels, registry, database, ports and config** are the
  extension points themselves.

### 3.3 Commands that change

| Before | After | Why |
|---|---|---|
| `/voice lab`, `/voice list`, `/voice delete` | `/tts voices lab`, `/tts voices list`, `/tts voices delete` | `/voice` belongs to voice; custom voices are DECtalk's (D4, D27) |
| `/urlrepl ...` | `/links ...` | Your answer to D26 |
| `/urltoggle` | unchanged | It is for everyone, while `/links` needs Manage Server. Discord sets default permissions per command, not per subcommand, so a toggle inside `/links` would be hidden from ordinary members. |

A build without a module never registers that module's commands. Discord
drops them at the next bulk registration, as it does any removed command.

### 3.4 One note on nicknames

Nicknames owns the Server Members intent (`track_changes`). The LLM's aliases
read nicknames from DPP's member cache, which only fills with that intent.
So a build without nicknames gives the model fewer names to hide. Nothing
breaks: a name the bot can't see was never sent anyway. The LLM's README
says so.

---

## 4. What a module is

### 4.1 The interface

```cpp
// src/core/include/core/modules/module.hpp
namespace latibot::modules {

class module {
public:
    virtual ~module() = default;

    /// "llm", "music": its name in logs, in schema_versions and in config.json.
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    /// Its schema steps, version 1 first, recorded under its name (§7).
    [[nodiscard]] virtual auto schema() const -> std::span<const db::migration> { return {}; }

    /// Offers capabilities to other modules (§5). Every module exists by now;
    /// none has started.
    virtual auto offer(capability_registry& offered) -> void {}

    /// Registers commands, stages, listeners, timers and panels, and looks up
    /// the capabilities it uses.
    virtual auto start(host& bot) -> void = 0;
};

} // namespace latibot::modules
```

Each module has one factory, `make_module(host&)`. The factory reads the
module's config section and secrets and builds its stores, which are plain
members of the module, as they are members of `bot` today.

### 4.2 The host: what the core gives a module

| Kind | What | Replaces |
|---|---|---|
| Services | `database()`, `settings()`, `bootstrap()`, `section(name)`, `gateway()`, `http()`, `raw()`, `clock()`, `cluster()`, `me()`, `capabilities()` | the constructor arguments `bot` hands out |
| Commands | `slash_commands().add(...)` | `register_commands` |
| Message stages | `add_stage(position, name, fn)` (§4.4) | `register_stages` |
| Discord events | `listen(cluster().on_message_create, "linkstats: media posts", fn)` | the lambdas in `register_events` |
| Timers | `every(interval, name, fn)` and `after(delay, name, fn)`, both guarded | `register_timers`, `attribute_later` |
| Panels | `panels().add(router, {"nicks", ...})`; a view name claimed twice stops startup | `route_component`, `on_form` |
| Startup checks | `intents(...)`, `permission(bits, purpose)`, `secret(value)` | `intents_for`, `passive_requirements`, `secrets_of` |
| Carrying out | `post(send_message)`, `detach(task, what)` | `carry_out` |

The details:
- **The namespace is `modules`, not `module`,** and the folder
  `core/modules/`. Inside a class derived from `module`, the name `module`
  means the base class, so `module::host` would not compile there; and the
  codebase's namespaces are plural (`commands`, `events`, `ports`).
- **Two names differ from v2:** `bootstrap()` rather than `config()`, and
  `slash_commands()` rather than `commands()`. The bot is the host, and a
  member called `config` or `commands` would hide those namespaces throughout
  `bot.cpp`.
- **Discord events** go straight to DPP's own event routers, which already
  take any number of listeners. `listen` adds only an exception guard and a
  name, so startup can log which module listens to what.
- **Intents** are set on `dpp::cluster::intents` after modules start and
  before it connects.
- **Voice's own hooks:** voice offers the modules that require it plain
  callbacks, such as `on_left(guild)` and `on_ready(guild)`, so the "is it
  the bot that left?" logic stays in one place.

### 4.3 Lifecycle

1. `main` reads `config.json` and the environment. It builds the host:
   opens the database, runs the core's schema, and creates the cluster
   without connecting.
2. Modules are constructed in dependency order, from the generated list
   (§4.8).
3. Each module's schema is applied, under its name (§7).
4. `offer`: every module offers its capabilities.
5. `start`: every module registers and looks up capabilities. Every offer
   came first, so the order here doesn't matter.
6. The host sets the intents, logs the modules and who listens to what, and
   connects.
7. On shutdown the cluster stops first, so no callback reaches a module
   being destroyed. Then the modules are destroyed in reverse order.

Threading doesn't change. Listeners run on DPP's pool, and each store
guards itself.

### 4.4 Pipeline order

The core names the positions, and a module picks one. Two stages claiming
one position stop startup.

```cpp
namespace latibot::events::stage_order {
inline constexpr int stop = 100;     // goodbye (core)
inline constexpr int rewrite = 200;  // link replacement
inline constexpr int reply = 300;    // trigger replies
inline constexpr int model = 400;    // the language model, last: it consumes what it answers
}
```

### 4.5 Actions

Stages return actions, and the shell carries them out, which keeps stages
testable. The core no longer lists every feature's action:

```cpp
struct background_task {
    std::string what;                                   // for the log
    std::move_only_function<dpp::task<void>()> run;
};
using action = std::variant<send_message, stop_bot, background_task>;
```

Each module keeps a pure decision function, such as the LLM's
`decide(message) -> std::optional<ask_llm>` or links' plan of
`replace_links`. Its tests keep reading those types. Only the line that
registers the stage wraps the decision into a `background_task`.

### 4.6 Panels and commands

- **Panels:** each router declares the view names it owns, and the host
  refuses a name claimed twice. Custom ids don't change, so buttons on old
  messages keep working.
- **Commands:** `commands::registry` and `command` are already the right
  shape. A module calls `bot.slash_commands().add(...)` from `start`.

### 4.7 Per-server settings

Each module declares its `guild_settings` keys once, with their default and
range, in a table like the config sections' (§8.2). The stored key names
don't change.

### 4.8 How `main` finds the modules

CMake writes `enabled_modules.cpp`: one function that calls each enabled
module's factory, in dependency order. It's explicit, greppable, and the
linker can't drop a module. Self-registration (static libraries drop it),
`#if` switches (the core would name every module) and DLL plugins were
turned down (v1 §4.8).

---

## 5. Modules using each other

### 5.1 Three kinds

| Kind | When | Build effect | Example |
|---|---|---|---|
| **Requires** | B makes no sense without A | B links A and includes A's public headers. CMake refuses B without A. | dectalk and music require voice; linkstats requires links |
| **Capability** | A can do more when B is there, through a small, stable interface | None. The interface lives in the core; B offers it; A asks and may get null. | The LLM speaks through dectalk |
| **Bridge** | The integration needs both modules' own types, or is large | A small third library, built only when both are on | A future LLM tool that queues music |

The rule: **a module includes only the core's public headers and those of
the modules it requires.** CMake enforces it with include directories.

### 5.2 Capabilities

They live in `src/core/include/core/capabilities/`, beside the ports.
Ports reach the outside world; capabilities reach a module that may not be
there.

```cpp
class capability_registry {
public:
    template <typename Interface> auto offer(Interface& implementation, std::string_view by) -> void;  // twice stops startup
    template <typename Interface> [[nodiscard]] auto find() const -> Interface*;  // null when nobody offers it
};
```

It is `capability_registry`, in `core/modules/`, so that inside the
`modules` namespace the name `capabilities` still means the interfaces'
namespace.

Keep them few and small. One that grows module-specific types becomes a
bridge instead.

### 5.3 The `speech` capability

```cpp
namespace latibot::capabilities {

/// Saying text aloud in a server's voice channel. Offered by dectalk.
class speech {
public:
    virtual ~speech() = default;

    /// Whether what is posted in `text_channel` is also spoken: it is the
    /// server's voice session's channel.
    [[nodiscard]] virtual auto speaks_in(dpp::snowflake guild, dpp::snowflake text_channel) const -> bool = 0;

    /// `text` as it will be said, and so posted: inline commands the model
    /// may not use are taken out.
    [[nodiscard]] virtual auto prepare_for_model(std::string_view text, dpp::snowflake guild) const -> std::string = 0;

    /// Says it within the server's limits, queued under `for_user` so they
    /// can /tts stop it.
    virtual auto say(dpp::snowflake guild, dpp::snowflake for_user, std::string text) -> dpp::task<void> = 0;

    /// The "## Speaking" section of the model's instructions.
    [[nodiscard]] virtual auto guide_for_model() const -> std::string = 0;
};

} // namespace latibot::capabilities
```

Without dectalk, `find<speech>()` is null and the LLM never speaks. That's
what happens today in a channel with no voice session. The LLM's tests use a
`mock_speech`.

### 5.4 Every cross-module use, mapped

| Today | Becomes |
|---|---|
| The LLM speaks replies, lists voices, checks for a session (K4–K6) | `speech` |
| Advanced triggers match like triggers (K3) | `util/match` in the core |
| Music decides where to play (K7) | voice, which music requires |
| Leaving voice tidies speech and music (K9) | voice's `on_left` |
| Music feeds the mixer | music registers its source with voice's mixer |
| Link stats read replacements | linkstats requires links: `replacements.hpp` and `url_rules.hpp` are links' public headers |
| A trigger's reply silences an advanced trigger | already core: `incoming_message::answered` and the stage order |

---

## 6. Folders, headers and CMake

### 6.1 Folders

```
src/
  app/main.cpp                       LatiBot.exe
  core/
    CMakeLists.txt
    include/core/...                 public: what modules may include
    src/...                          private: the host, the DPP adapters, preflight
    tests/
  modules/<name>/
    CMakeLists.txt
    README.md                        what it owns (§11)
    include/<name>/...               public: only for modules that require it
    src/...                          private
    tests/
tests/
  support/  mocks/                   shared test helpers (an INTERFACE library)
  fuzz/                              each fuzzer built with its module
conan/dpp/conanfile.py               the DPP recipe (§9)
```

### 6.2 Headers

- **The core's public headers:**
  - `util/`;
  - `db/database`, `db/statement`, `db/migrations`;
  - `config/guild_settings`, `config/bootstrap` (core keys),
    `config/section`;
  - `ports/` and `ui/`;
  - `commands/registry`, `commands/options`, `commands/message_options`;
  - `events/message_pipeline`, `events/stage_order`;
  - `discord/message_flags`, `discord/raw_api`;
  - `modules/` and `capabilities/`.
- **Include paths:**
  - the core keeps `#include "core/..."`;
  - modules use their own prefix, such as `#include "llm/aliases.hpp"`.
- **Enforcement:** `target_include_directories(... PUBLIC include PRIVATE
  src)`. A module that includes another's private header doesn't compile.

### 6.3 CMake

- `latibot_module(<name> SOURCES ... REQUIRES ... LINKS ...)` makes the
  static library `latibot_<name>`:
  - it links `latibot_core` and the modules it requires publicly;
  - it links the interface targets `latibot::warnings` and
    `latibot::sanitizers`;
  - it registers its test executable `latibot_<name>_tests`.
- **Switches:** `LATIBOT_WITH_<NAME>`, on by default. Voice has none; it is
  on when dectalk or music is. A module whose requirement is off stops the
  configure, naming both.
- **The executable:** `LatiBot.exe` links every enabled module plus the
  generated `enabled_modules.cpp`.
- **Libraries per module:**
  - DECtalk only with dectalk;
  - `ws2_32` with music;
  - `OpenSSL::Crypto`, for emoji image hashes, with linkstats;
  - `crypt32` stays in the core.

---

## 7. The database

### 7.1 Each module's version 1

Each is its tables exactly as they are after migration 15, written as one
set of `CREATE` statements with the `ALTER`s folded in.

| Module | Tables, views and triggers | From migrations |
|---|---|---|
| core | `guild_settings`, `allowed_bots` | 1, 3 |
| triggers | `triggers` (with `respond_to_bots`, `message_flags`), `trigger_responses` | 2, 3, 9 |
| nicknames | `nickname_history` | 4 |
| midnight | `midnight_messages` (with `message_flags`) | 5, 9 |
| links | `url_rules`, `url_opt_outs`, `known_mirrors`, `replacement_messages` (with `kind`), `replacement_links` | 6, 12 |
| linkstats | `reactions`, `reaction_log`, `emojis`, `emoji_aliases`, `backfill_progress`, `emoji_images`, `emoji_copies`, `emote_reactions`, the view `counted_reactions` | 7, 8, 13, 14 |
| dectalk | `tts_voices` | 10 |
| llm | `llm_usage`, `llm_documents`, `llm_memory`, `llm_memory_search` (FTS5) and its three triggers, `llm_blacklist`, `llm_triggers`, `llm_aliases` | 11, 15 |

Linkstats writes image posts into links' `replacement_messages`
(`kind = 'image'`), through links' public `replacement_store`.

### 7.2 At startup

`schema_versions(module TEXT PRIMARY KEY, version INTEGER)` records each
module's version.

| The database found | What happens |
|---|---|
| Has `schema_versions` | Each built module applies its steps above its recorded version. |
| Brand new, no tables | Each built module creates its version 1. The old migrations never run. |
| Old, `user_version` 1–15 | **Marked for deletion.** Migrations 1–15 bring it to 15, unedited. Then *adoption* creates `schema_versions` and records version 1 for every module in §7.1, built or not. |

A module built in later finds no row and creates its tables, unless
adoption recorded it.

**When you say the old migrations can go:**
- delete migrations 1–15, the adoption step and the comparison test;
- refuse a database below 15, with a message naming the bot version to run
  first.

All of these carry `// remove after: you say so`.

### 7.3 The comparison test

One database is built through 1–15, and another from every module's
version 1. They must match on:
- each table's columns, in order: `PRAGMA table_xinfo`;
- indexes and their columns;
- foreign keys;
- `WITHOUT ROWID`;
- views, triggers and virtual tables, compared by their normalised SQL.

`ALTER TABLE ADD COLUMN` appends, so the flattened tables list those
columns last.

---

## 8. Configuration

### 8.1 Sections

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
  place, and its mapping is marked `// remove after: you say so`.
- **A section for a module that isn't built** gets one warning, then is
  ignored.
- **An unknown key inside a built module's section** stops startup.

### 8.2 Each key declared once

```cpp
struct music_config {
    std::filesystem::path ytdlp_path;
    // ...
    int pot_provider_port = 4416;
};

inline const config::section<music_config> music_section{"music", {
    config::key("ytdlp_path", &music_config::ytdlp_path, "Where yt-dlp is. Empty: beside the bot, then PATH."),
    config::key("pot_provider_port", &music_config::pot_provider_port, "bgutil's PO token provider's port.",
                config::range{1, 65535}),
}};
```

One reader in the core does the rest from that table:
- reads each key as its member's type;
- checks ranges;
- refuses keys the table doesn't have;
- writes a section's defaults from a default-constructed struct.

`default_json()` and `config.example.json` come from the tables, and a test
keeps the example file current.

### 8.3 Secrets

They stay in environment variables, and each module reads its own:

| Module | Variables |
|---|---|
| llm | `ANTHROPIC_API_KEY`, `OPENAI_API_KEY` |
| music | `LATIBOT_YTDLP_COOKIES`, `LATIBOT_YTDLP_FIREFOX_PROFILE` |
| linkstats | `LATIBOT_DEBUG_RECOMPUTE_BOT_ID` |

Each passes its secret values to `host.secret()`, so the log channel masks
them.

---

## 9. The build system

### 9.1 What changes

| # | Change | Fixes |
|---|---|---|
| I1 | **DPP becomes a Conan package**, built from our submodule by `conan/dpp/conanfile.py`, with today's options: Conan's OpenSSL, zlib and opus; voice on; DPP's formatters; the `WITH_OPENSSL3` fix. It's built once per configuration, reused by every build folder, and cached in CI. | DPP compiled in every build folder (up to five times) and on every CI run |
| I2 | **DPP linked statically**, if it links cleanly. DECtalk stays a DLL with its dictionary beside it. | No `dpp.dll` to copy |
| I3 | **Ninja Multi-Config**, if two checks pass: CMake writes `compile_commands.json` for it, and VS Code's CMake Tools loads the Visual Studio environment. | The separate clang-tidy folder and its Conan install; `CMAKE_CONFIGURATION_TYPES` forcing |
| I4 | **Workflow presets**: one command per job, such as `cmake --workflow --preset asan`. `cmake_minimum_required` moves to 3.25. | Chained configure, build and test commands |
| I5 | **An install step**: `cmake --install` writes `out/LatiBot/` (Git-ignored), holding `LatiBot.exe`, `dectalk.dll`, `dtalk_us.dic`, and `Install-Dependencies.ps1` with its readme. The live bot runs from there; you move `config.json` and `.env` yourself. | Building while the bot runs; copying files by hand |
| I6 | **Interface targets** for warnings, sanitizers and runtime DLL copies, linked by `latibot_module()` | Every target remembering three function calls |
| I7 | **A Conan lockfile** (`conan.lock`), committed. CI's cache key is its hash. | Unpinned transitive versions and recipe revisions |
| I8 | **A precompiled header for DPP**, if a measured clean build is faster with it | Compile time |
| I9 | **ASan in CI**, if the runner image has MSVC's ASan component | ASan only running when you remember |

Deferred: dependencies built with ASan (D23). Not done: linking the C
runtime statically, and unity builds.

### 9.2 Build folders

All under one parent, `build/`:

| Preset | Folder | For | Workflow |
|---|---|---|---|
| `default` | `build/build` | Debug and Release, with `compile_commands.json` | `debug`, `release` |
| `asan` | `build/build-asan` | ASan Debug | `asan` |
| `fuzz` | `build/build-fuzz` | libFuzzer targets | build only |
| `core-only` | `build/build-core` | every module off | `core-only` |

Conan's generated files go to `build/conan/`. clang-tidy reads
`build/build/compile_commands.json`, and the `ninja-tidy` preset goes.

### 9.3 CI

| When | Jobs |
|---|---|
| Every push | Debug and Release (tests); ASan (I9); core-only (D10); the test catalog and formatting checks; the secret scan |
| Weekly | The full module matrix, and the fuzzers |

The dependency cache includes DPP.

---

## 10. Tests

- **One executable per module**, `latibot_<name>_tests`, plus
  `latibot_core_tests`. A test that leans on another module then fails to
  link, which tests R5.
- **Shared helpers:** `tests/support` and `tests/mocks` become an INTERFACE
  library. Mocks of a module's own ports move with it: `mock_media` to
  music, `mock_tts` to dectalk. `mock_speech` is new.
- **Tags:** each test is retagged with its module (`[nicknames]`,
  `[linkstats]`, ...) as its module moves. The catalog groups by module.
- **Files that split or change:**
  - `llm_answer_test` uses `mock_speech`;
  - `panels_test` splits per module;
  - `command_responses_test` and `registry_test` use stand-in commands,
    with a short per-module test that its commands pass `registry::add`;
  - `migrations_test` covers the core and adoption, and each module its own
    schema.
- **Fuzzers:**

  | Fuzzers | Module |
  |---|---|
  | `fuzz_text`, `fuzz_url_scan` | core |
  | `fuzz_dectalk_sanitizer` | dectalk |
  | `fuzz_legacy_parser` | linkstats |

- **The build matrix, `tools/Test-ModuleMatrix.ps1`**, builds and tests:
  - every module on;
  - core only;
  - each module off, in turn.

  Run it before changing `src/core/include/` or a capability.

---

## 11. Docs

- **Each module has a `README.md` in its folder** listing what it owns:
  - commands;
  - passive behaviour;
  - message stages and their positions;
  - Discord events;
  - timers;
  - tables;
  - `guild_settings` keys;
  - config keys and environment variables;
  - capabilities offered and used;
  - modules required;
  - its feature doc.
- **A test keeps the README in sync**, checking what code can list:
  commands, panel view names, config keys and settings keys.
- **`docs/features/`** stays as the behaviour specs, each with a line naming
  its module. The user guide marks each command's module.
- **`docs/architecture/`** is redrawn for modules at the end.

---

## 12. Order of work and progress

Every step ends building, with ASan tests passing and clang-tidy clean.
Nothing is pushed.

| Phase | Step | Status |
|---|---|---|
| 0 | C++23 (`eb0eb67`, `a5cf7e9`); the fuzz build fixed (`dd936f3`) | done |
| **1. Untie the knots** | 1a `ports::result` → `std::expected` (D31) | done, `193acf8` |
| | 1b K3: matching to `util/match` | done, `1c600c5` |
| | 1c K7: `plan_speak` with the voice code, as `discord::plan_voice` | done, `4406791` |
| | 1d K8: `/tts voices lab`, `list`, `delete` | done, `0dde567` |
| | 1e `/urlrepl` → `/links` (D26) | done, `089c839` |
| | 1f K1: generic actions, `background_task` | done, `5965399` |
| | 1g K4–K6: the `speech` interface, offered by the DECtalk code, used by the LLM | done, `76c4cfc` |
| | 1h K2: the LLM checks its own config | done, `86b0f29` |
| **2. The build system** | 2a DPP as a Conan package (I1), static if it links cleanly (I2) | done, `8b00511`, `ca194d0`: static links cleanly |
| | 2b Ninja Multi-Config (I3), after its checks; folders under `build/` (§9.2) | done, `4029409` |
| | 2c Workflow presets (I4), the lockfile (I7), the install step (I5) | done, `3b1af7c`; the lockfile leaves DPP out, since its recipe revision follows line endings |
| | 2d The precompiled header, measured (I8) | done, `6f70bd6`: a clean Debug build of our code, 124 s → 72 s |
| | 2e CI: the cached DPP, ASan (I9); the README, `DevEnvSetup.ps1`, tasks, testing docs | done: an AddressSanitizer job, which first checks the image has the runtime; not yet run on GitHub |
| **3. The module interface** | 3a `module`, `host`, `capabilities`, `stage_order`; `bot` becomes the host | done: also `ui::panel_routes`, and `tests/support/test_host.hpp` for modules' tests |
| | 3b `schema_versions`, the flattened schemas, adoption, the comparison test (§7) | done: the schemas are in `core/db/schemas.cpp` until each module takes its own; tests build databases from them |
| | 3c Config sections and key tables (§8) | done: the four feature sections live in `core/config/feature_sections.*` until their modules take them; `host::section` for modules |
| | 3d midnight as the first module | done, `e71f440`; the namespace became `modules` (§4.2) |
| **4. Folders and targets** | `src/core/{include,src,tests}`, `src/app`, `latibot_module()`, the generated list, per-module test executables; midnight moved | done but the core's split: `cmake/modules.cmake`, `LATIBOT_WITH_MIDNIGHT`, `latibot_midnight_tests` and `latibot_app_tests`, midnight's README checked by a test. The core's `include`/`src` split moves to the end of phase 5, once the features have left it, so its include lines change once rather than twice |
| **5. The other modules** | nicknames, triggers, links, linkstats, voice (with K9), dectalk, music, llm. Each step brings its settings table, its README with the sync test, and its tests moved and retagged. | |
| | 5z The core's `include`/`src` split (§6.1), and `latibot_tests` becomes `latibot_core_tests` | |
| **6. Matrix and docs** | `Test-ModuleMatrix.ps1`, the core-only CI job, the architecture docs, the user guide | |
| **When you say** | Delete migrations 1–15, adoption, the comparison test and the old config keys | |

---

## 13. Decisions

Settled across v1 and v2:

| # | Decision |
|---|---|
| D1 | Your six modules, plus triggers, linkstats and voice |
| D2 | Voice is its own module, built when dectalk or music is |
| D3 | Migrations flattened into per-module version 1s. 1–15 kept, unedited, marked for deletion until you say. |
| D4, D27 | Custom voices move to `/tts voices lab`, `list` and `delete` |
| D5 | `core/...` includes for the core, `<module>/...` for modules |
| D6 | Tests beside each module, one executable per module |
| D7 | A generated module list |
| D8, D25 | A section per module, with the §8.1 names. Old flat keys warn and are marked for removal. |
| D9 | Capabilities for small, stable interfaces (only `speech` now); bridges for anything bigger |
| D10 | The matrix runs locally before core or capability changes, plus a core-only CI job on every push |
| D11 | A compiled-out module's data is kept |
| D12 | A test keeps each module's README in sync |
| D13, D26 | `dectalk` and `links`. `/urlrepl` becomes `/links`, and `/urltoggle` stays its own command (§3.3). |
| D14 | Build time only |
| D15 | DPP as a Conan package from the submodule, keeping today's options |
| D16 | DPP static, if it links cleanly |
| D17 | Ninja Multi-Config, if its checks pass |
| D18 | Workflow presets; CMake 3.25 |
| D19 | An install step to `out/LatiBot/`; you move `config.json` and `.env` |
| D20 | A Conan lockfile |
| D21 | ASan in CI |
| D22 | A precompiled header if it saves time |
| D23 | Instrumented dependencies wait |
| D24 | The build system before the modules |
| D28 | The schema comparison test while the old migrations live; afterwards, old databases are refused |
| D29 | Key tables for config and per-server settings |
| D30 | `util/process` moves to music |
| D31 | `std::expected` for `ports::result`; `std::move_only_function` for `background_task` |

---

## 14. Risks

| Risk | How it's handled |
|---|---|
| Moving 200 files makes history hard to follow | Moves in commits of their own, with no content changes, so `git log --follow` and `git blame -C` see through them |
| Something runs in a different order: the pipeline, or leaving voice | Explicit stage positions. `on_left` keeps today's order: sessions, speech, music, mixer. |
| A DPP callback reaches a module being destroyed | The cluster stops first (§4.3) |
| The DPP recipe builds DPP differently: voice off, the wrong OpenSSL, no formatters | Compare the configure output and run the full suite against the packaged DPP before anything else moves |
| Static DPP misbehaves | Found by the full suite; the fallback is one recipe option |
| Ninja lacks the Visual Studio environment in some shell | I3's checks first; CMake Tools, the scripts and CI each set it up |
| A flattened schema differs from the migrated one | The comparison test, before any database is adopted |
| Adoption runs on the live database | It only adds `schema_versions` and its rows. Start right after a fresh backup. |
| The host grows a hook for every whim | Hooks are added only when a moving module needs one |

---

## Appendix A: every file, and where it goes

Paths are under `src/core/` today:
- **pub**: the core's or the module's `include/`;
- **priv**: its `src/`.

### core

| Files | |
|---|---|
| `util/*` (ca_certificates, env, log, text, url_scan), new `util/match` | pub |
| `db/database`, `db/statement`, `db/error`, `db/migrations` | pub (the runner, `schema_versions`, adoption, 1–15 marked for deletion) |
| `db/backup` | priv |
| `config/guild_settings`, `config/bootstrap` (core keys), new `config/section` | pub |
| `config/command_line` | priv |
| `ports/clock`, `ports/discord_gateway`, `ports/http_client`, `ports/result`, `ports/command_host` | pub |
| `ui/interaction`, `ui/paginator` | pub |
| `commands/registry`, `commands/options`, `commands/message_options` | pub |
| `commands/preflight`, `commands/unregister` | priv |
| `commands/basic` (without `join` and `leave`), `commands/bots`, `events/bot_allowlist`, `commands/logs`, `events/log_channel`, `events/goodbye` | priv |
| `events/message_pipeline`, new `events/stage_order` | pub |
| `discord/message_flags`, `discord/raw_api` | pub |
| `discord/dpp_gateway`, `discord/dpp_http_client`, `discord/dpp_log`, `discord/unregister_commands` | priv |
| `version`, new `module/*`, new `capabilities/*` | pub |
| `bot` | priv: the host |
| `main.cpp` | `src/app/` |

### voice

| Files | |
|---|---|
| `audio/voice_mixer`, `audio/pcm`, `ports/voice_output`, `events/voice_sessions`, `discord/voice_state`, `plan_speak` | pub |
| `discord/dpp_voice_output`, `commands/voice` (`start`, `stop`, `grace`), `join` and `leave` from `commands/basic` | priv |
| tests: `voice_mixer`, `voice_sessions`, `pcm`, the voice part of `basic_commands` | |

### dectalk

| Files | |
|---|---|
| `audio/dectalk_engine`, `audio/dectalk_sanitizer`, `audio/speech_queue`, `audio/voice_params`, `audio/voice_store`, `audio/wav`, `ports/tts_engine`, `commands/speak`, `commands/chat`, `commands/voice_lab`, the `speech` implementation | priv |
| tests: `dectalk_engine`, `dectalk_golden`, `dectalk_sanitizer`, `speech_queue`, `speak_command`, `chat_command`, `custom_voice`, `voice_lab`, `voice_params`, `wav`, `db/voice_store`; fuzzer `fuzz_dectalk_sanitizer` | |

### music

| Files | |
|---|---|
| `music/*`, `ports/media`, `commands/music`, `util/process`; from `bot.cpp`: `locate_pot_server`, `music_extras`, `log_music_tools`, `log_music_account`, `start_pot_provider`, `music_unavailable` | priv |
| tests: `music_command`, `music_cookies`, `music_links`, `music_player`, `music_queue`, `pot_provider`, `yt_dlp`, `yt_dlp_live`, `process`; `support/test_child.cpp` | |

### llm

| Files | |
|---|---|
| `llm/*`, `commands/llm`; from `bot.cpp`: `provider_for`, `llm_services`, `answer_with_llm`, its startup lines, `add_memory_tools` | priv |
| tests: `llm_aliases`, `llm_answer`, `llm_command`, `llm_guards`, `llm_provider`, `llm_tools`, `db/llm_store` | |

### triggers

| Files | |
|---|---|
| `events/triggers` (without the matching), `commands/trigger` | priv |
| tests: `triggers`, `trigger_command`, `db/trigger_store`, the trigger part of `panels` | |

### nicknames

| Files | |
|---|---|
| `events/nicknames`, `events/nickname_import`, `commands/nickname`; from `bot.cpp`: `record_nickname`, `on_member_update`, `on_audit_entry`, `attribute_later`, `reconcile_nicknames`, `guild_of`, `nickname_change_in`, the `nicknames.json` import, the intent | priv |
| tests: `nicknames`, `nickname_command`, `nickname_import`, `db/nickname_store`, `db/nickname_import` | |

### midnight

| Files | |
|---|---|
| `events/midnight`, `commands/midnight`, the midnight timer | priv |
| tests: `midnight`, `midnight_command`, `db/midnight_store` | |

### links

| Files | |
|---|---|
| `events/url_rules`, `events/replacements` | pub (linkstats uses them) |
| `events/url_replacer`, `events/embed_watch`, `commands/urlrepl` (as `/links`); from `bot.cpp`: `import_url_rules`, `retry_replacement`, `settle_stranded_replacements`, the stranded map, the preview tracker's timer and listeners | priv |
| tests: `url_rules`, `urlrepl_command`, `embed_watch`, `db/url_rule_store`, `db/replacement_store`, the URL part of `panels` | |

### linkstats

| Files | |
|---|---|
| `events/reactions`, `events/emote_reactions`, `events/media_posts`, `events/backfill`, `events/emoji_copies`, `events/legacy_replacements`, `commands/linkstats`; from `bot.cpp`: the reaction listeners, the media and emote listeners, `text_channels`, `copy_emojis` and its timer, `recompute_bot_id` | priv |
| tests: `linkstats_command`, `legacy_replacements`, `reactions`, `db/backfill`, `db/emoji_copies`, `db/emote_reactions`, `db/media_posts`, `db/reaction_store`; fuzzer `fuzz_legacy_parser` | |
