# LatiBot Stage 2 (v4): Final Implementation Plan

This supersedes [Porting_Plan_Stage2_v3.md](Porting_Plan_Stage2_v3.md), which
is left as-is with your review comments.

**This is meant to be the last planning document before implementation.** So
it does two things differently from v1–v3:

- Every question from v3 is **answered**. Where you didn't state a preference,
  I picked the recommended value and listed it in §20 as a **starting default**
  that's one setting change away from something else. Nothing is left blocking.
- The early sections are written to be built from: concrete names, concrete
  tables, concrete order of work.

Everything you approved in v1–v3 is kept short here. The earlier documents
hold the longer reasoning.

---

## 0. What changed since v3

| Area | v3 | v4 |
|---|---|---|
| Replacement messages | Sent as a reply to the original | **Plain message, never a reply** (§9.2) |
| Linking back to the original | Reply reference, or a path-matching heuristic | **The nearest earlier message that contains a link** (§9.7) |
| Reaction stats | Reactions received only | **Received, given, and self-reactions tracked separately** (§9.6) |
| Backfill | Marked `source = 'backfill'` | **No distinction from live data**; only the reaction time is missing (§9.7) |
| Retry button | Original poster and moderators | **Anyone** (§9.4) |
| Legacy formats | To be supplied | **All six iterations documented** (§9.7) |
| `[:play]` trust | Guild admin | **Guild admin in a trusted guild, or a trusted user** (§12.5) |
| Terminology | "fakes" | **"mocks"** (§17.3) |
| Docs | `java-reference/` | **`docs/porting/`** (done) |
| Tooling | Some now, some later | **Everything set up in Phase 0**: Catch2, CTest, clang-format, clang-tidy, ASan, fuzzing, CI (§17, §19) |
| Open decisions | 17 | **0 blocking**; §20 lists the chosen defaults |

---

## 1. State of the workspace

Done and verified:

- C++20, MSVC, Conan 2; DPP v10.1.6 and DECtalk as submodules; DPP built from
  source with voice and coroutines; Debug and Release both build and run.
- `.gitignore` excludes `java-reference/`, `data/`, build output and secrets.
- Porting docs live in `docs/porting/` (you moved them).
- `origin` is set to `https://github.com/mhorn00/LatiBot-cpp.git`.
  **Nothing has been committed yet.**

To do in Phase 0 (§19): the Conan and CMake changes in §18, the test harness,
the tooling, CI, and the first commits.

---

## 2. Findings carried forward

All approved in v3; kept here in one-line form so this document stands alone.

| # | Finding | Consequence |
|---|---|---|
| 2.1 | `co_request` uses the cluster-wide 60 s timeout, adjustable via `set_request_timeout` | Use `co_request` directly for the LLM. No DPP patch |
| 2.2 | DECtalk's callback passes 32-bit values; on x64 both the instance parameter and the buffer pointer are truncated | Never pass a pointer. Track submitted buffers ourselves and rely on FIFO order (§12.3) |
| 2.3 | The buffer callback needs no window or message loop; the ID is `RegisterWindowMessage("DECtalkBufferMessage")` | Streaming is the primary synthesis path |
| 2.4 | `[:play]` reads any `.wav` on the host; `[:log]` writes `log.txt`/`dbglog.txt` in the working directory; `[:debug]` prints to stdout; `[:loadv]` is crash-prone | Denylist sanitizer with a trust level (§12.5) |
| 2.4b | Engine settings persist between utterances | Reset before every request (§12.5) |
| 2.5 | `[:dv]` exposes ~35 voice parameters over up to 11 base voices | Custom voices and the voice lab (§12.6) |
| 2.6 | Old replacement messages record neither the original poster nor a reply reference | The backfill uses the preceding-link-message rule (§9.7) |
| 2.7 | The repo will be public | Synthetic test data only; secret scanning in CI |
| 2.8 | "Administrator" is per server | Trusted servers and users in `config.json` (§12.5) |

---

## 3. Decisions locked in

| Area | Decision |
|---|---|
| Repo | Public; `java-reference/` untracked; DPP and DECtalk as submodules; docs in `docs/porting/` |
| Language | C++20 |
| Data | SQLite, our own thin wrapper, one connection + mutex, WAL, FTS5 on |
| Regex | CTRE |
| Tests | Catch2 v3, CTest, ports and mocks, ASan, fuzzing, GitHub Actions CI |
| URL replacement | Plain message (no reply); 2 attempts per mirror; Retry button open to anyone |
| Reaction stats | Received, given and self-reactions; kept forever; emoji aliases |
| DECtalk | Our CMake, DLL, streaming, denylist sanitizer, trusted-list gating |
| LLM | Anthropic default (`claude-haiku-4-5`), tool-based memory, versioned personality and system documents, $20/month with a $2/day cap |

---

## 4. Target project structure

```
CMakeLists.txt              top level: options, dependencies, subdirectories
CMakePresets.json           msvc (default), ninja-tidy, asan, fuzz
conanfile.py
.clang-format  .clang-tidy
.github/workflows/ci.yml
cmake/
  dectalk.cmake             our DECtalk build (Phase 4)
  warnings.cmake            one place for /W4 /WX and DECtalk's relaxed flags
docs/
  porting/                  these plans
  ideas/                    future ideas (cobalt write-up)
src/
  main.cpp                  small: load config, construct bot, run
  core/                     -> latibot_core (static library)
    bot.hpp/.cpp
    config/     bootstrap.*  guild_settings.*
    db/         database.*  statement.*  migrations.*  backup.*  import_legacy.*
    discord/    raw_api.*  gateway_impl.*
    commands/   registry.*  basic.*  nickname.*  urlrepl.*  linkstats.*
                midnight.*  trigger.*  speak.*  voice.*  llm_admin.*
    events/     message_pipeline.*  nickname_tracker.*  reaction_tracker.*
                url_replacer.*  embed_watch.*
    ui/         panel.*  paginator.*  modal_forms.*
    audio/      voice_mixer.*  resample.*  wav.*
                dectalk_engine.*  dectalk_sanitizer.*  voice_params.*
    llm/        provider.hpp  anthropic.*  conversation.*  memory.*
                tools.*  documents.*  responder.*  spend.*
    ports/      clock.hpp  discord_gateway.hpp  http_client.hpp  tts_engine.hpp
    util/       url_scan.*  text.*  scheduler.*  log.*
tests/
  CMakeLists.txt
  unit/  db/  golden/  live/
  mocks/                    mock_clock, mock_discord, mock_http, mock_tts
  fixtures/                 synthetic data, recorded LLM responses
  fuzz/                     libFuzzer targets
third_party/  DPP/  dectalk/
data/                       runtime: bot.db, backups/, import/ (gitignored)
```

---

## 5. Core infrastructure

### 5.1 Configuration

- **`config.json`** (global, rarely edited): database path, log level, default
  LLM provider and model, backup schedule, **`trusted_guilds`** and
  **`trusted_users`** (§12.5), spend caps.
- **Secrets from environment variables only:** `DISCORD_BOT_TOKEN`,
  `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, and `LATIBOT_TEST_TOKEN` for live
  tests. Startup fails loudly without the bot token.
- **Per-guild settings in SQLite**, edited at runtime through commands and
  panels, with typed getters and defaults.

### 5.2 Database

Our own wrapper (`database`, `statement`, `transaction`), one connection
guarded by a mutex, `journal_mode=WAL`, `foreign_keys=ON`, snowflakes as
`INTEGER`, times as Unix seconds UTC, `PRAGMA user_version` migrations run in
a transaction at startup, and scheduled online backups with rotation.

Starting schema (refined during implementation):

```sql
nickname_history(id, guild_id, user_id, nickname NULL, changed_by NULL,
                 changed_at, source, imported_raw NULL)
url_rules(guild_id, domain, position, host, translate_suffix NULL)
url_opt_outs(guild_id, user_id)
replacement_messages(message_id PK, guild_id, channel_id, original_message_id NULL,
                     original_author_id NULL, domain, alternate_index,
                     state, created_at)     -- state: ok | failed | retrying
reactions(message_id, user_id, emoji_key, reacted_at NULL,
          PRIMARY KEY(message_id, user_id, emoji_key))
reaction_log(id, message_id, user_id, emoji_key, action, at)   -- live only
emoji_aliases(guild_id, emoji_key, canonical_key)
backfill_progress(guild_id, channel_id, oldest_scanned_id, updated_at)
midnight_entries(id, guild_id, timezone, channel_id, message, enabled, last_fired_date)
triggers(id, guild_id, pattern, match_mode, cooldown_s, enabled)
trigger_responses(trigger_id, response, weight)
llm_triggers(id, guild_id, pattern, match_mode, context_prompt, probability,
             cooldown_s, enabled, created_by)
llm_settings(guild_id, key, value)
llm_blacklist(guild_id, kind, target_id)
llm_memory(id, guild_id, subject_user_id NULL, content, created_at, ...)  -- + FTS5
llm_documents(guild_id, kind, version, content, edited_by, edited_at, note)
llm_usage(id, guild_id, model, input_tokens, output_tokens, cost_usd, at)
tts_voices(guild_id, name, base_voice, params, created_by, updated_at)
guild_settings(guild_id, key, value)
```

### 5.3 Command registry

Handlers return `dpp::task<void>`, declare their required bot permissions and
default member permissions, and stay thin: convert the event into a plain
struct, call a core function, carry out the result (§17.3).

### 5.4 Message pipeline

One `on_message_create`, ordered stages:

```
ignore self / non-allowlisted bots
→ admin goodbye phrase          (consumes)
→ URL replacement               (does not consume)
→ simple trigger responses      (does not consume; suppresses advanced triggers)
→ LLM addressed / advanced trigger (consumes)
```

The order is a list, so it's cheap to change once it's running.

### 5.5 Raw-API helper

Thin wrapper over `cluster::post_rest` / `post_rest_multipart`, which already
handle authentication and rate limits. Returns parsed JSON or a typed error,
with a multipart variant. Known uses: voice messages (§12.8), suppressing
embeds on someone else's message (§9.2, a PATCH carrying only `flags`), and
any component type DPP lacks.

---

## 6. Basic commands

`/ping`, `/say`, `/shutdown`, `/status`, `/join`, `/leave`, plus the
configurable "say goodbye latibot" phrase (Administrator, phrase must be
essentially the whole message, short delay before shutdown). `/leave` also ends
a voice session.

## 7. Permission preflight

Each command and passive feature declares what it needs. Checked per guild on
`on_ready` and `on_guild_create`, reported at WARN, **never exits**. Includes
`VIEW_AUDIT_LOG`, `MANAGE_NICKNAMES`, `READ_MESSAGE_HISTORY`, `MANAGE_MESSAGES`,
`CONNECT` and `SPEAK`.

---

## 8. Nickname tracking

### 8.1 Attribution

1. `on_guild_member_update` with a changed nickname → write a history row
   immediately, `changed_by = NULL`.
2. `on_guild_audit_log_entry_create` for `aut_member_update` with a `nick`
   change → fill in `changed_by`, `source = 'audit_log'`.
3. No match after ~10 s → one `co_guild_auditlog_get` query as a safety net.
4. Still unmatched → stays **`unknown`**.
5. **Audit entries whose actor is the bot never overwrite `changed_by`.**

**`/nickname`** writes its own row first (`changed_by = invoker`,
`source = 'command'`) and registers a pending expectation
`(guild, target, new_nick)` with a ~30 s TTL, so the member-update event
doesn't add a duplicate. On failure the row is removed and the user gets an
error.

**`X-Audit-Log-Reason` is dropped** (was decision 9). DPP takes that header
from a cluster-wide slot which the next request from any thread can consume,
so it cannot be attached reliably, and attaching the *wrong* reason to an
unrelated moderation entry is worse than attaching none. Discord's audit log
will keep showing the bot as the actor, as it has for years; the answer to
"who actually did it" comes from `nickname_history`.

### 8.2 Storage and display

Raw IDs stored; names and avatars resolved at display time. Handles empty
histories, cleared nicknames (`NULL`) and departed members. `/nicknames` uses
the shared paginator with ◀/▶ buttons and page state in the `custom_id`, with
a `.txt` attachment for very long histories.

### 8.3 Import

`data/import/nicknames.json` → `nickname_history`, `source = 'import'`, with
the original text kept in `imported_raw`.

```cpp
const auto* ct = std::chrono::locate_zone("America/Chicago");
// ambiguous (Nov fall-back): choose::earliest
// nonexistent (Mar spring-forward): catch and shift forward one hour
```

`America/Chicago` carries the full DST history, including the 2007 rule
change. Tested across CST, CDT, both edge cases and pre-2007 dates.

### 8.4 Startup reconciliation

Nickname changes made while the bot was offline are recorded with
`source = 'startup'` and `changed_by = NULL`.

### 8.5 Later

Appearance tracking (avatars, role colours, possibly a generated image) is a
later migration.

---

## 9. URL replacement

### 9.1 Scanner

CTRE, `ctre::search_all` over the message, host looked up in `url_rules`,
output spliced using match offsets. Fixes from v1 carried over: every link in
a message is handled (not just the last), spoilers are detected by counting
literal `||` before the match, and `translate_suffix` (`/en`) is applied by
parsing the URL rather than concatenating, so query strings, fragments and
trailing slashes survive.

### 9.2 Posting

The replacement is a **plain message, never a reply**, in the current format:

```
:link: [_](<replaced url>)
```

wrapped in `||…||` when the original link was spoilered. The original message
has its embeds suppressed. This matches what you have today, and what the
backfill in §9.7 expects to find.

### 9.3 Embed verification

Per link, each mirror gets **2 attempts**: `alt1, alt1, alt2, alt2, …`. A link
counts as embedded when `on_message_update` for our message shows an embed for
it; otherwise a ~6 s fallback timeout advances to the next attempt, editing our
message. Messages with several links track each link independently. With
coroutines this whole flow is one linear function.

### 9.4 Failure and Retry

When every attempt fails:
1. The original message's embeds are **un-suppressed**, so its normal preview
   returns.
2. Our message is **kept** and edited to a short failure note with a **Retry**
   button (`custom_id = urlretry:<our message id>`), with its own embeds
   suppressed.
3. **Anyone can press Retry.** It runs one pass: one attempt per mirror.
   - Success → our message goes back to the normal replacement, the original is
     suppressed again, the button is removed.
   - Failure → the note records the time of the retry, and the button stays.

The state needed (original message ID, link, mirrors tried) lives in
`replacement_messages`, so Retry still works after a restart.

### 9.5 Management

- **`/urlrepl`** with no arguments opens an ephemeral **panel**: one row per
  rule with the domain and its ordered mirrors, Edit/Delete buttons, Add rule,
  and paging.
- **Edit/Add** opens a **modal**: domain, and the mirrors one per line in
  priority order. Delete confirms in the panel itself.
- **Commands stay**: `/urlrepl list | set | remove | test`, with autocomplete.
  `test <url>` is a dry run.
- Per-user opt-outs, per guild. No webhook mode. A one-time importer for
  `UrlReplacements.txt`.

### 9.6 Reaction statistics

The point of this is people-facing stats, in three groups:

- **Received:** reactions on a replacement message count for the **person who
  posted the original link** ("who gets the most 💀 of anyone").
- **Given:** the same rows counted by the **person who reacted** ("your top 3
  reactions").
- **Self-reactions** (reacting to your own link) are **recorded but excluded
  from both** of the above, and reported separately as their own stat.

Because every row holds both the poster and the reactor, one table answers all
three. Queries filter with `user_id != original_author_id` for the normal
stats, and `user_id = original_author_id` for the self-reaction one.

```sql
-- received, per poster
SELECT rm.original_author_id, COUNT(*)
FROM reactions r JOIN replacement_messages rm ON rm.message_id = r.message_id
WHERE r.user_id <> rm.original_author_id
GROUP BY rm.original_author_id;
```

Emoji are keyed as `u:<unicode>` or `c:<custom emoji id>`, and **aliases**
merge emotes that should count as one (the same emote from another server, or
one deleted and re-added). Aliases are applied **when stats are read**
(`COALESCE(canonical_key, emoji_key)`), so adding one immediately affects all
history. Managed with `/linkstats alias add | remove | list`, plus
`/linkstats emojis`, which lists likely duplicates (same name, different IDs).

Live tracking: `on_message_reaction_add` / `_remove` update `reactions` and
append to `reaction_log`. Rows are kept forever.

`/linkstats` views: top posters by reactions received, top reactors, top
emoji, per-user breakdown, and the self-reaction leaderboard. All filterable
by domain and date range, paginated.

### 9.7 Backfill (`/linkstats recompute`)

`/linkstats recompute since:<date> [until:<date>] [channel:<#c>]`, admins only.

Backfilled rows are **ordinary rows**, with no `source` marker, exactly as you
asked. One caveat, which is a Discord limitation rather than a choice:

> **Discord's API doesn't say when a reaction was added.** Listing reactions
> returns *who* reacted, never *when*. So backfilled rows have
> `reacted_at = NULL`, and date-filtered stats fall back to the message's own
> timestamp for them. Everything else (who posted, who reacted, which emoji)
> is recovered exactly.

**Identifying our replacement messages.** A message counts as one when it was
written by the bot's user **and** contains at least one link whose host is a
known mirror host, current or historical (a list in `config.json` that starts
from the current rules). That rule works across all six formats below, since
they all contain the replaced link.

**The six historical formats**, from your notes:

| # | Format | Notes for the parser |
|---|---|---|
| 1 | Full copy of the original text, link replaced, **sent as a reply** | The reply reference gives the original message directly: use it when present |
| 2 | **Webhook mode**: original deleted, webhook posts as the user, `<original> [.](replaced)` | **Skipped.** These aren't authored by the bot user, and the original is gone. You said few of them have reactions |
| 3 | Full copy of the original text, link replaced, plain message | Link extracted from anywhere in the text |
| 4 | `[.](<replaced>)` | |
| 5 | `:link: [.](<replaced>)` | |
| 6 | `:link: [_](<replaced>)` (current) | |

Anything that matches the bot-plus-mirror-host rule but no known format is
**counted and logged with its ID**, never guessed at. That list is part of the
final report, so odd cases can be looked at by hand.

**Finding the original poster.** Walk backwards from our message through the
same channel and take **the first earlier message that contains a link**,
skipping messages without one. That covers the case where someone chats
between the original and our reply. As an extra check, when the mirror host is
mapped back to the real domain, the link's path should match ours; a mismatch
is logged rather than silently accepted. Format 1 skips all of this, since the
reply reference is exact.

**Reactions** come from `co_message_get_reactions` per emoji, paged 100 at a
time, since the message object only carries counts.

**Running it:**
- Walks history backwards in pages of 100 (`co_messages_get` with `before`)
  until it passes `since`.
- Progress message updated every few hundred messages;
  `/linkstats recompute cancel` stops it; `backfill_progress` lets an
  interrupted run resume.
- **Re-running is safe:** for each scanned message the reaction rows are
  rebuilt from what Discord currently shows.
- Final report: messages scanned, replacements found, attributed,
  unattributed, unparsed (with IDs), reactions recorded.

---

## 10. Midnight

Per guild, any number of `{timezone, channel, message, enabled,
last_fired_date}` entries. A 30 s wall-clock poll compares
`zoned_time{tz, now}`'s local date against `last_fired_date` and fires once
the local time is ≥ 00:00:05, saving the date in the same transaction. That
fixes the drifting-time bug and double posts across restarts.
`/midnight list | add | edit | remove | toggle`, with timezone autocomplete.
`due(entry, now)` is a pure function, so DST cases are unit-testable.

---

## 11. Simple triggers

Per guild: pattern, **match mode per trigger** (whole word or substring; no
user regex), per-channel cooldown that is **configurable and may be 0**
(default 30 s), enabled flag, and weighted responses. Managed with
`/trigger add | edit | remove | list` **and** a panel. Seeded with the Java
defaults (`420`, `4:20`, `69` → "nice"). Doesn't consume the message, so a
message with both "420" and a link gets both. `YesNoAnswers.txt` is dropped.

---

## 12. DECtalk

### 12.1 Build

`cmake/dectalk.cmake` builds from the submodule without modifying it:
the `dectalk` **DLL** (86 sources, `DECTALK.DEF`, `winmm`, the Release/x64
defines from v2 §2.4, warnings relaxed for this target only), the `dectalk_dic`
host tool, and a custom command that compiles `dtalk_us.dic` next to the exe on
every clean build. Based on upstream's `cmake` branch, with the source list
checked against `develop`'s project file.

### 12.2 Startup

```cpp
TextToSpeechStartupExFonix(&handle, WAVE_MAPPER, DO_NOT_USE_AUDIO_DEVICE,
                           &on_dectalk_message, /*instance*/ 0,
                           dictionary_path.c_str());   // absolute
```

The instance parameter is **not** a pointer (§2.2); with one engine the
callback reaches it through a static. An absolute dictionary path removes the
registry and working-directory dependency that broke dictionaries in the Java
version.

### 12.3 Streaming synthesis

1. `TextToSpeechOpenInMemory(handle, WAVE_FORMAT_1M16)` once, kept open.
2. A ring of ~4 buffers of ~0.25 s, queued with `AddBuffer` and also pushed
   onto our own deque.
3. `TextToSpeechSpeak(handle, text, TTS_FORCE)`.
4. On each buffer message: take the front buffer from the deque (the pointer
   in the callback is truncated, §2.2), hand the PCM to the worker, requeue the
   buffer.
5. The worker resamples 11025 Hz mono → 48 kHz stereo and feeds the mixer, so
   audio starts about a quarter second in.
6. End of utterance: `TextToSpeechSync`, then `ReturnBuffer` to flush the tail.

`/chat` voice messages use the same path in collect mode.

*Spike questions:* whether `AddBuffer` may be called from inside the callback,
buffer size vs latency, and FIFO behaviour across `TextToSpeechReset`.

### 12.4 Threading

One worker thread owns the handle; requests arrive through a queue and
complete as awaitables, so commands `co_await` without blocking DPP's threads.
Serialisation also means no two requests share engine state.

### 12.5 Sanitizer, trust levels and reset

`dectalk_sanitizer::clean(text, trust)`, where `trust` is `trusted`, `user` or
`llm`:

| | `user` | `trusted` | `llm` |
|---|---|---|---|
| `[:play]`, `[:log]` | stripped silently | kept | stripped |
| `[:debug]`, `[:loadv]`, `[:setv]` | stripped | kept | stripped |
| `[:dv save]` | stripped | stripped | stripped |
| everything else | kept | kept | kept |

**Who is `trusted`:** a user listed in `trusted_users`, **or** a user with
Administrator in a guild listed in `trusted_guilds`. Both lists live in
`config.json`. That keeps host file access tied to servers you control, rather
than to whoever happens to be an admin somewhere the bot gets added (§2.8).

**LLM output is never trusted**, even when an admin asked the question, so a
prompt-injected message can't make the model emit `[:play "C:\..."]`.

Parsing mirrors DECtalk's own matcher: `[:name args]`, case-insensitive,
accepted as soon as the prefix is unique (`[:pla …]` counts), several commands
chainable inside one bracket (`[:rate 200 :play "x"]`). Heavily tested and
fuzzed (§17.5).

**Reset before every request** (engine settings persist, §2.4b): either
`TextToSpeechReset(handle, FALSE)` or an explicit preamble that sets the
requested voice and resets rate, mode and punctuation. The spike picks
whichever also clears `[:dv]` edits.

**Input cap:** 1000 characters for `/speak`, configurable per guild.

### 12.6 `/speak`, custom voices, voice lab

`/speak text [voice] [rate] [volume]`, with `voice` autocompleting the built-in
voices plus this server's saved ones. Volume via `TextToSpeechSetVolume`.

Custom voices live in `tts_voices` as a base voice plus ordered `[:dv]` pairs,
rendered as a preamble at speak time and clamped to each parameter's
documented range. **Anyone can create one; the creator or an admin can delete
it.**

**`/voice lab`** opens an ephemeral panel: current parameters grouped
(Identity, Pitch, Quality, Formants, Gains), a **▶ Test** button that speaks a
test phrase in the bot's voice channel, per-group **Edit** modals (≤5 inputs
each), a **Raw** modal for pasting a whole `[:dv …]` string, **Save as…**, and
**Reset**. The working draft is kept per user for ~30 minutes, so closing the
panel doesn't lose it.

### 12.7 Stopping and limits

- **`/tts stop`** (trusted users, admins, or whoever queued the current
  utterance): flush the engine, clear the queue, drop queued mixer audio and
  call `stop_audio`. Music resumes as normal.
- **`/tts skip`:** current utterance only.
- **Duration cap:** 60 s of generated audio per utterance, configurable per
  guild. This is what contains `[:rate 75]` on long text, long `[:pause]` and
  `[:tone]`, without having to anticipate each trick.

### 12.8 `/chat` voice message

Synthesize, wrap in a WAV header, compute duration and the 256-bucket peak
waveform, and send via `post_rest_multipart` with `flags: 8192`. No temp files.

---

## 13. Mixer and voice sessions

**Mixer:** one per guild, owns the voice client, prioritized sources. TTS
pauses music and resumes it afterwards (DPP `pause_audio` + track markers).
Ducking and overlay are later options.

**Voice sessions:**
- `/voice start` — the bot joins the voice channel **you** are in (moving if
  it's elsewhere in that guild) and records
  `{guild, voice_channel, text_channel, started_by}`.
- While active: LLM replies in that text channel are **spoken as well as
  posted**, and `/speak` from anywhere in the guild goes to that channel.
  The posted text keeps the **inline `[:commands]` as written** (after
  sanitizing), so you can see what the model was trying to do with the voice.
- Ends on `/voice stop` or `/leave`, on disconnect, or when no humans are left
  after a **30 s grace period**, at which point the bot leaves. Auto-leave also
  applies after a plain `/join`.

---

## 14. LLM

### 14.1 Providers

`llm_provider` interface, Anthropic default, OpenAI optional. Provider and
model per guild, defaults in `config.json`. Default model **`claude-haiku-4-5`**
($1/$5 per million tokens). `claude-sonnet-5` and `claude-opus-5` selectable.
Model IDs carry no date suffixes. The request builder is **model-aware**: it
only sends `temperature` to models that accept it, and only sends effort
settings to models that support them.

### 14.2 Reply mode

Text by default; spoken **and** posted while a voice session is active in that
guild and the message is in the session's text channel. Voice replies use the
DECtalk-aware prompt and the `llm` trust level. The posted copy keeps the
sanitized inline commands, so the DECtalk markup is visible rather than
stripped for display.

### 14.3 When it responds

- **Addressed:** @mention, a reply to one of its messages, or a message
  starting with its name. Consumes the message.
- **Advanced triggers** (separate from §11): `llm_triggers` rows with a
  pattern, match mode, a short **`context_prompt`** describing *what* to say,
  a probability and a per-channel cooldown. *How* to say it comes from the
  shared **trigger style** document (§14.5), so each trigger's prompt stays
  short. Context window: 5 recent messages. Admin-managed by command and panel.
- Blacklists and the spend cap are checked first.
- **When a simple and an advanced trigger both match one message, the simple
  one wins** and the advanced one doesn't fire. That keeps the cheap, instant
  response in charge and avoids paying for a model call on a message that
  already has an answer. Tunable after implementation.

### 14.4 Bot-to-bot

Per-guild allowlist of bot user IDs. At most ~6 consecutive bot turns per
channel, a minimum delay between turns, a daily cap, and a human message
resets the counter. Optionally only when a human started the exchange. Limits
are per-guild settings to tune after implementation.

**The allowlist itself was built in phase 1, not here.** §5.4 already put
"ignore self / non-allowlisted bots" at the top of the message pipeline, so
every feature that reads a message needs it, not just the LLM. It is the
`allowed_bots` table, `events::bot_allowlist`, and `/bots allow | deny |
list`.

Hearing and answering are separate decisions, and only the first one is
shared. The allowlist says which bots reach the pipeline at all; each feature
then opts in for itself, because a trigger firing on another bot's message is
a much smaller commitment than an LLM conversation with one. For simple
triggers that opt-in is `triggers.respond_to_bots`, off by default, set by
`/trigger add bots:true` or the panel's **Answer bots** button.

What stays in phase 5 is everything above: the turn limits, the delay, the
daily cap, and the "a human started it" rule. Those exist because an LLM
exchange is expensive and open-ended. A trigger reply is neither, and its
cooldown already bounds it.

### 14.5 Memory, settings and documents

**Short-term:** rolling per-channel window, bounded by message count and an
approximate token budget. **Admin-editable at runtime** through
`/llm settings` (panel + modal), stored in `llm_settings`, read per request:
context message count, token budget, max output tokens, per-user and
per-channel rate limits, advanced-trigger context size. Values are validated
against documented ranges.

**Long-term:** `llm_memory` with FTS5. The model manages it through the
`remember` / `recall` / `forget` tools. The tool loop is a **general
framework** (registry of name + JSON schema + handler), capped at 4 rounds per
reply, so later features can add tools without touching the loop. The most
relevant memories are also injected up front. Admins get
`/memory list | forget | clear`; users can remove their own.

**Documents** (`llm_documents`, kinds `personality`, `system`,
`trigger_style`): every edit is a new version, so nothing is lost and
`revert` is one command.
`/llm personality view | edit | history | diff | revert`, likewise for the
others. Editing uses a pre-filled modal (up to 5 sections × 4000 characters),
or a `.txt`/`.md` attachment for longer text.

- **`system` and `trigger_style`: admins only.**
- **`personality`:** gated by the `personality_editor_role` setting, which
  **defaults to `@everyone`**, since it's meant to be a living document people
  tune. Point it at any role to narrow it. Edits aren't announced in Discord;
  they go to the bot's own log, and `history` / `diff` / `revert` show them on
  demand.
- **Prompt order:** fixed in-code rules → `system` → `personality` →
  memories → conversation. The personality section is labelled as style
  guidance that can't override what's above it.
- Token counts are estimated on save and warn when large, since documents are
  sent on every request. **Prompt caching** (`cache_control` on the stable
  prefix) is on from the start.

### 14.6 Guards

Per-guild blacklists of users and roles; per-user and per-channel rate limits;
output token cap; per-guild on/off. **Spend cap: $20/month and $2/day**,
computed from `usage` counts × per-model prices in `llm_usage`; the LLM
switches itself off at the limit and tells admins. Keys from environment
variables only.

### 14.7 HTTP

`co_request` with the cluster's 60 s timeout. Typing indicator while waiting.
No streaming responses.

---

## 15–16. Music and emote stats (later)

Music: queue commands stay unregistered; the later design uses DPP track
markers, the §13 mixer, yt-dlp and ffmpeg. Emote stats: incremental scanning
with per-channel high-water marks, reusing `backfill_progress`.

---

## 17. Testing

### 17.1 Framework

**Catch2 v3** (decided), with CTest. Tags: `[url]`, `[db]`, `[golden]`,
`[live]`. `catch_discover_tests` registers each case, so VS Code's Testing
sidebar lists them individually.

### 17.2 Build layout

```cmake
add_library(latibot_core STATIC ${CORE_SOURCES})
target_link_libraries(latibot_core PUBLIC dpp SQLite::SQLite3 ctre::ctre)

add_executable(LatiBot src/main.cpp)
target_link_libraries(LatiBot PRIVATE latibot_core)

option(LATIBOT_BUILD_TESTS "Build unit tests" ON)
option(LATIBOT_BUILD_FUZZERS "Build libFuzzer targets" OFF)
if(LATIBOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

`ctest --test-dir build -C Debug` runs everything except `[live]`.

### 17.3 Ports and mocks

**Functional core, thin shell:** features are functions from plain data to
**decisions** as plain data (`url_replacer::plan(msg, rules, opt_outs)`,
`midnight::due(entry, now)`). The DPP handler converts, calls, and carries out.

**Ports** (`src/core/ports/`) wrap the outside world, with **mocks** in
`tests/mocks/` (your preferred term, even though they're hand-written):

| Port | Real | Mock |
|---|---|---|
| `clock` | `system_clock` | `mock_clock`: set and advance time |
| `discord_gateway` | the handful of `dpp::cluster` calls we use | `mock_discord`: records calls, returns scripted results and history pages |
| `http_client` | `co_request` | `mock_http`: replays recorded responses |
| `tts_engine` | DECtalk worker | `mock_tts`: a tone of the right length |

**Coroutines:** `dpp::task::sync_wait_for(2s)` runs a flow to completion in a
test, and the timeout turns a hang into a failure. Mocks return
already-complete results, so no event loop is needed.

### 17.4 Database tests

Fresh `":memory:"` database per test, running the real migrations: 0 → latest,
stored older-version fixtures migrating forward, importers over synthetic
fixtures (empty, malformed, edge-case timestamps), and a backup taken during
an open write transaction that then passes `PRAGMA integrity_check`.

### 17.5 Coverage per feature

| Feature | Tests |
|---|---|
| URL scanner | multiple links, spoilers, `/en` with query/fragment/slash, unknown domains, opt-outs; 100 KB pathological input under a time limit; `BENCHMARK` |
| Embed flow | coroutine tests with `mock_discord` + `mock_clock`: success, retry schedule, all-fail → un-suppress + button, retry success → re-suppress |
| Legacy parser | each of the six formats; format 2 skipped; unknown → reported not guessed; preceding-link-message rule with chat in between, with no candidate, with a path mismatch |
| Reaction stats | received / given / self split, alias merge and un-merge, backfill idempotency |
| Nicknames | pending expectation, bot-as-actor never overwrites, 10 s fallback, unmatched → unknown |
| CT import | CST, CDT, ambiguous 1:30, nonexistent 2:30, pre-2007 |
| Midnight | `due()` across DST, offline gap, no double fire |
| Triggers | word vs substring, cooldown 0 and N, per-channel isolation |
| Paginator | `custom_id` round-trip, 100-character limit, bounds |
| DECtalk sanitizer | the §12.5 table per trust level; prefixes, chaining, mixed case, unterminated brackets; **fuzzed** |
| DECtalk engine | buffers return in order, reset clears state between requests, duration cap, stop; **golden audio** |
| Resampler / WAV | sine in → expected length and frequency; header bytes; waveform of silence and tone; `BENCHMARK` |
| LLM | model-aware fields, prompt order, cache placement, budget trimming, tool loop, max rounds, 429/529, malformed JSON |
| Spend cap | cost maths, daily and monthly rollover, auto-disable |
| Voice session | grace period, auto-leave, move vs join |

**Golden audio:** fixed phrases produce a stored hash and sample count; a
change fails the test and writes the `.wav` for listening. One command updates
a golden file after a deliberate change.

**Fuzzing** (set up in Phase 0, targets added as the parsers land): libFuzzer
via `/fsanitize=fuzzer`, `LATIBOT_BUILD_FUZZERS=ON`, for the URL scanner, the
DECtalk sanitizer and the legacy parser. Run on demand and nightly in CI, with
a seed corpus and any crash inputs checked in as regression tests.

**Live tests** (`[live]`, lower priority): excluded unless `LATIBOT_TEST_TOKEN`
is set, run against your test bot and a test server. They cover the
modal-submit question (§9.5), audit-log timing, real embed behaviour per
mirror, and voice. Everything else stays a short manual checklist in
`tests/live/CHECKLIST.md`.

### 17.6 Tooling

| Tool | Setup |
|---|---|
| CTest + VS Code Testing | built in |
| AddressSanitizer | install `Microsoft.VisualStudio.Component.VC.ASAN`; `asan` preset (also disables `/RTC1`, which conflicts) |
| clang-tidy | `ninja-tidy` preset, since `compile_commands.json` needs Ninja; `.clang-tidy` checks our code only |
| clang-format | `.clang-format`, format on save |
| OpenCppCoverage | optional local HTML coverage report |
| Warnings | `/W4 /WX /permissive-` on our targets; DECtalk and DPP keep relaxed flags via `cmake/warnings.cmake` |

### 17.7 CI

GitHub Actions on `windows-latest` (free for public repos), on push and PR to
`main`: checkout with submodules → restore Conan and DPP build caches →
`conan install` → configure → build → `ctest` excluding `[live]` → upload
results. **gitleaks** runs on every push, and GitHub push protection should be
enabled on the repo. clang-tidy, ASan and fuzzing run on PRs and nightly to
keep pushes fast. A status badge goes in the README.

CI has no Discord token and no audio device, which is exactly why logic sits
behind ports.

### 17.8 Working rules

- A phase isn't done until its tests are green in CI.
- Bug fixes start with a failing test, including each of the four URL bugs
  ported from Java.
- Only synthetic data in `tests/`.

---

## 18. Dependency and build changes for Phase 0

```python
# conanfile.py
requires = ("openssl/3.6.4", "zlib/1.3.2", "opus/1.6.1",
            "sqlite3/3.53.4", "ctre/3.11.0")
test_requires = ("catch2/3.16.0",)
default_options = {"sqlite3/*:enable_fts5": True}
```

Plus: `CMakePresets.json` (msvc, ninja-tidy, asan, fuzz), `.clang-format`,
`.clang-tidy`, `cmake/warnings.cmake`, `tests/`, `.github/workflows/ci.yml`,
and a README section for the ASan component, Ninja and OpenCppCoverage.

Note that enabling FTS5 rebuilds SQLite from source on the next
`conan install`, which takes a few minutes once.

---

## 19. Build order

**Phase 0 — foundations, tests and tooling**
- **0a.** `latibot_core` / `LatiBot` / `latibot_tests` split; Catch2 and CTRE;
  FTS5; presets; `.clang-format`; `.clang-tidy`; warnings module; one passing
  test; ASan and fuzz presets proven on a throwaway target.
- **0b.** First commits (§19.1) and the CI workflow, green.
- **0c.** SQLite wrapper, migrations, backups, with their tests.
- **0d.** Bootstrap config, per-guild settings, the four ports and their mocks.
- **0e.** Command registry, raw-API helper, logging.

**Phase 1 — framework proof**
Basic commands and the goodbye phrase; permission preflight; message pipeline;
simple triggers; panel and paginator UI primitives.

**Phase 2 — data features**
`nicknames.json` import (CT/DST); nickname tracking, `/nickname` attribution,
reconciliation, `/nicknames`; midnight.

**Phase 3 — URL replacement**
CTRE scanner (each Java bug as a failing test first); posting and embed
verification; failure and Retry; `UrlReplacements.txt` import; reaction
tracking; `/linkstats` views; emoji aliases; the legacy parser and backfill;
commands, then the panel.

**Phase 4 — voice**
DECtalk CMake and dictionary; streaming spike (FIFO, reset, determinism,
latency); sanitizer with fuzzing; `/tts stop` and the duration cap; resampler
and mixer; `/speak`; voice sessions; custom voices and the voice lab;
`/chat` voice message.

**Phase 5 — LLM**
Provider and tool framework; text replies; guards, spend cap, prompt caching;
runtime settings; documents; short- then long-term memory; advanced triggers;
voice-session replies; bot-to-bot pacing (the allowlist itself shipped in
phase 1, see §14.4).

**Later:** music, emote stats, appearance tracking, and possibly the cobalt
idea.

### 19.1 First commits

Proposed sequence, once you're happy with this plan:

1. `chore: workspace setup` — `.gitignore`, `CMakeLists.txt`, `conanfile.py`,
   `README.md`, `src/main.cpp`, `.gitmodules` and both submodules.
2. `docs: porting plans` — `docs/porting/`, `docs/ideas/`.
3. `build: test harness and tooling` — presets, tests, formatting,
   clang-tidy, CI.

Before the first push I'll check that nothing in the tree contains a token,
and `java-reference/` stays untracked.

---

## 20. Starting defaults (change any of these later)

No open questions remain. These are the values I picked where you didn't
state one; each is a setting or a small code change, not a design decision.

| # | Setting | Default | Where |
|---|---|---|---|
| 1 | Trusted for `[:play]`/`[:log]` | `trusted_users`, or Administrator in a `trusted_guilds` server | `config.json` |
| 2 | `[:debug]`, `[:loadv]`, `[:setv]` | trusted only | §12.5 |
| 3 | Custom voices | anyone creates; creator or admin deletes | §12.6 |
| 4 | Max utterance | 60 s | per guild |
| 5 | `/speak` input | 1000 characters | per guild |
| 6 | Voice-session grace | 30 s | per guild |
| 7 | Voice sessions | speak **and** post the text, inline commands kept visible | per guild |
| 8 | Simple trigger cooldown | 30 s (0 allowed) | per trigger |
| 9 | Advanced trigger context | 5 messages | per guild |
| 10 | Personality editing | `personality_editor_role`, defaulting to `@everyone` | per guild |
| 11 | Document edit notices | internal log only, no Discord announcement | §14.5 |
| 12 | Spend caps | $20/month, $2/day | `config.json` |
| 13 | LLM tool rounds | 4 per reply | `config.json` |
| 14 | Bot-to-bot pacing | 6 turns, daily cap, human resets | per guild |
| 14a | Bot allowlist | empty: every bot ignored | per guild, `/bots` |
| 14b | Trigger answers bots | off | per trigger, `/trigger … bots:` |
| 15 | Embed timeout | ~6 s per attempt, 2 attempts per mirror | per guild |
| 16 | Simple + advanced trigger on one message | the simple trigger wins | §14.3 |

If you're happy with this document, the next step is Phase 0a: the library
split, Catch2, the presets and the first green test.
