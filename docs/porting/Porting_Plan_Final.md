# LatiBot: Final Porting Plan

The plan the port follows. It replaces
[Porting_Initial_Analysis.md](Porting_Initial_Analysis.md) and
[v1](Porting_Plan_v1.md) / [v2](Porting_Plan_v2.md) / [v3](Porting_Plan_v3.md) /
[v4](Porting_Plan_v4.md), which stay in the repository as the record of how the
design was arrived at, review comments and all. Nothing here needs those to be
read first.

Two conventions worth knowing before reading:

- **Section numbers match v4.** Code comments across `src/` cite the plan as
  `plan v4 §5.4` and similar, and those references still resolve here. Sections
  §0–§20 keep their v4 meaning; anything new was added as §21 onward.
- **Each feature says where it stands.** ✅ built and tested, 🚧 partly built,
  ⏳ planned. What is built is described as it behaves; what is planned is
  described as it is intended to behave.

For the user-facing side of what exists today — commands, options, replies —
see [docs/features/](../features/README.md). This document is the design and
the order of work behind it.

---

## 0. Where the port stands

| Phase | Contents | State |
|---|---|---|
| 0 | Build split, tests, tooling, CI, database, config, ports, registry | ✅ done |
| 1 | Basic commands, goodbye phrase, preflight, pipeline, triggers, panel UI, bot allowlist | ✅ done |
| 2 | Nicknames, the import, midnight, scheduled backups | ✅ done |
| 3 | URL replacement, reaction statistics, backfill | ✅ done |
| 4 | DECtalk, mixer, voice sessions | ⏳ next |
| 5 | LLM | ⏳ |
| — | Music, emote statistics, appearance tracking | ⏳ unscheduled |

439 tests pass in Debug, Release and under AddressSanitizer, and clang-tidy is
clean over `src/`. Three libFuzzer targets cover the text that arrives from
people: the text helpers, the URL scanner and the legacy replacement parser.

---

## 1. What is being ported

The original is a Java 17 / JDA 5 bot (`java-reference/LatiBot v1.1`, kept
locally and deliberately untracked: it is large and contains personal data).
Its shape, for context on why the C++ version is arranged as it is:

- One `JDA` instance, `ListenerAdapter` subclasses, and a hand-rolled slash
  command registry (`BaseCommand`, `CommandRegistry`, a static `Commands`
  holder).
- **Global mutable state** on static fields of `LatiBot` — the JDA instance,
  the audio player, the track manager, the DECtalk handle. Commands and
  listeners reach into those statics directly.
- **Startup** builds JDA, waits for ready, hard-checks a fixed permission list
  **against one hardcoded guild id**, exits if anything is missing, registers
  commands globally, then schedules the midnight task.
- **Persistence is flat files** beside the working directory: `nicknames.json`,
  `UrlReplacements.txt`, plus generated `tts/*.wav` and `emotes/*`.
- **Audio** is LavaPlayer for music, and DECtalk (a native Windows DLL reached
  through a JNI shim) synthesizing speech to a `.wav` that is then queued as
  just another track.

### 1.1 How the pieces map

| Java | C++ / DPP |
|---|---|
| `ListenerAdapter` subclasses | `dpp::cluster::on_*` handlers, registered in one place (`bot::register_events`) |
| `BaseCommand` / `CommandRegistry` | `commands::command` + `commands::registry` (§5.3) |
| Static globals on `LatiBot` | One `latibot::bot` owning the cluster and subsystems, passed by reference |
| `nicknames.json`, `UrlReplacements.txt` | SQLite, with one-time importers (§5.2) |
| LavaPlayer | Nothing direct: yt-dlp + ffmpeg when music is built (§15) |
| DECtalk through JNI | Direct calls into the DLL — simpler, no shim (§12) |
| logback | `util::log`, with DPP's own log routed into it |

### 1.2 Feature verdicts

From the initial analysis, with the reasoning kept where it matters.

| Java feature | Verdict | Why |
|---|---|---|
| Music playback | **Later** | LavaPlayer has no C++ equivalent; needs a redesign, not a port (§15) |
| DECtalk TTS in voice | **Keep** | Simpler in C++ than through JNI; streams instead of writing `.wav` files (§12) |
| `/chat` voice message | **Keep**, promoted to a real command | It was a test command that worked well enough to become permanent (§12.8) |
| Nickname history | **Keep**, with pagination | Histories are long past the 2000-character limit the Java version gave up at (§8) |
| Owner-confirmation nickname flow | **Dropped** | Solved socially instead: the server owner moved to an alt account, so the bot can just change nicknames like anyone else's |
| Emote statistics | **Later** | Wants a redesign around incremental scanning (§16) |
| Emote export | **Dropped** | A one-off migration tool that has served its purpose |
| URL replacement | **Keep**, redesigned | The most-used feature, and the buggiest (§9) |
| Reaction-triggered refresh | **Dropped** | Targeted another bot that no longer interacts with this one |
| Midnight message | **Keep**, expanded | Per guild, per timezone, with the random-fire bug fixed (§10) |
| Basic commands | **Keep all** | Plus the "say goodbye latibot" phrase (§6) |
| Permission check | **Generalized** | Per feature, per guild, never fatal (§7) |
| LLM integration | **New build** | The Java `ApiDriver` was never wired up; designed fresh (§14) |
| Trigger responses | **New build** | Generalizes the hardcoded `420`/`69` → "nice" (§11) |
| `YesNoAnswers.txt` | **Dropped** | Superseded by the LLM |

---

## 2. Findings that shaped the design

Everything here was checked against the real sources — DPP 10.1.6 and the
DECtalk `develop` branch — rather than recalled. Where an earlier plan got
something wrong, the correction is stated, since the wrong version is still
written down in v1–v3.

### 2.1 DPP

**`co_request` has a usable timeout.** v2 claimed it was stuck at 5 seconds and
that the callback form took a timeout parameter. Neither is true.
`cluster::request` has no timeout parameter, `co_request` is a thin wrapper
around it, and both reach `http_request::run` in `queues.cpp`, which passes
`owner->request_timeout` — cluster-wide, **60 seconds by default**, settable
with `cluster::set_request_timeout`. The 5-second figure is the default
argument of the low-level `https_client` constructor, which the cluster never
relies on. So the LLM uses `co_request` directly: no patch, no fork, no
wrapper. Arbitrary HTTP also goes through DPP's `raw_rest` queue, separate from
the Discord REST queue, so a slow model call cannot hold up Discord calls.

**DPP's bundled Windows dependencies are old.** On MSVC, DPP links prebuilt
binaries from its `win32/` folder: OpenSSL 1.1.1k (March 2021, end of life
since September 2023) and zlib 1.2.11 (2017). A bot holding a long-lived TLS
connection should not ship those, so `CONAN_EXPORTED=ON` makes DPP use
`find_package`, and Conan supplies current versions.

**Voice has to be forced on.** DPP only enables voice on Windows when
`HAVE_OPUS_OPUS_H` is set, and it only sets that on its bundled-binaries path.
The ConanCenter recipe does not set it either, so the `dpp/10.0.35` package used
during initial setup was almost certainly built with the voice stub — `/join`
would have thrown "voice support not compiled in" at runtime. Our CMake sets
`HAVE_OPUS_OPUS_H` and `OPUS_LIBRARIES` explicitly, and configure output
includes `VOICE support will be enabled`.

**OpenSSL 3 needs one workaround.** DPP hardcodes `OPENSSL_VERSION "1.1.1f"` on
Windows, which makes its bundled `mlspp` compile an OpenSSL 1.1 code path that
fails against OpenSSL 3 (`FIPS_mode` is gone). Only the `hpke` target branches
on it, so our CMake adds `WITH_OPENSSL3` to that target and leaves the submodule
untouched. **Recheck when upgrading DPP.**

**Sending a voice message is a genuine gap.** DPP models *received* voice
messages (`attachment::duration_secs`, `attachment::waveform`,
`message::is_voice_message()`), but `message::add_file` has no parameter for
duration or waveform, so there is no first-class way to send one. JDA's
`FileUpload.asVoiceMessage` has no equivalent. The answer is
`cluster::post_rest_multipart` with hand-built `payload_json` carrying
`flags: 8192` and the attachment metadata (§5.5, §12.8). This is the one place
in the whole port where DPP is materially behind JDA.

**Modals are more limited than they look.** At most **5 components**; a modal
**cannot be updated in place**; a modal submit **cannot be answered with
another modal**; `custom_id` is 1–100 characters. DPP 10.1.6 has Label, Text
Display, Section, Container and File Upload, but **not** Radio Group, Checkbox
Group or Checkbox (types 21–23). So an "editable list" cannot live in a modal
alone — it needs a panel message plus modals, which is what §9.5 and §11
describe.

**Useful pieces that already exist:** `on_guild_audit_log_entry_create`
(§8.1), `discord_voice_client::insert_marker` / `skip_to_next_marker` /
`pause_audio` / `stop_audio` and `on_voice_track_marker` (§13, §15),
`cluster::start_timer` (§10), `on_autocomplete`, `on_form_submit` and
`interaction_modal_response` (§9.5).

### 2.2 DECtalk

**The in-memory API removes the `.wav` file entirely.** `ttsapi.h` exposes
`TextToSpeechOpenInMemory` / `AddBuffer` / `ReturnBuffer` / `CloseInMemory`,
handing back raw PCM plus phoneme and index-mark arrays. The old JNI shim never
touched these, which is why the Java version wrote files. So the pipeline is
**text → PCM in RAM → Discord**, and the retention and cleanup problem
disappears with it.

**The callback is 32-bit, on a 64-bit build.** Its signature is
`void (*)(LONG, LONG, DWORD, UINT)`, and on 64-bit Windows `LONG` and `DWORD`
are 32 bits. Two consequences:

- v2's `reinterpret_cast<LONG>(this)` **would have truncated the pointer** and
  crashed or corrupted memory. Pass a small integer engine id, or nothing at
  all — there is one engine, so a static reaches it.
- **The buffer pointer is truncated too.** `ttsapi.c` calls
  `Report_TTS_Status(phTTS, uiID_Buffer_Message, 0, (LPARAM)pTTS_Buffer)` and
  `Report_TTS_Status` takes `long lParam2`, so the callback receives the low 32
  bits of the buffer address. The workaround needs no DECtalk change: we
  allocate and queue the buffers, the engine returns them **in the order they
  were added**, and we keep a `std::deque` and take the front one on each
  buffer message. The truncated value is still worth comparing against the low
  32 bits of the front buffer as a sanity check, logging a mismatch as an
  engine error.

**Streaming needs no window or message loop.** `TextToSpeechStartupEx` is
exported in the Windows `.DEF`, and the HWND-based `TextToSpeechStartup` is a
wrapper that forwards through `PostMessage`. The buffer-ready message id is the
value of `RegisterWindowMessage("DECtalkBufferMessage")`, not the `TTS_MSG_BUFFER`
constant, which is only defined for non-Windows builds. This is what unblocked
streaming as the primary design rather than a later optimization.

**Why the dictionaries never worked in the Java version.** On Windows DECtalk
reads the dictionary path from a registry key under `HKEY_LOCAL_MACHINE`. When
that key is absent it falls back to a bare relative `DTALK_US.DIC` resolved
against the **current working directory**, and tries to append an error note to
`\dtdic.log` at the drive root. The fix is `TextToSpeechStartupExFonix`, which
takes an explicit dictionary file name: we pass an absolute path to the `.dic`
sitting next to the exe, so neither the registry nor the working directory
matters.

**What the risky inline commands actually do**, after reading `cm_copt.c`,
`cmd_wav.c` and the command table in `c_us_cde.h`:

| Command | What it really does |
|---|---|
| `[:play "path"]` | Opens **any** `.wav` path on the host with `mmioOpen` and plays it |
| `[:log …]` | Writes fixed names `log.txt` / `dbglog.txt` in the working directory — not an arbitrary path, as v2 implied |
| `[:debug n]` | Sets engine debug flags; prints diagnostics to the bot's stdout |
| `[:loadv n]` / `[:setv n]` | Stores and replays a command macro. The source itself says `loadv` "will probably crash and burn if a flush happens in the middle", and `/tts stop` flushes |
| `[:dv save]` | Makes voice edits permanent for the engine handle — we store custom voices ourselves instead (§12.6) |
| `[:tone]`, `[:dial]`, `[:pause]` | Generate tones and silence of any length; bounded by the duration cap rather than by the sanitizer |

**Engine state persists between utterances.** `[:rate]`, `[:dv …]`, `[:mode]`,
`[:phoneme on]`, `[:punct]` and others stay set on the handle, so one user's
`[:rate 75][:dv ap 300]` would carry into the next person's `/speak`. Every
request therefore starts from a reset (§12.5).

**Custom voices are built in.** `[:dv <param> <value> …]` edits the current
speaker across about 35 parameters, grouped as Identity (`sx`, `hs`, base
voice), Pitch (`ap`, `pr`, `as`, `hr`, `sr`, `bf`), Quality (`br`, `ri`, `sm`,
`la`, `lx`, `qu`), Formants (`f4 b4 f5 b5 f7 f8`) and Gains (`gf gh gv gn
g1`–`g5`). So a custom voice is a base voice plus an ordered list of pairs,
which is exactly what §12.6 stores and replays. The voice table holds up to 11
base voices, not the 9 v2 assumed: `val` is always present and `chris` depends
on a build flag.

**Build facts** for our CMake: the DLL project is 86 C files, exports come from
`DECTALK.DEF`, it links `winmm`, and the Release/x64 defines are
`WIN32;DECTALKAPI_EXPORTS;_WINDOWS;_USRDLL;USE_CORE_DLL;ACNA;BLD_DECTALK_DLL;ENGLISH_US;ENGLISH;NDEBUG;AMD64`.
The dictionary is compiled from text with
`Internal Dictionary Compiler.exe dic/Dic_us.txt dtalk_us.dic /t:win32`.
Upstream has a `cmake` branch (February 2023) with a working port including that
post-build step; ours is based on it with the source list checked against
`develop`'s project file.

**Licence.** DECtalk is proprietary Fonix code. Keeping it as a submodule means
this repository references it without redistributing it, which matters because
the repository is public.

### 2.3 Consequences of a public repository

- Real `nicknames.json` and `UrlReplacements.txt` live in `data/import/`, which
  is gitignored. **Tests use synthetic data only** — never real IDs or names.
- gitleaks runs in CI on every push, and GitHub push protection is on.
- Secrets come from the environment, never from a committed file (§5.1).

### 2.4 "Administrator" is per server

Allowing `[:play]` for administrators would mean administrators of *whichever
server the command came from*. If the bot is ever added elsewhere, that
server's admins could play any `.wav` on the host and write `log.txt` into the
bot's folder. Hence trusted **guilds** and trusted **users** in `config.json`
(§12.5), which costs nothing while the bot is on one server and closes the hole
if that changes. **LLM output is never trusted**, whoever asked the question, so
a prompt-injected message cannot make the model emit `[:play "C:\…"]`.

### 2.5 Thread safety is a real difference from Java

DPP dispatches events from several threads. The Java version shares plain
`HashMap`s across gateway threads and command handlers with no synchronization
(`MessageListener.domains`, `NicknameListener.nicknamesHistory`, `hashes`,
`userBlacklist`). In Java that usually gets away with it. In C++ a concurrent
`std::unordered_map` read during a rehash is **undefined behaviour and will
eventually crash**. Every shared store is behind a mutex or owned by a class
that enforces one — the database included (§5.2).

---

## 3. Decisions locked in

| Area | Decision |
|---|---|
| Repository | Public, as a portfolio piece; `java-reference/` untracked; DPP and DECtalk as submodules |
| Language | C++20. C++23 was considered and deferred: on MSVC it means `/std:c++latest`, where library features still move between compiler updates |
| Data | SQLite, our own thin wrapper, one connection behind a mutex, WAL, FTS5 on |
| Regex | CTRE, compile-time and non-recursive |
| Tests | Catch2 v3, CTest, ports and hand-written mocks, ASan, libFuzzer, GitHub Actions |
| URL replacement | Plain message, never a reply; 2 attempts per mirror; Retry open to anyone |
| Reaction statistics | Received, given and self-reactions tracked separately; kept forever; emoji aliases |
| DECtalk | Our own CMake, built as a DLL, streaming, denylist sanitizer, trusted-list gating |
| LLM | Anthropic default (`claude-haiku-4-5`), tool-based memory, versioned documents, $20/month with a $2/day cap |

---

## 4. Project structure

What exists now, with planned files marked. The rule is that everything except
`main.cpp` lives in `latibot_core`, so the tests link exactly the code that
ships (§17.2).

```
CMakeLists.txt          options, dependencies, subdirectories
CMakePresets.json       msvc (default), asan, fuzz, ninja-tidy
conanfile.py            openssl, zlib, opus, sqlite3, ctre, catch2
.clang-format .clang-tidy
.github/workflows/ci.yml
cmake/                  warnings, sanitizers, shared helpers; dectalk.cmake (phase 4)
tools/                  catalog generator, clang-tidy and clang-format wrappers
.vscode/                tasks, launch, IntelliSense, the grouped test tree
docs/
  porting/              this plan and its predecessors
  features/             what the bot does, for users
  testing/              strategy and the generated catalog
  ideas/                parked ideas
src/
  main.cpp              load config, set up certificates, build the bot, run
  core/                 -> latibot_core
    bot.*               the shell: wires DPP events to the core
    version.*
    config/   bootstrap.*  guild_settings.*
    db/       database.*  statement.*  migrations.*  backup.*
    discord/  raw_api.*  dpp_gateway.*  dpp_http_client.*
    commands/ registry.*  basic.*  trigger.*  bots.*  preflight.*
              nickname.*  midnight.*  urlrepl.*  linkstats.*
              speak.*  voice.*  llm_admin.*                (phases 4-5)
    events/   message_pipeline.*  goodbye.*  triggers.*  bot_allowlist.*
              nicknames.*  nickname_import.*  midnight.*
              url_rules.*  url_replacer.*  embed_watch.*  replacements.*
              reactions.*  legacy_replacements.*  backfill.*
    ui/       paginator.*
              panel.*  modal_forms.*                       (when a second panel exists)
    audio/    voice_mixer.*  resample.*  wav.*
              dectalk_engine.*  dectalk_sanitizer.*  voice_params.*   (phase 4)
    llm/      provider.hpp  anthropic.*  conversation.*  memory.*
              tools.*  documents.*  responder.*  spend.*   (phase 5)
    ports/    clock.hpp  discord_gateway.hpp  http_client.hpp  tts_engine.hpp
    util/     log.*  text.*  env.*  ca_certificates.*  url_scan.*
tests/
  unit/  db/  mocks/  support/  fuzz/  live/  fixtures/
third_party/  DPP/  dectalk/
data/                   runtime: bot.db, backups/, import/, ca-bundle.pem (gitignored)
```

---

## 5. Core infrastructure ✅

### 5.1 Configuration ✅

Three layers, deliberately separated by how often each changes and by who edits
it.

**`config.json`** — global, rarely edited, read once at startup. Unknown keys
are rejected rather than ignored, so a typo is reported instead of silently
doing nothing. Keys: `log_level`, `database_path`, `backup_directory`,
`backups_to_keep`, `backup_interval_minutes`, `track_nicknames`,
`llm_provider`, `llm_model`,
`spend_cap_daily_usd`, `spend_cap_monthly_usd`, `llm_tool_rounds`,
`trusted_guilds`, `trusted_users`. Discord ids are given as **strings**,
because a JSON number cannot hold a snowflake exactly.

**Secrets from the environment only:** `DISCORD_BOT_TOKEN`,
`ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, and `LATIBOT_TEST_TOKEN` for live tests.
Startup fails loudly and immediately without the bot token. A gitignored `.env`
beside the executable is loaded first as a convenience for local runs, and never
overrides a variable the environment already set, so CI and containers are
unaffected (§21.1).

**Per-guild settings in SQLite**, edited at runtime through commands and
panels, with typed getters that fall back to a caller-supplied default. A value
that cannot be parsed falls back rather than throwing: one hand-edited row
should not take a feature down.

### 5.2 Database ✅

Our own wrapper — `database`, `statement`, `transaction` — over `sqlite3`, with
**one connection guarded by a mutex**, `journal_mode=WAL`, `foreign_keys=ON`,
snowflakes as `INTEGER` (exact at 64 bits, unlike JSON numbers), times as Unix
seconds UTC, and `PRAGMA user_version` migrations run inside a transaction at
startup. A failing migration rolls back and keeps the previous version.

Migrations are **append-only**: a shipped migration is never edited, because
every running install has already applied it. Migration 3 is the first to
`ALTER TABLE` a populated table, and it has a test that migrates to the previous
version, inserts rows, upgrades, and checks the rows survived — the fresh
database path would not have caught a mistake there.

Applied so far:

```sql
-- 1: guild_settings
guild_settings(guild_id, key, value)                     -- PK (guild_id, key)

-- 2: triggers
triggers(id, guild_id, pattern, match_mode, cooldown_s, enabled)
trigger_responses(trigger_id -> triggers(id) ON DELETE CASCADE, response, weight)

-- 3: bot allowlist
allowed_bots(guild_id, bot_id)                           -- PK (guild_id, bot_id)
triggers.respond_to_bots                                 -- added, default 0

-- 4: nickname_history
nickname_history(id, guild_id, user_id, nickname NULL, changed_by NULL,
                 changed_at, source, imported_raw NULL)

-- 5: midnight_messages
midnight_messages(id, guild_id, channel_id, timezone, message, enabled, last_fired_date NULL)

-- 6: url_replacement
url_rules(guild_id, domain, position, host, translate_suffix NULL)
url_opt_outs(guild_id, user_id)
known_mirrors(guild_id, host, domain)                    -- never pruned (§9.7)
replacement_messages(message_id PK, guild_id, channel_id, original_message_id NULL,
                     original_author_id NULL, state, created_at, retried_at NULL)
                                                  -- state: pending | ok | failed | retrying
replacement_links(message_id -> replacement_messages, position, original_url,
                  domain, spoilered)

-- 7: reaction_stats
reactions(message_id -> replacement_messages, user_id, emoji_key, reacted_at NULL)
reaction_log(id, message_id, user_id, emoji_key, action, at)    -- live only
emojis(emoji_key PK, name, animated)
emoji_aliases(guild_id, emoji_key, canonical_key)

-- 8: backfill_progress
backfill_progress(guild_id, channel_id, since, until NULL, oldest_scanned_id NULL,
                  complete, updated_at)
```

`replacement_messages` lost the plan's `domain` and `alternate_index` columns
to `replacement_links`: a replacement carries up to five links, each on its own
mirror, which one row per message could not describe. `known_mirrors` and
`emojis` are new: the first so a changed rule does not hide its old messages
from the backfill, the second because a custom emoji is only an id in
`reactions` and has to be shown by name.

Planned, refined as each phase lands:

```sql
llm_triggers(id, guild_id, pattern, match_mode, context_prompt, probability,
             cooldown_s, enabled, created_by)
llm_settings(guild_id, key, value)
llm_blacklist(guild_id, kind, target_id)
llm_memory(id, guild_id, subject_user_id NULL, content, created_at, …)   -- + FTS5
llm_documents(guild_id, kind, version, content, edited_by, edited_at, note)
llm_usage(id, guild_id, model, input_tokens, output_tokens, cost_usd, at)
tts_voices(guild_id, name, base_voice, params, created_by, updated_at)
```

**Backups** ✅ use SQLite's online backup API, so a copy can be taken while the
bot runs; `create_backup` writes a timestamped file and rotates to the newest
N. Tested including taking a backup during an open write transaction and
checking the copy passes `PRAGMA integrity_check`. A cluster timer runs it
every `backup_interval_minutes`, keeping `backups_to_keep`; a failure is logged
and never propagates, and either setting at zero turns backups off, which the
startup log says.

### 5.3 Command registry ✅

A `command` declares its name, description, aliases, the **bot** permissions it
needs, the **member** permissions Discord should default to, and whether it is
guild-only. Handlers return `dpp::task<void>` and stay thin: convert the event
into plain data, call a core function, carry out the answer (§17.3). Aliases
build a second `dpp::slashcommand` pointing at the same handler.

The registry can report the union of every registered command's bot
permissions, which is what the preflight check uses (§7). A bulk registration
with an empty list would **delete every registered command**, so an empty
registry is refused with a warning rather than published.

### 5.4 Message pipeline ✅

One `on_message_create`, feeding ordered stages. The order is a list, so
changing it is one line:

```
ignore self / bots this guild has not allowed
→ admin goodbye phrase          (consumes)
→ URL replacement               (does not consume)
→ simple trigger responses      (does not consume; suppresses advanced triggers)
→ LLM addressed / advanced trigger (consumes)               phase 5
```

This exists because of a real bug in the Java version: `onMessageReceived`
handled each feature with early `return`s, so **a message containing both "420"
and a link got "nice" but no link replacement**. Making "consumes or not"
explicit per stage fixes the class of bug rather than the one instance.

A stage is a function from an `incoming_message` — plain data — to the actions
it wants taken, which is what makes it testable with no gateway. `bot::carry_out`
is the only code that touches Discord. A stage that throws is logged and the
rest still run: an exception reaching DPP's event thread would end the process.

**Who is heard.** Our own messages are always ignored; an answer to ourselves is
a loop with no exit. Other bots are ignored unless the guild allowed that one
(§11.1). Reaching the stages is only permission to be considered — each stage
still decides whether it answers a bot.

### 5.5 Raw-API helper ✅

A thin wrapper over `cluster::post_rest` / `post_rest_multipart`, which already
carry authentication and rate-limit handling. It adds awaitables returning
parsed JSON or a typed error, endpoint building, and a multipart variant for
hand-built `payload_json` plus files. Known uses: sending voice messages
(§12.8), suppressing embeds on someone else's message (§9.2 — a PATCH carrying
only `flags`), and any component type DPP lacks (§2.1).

### 5.6 Ports ✅

Four interfaces wrap the outside world so the core can be tested without it:
`clock`, `discord_gateway`, `http_client`, `tts_engine`. Each has a
hand-written mock in `tests/mocks/`. See §17.3.

---

## 6. Basic commands ✅

`/ping`, `/say`, `/status`, `/join`, `/leave`, `/shutdown`, plus `/goodbye` to
configure the phrase. Exact options and replies are in
[docs/features/](../features/README.md).

**The goodbye phrase** is the one that needs care. The author must have
Administrator **in that guild** — checked on the member, not the user globally —
and the phrase must be essentially the whole message, so quoting it in
conversation cannot stop the bot. Comparison is on lowercased word sequences:
punctuation makes no difference, but a trailing emoji does, because bytes above
127 count as word characters. This is deliberate — it stops the bot, and near
enough is not good enough. The reply goes out, then a short delay, then
shutdown, so the bot does not vanish mid-sentence.

The phrase is per guild and configurable. Turning it off stores an **empty
string rather than erasing the row**, because erasing would fall back to the
default on the next read, which is the opposite of off.

---

## 7. Permission preflight ✅

Each command and passive feature declares what it needs. Checked per guild on
`on_guild_create` — which also fires for guilds the bot was already in when the
gateway connects, so it covers startup and later joins without a separate sweep
— and reported at WARN. **It never exits.** The Java version called
`System.exit(-3)` when its one hardcoded guild was missing a permission; with
multiple guilds, one under-permissioned server must not take the bot down. The
feature degrades there instead.

Administrator short-circuits the check, since Discord treats it as everything.
Only the *missing* bits are reported, named rather than printed as a bitmask,
with unknown bits shown as hex. The list grows as features land:
`VIEW_AUDIT_LOG` (§8), `MANAGE_NICKNAMES` (§8), `READ_MESSAGE_HISTORY` (§9.7),
`MANAGE_MESSAGES` and `EMBED_LINKS` (§9.2), `CONNECT` and `SPEAK` (§12). Embed
Links was not on the original list and is the one most worth having there: a
replacement posted without it shows no preview, which looks exactly like a
broken mirror.

---

## 8. Nickname tracking ✅

### 8.1 Attribution ✅

1. `on_guild_member_update` with a changed nickname → write a history row
   immediately, `changed_by = NULL`. Recording must never depend on the audit
   log arriving.
2. `on_guild_audit_log_entry_create` for `aut_member_update` with a `nick`
   change → fill in `changed_by`, `source = 'audit_log'`.
3. No match after ~10 s → one `co_guild_auditlog_get` query as a safety net,
   covering missed gateway events and reconnects.
4. Still unmatched → stays **`unknown`**. Self-changes *are* audit-logged, so an
   unmatched row is genuinely unknown rather than presumed self. The Java
   version guessed "self", which was often wrong.
5. **An audit entry whose actor is the bot never overwrites `changed_by`.**

This fixes a real correctness bug: today any nickname change made through the
Discord UI by a moderator is recorded as if the target changed it themselves,
because only `/nickname` produced a correlation hash.

**`/nickname`** writes its own row first (`changed_by = invoker`,
`source = 'command'`) and registers a pending expectation
`(guild, target, new_nick)` with a ~30 s TTL, so the member-update event does
not add a duplicate. On failure the row is removed and the caller gets an error.

**`X-Audit-Log-Reason` is dropped.** It was approved in v3 and then reversed:
DPP takes that header from a cluster-wide slot that the next request from any
thread can consume, so with coroutines running concurrently it cannot be
attached reliably — and attaching the *wrong* reason to an unrelated moderation
entry is worse than attaching none. Discord's audit log keeps showing the bot as
the actor, as it has for years; "who actually did it" comes from
`nickname_history`.

The Java owner-confirmation flow stays dropped. The server owner's nickname
cannot be changed by bots, so `/nickname` just says so.

### 8.2 Storage and display ✅

Raw ids are stored and names resolved at display time. The Java version resolved
live `Guild` and `Member` objects at load time, which is why loading had to wait
for the gateway and why it broke for departed members. Three Java edge cases are
fixed while porting: `getLatestNickname()` indexing an empty list, cleared
nicknames stored as null, and a null member during load.

`/nicknames` uses the shared paginator with ◀/▶ buttons and page state in the
`custom_id`, falling back to a `.txt` attachment for very long histories. The
Java version gave up entirely past 2000 characters.

### 8.3 Importing `nicknames.json` ✅

The Java times are local wall-clock in US Central with no zone recorded:

```cpp
const auto* ct = std::chrono::locate_zone("America/Chicago");
// ambiguous (November fall-back, 1:00-1:59):  choose::earliest
// nonexistent (March spring-forward, 2:00-2:59): catch and shift forward an hour
```

`America/Chicago` carries the full DST history including the 2007 rule change.
Ambiguous times are at most an hour off, on one day a year, and there is no way
to know which was meant. Each row keeps the original text in `imported_raw`, so
the conversion can be redone. Tested across CST, CDT, both edge cases and
pre-2007 dates.

### 8.4 Startup reconciliation ✅

Changes made while the bot was offline are recorded with `source = 'startup'`
and `changed_by = NULL`.

### 8.5 Later: appearance tracking

Avatars, per-server profiles, role colours including gradients, and decorations.
Deferred, and expected to need a raw API read (§5.5) plus a generated image,
since an embed colour cannot represent a gradient. Migrating the schema when it
arrives is fine.

---

## 9. URL replacement ✅

The most-used feature, and the one with the most accumulated problems. Each
reported bug has an identified root cause in the Java source, and each has a
test written against the behaviour it got wrong (§17.8).

### 9.1 Scanner ✅

CTRE, `ctre::search_all` over the message, host looked up in `url_rules`, output
spliced using match offsets.

*As built:* `util::find_links` also trims what Discord leaves off a link
(trailing punctuation, a closing bracket the link did not open) and marks links
inside code and links written as `<…>`; `plan_replacements` leaves both alone,
since Discord was never going to embed them. A link is matched by host after
lowercasing and dropping `www.`, credentials and port. At most five links are
replaced per message, and links are dropped from the end if the post would pass
Discord's 2000 characters. `explain_links` gives every link a verdict and
`plan_replacements` is that list filtered, so the dry run in `/urlrepl test`
cannot disagree with a real message.

**Bug: multiple links in one message.** The Java regex wrapped the URL pattern
in greedy `(?<before>.*)` and `(?<after>.*)` groups, so on a message with two
links the greedy `before` consumed as far as possible and the first `find()`
swallowed the whole string, matching one URL before the loop exited. Compounding
it, `index = index % replacements.size()` reassigned the shared index inside the
loop, corrupting alternate selection across links. The fix drops the capture
groups, collects every match's offsets, and tracks the alternate index **per
link**.

**Bug: spoilers not preserved.** `matcher.group("before").split("||")` — Java's
`String.split` takes a *regex*, and `"||"` as a regex is `empty|empty|empty`,
which matches everywhere and splits into individual characters. So
`.split("||").length % 2 == 0` was really testing whether the character count
was even. The fix counts literal `||` before the match position; an odd count
means the link is inside an open spoiler and the replacement is wrapped too.

**`translate_suffix`** (`/en`, for mirrors that support auto-translation) is
always applied where a rule has it, by parsing the URL rather than concatenating,
so query strings, fragments and trailing slashes survive.

`std::regex` is avoided deliberately: it is slow everywhere and MSVC's is
recursive, so it can overflow the stack on long input — a crash risk on a path
that sees every message. CTRE compiles the pattern into ordinary C++ at build
time, so a typo is a compile error, and matching uses no heap. A test still
feeds 100 KB of URL-like fragments and checks it finishes quickly.

### 9.2 Posting ✅

The replacement is a **plain message, never a reply**:

```
:link: [_](<replaced url>)
```

wrapped in `||…||` when the original was spoilered, with the original message's
embeds suppressed. Replies were tried during the Java version's development and
rejected on looks; the bot is fast enough that its message is almost always the
next one anyway. This is also the format the backfill expects to find (§9.7).

*As built:* one `🔗 [_](link)` line per link, with the emoji itself rather than
the `:link:` shortcode (Discord shows the two identically; the Java bot sent the
emoji too), and the spoiler bars around the link rather than the line. Posting
comes before suppressing the original, so a post that fails leaves the original
its preview. Notifications are suppressed and no mention is parsed. The
suppression is `discord_gateway::set_embeds_suppressed`, a flags-only PATCH,
which is the one edit Discord allows on somebody else's message.

### 9.3 Embed verification ✅

Polling was the Java approach and produced false failures: it checked 5 seconds
later, treated `embeds.size() < replaceCount` as failure, retried up to 10 times
editing the visible message each time, and `replaceCount` was wrong whenever
several links were involved. Slow networks looked identical to failure, and some
links legitimately never embed.

Event-driven instead. Per link, each mirror gets **2 attempts**:
`alt1, alt1, alt2, alt2, …`. A link counts as embedded when `on_message_update`
for our message shows an embed for it; otherwise a ~6 s fallback timeout
advances to the next attempt and edits our message. Messages with several links
track each link independently. With coroutines the whole flow is one linear
function.

*As built:* not one coroutine per message but a state machine,
`events::embed_tracker`, fed by `on_message_update` and a one-second cluster
timer, returning what to edit as plain data. That tests with a mock clock and no
waiting at all, and one timer serves every message. A preview is matched to a
link by path, since mirrors usually report the original site as their URL,
with left-over previews going to waiting links in order; several previews with
one URL (a post with four images) all belong to one link. A preview update can
overtake the reply to our own post, so updates for a message nobody is watching
yet are held for thirty seconds (§21.11). If one link of several embeds, the
replacement counts as working and the others stay on their last mirror. The
timeout is one constant rather than the per-guild setting §20 suggested; nobody
has needed another value.

### 9.4 Failure and Retry ✅

When every attempt fails, the Java version deleted its message. Instead:

1. The original message's embeds are **un-suppressed**, so its normal preview
   returns.
2. Our message is **kept**, edited to a short failure note with a **Retry**
   button (`custom_id = urlretry:<our message id>`), its own embeds suppressed.
3. **Anyone can press Retry** — it runs one pass, one attempt per mirror.
   Success restores the normal replacement and re-suppresses the original;
   failure records the retry time and keeps the button.

The state this needs — original message id, link, mirrors tried — lives in
`replacement_messages`, so Retry still works after a restart.

*As built:* the links live in `replacement_links`; the mirrors are not stored,
because Retry uses the rule as it is when pressed, which is usually why
somebody presses it. The button's answer is the first attempt itself (an
`UPDATE_MESSAGE` response), so a second press finds the state already
`retrying` and is told so.

### 9.5 Management ✅

`/urlrepl` with no arguments opens an ephemeral panel: one row per rule with
the domain and its ordered mirrors, Edit and Delete buttons, Add rule, and
paging. Edit and Add open a modal with the domain and the mirrors **one per
line in priority order**, which makes reordering a matter of rewriting lines.
Delete confirms in the panel itself rather than a second message, so nothing is
left behind if it is ignored.

Commands stay for quick use: `/urlrepl list | set | remove | test`, with domain
autocomplete — the single biggest usability win, since it removes the "what did
I call it again" problem. `test <url>` is a dry run, which is what makes the
bugs above debuggable. Per-user opt-outs are per guild and **persisted**; the
Java `/toggle` wrote to an in-memory list that reset on restart. A missing rules
file starts an empty ruleset with a warning rather than throwing at class-load,
as the Java static initializer did. Webhook mode is gone.

*As built:* a command with subcommands cannot also run bare, so the panel is
`/urlrepl panel` (§21.13). `test` takes a whole message rather than one URL,
which is what shows the spoiler and code rules at work. A mirror's translation
suffix is written on the mirror itself, `fxtwitter.com/en`. The opt-out is its
own command, `/urltoggle`, because default permissions are per command:
`/urlrepl` needs Manage Server and opting yourself out should need nothing.
`UrlReplacements.txt` is imported into each guild once, from beside the
database, never over an existing rule; a missing file is the ordinary case and
only means the guild starts with no rules.

*Added after phase 3:* replacement is **off per guild until it is turned on**,
with `/urlrepl enable | disable` or a button on the panel, both behind the same
Manage Server. The Java bot replaced links in every server it was in; the bot
now joins servers for other features too, and rules alone (the import creates
them everywhere) should not be enough to start rewriting links. The switch is
the `url_replacement_enabled` row in `guild_settings`, so it needs no migration
and outlasts a restart. Off stops the pipeline stage and Retry; `test` still
works so rules can be tried first, and posted replacements keep counting
reactions.

### 9.6 Reaction statistics ✅

People-facing stats, in three groups:

- **Received:** reactions on a replacement message count for the **person who
  posted the original link** — "who gets the most 💀".
- **Given:** the same rows counted by the **person who reacted** — "your top 3
  reactions".
- **Self-reactions** are recorded but **excluded from both**, and reported
  separately as their own stat.

Because every row holds both the poster and the reactor, one table answers all
three: filter `user_id <> original_author_id` for the normal stats and
`user_id = original_author_id` for the self-reaction one.

Emoji are keyed `u:<unicode>` or `c:<custom emoji id>`, and **aliases** merge
emotes that should count as one — the same emote from another server, or one
deleted and re-added, which Discord treats as a new emoji. Aliases are applied
**when stats are read** (`COALESCE(canonical_key, emoji_key)`), so adding one
immediately affects all history. Managed with
`/linkstats alias add | remove | list`, plus `/linkstats emojis`, which lists
likely duplicates by name.

Live tracking updates `reactions` and appends to `reaction_log` on add and
remove. Rows are kept forever — the data is wanted and it does not grow fast.

*As built:* `/linkstats top [by] [emoji] [since] [until] [domain]` ranks
received, given, self, or the emojis themselves, ten a page, with the board's
filters carried in the buttons' `custom_id`; `/linkstats user` is one person's
received and given totals with their top three emojis each. Both are public.
Unicode keys drop U+FE0F, so the two spellings of one heart are one key. Alias
chains are flattened when written and loops refused, so reading needs a single
join. Emoji options autocomplete from the emojis actually used, and a bare name
is looked up the same way.

### 9.7 Backfill ✅

`/linkstats recompute since:<date> [until:<date>] [channel:<#c>]`, admins only,
to recover years of existing reactions.

Backfilled rows are **ordinary rows** with no source marker. One caveat, which
is a Discord limitation rather than a choice:

> **Discord's API does not say when a reaction was added.** Listing reactions
> returns *who*, never *when*. Backfilled rows therefore have
> `reacted_at = NULL`, and date-filtered stats fall back to the message's own
> timestamp. Everything else — who posted, who reacted, which emoji — is
> recovered exactly.

**Identifying our replacement messages.** A message counts when it was written
by the bot's user **and** contains at least one link whose host is a known
mirror, current or historical. That rule works across every format below, since
they all contain the replaced link.

**The six historical formats:**

| # | Format | Parser notes |
|---|---|---|
| 1 | Full copy of the original text, link replaced, **sent as a reply** | The reply reference gives the original message directly; use it when present |
| 2 | **Webhook mode**: original deleted, webhook posts as the user, `<original> [.](replaced)` | **Skipped** — not authored by the bot user, and the original is gone. Few of these have reactions |
| 3 | Full copy of the original text, link replaced, plain message | Link extracted from anywhere in the text |
| 4 | `[.](<replaced>)` | |
| 5 | `:link: [.](<replaced>)` | |
| 6 | `:link: [_](<replaced>)` (current) | |

Anything matching the bot-plus-mirror-host rule but no known format is
**counted and logged with its id**, never guessed at. That list is part of the
final report.

**Finding the original poster.** Walk backwards from our message through the
same channel and take **the first earlier message containing a link**, skipping
messages without one. That covers someone chatting between the original and the
replacement, which is the only realistic gap given how fast the bot answers. As
a check, the link's path should match ours once the mirror host is mapped back
to the real domain; a mismatch is logged rather than silently accepted. Format 1
skips all of this.

**Reactions** come from `co_message_get_reactions` per emoji, paged 100 at a
time, since the message object carries only counts.

**Running it:** history walks backwards in pages of 100 (`co_messages_get` with
`before`) until it passes `since`; a progress message updates every few hundred
messages; `/linkstats recompute cancel` stops it; `backfill_progress` lets an
interrupted run resume. **Re-running is safe** — for each scanned message the
reaction rows are rebuilt from what Discord currently shows. The final report
gives messages scanned, replacements found, attributed, unattributed, unparsed
(with ids), and reactions recorded.

*As built:* `recompute start | cancel`, a subcommand group, since Discord will
not have `recompute` be both a subcommand and a group (§21.13); Manage Server,
following the user-facing spec. A path mismatch is not accepted: the search
keeps going, up to ten earlier links, and failing that the replacement is left
unattributed, counted as such in the report and logged by id (§21.12). Each
page is read together with the next, so an original just across a page boundary
is still found, and a reply whose original is further back is fetched. Progress
is saved per channel for one date range; running the same range again resumes,
`fresh:true` starts over, and a finished channel is skipped. A reaction lookup
that fails leaves the message's existing rows alone rather than erasing them. A
replacement the bot recorded as it posted it keeps its author. Old
replacements get `replacement_links` too, filed under the site each mirror stood
in for, so the site filter covers history. Not covered: threads, when no
channel is named, and super-reactions, which the reactions endpoint lists
separately and DPP does not ask for.

---

## 10. Midnight ✅

Per guild, any number of `{timezone, channel, message, enabled,
last_fired_date}` entries.

**The random-fire bug, diagnosed.** The Java version used
`ScheduledExecutorService.schedule`, which measures its delay against a
**monotonic clock** while the delay was *computed* from the **wall clock**.
Those diverge when the machine sleeps or the system clock jumps, so on resume
the pending task fires at whatever wall-clock time that happens to be — then the
handler recomputes the next run correctly, which is exactly the "goes back to
normal for a while" behaviour that was observed. (The `if (now.isAfter(nextMidnight))`
branch logging `"huh???"` is also unreachable, since `nextMidnight` is always
constructed as tomorrow.)

**The fix is to poll the wall clock.** A 30 s tick compares
`zoned_time{tz, now}`'s local date against `last_fired_date` and fires once the
local time is ≥ 00:00:05, saving the date in the same statement that claims it.
That is immune to suspend, clock jumps and DST, and the saved date makes a
double post impossible even across a restart at 00:00:30.

**A midnight the bot was not running for is skipped, not posted late.** The
window closes five minutes into the local day: past that, the bot cannot have
been there for it, and yesterday's midnight message over breakfast is worse
than none. Nothing is written down for a missed day — `last_fired_date` means
"posted", and the time since midnight only grows, so the rest of that day
answers "wait" on its own and the next midnight starts clean. The window is
deliberately wider than the tick, so jitter and a quick restart still post.

`/midnight list | add | edit | remove | toggle`, with timezone autocomplete.
`verdict_for(entry, now)` is a pure function returning *wait*, *post* or
*missed*, so both DST nights and the overnight gap are unit-testable. MSVC's
`<chrono>` uses Windows' ICU time-zone data (1903+), so no dependency is needed.

---

## 11. Simple triggers ✅

Per guild: a pattern, a **match mode per trigger** (whole word or substring —
no user-written regex, which is a performance and stack-overflow risk), a
per-channel cooldown that is configurable and **may be 0** (default 30 s), an
enabled flag, weighted responses, and whether it answers bots (§11.1).

Matching is case-insensitive and literal. Whole-word mode checks **every**
occurrence for word boundaries, so "4200 and also 420" matches. `choose()` picks
a weighted response from a caller-supplied roll, which is what makes the
distribution testable; it returns nothing when every weight is zero or negative.
A zero cooldown always fires, which is the documented way to turn cooldowns off.

Managed with `/trigger add | edit | remove | list | panel`. Seeded with the Java
defaults (`420`, `4:20`, `69` → "nice") only when a guild has none, so deleting
them all does not bring them back on the next restart. The stage does not consume
the message, so one containing both "420" and a link gets both answers.

### 11.1 Which bots the bot hears ✅

Per guild, an allowlist of bot user ids, **empty by default** — every bot is
ignored until someone says otherwise, because two bots answering each other is a
loop nobody asked for. Managed with `/bots allow | deny | list`.

Hearing and answering are **separate decisions**, and only the first is shared.
The allowlist says which bots reach the pipeline at all; each feature then opts
in for itself, because a trigger firing on another bot's message is a much
smaller commitment than an LLM conversation with one. For triggers that opt-in
is `respond_to_bots`, off by default.

This was originally planned as part of the LLM phase (§14.4). It moved here
because §5.4 has always had "ignore non-allowlisted bots" at the top of the
pipeline, which every message-reading feature needs — not just the LLM. What
remains in phase 5 is the *pacing*, which is genuinely LLM-specific.

---

## 12. DECtalk ⏳ (phase 4)

### 12.1 Build

`cmake/dectalk.cmake` builds from the submodule without modifying it: the
`dectalk` **DLL** (§2.2 for the source list, defines and exports), the
`dectalk_dic` host tool, and a custom command that compiles `dtalk_us.dic` next
to the exe on every clean build, the same way `dpp.dll` is copied. Warnings are
relaxed for this target only — it is 1990s C.

### 12.2 Startup

```cpp
TextToSpeechStartupExFonix(&handle, WAVE_MAPPER, DO_NOT_USE_AUDIO_DEVICE,
                           &on_dectalk_message, /*instance*/ 0,
                           dictionary_path.c_str());   // absolute
```

The instance parameter is **not** a pointer (§2.2); with one engine the callback
reaches it through a static. The absolute dictionary path removes the registry
and working-directory dependency that broke dictionaries in the Java version.

### 12.3 Streaming synthesis

1. `TextToSpeechOpenInMemory(handle, WAVE_FORMAT_1M16)` once, kept open.
2. A ring of ~4 buffers of ~0.25 s (11025 Hz mono 16-bit ≈ 5.5 KB each), queued
   with `AddBuffer` and also pushed onto our own deque.
3. `TextToSpeechSpeak(handle, text, TTS_FORCE)`.
4. On each buffer message: take the front buffer from the deque (the callback's
   pointer is truncated, §2.2), hand the PCM to the worker, requeue the buffer.
5. The worker resamples 11025 Hz mono → 48 kHz stereo and feeds the mixer, so
   audio starts about a quarter second in.
6. End of utterance: `TextToSpeechSync`, then `ReturnBuffer` to flush the tail.

The resampling is about 40 lines: 11025 → 48000 is not an integer ratio
(4.3537…), so it needs a fractional-position resampler rather than sample
duplication — linear interpolation is ample for lo-fi robotic speech — and mono
→ stereo is writing each sample twice. DPP wants 48 kHz stereo 16-bit in frames
of `dpp::send_audio_raw_max_length` = 11520 bytes, which is exactly 20 ms.
**ffmpeg is therefore a music-only dependency**, not a TTS one.

`/chat` voice messages use the same path in collect mode.

*Spike questions:* whether `AddBuffer` may be called from inside the callback,
buffer size against start-up latency, and whether FIFO order holds across
`TextToSpeechReset`.

### 12.4 Threading

One worker thread owns the handle; requests arrive through a queue and complete
as awaitables, so commands `co_await` without blocking DPP's threads. Synthesis
is CPU work and must never run on an event thread. Serialising also means no two
requests share engine state.

### 12.5 Sanitizer, trust levels and reset

`dectalk_sanitizer::clean(text, trust)`, where `trust` is `trusted`, `user` or
`llm`:

| | `user` | `trusted` | `llm` |
|---|---|---|---|
| `[:play]`, `[:log]` | stripped silently | kept | stripped |
| `[:debug]`, `[:loadv]`, `[:setv]` | stripped | kept | stripped |
| `[:dv save]` | stripped | stripped | stripped |
| everything else | kept | kept | kept |

A **denylist**: inline commands are a feature, and stripping is silent — the
command is removed and the rest is spoken, with no error.

**Who is `trusted`:** a user in `trusted_users`, **or** a user with
Administrator in a guild listed in `trusted_guilds` (§2.4). **LLM output is
never trusted**, whoever asked.

Parsing mirrors DECtalk's own matcher (`cm_cmd_match_comm`): `[:name args]`,
case-insensitive, accepted as soon as the prefix is unique — so `[:pla …]`
counts — and several commands chain inside one bracket
(`[:rate 200 :play "x"]`). Heavily tested and fuzzed (§17.5).

**Reset before every request**, because engine settings persist (§2.2): either
`TextToSpeechReset(handle, FALSE)` or an explicit preamble setting the requested
voice and resetting rate, mode and punctuation. The spike picks whichever also
clears `[:dv]` edits.

**Input cap:** 1000 characters for `/speak`, configurable per guild.

### 12.6 `/speak`, custom voices, voice lab

`/speak text [voice] [rate] [volume]`, with `voice` autocompleting the built-in
voices plus this server's saved ones. Volume goes through
`TextToSpeechSetVolume`, which is the fix for the Java version's "make dectalk
louder lol" TODO.

Custom voices live in `tts_voices` as a base voice plus ordered `[:dv]` pairs,
rendered as a preamble at speak time and clamped to each parameter's documented
range. **Anyone can create one; the creator or an admin can delete it.**

**`/voice lab`** opens an ephemeral panel: current parameters grouped as in
§2.2, a **▶ Test** button that speaks a test phrase in the bot's voice channel,
per-group **Edit** modals (≤5 inputs each, so the common parameters sit on the
first two), a **Raw** modal for pasting or copying a whole `[:dv …]` string,
**Save as…**, and **Reset**. The working draft is kept per user for ~30 minutes,
so closing the panel by accident does not lose it.

### 12.7 Stopping and limits

- **`/tts stop`** (trusted users, admins, or whoever queued the current
  utterance): flush the engine, clear the queue, drop queued mixer audio, call
  `stop_audio`. Music resumes as normal.
- **`/tts skip`:** the current utterance only.
- **Duration cap:** 60 s of generated audio per utterance, configurable per
  guild. This is what contains `[:rate 75]` on long text, long `[:pause]` and
  `[:tone]`, without having to anticipate each trick.

### 12.8 `/chat` voice message

Synthesize, wrap in a 44-byte WAV header, compute the duration and the
256-bucket peak waveform, and send through `post_rest_multipart` with
`flags: 8192`. No temp files anywhere in this path.

**The Java waveform was garbage.** `ChatTestCmd` summed **raw signed bytes of
the WAV file**, header included, and averaged them into a byte. Since 16-bit PCM
bytes are roughly symmetric around zero, every bucket averaged to approximately
zero, so the waveform Discord displayed was noise. The correct version skips the
RIFF header, reads `int16` samples, takes the peak absolute amplitude per bucket
across 256 buckets, normalizes to 0–255, and base64s the result.

---

## 13. Mixer and voice sessions ⏳ (phase 4)

**There is exactly one `discord_voice_client` per guild.** Music and TTS feed
the same socket, so they cannot be fully independent — something has to
arbitrate. A `voice_mixer` per guild owns the connection and accepts audio from
prioritized sources; the music queue and the TTS engine stay separate modules
that know nothing about each other and both talk to the mixer. TTS pauses music
and resumes it afterwards, using DPP's `pause_audio` and track markers. Ducking
and overlay are later options.

**Voice sessions** replace an earlier same-voice-channel heuristic, which would
never have fired because the voice channel's built-in text chat is not used:

- `/voice start` — the bot joins the voice channel **you** are in, moving if it
  is elsewhere in that guild, and records
  `{guild, voice_channel, text_channel, started_by}`.
- While active: LLM replies in that text channel are **spoken as well as
  posted**, and `/speak` from anywhere in the guild goes to that channel. The
  posted text keeps the sanitized inline `[:commands]` **as written**, so what
  the model was trying to do with the voice is visible.
- Ends on `/voice stop` or `/leave`, on disconnect, or when no humans are left
  after a **30 s grace period** — long enough that a quick rejoin does not kill
  it — at which point the bot leaves. Auto-leave also applies after a plain
  `/join`, so the bot never sits alone in a channel forever.

---

## 14. LLM ⏳ (phase 5)

Designed fresh. The Java `ApiDriver` is ignored: it was fully written but never
wired up, and its prompt configuration does not survive contact with current
models.

### 14.1 Providers and models

An `llm_provider` interface, Anthropic by default, OpenAI optional. Provider and
model are per guild with the defaults in `config.json`.

| Model | ID | $/1M in | $/1M out | Notes |
|---|---|---|---|---|
| Claude Haiku 4.5 | `claude-haiku-4-5` | $1 | $5 | **Default** |
| Claude Sonnet 5 | `claude-sonnet-5` | $2 | $10 | A step up |
| Claude Opus 5 | `claude-opus-5` | $5 | $25 | Selectable, not expected |

Model ids are used exactly as written, with **no date suffixes**. The request
builder is **model-aware**: it sends `temperature` only to models that accept
it, and effort settings only to models that support them. Otherwise changing the
model in config produces 400s.

Two gotchas that break a direct translation of the Java prompt config:
**`temperature` is rejected with a 400** on current Claude models — the Java
`ApiDriver` set `1.5` for variety, which has to come from the system prompt
instead — and **`max_tokens` must cover thinking**, since thinking tokens count
toward the output budget, so the Java `maxCompletionTokens(100)` would truncate.
Give real headroom with a low effort setting rather than disabling thinking,
which has documented failure modes. Assistant prefill also 400s.

### 14.2 Reply mode

Text by default; spoken **and** posted while a voice session is active in that
guild and the message is in the session's text channel. Voice replies use the
DECtalk-aware prompt and the `llm` trust level, and the posted copy keeps the
sanitized inline commands rather than stripping them for display.

For voice, the system prompt carries a short curated reference of the inline
commands the model may use and asks for plain spoken text — no markdown, no
emoji. The prompt is guidance; the sanitizer is enforcement.

### 14.3 When it responds

- **Addressed:** @mention, a reply to one of its messages, or a message starting
  with its name. Consumes the message.
- **Advanced triggers**, separate from §11: `llm_triggers` rows with a pattern,
  match mode, a short user-written **`context_prompt`** describing *what* to say
  ("Someone mentioned pineapple pizza. Defend it with unreasonable passion"), a
  probability and a per-channel cooldown. *How* to say it comes from the shared
  **trigger style** document (§14.5), so each trigger's prompt stays short.
  Context window: 5 recent messages. Admin-managed by command and panel.
- Blacklists and the spend cap are checked first.
- **When a simple and an advanced trigger both match, the simple one wins** and
  the advanced one does not fire. That keeps the cheap, instant response in
  charge and avoids paying for a model call on a message that already has an
  answer.

### 14.4 Bot-to-bot pacing

The allowlist shipped in phase 1 (§11.1). What remains here is the pacing, which
exists because an LLM exchange is expensive and open-ended in a way a trigger
reply is not: at most ~6 consecutive bot turns per channel, a minimum delay
between turns, a daily cap, and a human message resets the counter. Optionally
only when a human started the exchange. All per-guild settings, to tune after
implementation.

### 14.5 Memory, settings and documents

**Short-term:** a rolling per-channel window bounded by message count **and** an
approximate token budget. **Admin-editable at runtime** through `/llm settings`
(panel plus modal), stored in `llm_settings` and read per request, so a change
applies to the next message: context message count, token budget, max output
tokens, per-user and per-channel rate limits, advanced-trigger context size.
Values are validated against documented ranges, and out-of-range input is
rejected showing the allowed range.

**Long-term:** `llm_memory` with FTS5, managed by the model through
`remember` / `recall` / `forget` tools. The tool loop is a **general framework**
— a registry of name, JSON schema and handler — capped at 4 rounds per reply, so
later features can add tools without touching the loop. The most relevant
memories are also injected up front so common facts need no tool call. Admins
get `/memory list | forget | clear`; users can remove their own.

**Documents** (`llm_documents`, kinds `personality`, `system`,
`trigger_style`): every edit is a new version, so nothing is lost and `revert`
is one command. `/llm personality view | edit | history | diff | revert`, and
likewise for the others. Editing uses a pre-filled modal — up to 5 sections ×
4000 characters — or a `.txt`/`.md` attachment for longer text.

- **`system` and `trigger_style`: admins only.**
- **`personality`:** gated by `personality_editor_role`, which **defaults to
  `@everyone`**, since it is meant to be a living document people tune. Point it
  at any role to narrow it. Edits go to the bot's own log rather than being
  announced in Discord; `history` / `diff` / `revert` show them on demand.
- **Prompt order:** fixed in-code rules → `system` → `personality` → memories →
  conversation. The personality section is labelled as style guidance that
  cannot override what is above it, which limits what an edited personality can
  do, including attempts to use it as a jailbreak.
- Token counts are estimated on save and warn when large, since documents are
  sent on every request. **Prompt caching** (`cache_control` on the stable
  prefix) is on from the start.

### 14.6 Guards

Per-guild blacklists of users and roles; per-user and per-channel rate limits;
an output token cap; a per-guild on/off switch. **Spend cap: $20/month and
$2/day**, computed from usage counts × per-model prices recorded in `llm_usage`;
the LLM switches itself off at the limit and tells admins. Keys from environment
variables only.

### 14.7 HTTP

`co_request` with the cluster's 60 s timeout (§2.1). A typing indicator while
waiting. No streaming responses — a Discord bot cannot edit a message per token
without hitting rate limits anyway.

---

## 15. Music ⏳ (unscheduled)

Queue commands stay **unregistered** rather than stubbed, so the slash menu is
not cluttered with commands that reply "not implemented". `/join` and `/leave`
are real, because TTS needs them.

The eventual design: an `audio_source` interface with two implementations — TTS
(PCM already in memory) and streamed media (yt-dlp resolves, ffmpeg decodes to
PCM) — both feeding the §13 mixer, with DPP's track markers owning playback
position instead of the hand-rolled `TrackManager`/`SongQueue` bookkeeping. yt-dlp
and ffmpeg become external runtime prerequisites.

## 16. Emote statistics ⏳ (unscheduled)

The expensive part is walking full channel history. The Java version re-read
everything on every invocation, which is what made it slow. The port scans
incrementally with per-channel high-water marks, reusing `backfill_progress`.
`commons-collections4 Bag` is a multiset — `std::unordered_map<snowflake, int>`.

---

## 17. Testing ✅

Strategy and conventions live in [docs/testing/](../testing/README.md); the
generated list is [Test_Catalog.md](../testing/Test_Catalog.md). Summarised here
because it is part of the plan rather than an afterthought.

### 17.1 Framework

**Catch2 v3** with CTest. It was chosen over GoogleTest and doctest for
readability, `SECTION`-based shared setup, and built-in `BENCHMARK` — which
matters here, since the URL scanner runs on every message and the resampler runs
in real time. gMock is not needed because the design uses a few small
hand-written mocks. `catch_discover_tests` registers each case individually, so
the VS Code Testing sidebar lists them one by one.

### 17.2 Build layout

```cmake
add_library(latibot_core STATIC ${CORE_SOURCES})
target_link_libraries(latibot_core PUBLIC dpp SQLite::SQLite3 ctre::ctre)

add_executable(LatiBot src/main.cpp)
target_link_libraries(LatiBot PRIVATE latibot_core)

option(LATIBOT_BUILD_TESTS "Build unit tests" ON)
option(LATIBOT_BUILD_FUZZERS "Build libFuzzer targets" OFF)
```

Both the bot and the tests link `latibot_core`, so tests exercise exactly the
code that ships. Every test carries **one component tag** plus any number of
trait tags; the component tag is the axis the catalog and the test tree group
by. `tools/Update-TestCatalog.ps1` fails if a test has no component tag, two of
them, or a tag nobody recognises.

### 17.3 Ports and mocks

**Functional core, thin shell.** Features are functions from plain data to
**decisions** as plain data — `plan_join(target, bot_channel)`,
`midnight::verdict_for(entry, now)`, `is_goodbye(content, phrase)`. The DPP handler
converts, calls, and carries out. Most logic then needs no mock at all.

**Ports** wrap the outside world where a feature needs I/O mid-logic:

| Port | Real | Mock |
|---|---|---|
| `clock` | `system_clock` | `mock_clock`: set and advance time |
| `discord_gateway` | the handful of `dpp::cluster` calls we use | `mock_discord`: records calls, returns scripted results and history pages |
| `http_client` | `co_request` | `mock_http`: replays recorded responses |
| `tts_engine` | the DECtalk worker | `mock_tts`: a tone of the right length |

There is no mocking framework: a mock that fits on one screen is easier to
trust. **Always drive coroutines with `sync_wait_for(2s)`**, never `sync_wait` —
a deadlock then fails a test in two seconds instead of hanging the suite.

### 17.4 Database tests

A fresh `":memory:"` database per test, running the real migrations. It is fast
and it tests the actual SQL, which is the part most likely to be wrong. Covers
0 → latest, in-place upgrades with existing rows (§5.2), importers over
synthetic fixtures including empty, malformed and edge-case timestamps, and a
backup taken during an open write transaction that then passes
`PRAGMA integrity_check`.

### 17.5 Coverage per feature

| Feature | Tests |
|---|---|
| URL scanner ✅ | multiple links, spoilers (including the even-length case Java got wrong), `/en` with query/fragment/slash, unknown domains, opt-outs, code and `<link>`; 100 KB pathological input and 50 000 trailing brackets under a time limit; a hidden `BENCHMARK` (`[!benchmark]`); fuzzed |
| Embed flow ✅ | tracker tests with `mock_clock`: success, the alt1-alt1-alt2-alt2 schedule, per-link tracking, a preview that overtakes the post, all-fail → un-suppress + button, retry success → re-suppress, retry failure; posting and carrying out through `mock_discord` |
| Legacy parser ✅ | each of the six formats; format 2 skipped; unknown shapes reported not guessed; the preceding-link rule with chat in between, with no candidate, with a nearer link that is not ours, past the candidate limit; fuzzed |
| Reaction stats ✅ | received / given / self split, alias merge and un-merge, chains flattened, loops refused, date filters for live and backfilled rows, site filter, paging; backfill idempotency, resume, cancel, an unreadable channel, a failed reaction lookup keeping old rows |
| Nicknames ✅ | pending expectation claimed once and expiring, bot-as-actor never overwrites, first attribution wins, unmatched → unknown, cleared ≠ empty, the window that stops an old identical change being credited. The 10 s fallback itself lives in the shell and is untested (§17.9) |
| CT import ✅ | CST, CDT, ambiguous 1:30, nonexistent 2:30, pre-2007, idempotent re-import, malformed rows named not dropped |
| Midnight ✅ | `verdict_for()` across both DST nights, per-timezone firing, no double fire across a restart, a new entry waiting for the next midnight, an overnight gap skipped rather than posted late |
| Triggers | word vs substring, cooldown 0 and N, per-channel isolation, bot opt-in |
| Paginator | `custom_id` round-trip, the 100-character limit, bounds |
| Modals | every label, id and placeholder against Discord's limits (§21.4) |
| DECtalk sanitizer | the §12.5 table per trust level; prefixes, chaining, mixed case, unterminated brackets; **fuzzed** |
| DECtalk engine | buffers return in order, reset clears state between requests, duration cap, stop; **golden audio** |
| Resampler / WAV | sine in → expected length and frequency; header bytes; waveform of silence and tone; `BENCHMARK` |
| LLM | model-aware fields, prompt order, cache placement, budget trimming, tool loop, max rounds, 429/529, malformed JSON |
| Spend cap | cost maths, daily and monthly rollover, auto-disable |
| Voice session | grace period, auto-leave, move vs join |

**Golden audio** stores a hash and sample count per phrase and writes the `.wav`
next to it on a mismatch, so a difference can be listened to rather than guessed
at. One command updates a golden file after a deliberate change.

**Fuzzing** covers the parsers that see untrusted text — the URL scanner, the
DECtalk sanitizer, the legacy parser — through libFuzzer, with a seed corpus and
any crash input checked in as a regression test.

**Live tests** are tagged `[live]`, excluded by every preset, and need
`LATIBOT_TEST_TOKEN` plus a test server. They cover only what real Discord can
answer: modal behaviour, audit-log timing, whether embeds actually appear per
mirror, and voice. Everything else stays a short manual checklist.

### 17.6 Tooling

| Tool | What it adds |
|---|---|
| CTest | runs the suite, integrates with the IDE |
| AddressSanitizer | use-after-free, overruns, leaks; especially relevant with 1990s C and raw buffers ahead |
| libFuzzer | random input against the parsers |
| clang-tidy | static analysis over our code only |
| clang-format | one style, applied on save |
| OpenCppCoverage | optional local coverage report |

Warnings are errors (`/W4 /WX /permissive-`) on our targets; DPP and DECtalk keep
relaxed flags.

### 17.7 CI

GitHub Actions on `windows-latest`, on push and PR to `main`: checkout with
submodules → restore Conan and DPP caches → `conan install` → configure → build
→ `ctest` excluding `[live]` → upload results. gitleaks runs on every push.
clang-tidy, ASan and fuzzing run on PRs and nightly to keep pushes fast.

**CI has no Discord token and no audio device.** That is deliberate, and it is
the reason logic lives behind ports rather than inside event handlers.

### 17.8 Working rules

- A phase is not done until its tests are green in CI.
- **A bug fix starts with a failing test.** This applies to each of the four URL
  bugs carried over from Java, and to anything found in production (§21.4).
- Only synthetic data in `tests/`.

### 17.9 What the shell keeps, and what that costs

`bot` is not tested, by design: it wires DPP to the core, and testing it would
mean mocking `dpp::cluster`, which is what the ports exist to avoid. The price
is that a handful of decisions live where no test reaches them, and they are
worth naming rather than assuming away:

- **Component and modal routing.** `on_component` and `on_form` dispatch by
  view name. Everything they call is tested; the routing between them is not.
- **The nickname handlers.** Turning a member update into "is this a change",
  and an audit entry into "does this describe a row", are both pure and
  tested. What is not tested is that they are hooked to the right events, that
  the guild comes out of the raw frame correctly, or that the delayed fallback
  fires.
- **The timers.** That the midnight tick, the embed tracker's one-second tick
  and the backup schedule are started at all, and at the right interval.
- **The URL replacement wiring.** `on_message_update` feeding the tracker,
  `on_message_delete` forgetting a message, the four reaction events reaching
  the reaction store, and `recompute`'s list of text channels coming from
  DPP's cache. Each thing they call is tested; that they are called is not.

These are covered by running the bot rather than by CI, which is the honest
description. `[live]` tests are where they would go.

---

## 18. Dependencies

```python
requires = ("openssl/3.6.4", "zlib/1.3.2", "opus/1.6.1",
            "sqlite3/3.53.4", "ctre/3.11.0")
test_requires = ("catch2/3.16.0",)
default_options = {"sqlite3/*:enable_fts5": True}
```

DPP 10.1.6 and DECtalk are submodules built from source. `nlohmann::json` comes
from DPP's own copy, so there is one JSON library and nothing to mismatch. FTS5
backs the LLM long-term memory search and rebuilds SQLite from source once.
clang-tidy, clang-format and ASan come from the Visual Studio Build Tools;
OpenCppCoverage and Ninja are optional extras. yt-dlp and ffmpeg arrive with
music; an image library arrives with appearance tracking.

---

## 19. Build order

**Phase 0 — foundations, tests and tooling** ✅
`latibot_core` / `LatiBot` / `latibot_tests` split; Catch2 and CTRE; FTS5;
presets; formatting and lint config; warnings module; CI green; SQLite wrapper,
migrations and backups; bootstrap config and per-guild settings; the four ports
and their mocks; command registry, raw-API helper, logging.

**Phase 1 — framework proof** ✅
Basic commands and the goodbye phrase; permission preflight; message pipeline;
simple triggers; paginator and a concrete panel; the bot allowlist (§11.1).

**Phase 2 — data features** ✅
`nicknames.json` import (CT/DST); nickname tracking, `/nickname` attribution,
startup reconciliation, `/nicknames`; midnight; the database backups from §5.2
are now scheduled.

**Phase 3 — URL replacement** ✅
CTRE scanner, each Java bug with a test against it; posting and embed
verification; failure and Retry; `UrlReplacements.txt` import; commands and the
panel; reaction tracking; `/linkstats` views; emoji aliases; the legacy parser
and backfill.

**Phase 4 — voice** ⏳ next
DECtalk CMake and dictionary; the streaming spike (FIFO, reset, determinism,
latency); sanitizer with fuzzing; `/tts stop` and the duration cap; resampler
and mixer; `/speak`; voice sessions; custom voices and the voice lab; `/chat`
voice message.

**Phase 5 — LLM**
Provider and tool framework; text replies; guards, spend cap, prompt caching;
runtime settings; documents; short- then long-term memory; advanced triggers;
voice-session replies; bot-to-bot pacing.

**Later:** music, emote statistics, appearance tracking, and possibly the
self-hosted embeds idea in [docs/ideas/](../ideas/Self_Hosted_Embeds.md).

---

## 20. Starting defaults

Values chosen where no preference was stated. Each is a setting or a small code
change, not a design decision.

| # | Setting | Default | Where |
|---|---|---|---|
| 1 | Trusted for `[:play]` / `[:log]` | `trusted_users`, or Administrator in a `trusted_guilds` server | `config.json` |
| 2 | `[:debug]`, `[:loadv]`, `[:setv]` | trusted only | §12.5 |
| 3 | Custom voices | anyone creates; creator or admin deletes | §12.6 |
| 4 | Max utterance | 60 s | per guild |
| 5 | `/speak` input | 1000 characters | per guild |
| 6 | Voice-session grace | 30 s | per guild |
| 7 | Voice sessions | speak **and** post, inline commands kept visible | per guild |
| 8 | Simple trigger cooldown | 30 s (0 allowed) | per trigger |
| 9 | Advanced trigger context | 5 messages | per guild |
| 10 | Personality editing | `personality_editor_role`, defaulting to `@everyone` | per guild |
| 11 | Document edit notices | internal log only | §14.5 |
| 12 | Spend caps | $20/month, $2/day | `config.json` |
| 13 | LLM tool rounds | 4 per reply | `config.json` |
| 14 | Bot-to-bot pacing | 6 turns, daily cap, human resets | per guild |
| 14a | Bot allowlist | empty: every bot ignored | per guild, `/bots` |
| 14b | Trigger answers bots | off | per trigger, `/trigger … bots:` |
| 15 | Embed timeout | ~6 s per attempt, 2 attempts per mirror | a constant for now (§9.3) |
| 16 | Simple + advanced trigger on one message | the simple trigger wins | §14.3 |
| 17 | Goodbye phrase | "say goodbye latibot", Administrator only | per guild, `/goodbye` |

---

## 21. Corrections from implementation

Things the plan did not anticipate, found by building and running it. Recorded
here because each cost real time to diagnose and would cost it again.

### 21.1 Secrets need a local path that is not a file in the repo

The plan said secrets come from the environment and stopped there, which is
correct for deployment and tedious for development — it meant exporting three
variables into every new shell. `LatiBot.exe` now loads a gitignored `.env`
beside it at startup, **never overriding a variable the environment already
set**, so the rule is unchanged and only the convenience is added. Parsing is a
pure function with tests; the half that reads the file and writes to the process
environment is shell.

### 21.2 The message content intent is not optional

Discord delivers guild messages with an **empty `content`** unless the
application requests `i_message_content` *and* it is enabled in the developer
portal under Privileged Gateway Intents. Without it the goodbye phrase and every
trigger silently do nothing while the slash commands keep working, so the bot
looks half alive rather than broken. DPP warns about this at startup, and that
warning is the only clue.

`i_guild_members` was deliberately **not** requested through phase 1: the only
thing needing a member object was the Administrator check on the goodbye
phrase, and Discord sends a partial member with every guild message, which DPP
caches before our handler runs. Phase 2 changed that — nickname tracking (§8)
needs it, and it is the only way `GUILD_MEMBER_UPDATE` or a complete member
list arrives at all. See §21.7.

### 21.3 OpenSSL on Windows has no certificates

The OpenSSL that Conan builds has an **empty `OPENSSLDIR`**, so every TLS
handshake fails verification. DPP reports that as
`HTTP(S) error … Malformed HTTP response`, because the request never gets far
enough to have a status — which sends you looking at HTTP rather than at trust.

`util::ca_certificates` fixes it at startup by exporting the Windows root store
to `data/ca-bundle.pem` and setting `SSL_CERT_FILE`, which is what OpenSSL reads
for its default verify paths. That is the only hook available, since DPP asks
for those paths internally and the submodule is not ours to patch. The roots stay
whatever Windows Update says they are, so nothing has to be vendored or rotated,
and an explicitly set `SSL_CERT_FILE` still wins.

### 21.4 Discord's component limits are only enforced at the API

A text input label may be **45 characters**, and Discord rejects the entire modal
over it with `50035 Invalid Form Body`. Nothing local validates this, so the
first sign was a panel button that appeared to do nothing. The trigger modal's
responses label was 50 characters; the weight syntax moved to the placeholder,
which allows 100.

The lesson generalises: anything built for Discord's API has limits worth
asserting in a test that constructs the payload, because the alternative is
finding out in production. `custom_id` is 100 characters — which the paginator
already refuses to exceed rather than truncating — a modal holds 5 components, a
message holds 5 action rows and a row holds 5 buttons.

### 21.5 Generic abstractions wait for a third example

§4 lists `ui/panel.*` and `ui/modal_forms.*`. They do not exist yet. The trigger
panel is concrete, and the part that genuinely is shared — encoding
`view:page:argument` into a `custom_id` — already lives in `ui/paginator`. What
the panels have in common is better read off `/urlrepl` and `/llm settings` when
they exist than guessed at from one example. The panel code will be abstracted
when the second and third arrive, not before.

The URL rule panel is the second, and it came out the same shape: a menu to
pick an item, a selection row, and a footer with Add and paging, all state in
the `custom_id`. Its routing sits beside the trigger panel's as
`on_url_component` and `on_trigger_component`, each claiming its own view
names. What a shared version would take is now visible — a list, a way to
describe one item, and the buttons for a selected one — and `/llm settings` is
the example that decides whether that is enough.

### 21.6 Formatting and generated files interact

The test catalog links to tests by line number, so a formatting pass invalidates
it. CI caught this a commit late; `tools/Invoke-ClangFormat.ps1` now regenerates
the catalog whenever it reformats anything under `tests/`, so the two cannot
disagree in the first place.

### 21.7 A privileged intent is refused, not degraded

Phase 2 needs `i_guild_members`, and a bot that requests a privileged intent it
was not granted does not lose that feature — Discord closes the gateway with
**4014 Disallowed intent(s)** and the bot reconnects in a loop, never
connecting at all. DPP reports it as
`OOF! Error from underlying websocket: 4014`, which says nothing about which
toggle is missing.

So the intent is requested only when `track_nicknames` is on, which is the
escape hatch if the portal toggle cannot be enabled, and the log handler
watches for 4014 and prints the setting and the portal page by name. The
general rule: a privileged intent is a deployment prerequisite, not a runtime
capability to probe for.

### 21.8 DPP updates its cache before it calls the handler

`guild_member_update` writes the new member into `guild.members` **before**
dispatching, so "what were they called a moment ago" is not knowable from the
cache inside the handler. The old nickname has to come from our own history.

This turned out to be a simplification rather than a problem: comparing an
observed nickname against the last one recorded is the same question at startup
as it is mid-run, so the startup reconciliation of §8.4 needed no separate rule
— it is `is_new_nickname` over the member list, with a different `source`.

Two related shapes, both worth knowing before reaching for them:
`dpp::audit_entry` carries **no guild id**, so the guild has to be read out of
the event's raw frame; and `audit_change::new_value` is stored as **dumped
JSON**, so a nickname arrives quoted and a cleared one arrives as the four
characters `null`.

### 21.9 The Java attribution is worth more than it looks

§8.1 says imported rows should carry no author, on the grounds that the Java
bot guessed. That is half right. It wrote the member's own id whenever it could
not identify the actor, so **self-attribution carries no information** — but a
*different* id could only have come from its `/nickname`, which is the one case
it did know for certain.

Keeping the second kind and dropping the first is what the importer does, and
it is not a marginal call: in the file being ported, all 140 entries name
somebody other than the member. Dropping imported attribution outright would
have thrown away every one of them.

### 21.10 "Fire once per local day" needs a start and an end

§10's rule — fire when the local date differs from `last_fired_date` and the
local time is past 00:00:05 — turned out to be open at both ends.

**No starting point.** A new entry has never fired, so its date always differs,
and adding one at three in the afternoon posts it within thirty seconds.
`/midnight add` therefore records today, in the entry's own zone, as already
posted, so adding one means "from the next midnight" — which is what anybody
typing it expects. Found by a test that failed, which is the argument for the
decision being a pure function in the first place.

**No end.** The same openness means a bot that was down all night posts at
whatever time it comes back: the date differs, so it fires at nine in the
morning. That is the *shape* of the Java bug the section exists to fix,
arrived at from the other direction. The window now closes five minutes into
the local day, and a day past it is skipped.

The general lesson: "once per day" is two questions, not one. A day that has
not started yet and a day that is nearly over both need an answer, and neither
is the answer for an ordinary day.

### 21.11 A preview can arrive before the message it belongs to

§9.3 has the embed tracker start watching once the post returns our message's
id. But Discord builds the preview on its own schedule, and the
`MESSAGE_UPDATE` carrying it arrives on the gateway, while the reply to the post
arrives over REST. When the preview is quick the update can win, reach a
tracker that has never heard of the message, and be dropped — after which the
tracker waits six seconds and "retries" a link that already worked.

So an update for a message nobody is watching is held for thirty seconds, up to
256 of them, and a new watch starts by absorbing any it finds. Every message
update in every guild passes through, and almost none of them are ours, which
is why the buffer is bounded both ways and holds only updates that carry
previews. The general point: two channels from one service are not ordered
relative to each other, and "I asked, then it happened" is not the order the
events arrive in.

### 21.12 The nearest link is not always the right link

§9.7 said to take the first earlier message with a link and log a mismatch
between its path and ours. Building it made the cost of that plain: a mismatch
means somebody posted a link between the original and the bot's answer, and
accepting it credits every reaction to the wrong person, silently apart from a
log line. That is a wrong number on a leaderboard, not noise.

So a mismatch is not accepted. The search continues past it, up to ten earlier
links, and takes the first whose path matches; failing that the replacement is
left unattributed, counted as a mismatch in the report and logged by id, and
its reactions still count as given. A link to a site's front page matches
nothing, since its empty path would match every other one. The rule the plan
wrote down is still the rule for the common case — the nearest link *is* the
original nearly every time — it just is not trusted when it disagrees.

### 21.13 A command with subcommands cannot also run bare

Two designs assumed shapes Discord does not allow. `/urlrepl` on its own was to
open the panel, but a command with subcommands must be given one, so it is
`/urlrepl panel`. `/linkstats recompute since:…` and `/linkstats recompute
cancel` would make `recompute` a subcommand and a group at once, which is
refused, so it is `recompute start` and `recompute cancel`. Default member
permissions are per command as well, not per subcommand, which is why opting
out is `/urltoggle` rather than a subcommand of the Manage Server `/urlrepl`,
and why `/linkstats`, open to everyone, checks Manage Server itself for its
alias and recompute subcommands.

### 21.14 CMake 4.4 and VS Code's File API query

Configuring `build/` began printing dozens of
`IMPORTED_LOCATION not set for imported target "CONAN_LIB::…_RELEASE"
configuration "Debug"` errors, and the reverse, while a fresh build folder
configured cleanly with the same cache. The difference was
`build/.cmake/api/v1/query/client-vscode`: VS Code's CMake Tools asks for the
codemodel, and CMake 4.4 answering it asks each of Conan's per-configuration
imported libraries where it lives in the other configuration, which it cannot
say. The generated projects are unaffected and configure exits 0; the visible
cost is that the first `cmake --build` after a `CMakeLists.txt` edit can skip
newly added files, and a second build picks them up. Left alone rather than
worked around in our CMake, since the fix belongs to Conan's `CMakeDeps` or to
CMake; the README's notes describe it.
