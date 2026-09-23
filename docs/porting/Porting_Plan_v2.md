# LatiBot Stage 2 (v2): Detailed C++ Implementation Plan

This supersedes [Porting_Plan_v1.md](Porting_Plan_v1.md) (left as-is with
your review comments). It folds in every comment from that review, records what
has already been done to the workspace, reports what the new research turned up,
and ends with the decisions that are still open.

Items you approved without changes are kept short here; see v1 for the
longer reasoning behind them. Anything new or changed is written out in full.

---

## 0. What changed since v1

| Area | v1 | v2 |
|---|---|---|
| DPP | Conan package 10.0.35, C++17 | **Vendored v10.1.6, built from source** by our CMake (done) |
| C++ standard | 17 | **20** (done); 23 is an open decision (§19) |
| Async style | Callbacks only | **Coroutines available** (`co_await`, `dpp::task`) (done) |
| Storage | JSON files with atomic writes | **SQLite** for all bot state; JSON only for bootstrap config |
| Config scope | Global | **Per-guild** for everything guild-specific |
| Nickname attribution | Pending-map first, audit log later | **Audit log only**, event-driven |
| URL rule management | Slash commands + optional modal | **Panel + modal UI** as the main interface, commands kept too |
| `/en` suffix | Your call | **Always on** where a rule supports it |
| Reaction stats | Retention policy TBD | **Kept forever** |
| LLM | Anthropic/OpenAI, voice via DECtalk | Fresh design; **cheap models by default**, **text by default**, long + short-term memory, bot-to-bot, DECtalk-aware prompting, admin blacklists |
| DECtalk build | Prebuilt DLL | **Our own CMake**, including the dictionary compiler |
| Mixer | Pause or duck | **Pause/resume** now, configurable later |
| DPP gaps | Case by case | **General raw-API helper** |
| Goodbye phrase | Hardcoded reply | **Configurable** message |

---

## 1. Workspace changes already made

Done in this session, and verified by building both Debug and Release:

- **Git repository** initialized on `main`. `.gitignore` now also covers `.env`,
  `token.txt`, `openai_key.txt` and `data/`. Checked that the Java reference's
  `token.txt` / `openai_key.txt` are ignored. **Nothing has been committed yet.**
- **Submodules** (see §19 for the submodule-vs-copy decision):
  - `third_party/DPP` pinned to tag **v10.1.6** (shallow).
  - `third_party/dectalk` on branch **develop** (`69ebb459`).
- **Conan** (`conanfile.py`): `dpp` removed. It now provides DPP's
  dependencies directly (`openssl/3.6.4`, `zlib/1.3.2`, `opus/1.6.1`) plus
  `sqlite3/3.53.4`. The default Conan profile is now `compiler.cppstd=20`.
- **CMake**: C++20, DPP added with `add_subdirectory`, voice support forced on,
  coroutines on, and all binaries output to `build/bin/<Config>/`.
- **README** updated for the new setup steps.

The DECtalk CMake build is **not** written yet. It's real engineering work and
belongs to Phase 4 (§18).

---

## 2. New findings from this round

### 2.1 DPP's bundled Windows dependencies are outdated and insecure

On MSVC, DPP's CMake doesn't look up packages. It links prebuilt binaries
shipped in its `win32/` folder: **OpenSSL 1.1.1k (March 2021)** and **zlib 1.2.11 (2017)**.
OpenSSL 1.1.1 reached end of life in September 2023, and both versions have
published CVEs since. A bot that holds a long-lived TLS connection to Discord
shouldn't ship those.

What we do instead: set `CONAN_EXPORTED=ON` so DPP uses `find_package`, and let
Conan supply OpenSSL 3.6.4 / zlib 1.3.2 / opus 1.6.1.

### 2.2 The Conan DPP from initial setup was probably built without voice

On Windows, DPP only turns voice on when `HAVE_OPUS_OPUS_H` is set, and it only
sets that on its bundled-binaries path. The ConanCenter recipe doesn't set it
either. So the `dpp/10.0.35` package installed during setup was very likely
built with the voice **stub**, and `/join` or `/speak` would have thrown
"voice support not compiled in" at runtime. Our CMake now sets
`HAVE_OPUS_OPUS_H` and `OPUS_LIBRARIES=Opus::opus` explicitly, and configure
prints `VOICE support will be enabled`.

### 2.3 An OpenSSL 3 workaround was needed

DPP hardcodes `OPENSSL_VERSION "1.1.1f"` on Windows. That makes its bundled
`mlspp` library (used for Discord's voice encryption) compile its OpenSSL 1.1
code path, which fails against OpenSSL 3 (`FIPS_mode` no longer exists).
Only the `hpke` target branches on this, so our CMake adds
`target_compile_definitions(hpke PRIVATE WITH_OPENSSL3)`. The submodule itself
is unmodified. **If DPP gets upgraded, check whether this line is still needed.**

### 2.4 DECtalk: what the real source says

You asked for the headers to be checked against the actual repo:

- **`ttsapi.h` is byte-for-byte identical** to your reference copy (ignoring
  line endings), so the API analysis in v1 still holds.
- **Streaming works on Windows with a plain callback.** `TextToSpeechStartupEx`
  (callback-based) is exported in the Windows `.DEF` file, and the HWND-based
  `TextToSpeechStartup` is just a wrapper around it that forwards via
  `PostMessage`. v1's worry about needing a hidden message-only window is gone.
  One Windows detail: the buffer-ready message ID passed to the callback is the
  value of `RegisterWindowMessage("DECtalkBufferMessage")`, not the constant `9`
  (`TTS_MSG_BUFFER`), which is only defined for non-Windows builds. Calling
  `RegisterWindowMessage` with the same string in our code returns the same ID.
- **Why the dictionaries never worked in the Java version.** On Windows, DECtalk
  reads the dictionary path from a **registry key under `HKEY_LOCAL_MACHINE`**.
  If that key doesn't exist, it falls back to a bare relative `DTALK_US.DIC`,
  resolved against the **current working directory** (not the exe's folder),
  and tries to append an error note to `\dtdic.log` at the drive root.
- **The fix:** `TextToSpeechStartupExFonix(..., dictionary_file_name)` is
  exported, described as "startup with dictionary name". We pass an absolute
  path to the dictionary sitting next to the exe. No registry, no dependence
  on the working directory.
- **How the dictionary is built.** The `.dic` file is compiled from text:
  `Internal Dictionary Compiler.exe dic/Dic_us.txt dtalk_us.dic /t:win32`.
  The upstream VS2022 solution runs this as a post-build step. Your reference
  folder's `dic_*.txt` files are these sources.
- **Build facts for our CMake.** The DLL project is 86 C files, exports come
  from `DECTALK.DEF`, it links `winmm`, and the Release/x64 defines are
  `WIN32;DECTALKAPI_EXPORTS;_WINDOWS;_USRDLL;USE_CORE_DLL;ACNA;BLD_DECTALK_DLL;ENGLISH_US;ENGLISH;NDEBUG;AMD64`.
- **Prior art.** Upstream has a `cmake` branch (February 2023, 14 commits ahead
  and 117 behind `develop`) with a working CMake port, **including the
  dictionary-compiler post-build step**. We'll base our CMake on it, with the
  source list checked against `develop`'s `.vcxproj`.
- **Licence.** DECtalk's `LICENCE` is proprietary Fonix code ("possession, use,
  or copying ... authorized only pursuant to a valid written license"). Keeping
  it as a submodule means this repo only *references* it and doesn't
  redistribute it. That matters if the repo is ever made public (§19).

> Looks good.

### 2.5 Two DECtalk inline commands touch the host filesystem

You want inline `[:commands]` supported by default in `/speak` and in LLM
speech. Two of them are dangerous in a chat bot:

| Command | What it does | Risk |
|---|---|---|
| `[:play "path"]` (`cm_cmd_play` in `cmd_wav.c`) | Opens a `.wav` file **by path from the host disk** and plays it | Anyone could read and broadcast any WAV file on the bot's machine |
| `[:log ...]` (`cm_cmd_log`) | Opens a **log file for writing** | Writes files on the host |

These must be stripped before text reaches the engine, whether it came from a
user or from the LLM. Denylist vs allowlist is an open decision (§19).

> Lets limit using [:play] and [:log] to users with administrator permissions. Silently strip them out if a normal user tries to use them.  

### 2.6 What Discord modals can and can't do

Checked against Discord's current documentation for the URL-rule editor
question:

- **Allowed in modals now:** Label (the required wrapper), Text Input (up to
  4000 characters), String/User/Role/Channel/Mentionable Select (up to 25
  options), Text Display, File Upload, Radio Group, Checkbox Group, Checkbox.
- **Limits that matter:** at most **5 components** per modal. Modals **can't be
  updated in place**, and **a modal submit can't be answered with another
  modal** (quote: "Not available for `MODAL_SUBMIT` and `PING` interactions").
  `custom_id` is 1–100 characters.
- **DPP 10.1.6 support:** Label, Text Display, Section, Container and File
  Upload are there. **Radio Group, Checkbox Group and Checkbox (types 21–23)
  are not**, so those need raw JSON via the §5.7 helper, or we avoid them.

So a pure-modal "editable list" isn't possible. A panel message plus modals
is, as described in §9.4.

### 2.7 Coroutines are on, with one gotcha

Built from source as C++20, DPP turns coroutines on automatically. Event
handlers can be `dpp::task<void>` lambdas, and every REST call has a `co_`
variant (for example `co_guild_auditlog_get`).
**Gotcha:** `co_request` (arbitrary HTTP) has **no timeout parameter** and uses
the 5-second default, which is far too short for LLM calls. The callback
`request(..., request_timeout)` does take a timeout, so the LLM client wraps
that in a `dpp::async` instead (§14.8).

> perhaps should look into making a patch or smthin to add the timeout parameter to co_request if it is as simple as that. Likely requires analysis to determine how complicated that would be and may be more effort that its worth. Ideally, i dont want to make a fork that i have to maintain.

### 2.8 Audit-log attribution can be event-driven

DPP has `on_guild_audit_log_entry_create`, the `GUILD_AUDIT_LOG_ENTRY_CREATE`
gateway event. It needs the `VIEW_AUDIT_LOG` permission plus the
GUILD_MODERATION intent (`dpp::i_guild_bans`, bit 2, already in DPP's default
intents). So we can react to `MEMBER_UPDATE` entries as they happen instead of
querying the audit log after each nickname change (§8.1).

>Approved.

---

## 3. Decisions locked in

| Feature | Verdict | Key points |
|---|---|---|
| Music | Later, stubbed | yt-dlp + ffmpeg; redesigned around DPP track markers; plays through the mixer |
| DECtalk in voice | Keep | Our own CMake; in-memory synthesis first, streaming later; inline commands on by default (minus the dangerous ones) |
| `/chat` voice message | Keep | Hand-built request through the raw-API helper; no temp files |
| Nickname history | Keep | Audit-log attribution, SQLite, pagination; appearance tracking later |
| Owner-confirm flow | Dropped | |
| Emote stats | Later | Redesign with incremental scanning |
| Emote export | Dropped | |
| URL replacement | Keep, redesign | 4 bug fixes, event-driven embed check, panel/modal UI, `/en` always on, reaction stats forever, no webhooks |
| Reaction refresh | Dropped | |
| Midnight | Keep, expand | Per guild, multiple timezones, wall-clock polling |
| Basic commands | Keep | Plus the configurable "say goodbye latibot" |
| Permission check | Generalize | Declared per feature, checked per guild, never exits |
| Trigger words | New | Per guild |
| LLM | New | Anthropic default, cheap models, text by default, voice when appropriate, memory, bot-to-bot |

---

## 4. Target project structure

```
CMakeLists.txt
conanfile.py
cmake/
  dectalk.cmake              our DECtalk build (sources live in the submodule)
src/
  main.cpp
  bot.hpp/.cpp               owns the cluster and subsystems
  config/
    bootstrap.hpp/.cpp       config.json + environment secrets
    guild_settings.hpp/.cpp  per-guild settings, backed by SQLite
  db/
    database.hpp/.cpp        thin RAII wrapper over sqlite3
    migrations.hpp/.cpp      schema versioning (PRAGMA user_version)
    backup.hpp/.cpp          scheduled online backups with rotation
    import_legacy.cpp        one-time import of nicknames.json / UrlReplacements.txt
  discord/
    raw_api.hpp/.cpp         helper for features DPP doesn't cover
  commands/                  registry.*, basic, voice, nickname, urlrepl,
                             midnight, triggers, speak, chat, llm_admin
  events/
    message_pipeline.*       one on_message_create, ordered stages
    nickname_tracker.*       member updates + audit log entries
    reaction_stats.*
    url_replacer.*
  ui/
    url_panel.*              panel message + modal flows
    paginator.*              reusable button pagination
  audio/
    voice_mixer.*  resample.*  dectalk_engine.*  dectalk_sanitizer.*
    music/                   stubs
  llm/
    provider.hpp  anthropic.*  openai.*
    conversation.*           short-term context window
    memory.*                 long-term memory store + tools
    responder.*              addressed / trigger / bot-to-bot policy
  util/
    scheduler.*  text.*  url_scan.*
third_party/
  DPP/                       submodule, v10.1.6
  dectalk/                   submodule, develop
data/                        runtime: bot.db, backups/ (gitignored)
```

> Looks good.

---

## 5. Core infrastructure

### 5.1 Configuration: two layers

- **`config.json` (bootstrap, global):** database path, log level, default
  LLM provider and model, backup schedule. Rarely changes.
- **Secrets from the environment only:** `DISCORD_BOT_TOKEN`,
  `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`. Startup fails loudly if the token is missing.
- **Per-guild settings in SQLite:** midnight entries, URL rules, opt-outs,
  trigger words, the goodbye message, LLM settings and blacklists. These are
  edited through commands and panels, so they belong in the database rather
  than a file that's hand-edited while the bot runs.

A `guild_settings` accessor provides typed getters with defaults, so a guild
with no rows behaves sensibly.

> Approved.

### 5.2 Thread safety

Unchanged from v1: any shared in-memory state gets a `std::shared_mutex` or
lives behind a class that owns the lock. SQLite is built `threadsafe=1`
(serialized), which makes it safe but not concurrent. How the bot shares
connections is an open decision (§19).

### 5.3 Persistence on SQLite

SQLite covers everything v1 wanted from the JSON helper:

- **Atomicity:** transactions replace the write-to-temp-and-rename scheme.
  Run in `journal_mode=WAL` so reads don't block writes.
- **Backups (you approved keeping a couple):** SQLite's online backup API
  (`sqlite3_backup_init/step/finish`) makes a consistent copy while the bot is
  running. Keep a rotating set, e.g. `data/backups/bot-YYYYMMDD.db`, last N.
- **Debouncing:** mostly unnecessary now, since single-row inserts are cheap. Dropped.
- **Snowflakes:** v1 said to store them as JSON strings because JSON numbers
  lose precision past 2^53. SQLite's `INTEGER` is an exact signed 64-bit value
  and snowflakes fit, so **store them as `INTEGER`**. The string rule only
  applies at JSON boundaries (config, LLM payloads, import).
- **Schema migrations:** `PRAGMA user_version` holds the schema version, and
  numbered migrations run in order at startup inside a transaction. This also
  covers the future appearance-tracking migration you said you're fine with.
- **Timestamps:** store UTC as Unix epoch seconds (or ISO-8601 UTC text). Never
  local time. See the import caveat in §8.3.

Starting schema, to be refined during implementation:

```sql
nickname_history(id, guild_id, user_id, nickname NULL, changed_by NULL,
                 changed_at, source)          -- source: audit_log|self|startup|import
url_rules(guild_id, domain, position, host, translate_suffix NULL)
url_opt_outs(guild_id, user_id)
replacement_messages(message_id PK, guild_id, channel_id, original_author_id,
                     domain, created_at)
reaction_events(message_id, user_id, emoji, added_at)      -- kept forever
midnight_entries(id, guild_id, timezone, channel_id, message, enabled, last_fired_date)
triggers(id, guild_id, pattern, match_mode, cooldown_s, ...)
trigger_responses(trigger_id, response, weight)
llm_settings(guild_id, ...), llm_blacklist(guild_id, kind, target_id),
llm_memory(id, guild_id, subject_user_id NULL, content, created_at, ...)  -- + FTS5 index
guild_settings(guild_id, key, value)          -- small scalar settings
```

### 5.4 SQLite access layer

A small RAII wrapper: `database` owns the `sqlite3*`, `statement` owns the
`sqlite3_stmt*`, with typed `bind`/`get` built on C++20 concepts and a
`transaction` guard. This is a good fit for learning modern C++, and there's
no ORM to fight. The alternative is SQLiteCpp (on Conan). Open decision (§19).

### 5.5 Command registry, now with coroutines

Same design as v1, but handlers return `dpp::task<void>`:

```cpp
struct command {
    std::string name, description;
    std::vector<std::string> aliases;
    uint64_t required_bot_permissions = 0;
    dpp::permission default_member_permissions;
    bool guild_only = true;
    virtual dpp::task<void> execute(const dpp::slashcommand_t&) = 0;
    virtual dpp::slashcommand build(std::string_view name_or_alias) const = 0;
};
```

Coroutines make the multi-step flows in this bot read top to bottom: the
embed retry, the audit-log follow-up, paging, and the LLM tool loop.

### 5.6 Message pipeline

Unchanged from v1, except for the first stage. It used to be "ignore all bots"
and is now "**ignore self; ignore other bots unless allowlisted for LLM
conversation in this guild**" (§14.4). Stages:

```
ignore self / non-allowlisted bots
→ admin goodbye phrase         (consumes)
→ URL replacement              (does not consume)
→ trigger responses            (does not consume)
→ LLM addressed / triggered    (consumes)
```

> seems ok. wont be certain on what will consume and what wont untill i can run the bot and test.

### 5.7 Raw-API helper

This is the general utility you suggested for filling gaps in DPP. It's thin,
because `cluster::post_rest` / `post_rest_multipart` are public and already go
through DPP's REST queue, with **bot authentication and Discord rate-limit
handling included**. The helper adds:

- `co_`-style awaitables returning parsed JSON (or a typed error).
- Building a Discord endpoint from path + parameters.
- A multipart variant for hand-built `payload_json` + files.

Known uses so far: sending voice messages (§12.7), reading role gradient
colours (§8.6), and the component types 21–23 if we ever want them (§2.6).

> looks good.

---

## 6. Basic commands

Approved as in v1. `/ping`, `/say`, `/shutdown`, `/status`, `/join`, `/leave`.
`/join`/`/leave` are built now because TTS needs them.

**"say goodbye latibot":** the reply is **configurable per guild** (the
`goodbye_message` setting, with a default). Rules from v1 stay: the author must
have Administrator **in that guild**, the phrase has to be essentially the
whole message, and there's a short delay after the reply before shutdown.

---

## 7. Permission preflight

Approved as in v1: each command and passive feature declares what it needs,
checked per guild on `on_ready` and `on_guild_create`, reported at WARN level,
and **never exits the process**. New requirements to add to the declarations:
**`VIEW_AUDIT_LOG`** (nickname attribution) and **`CONNECT`/`SPEAK`** (TTS).

---

## 8. Nickname tracking

### 8.1 Attribution through the audit log (event-driven)

This replaces the SHA-256 hash machinery entirely.

1. `on_guild_member_update`: if the nickname changed, write a history row right
   away with `changed_by = NULL` (unknown). Recording the change must never
   depend on the audit log arriving.
2. `on_guild_audit_log_entry_create`: for an `aut_member_update` entry whose
   changes include the `nick` key, find the matching recent history row (same
   guild, same target user, same new nickname, within a short window) and set
   `changed_by = entry.user_id` and `source = 'audit_log'`.
3. Safety net: if no entry has matched after ~10 s, query once with
   `co_guild_auditlog_get(guild, 0, aut_member_update, ...)`. This covers
   missed gateway events and reconnects.

This also attributes renames done through the Discord UI by moderators, which
the Java version recorded as self-changes. **To verify during implementation:**
whether a user changing their *own* nickname creates an audit-log entry. If
not, an unmatched row is best recorded as "self" rather than "unknown", since
Discord logs moderator changes.

> Also need to ensure nickname changes are properly recorded when using the /nickname command to change someones nickname.

### 8.2 Storage

This is now the `nickname_history` table (§5.3). Keep raw IDs and resolve
names and avatars when displaying (approved in v1). Handles empty histories,
cleared nicknames (`NULL`) and departed members.

### 8.3 Importing `nicknames.json` (one time)

The importer reads the existing file into `nickname_history` with
`source = 'import'`. One catch: **the Java version wrote `datetime` as local time
with no timezone** (`SimpleDateFormat("yyyy-MM-dd HH:mm:ss")` uses the host's
zone). Converting to UTC means assuming a zone, probably `America/Chicago`
given the midnight feature. Needs your confirmation (§19).

> Timezone should be US Central Time (CT). may need to adjust for daylight savings based on the date. 

### 8.4 Startup reconciliation

Approved: changes that happened while the bot was offline are recorded with
`changed_by = NULL` and `source = 'startup'`.

### 8.5 `/nicknames` pagination

Approved: an embed with ◀/▶ buttons and page state in the `custom_id`
(`nick:<user>:<page>`, under 100 characters). A `.txt` attachment is the
fallback for very long histories. Built on a reusable `ui/paginator` so
`/urlrepl list` and `/linkstats` use the same code.

### 8.6 Later: appearance tracking

Deferred. Notes from your review:
- Migrating the schema later is fine. It's a new migration (§5.3) that adds an
  appearance table or columns.
- Gradient role colours most likely need a raw API read through §5.7.
  Check whether DPP 10.1.6's `role` exposes them before writing that.
- The display is probably a generated image in the embed rather than embed
  fields. That will need an image library, to be chosen when we get there.

> looks good.

---

## 9. URL replacement

### 9.1–9.3 Bug fixes (approved as in v1)

- **Multiple links:** match only the URL (no greedy `before`/`after` groups),
  collect every match's offsets, splice the output back together, and track
  the alternate index per link.
- **Spoilers:** count literal `||` before each match. An odd count means the
  link is inside a spoiler, so the replacement gets wrapped too.
- **Embed verification:** event-driven with `on_message_update`, one fallback
  timeout, retries capped at the number of alternates. With coroutines this is
  a single linear function.

> retry each alternative at least once (2 attempts total for each). Also, when a msg fails, the old implementation deleted its msg and reenabled the embed on the original. This new impl should reenable the original msg embed and keep its msg, but update it with a failure msg and a button to do a single shot retry, redisabling the original if it works after a retry. 

**New implementation note on the URL scanner:** avoid `std::regex`. It's slow
everywhere, and MSVC's implementation is recursive and can **overflow the stack**
on long inputs, which is a crash risk on a hot path that sees every message.
Options: a hand-written scanner (find `http://`/`https://`, read to the URL's
end, parse the host), or **CTRE** (compile-time regular expressions, a C++20
header-only library on Conan). Open decision (§19).

> im interested in this CTRE library.

### 9.4 Management UI: a panel plus modals

You preferred a dialog-driven editor over commands. Given the modal limits in
§2.6, this is the design that works:

- **`/urlrepl`** (no arguments) opens an **ephemeral panel message** in the
  current guild: one row per rule showing the domain and its ordered
  alternates, with **Edit** and **Delete** buttons, plus **Add rule** at the
  bottom. Paging buttons appear when there are more rules than fit.
- **Edit / Add** opens a **modal** (allowed because it's answering a button
  press): a Label + Text Input for the domain, and a Label + paragraph Text
  Input holding the alternates **one per line, in priority order**. Editing is
  as simple as rewriting lines. It's pre-filled when editing.
- **Delete** asks for confirmation by updating the panel with
  Confirm/Cancel buttons (no modal needed).
- **After a modal is submitted,** the rule is validated and saved, and the panel
  refreshes to show it. *Spike item:* confirm that a modal submit opened from a
  message button can reply with `UPDATE_MESSAGE` to edit that panel. If it
  can't, the panel is edited through the interaction's original-response
  endpoint instead. The result for the user is the same.
- **Commands stay** for quick use and scripting: `/urlrepl list | set | remove | test`,
  with domain autocomplete. `test <url>` (dry run) is the most useful of these
  for debugging.

We keep to component types DPP supports (no radio or checkbox types), so the
§5.7 helper isn't needed for this.

> looks good. Approved.

### 9.5 `/en`: always on

A rule's alternate can carry `translate_suffix` (e.g. `/en`). When present it's
**always applied**. The URL is parsed rather than string-concatenated, so an
existing query string, fragment or trailing slash is handled correctly.

> Approved.

### 9.6 Reaction statistics: kept forever

`replacement_messages` and `reaction_events` tables (§5.3), with no retention
limit, per your decision. Recording individual add/remove events, rather than
only a running count, keeps the full history available for whatever stats you
want later. `/linkstats` has views for top posters by reactions received and
top emoji, filterable by domain and date range, paginated with the shared
paginator.

> The bot should also have a oneshot recompute statistics functionality. This will be useful mainly to have the bot go back through the message history and tally up reactions on all the many existing link replacements that have reactions already on them. Should be able to supply a date to bound the bots search. All messsages will be from the same bot user, but the format of the message content has changed slightly a couple of times. Will also need the ability to alias emotes together as there are some cases where different people have the same emote but from different servers, which would split the count between the multiple version of what should be identical emotes (also relevant for cases where an emote was removed and readded to a server, which discord classifys as different emotes). This aliasing would be somthing we would manually add as we run into them.

### 9.7 Other fixes (approved)

Webhook mode removed. Per-user opt-outs persisted (now per guild in SQLite).
No crash when there's no configuration. A one-time importer for
`UrlReplacements.txt` into your guild's rules.

---

## 10. Midnight

Approved as in v1, now per guild and on C++20:

- The `midnight_entries` table holds, per guild, any number of
  `{timezone, channel, message, enabled, last_fired_date}` rows.
- A wall-clock poll every 30 s (`cluster::start_timer`) checks each entry
  using `std::chrono::zoned_time{entry.timezone, system_clock::now()}`. It
  fires when the local date differs from `last_fired_date` and the local time is
  ≥ 00:00:05, then saves `last_fired_date` in the same transaction.
  This fixes the random-time firing bug (suspend/resume with a monotonic-clock
  delay) and prevents double posts across restarts.
- MSVC's `<chrono>` time-zone support uses Windows' ICU time-zone data
  (Windows 10 1903 and later), so it works on your Windows 11 machine with no
  extra dependency.
- `/midnight list | add | edit | remove | toggle`, with autocomplete on the
  timezone name.

---

## 11. Trigger-word responses (new)

This generalizes the hardcoded `420`/`4:20`/`69` → "nice".

- A trigger is a per-guild row: pattern, match mode, cooldown, enabled, and
  one or more responses with **weights**. `YesNoAnswers.txt` from the Java repo
  is exactly a weighted response pool, so the same format can seed a trigger
  if you want that list back in some form.
    > the YesNoAnswers.txt can be dropped, superceeded by the llm integration.
- Match modes: *whole word* (the default; the old regex used `\b` for this)
  and *substring*. User-defined regex is left out on purpose: regex written by
  users is a performance and stack-overflow risk (§9.1).
    > Approved.
- Per trigger, per channel cooldown so the bot can't spam.
  > should be configurable, no cooldown is valid but not the default.
- The pipeline stage doesn't consume the message, so a message that contains
  "420" **and** a link gets both the "nice" and the link fix. That was the Java bug.
- Management: `/trigger add | edit | remove | list`, or a panel like §9.4. The
  panel pattern is reusable, so it's cheap once §9.4 exists.
  > both is good.
- Seeded with the Java version's defaults on first run.
  > approved
---

## 12. DECtalk

### 12.1 Our own CMake build

In `cmake/dectalk.cmake`, reading sources from the submodule, so the submodule
itself is never edited:

- `dectalk` library: the 86 sources from `develop`'s `DECtalk API.vcxproj`,
  compared against the upstream `cmake` branch's list, which was written for
  an older tree. Uses `DECTALK.DEF` for exports, the defines from §2.4, and
  links `winmm`. Compiled as C with warnings reduced for this target only,
  since it's 1990s code.
- `dectalk_dic` host tool: `dic/dic.c` with `WIN32;_CONSOLE;ENGLISH_US;ENGLISH;WINDIC`.
- A custom command runs `dectalk_dic dic/Dic_us.txt <bin>/dtalk_us.dic /t:win32`
  so the dictionary is **built from source on every clean build** and placed
  next to the exe, the same way `dpp.dll` is.
- Library type (DLL vs static) is open (§19). The DLL matches the `.DEF`
  exports and upstream; a static lib saves a file but needs the DLL-only
  bits in `ttsapi.c` checked first.

> A dll is fine.

### 12.2 Startup

```cpp
TextToSpeechStartupExFonix(&handle, WAVE_MAPPER, DO_NOT_USE_AUDIO_DEVICE,
                           &on_dectalk_message, reinterpret_cast<LONG>(this),
                           absolute_path_to("dtalk_us.dic"));
```

With an explicit dictionary path, no registry, no dependence on the working
directory, and no audio device, the dictionary problem from §2.4 is handled.
Check the exact `uiDeviceNumber` value against upstream's samples during the
spike; the Java shim passed `0xFFFFFFFF`.

### 12.3 Synthesis: in memory first, streaming later

As agreed:
1. **Now:** `OpenInMemory(WAVE_FORMAT_1M16)` → `AddBuffer` ×N → `Speak` →
   `Sync` → collect PCM → `CloseInMemory`. Measure the delay before audio starts.
2. **If that delay is too long:** stream through the callback. Handle the
   `RegisterWindowMessage("DECtalkBufferMessage")` ID, call `ReturnBuffer`,
   send the PCM to the mixer, and `AddBuffer` the buffer again.

Output is resampled from 11025 Hz mono to 48 kHz stereo (as in v1), then
split into 11520-byte frames.

> I belive the reason we didnt do streaming first was due to the open question of how the streaming event messages were handled. If that was resolved i would rather go with the streaming over buffered. Likely reevaluate. 

### 12.4 Engine threading

One dedicated worker thread owns the DECtalk handle. Requests go in through
a queue and results come back as an awaitable, so a command can
`co_await engine.synthesize(text, voice)` without blocking a DPP event thread.

> sounds good.

### 12.5 Inline commands on by default, sanitized

Inline `[:commands]` pass through by default, as required. A **sanitizer** runs
on all text going to the engine, from any source:
- Removes `[:play ...]` and `[:log ...]` (§2.5), and anything else found to
  touch the filesystem or device during the build audit.
  > lets filter for normal users, but allow administrators to use :play and :log.
- Limits input length and output duration. Something like `[:rate 75]` on a
  long text could otherwise hold the voice channel for many minutes.
  > this is a good point. There should also be an admin command to stop the bot tts in case it still ends up going for too long.
- Denylist vs strict allowlist is open (§19).
  > deny list likely.

### 12.6 `/speak`

Options for voice (the 9 built-in speakers), rate and volume. Setting volume
through `TextToSpeechSetVolume` takes care of the old "make dectalk louder"
TODO. Output goes to the mixer as a high-priority source.

> i believe dectalk has commands to define a custom voice as well, so it would be nice to have to option to build a custom voice. maybe also with a way to quickly edit the voice parameters and hear a test phrase for quick iteration  

### 12.7 `/chat` (voice message)

Synthesize in memory, wrap it in a 44-byte WAV header, compute
`duration_secs`, and compute the 256-bucket peak **waveform** correctly
(approved in v1). Send it through the §5.7 helper as `post_rest_multipart`
with `flags: 8192` and attachment metadata. No temp files.

> Approved.

---

## 13. Voice mixer

One per guild. It owns the `discord_voice_client` and takes audio from
prioritized sources:

- **Now:** when TTS starts, music is **paused**; it **resumes** once TTS
  (including queued TTS) finishes. That uses DPP's `pause_audio` and track
  markers.
- **Later:** a per-guild setting with pause, duck (music at reduced volume,
  which means mixing PCM ourselves instead of pausing), or overlay.

> Approved.

---

## 14. LLM integration (fresh design)

The Java `ApiDriver` is ignored completely.

### 14.1 Providers and models

- **Anthropic is the default**, OpenAI is optional, and both sit behind the same
  `llm_provider` interface. Provider and model are set per guild, with the
  global default from `config.json`.
- **Cheap models by default, as you asked.** Anthropic options:

  | Model | ID | $/1M in | $/1M out | Notes |
  |---|---|---|---|---|
  | Claude Haiku 4.5 | `claude-haiku-4-5` | $1 | $5 | **Proposed default.** Accepts `temperature`; thinking is off unless budget-style thinking is requested |
  | Claude Sonnet 5 | `claude-sonnet-5` | $2 | $10 | Step up. Adaptive thinking; **rejects `temperature`** (400) |
  | Claude Opus 5 | `claude-opus-5` | $5 | $25 | Available in config, not expected to be used |

  Model IDs are used exactly as written, with no date suffixes. The request
  builder is **model-aware**: it only sends `temperature` to models that accept
  it, and only sends `output_config.effort` to models that support it.
  Otherwise, changing the model in config could produce 400 errors.
- OpenAI model choice is yours when it's configured.

> looks good.

### 14.2 Reply mode: text or voice

**Text is the default.** Proposed rule for replying by voice instead: the bot is
in a voice channel in that guild, **and** the message came from someone in that
same voice channel (for example via the voice channel's built-in text chat),
**and** voice replies are enabled for the guild. Otherwise it replies in text.
Open decision (§19).

> We never use the voice channels build in chat so not super helpful. Probably should be a distinct enable command or etc that will have the bot join the user in the vc they are in (if not already present) and route all tts to there until disabled, the bot leaves, or all the users leave the vc (where the bot will auto leave).

### 14.3 When the bot responds

- **Addressed:** it's @mentioned, someone replies to one of its messages, or
  the message starts with its name. The stage consumes the message.
- **Triggered:** per-guild trigger words with a probability (0–1) and a
  per-channel cooldown.
- Admin blacklists (§14.7) are checked before either.

> These 'advanced response' triggers should be seperate from the 'simple response' triggers like "420 -> nice" and etc. The 'advanced response' should basically provide the model with a short piece of context provided by the user on what to say with a standard system text shared between all the 'advance response' triggers that will instruct the model on the global style of response (length limits, style, tone (not vocal tone), etc.).  

### 14.4 Bot-to-bot conversations

For your second bot:
- A **per-guild allowlist of bot user IDs** the LLM may answer. Every other bot
  is ignored, as before.
- **Loop limits:** at most N consecutive bot-to-bot turns per channel
  (default ~6), a minimum delay between bot turns, and a hard daily limit on
  bot-to-bot turns. Any message from a human resets the turn counter.
- A setting that allows bot-to-bot replies only when a human started the
  exchange.

> Makes sense. Approved.

### 14.5 Memory

**Short-term (the conversation):** a per-channel rolling window of recent
messages, bounded by message count **and** an approximate token budget. Both are
configurable in `config.json` with per-guild overrides (not exposed as commands,
as you asked). Built from the message cache, with a fetch on cold start.
> I think i've changed my mind on this a bit. It would be nice for the message window size and etc to be configurable by admins to tweak without having to restart the bot.

**Long-term (things worth remembering):** stored in the `llm_memory` table,
scoped per guild with an optional subject user.
Recommended design: **the model manages its own memory through tools**,
`remember(fact, about_user?)`, `recall(query)` and `forget(id)`, implemented on
our side against SQLite. Search uses **SQLite FTS5** full-text search. FTS5 is
off in the Conan `sqlite3` recipe by default; one line in `conanfile.py`
turns it on (listed in §17, not done yet). A few of the most relevant memories
can also be put into the system prompt up front, so the model doesn't need a
tool call for common facts.

> Good apprach. Approved. Some feature i have planned for the far future will need to allow the model to make tool calls anyway, so its good to implement now for reuse as we add more advanced features.

Tool use means a request loop (the model calls a tool, we answer, it
continues). That costs more per reply, which is one reason to keep a cheap
default model. Admins get `/memory list | forget | clear`, and users can ask
to have their own memories removed. Design choices are in §19.

> sounds good.

> The bot should also have a way to configure its "personality" as part of its memory. Ideally can be editable with commands/modals/etc so it can be more of a 'living document' user can update to fine tune the style and personality we want. Also likely should have the same for system instructions that normal users should not be able to edit (admins only).

### 14.6 DECtalk-aware prompting

For voice replies, the system prompt gets a short, curated reference of the
inline commands the model may use (voices, `[:rate]`, pitch and voice
parameters, phoneme mode) and asks for **plain spoken text** (no markdown or
emoji). The §12.5 sanitizer still runs on the output: the prompt is guidance,
the sanitizer is what enforces it. Text replies use a separate prompt.

> Approved.

### 14.7 Guards

- **Admin blacklists of users and roles**, per guild. Blacklisted members are
  ignored for all LLM features.
- Rate limits per user and per channel, a cap on output tokens per reply, and
  a per-guild on/off switch.
- **A spend cap:** add up the `usage` token counts × price per model, with a
  daily and monthly limit. The LLM switches off automatically at the limit.
  Given that you want to keep costs low, it's cheap insurance.
- Keys from environment variables only.

> Looks good.

### 14.8 HTTP

`cluster::request(url, method, callback, body, "application/json", headers, "1.1", 60)`
wrapped in a `dpp::async`, because `co_request` can't take a timeout (§2.7).
No streaming responses (approved). A typing indicator is shown while waiting.

> See comment about potentiall making a patch to add a timeout to co_request. Very dependent on analaysis on feasibility as it may be more effort that its worth. 

---

## 15. Music (later)

Approved as in v1: the queue commands aren't registered yet; `/join`/`/leave`
are real. The later design is built on DPP track markers (you confirmed), the
§13 mixer, and yt-dlp + ffmpeg.

## 16. Emote stats (later)

Noted as in v1: scan incrementally with per-channel high-water marks (now a
SQLite table) instead of re-reading all history each time. Coroutines make
the paged history walk simple to write.

---

## 17. Dependencies

| Dependency | Source | Status |
|---|---|---|
| DPP 10.1.6 | submodule, built from source | ✅ done, voice + coroutines verified at configure |
| OpenSSL 3.6.4, zlib 1.3.2, opus 1.6.1 | Conan | ✅ done |
| SQLite 3.53.4 | Conan | ✅ done. **To do:** `default_options = {"sqlite3/*:enable_fts5": True}` if §14.5 is approved |
| nlohmann::json | bundled inside DPP (`<dpp/json.h>`) | ✅ available. We use DPP's copy, so there's one JSON library and nothing to mismatch |
| DECtalk (develop) | submodule, our CMake | ⏳ vendored; build is Phase 4 |
| CTRE | Conan (header-only) | ❓ only if picked for §9.1 |
| SQLiteCpp | Conan | ❓ only if picked over our own wrapper (§5.4) |
| yt-dlp, ffmpeg | external tools | later (music) |
| Image library | TBD | later (appearance tracking) |

---

## 18. Build order

**Phase 0: Foundations**
- ✅ Git, submodules, C++20, DPP from source, Conan dependencies, README
- SQLite layer: wrapper, migrations, backups (§5.3–5.4)
- Bootstrap config + per-guild settings (§5.1)
- Bot context, coroutine command registry (§5.5), raw-API helper (§5.7)

**Phase 1: Framework proof**
- Basic commands + goodbye phrase (§6), permission preflight (§7)
- Message pipeline (§5.6) + trigger responses (§11)

**Phase 2: Data features**
- `nicknames.json` import, nickname tracking with audit-log attribution,
  reconciliation, `/nicknames` + the shared paginator (§8)
- Midnight (§10)

**Phase 3: URL replacement**
- Core rewrite + `UrlReplacements.txt` import + `/en` (§9.1–9.3, 9.5, 9.7)
- Reaction statistics + `/linkstats` (§9.6)
- Commands, then the panel/modal UI (§9.4)

**Phase 4: Voice**
- DECtalk CMake + dictionary build (§12.1), engine + sanitizer (§12.2–12.5)
- Resampler + mixer (§13), `/speak` (§12.6)
- `/chat` voice message through the raw-API helper (§12.7)

**Phase 5: LLM**
- Provider layer + text replies + guards (§14.1–14.3, 14.7–14.8)
- Short-term then long-term memory (§14.5)
- Voice replies with DECtalk prompting (§14.2, 14.6)
- Bot-to-bot (§14.4)

**Later:** music, emote stats, appearance tracking.

---

## 19. Open decisions

New ones raised by this round's findings, roughly in the order they'll come up:

**Repository and build**
1. **Initial commit and `java-reference/`.** Nothing is committed yet. The Java
   reference is ~282 MB (build output, DLLs, jars) and includes
   `nicknames.json` with your friends' Discord IDs and usernames. Commit all of
   it, commit only the source, or keep it out of git altogether?

> Dont include in git repo at all. Ok to move nicknames.json and etc into repo.

2. **Submodules vs copying the source in.** I used submodules: pinned
   versions, easy updates, a small repo, and DECtalk's proprietary code isn't
   redistributed. The downside is `git clone --recursive` (the README covers
   it). OK?

> Submodles is fine.

3. **C++20 or C++23.** On MSVC, `CMAKE_CXX_STANDARD 23` becomes
   `/std:c++latest`, where some library features are still incomplete or can
   change between compiler updates. Recommendation: stay on 20 now and revisit
   once the bot is working.

> Lets stay with cpp20.

4. **Repo visibility.** If it will ever be public, DECtalk's licence (§2.4) is
   one more reason for submodules, and the importers must never ship the real
   `nicknames.json`.

> Repo will be public. But mostly for my personal portfolio rather than to publicly distribute for other to use. This is mostly a personal bot for my personal server.

**Data layer**
5. **SQLite access:** our own thin C++20 wrapper (better for learning, less
   code to depend on) or SQLiteCpp (quicker to start)? Leaning toward our own.

> Lets go with our own wrapper.

6. **Concurrency:** one connection with a mutex, a connection per thread, or a
   dedicated database thread with an awaitable queue? Leaning toward one
   connection plus a mutex in WAL mode; the load is tiny.

> one connection plus mutex should be fine.

7. **Timezone for importing `nicknames.json`** (§8.3): assume `America/Chicago`?

> Use US Central Time (CT). Likley will need to adjust for daylight savings based on date. 

**Features**
8. **Own-nickname changes and the audit log** (§8.1): if Discord doesn't log
   self-changes, should unmatched changes be recorded as "self" or "unknown"?
   (Will verify during implementation.)

> I have confirmed own nickname changes are logged in the audit logs. in the case of unmatched happening, use "unknown". 

9. **URL scanner:** hand-written, or CTRE (a nice chance to use C++20)?

> Im interested in this CTRE library, let go with that.

10. **URL panel design** (§9.4): does the rule list + Edit/Delete + "one
    alternate per line" modal work for you?

> Yes, seems good. May have feedback after trying it once implemented.

11. **Trigger match modes** (§11): whole word + substring OK, and should the
    `YesNoAnswers` pool come back as a weighted trigger?

> should be configurable match mode per. no, drop YesNoAnswers.

**Voice and DECtalk**
12. **DECtalk as a DLL or a static library** (§12.1). Leaning DLL.

> use DLL.

13. **Inline command policy** (§12.5): block known-dangerous commands and
    allow the rest, or allow only a listed set? Should any commands be
    restricted to certain roles?

> silently filter out dangerous things for normal users, let admins use them. No other commands that i know of that need to be restricted, but im not super familiar with all the commands offered.

**LLM**
14. **Default model:** Haiku 4.5, or Sonnet 5?

> Haiku 4.5

15. **Monthly spend cap** amount (§14.7)?

> lets say 20$. exact value not super important as long as its reasonable.

16. **Voice-reply rule** (§14.2): is "same voice channel + enabled" right?

> likely best is explicit enable command/action that will have the bot join the user in the vc they are in. See comment above in §14.2 

17. **Long-term memory** (§14.5): model-managed through tools (recommended), or
    automatic extraction after each conversation (no tool calls, but the model
    decides nothing)? Scope: per guild, per user, or both?

> Tool call approach. Tool calling is also somthing that will be used by advanced features later down the line, so good to get started.

18. **Bot-to-bot limits** (§14.4): the defaults above, or something stricter?

> Limits are fine as is. I will fine tune after implementation.
