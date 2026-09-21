# LatiBot Stage 2 (v3): Detailed C++ Implementation Plan

This supersedes [Porting_Plan_Stage2_v2.md](Porting_Plan_Stage2_v2.md), which is
left as-is with your review comments. v3 does four things:

- folds in every comment and §19 answer from the v2 review;
- corrects two things v2 got wrong;
- adds a **testing strategy** (§17), which v2 didn't cover;
- ends with the decisions that are still open (§20).

Anything you approved without changes is kept short here. v1 and v2 have the
longer reasoning.

---

## 0. What changed since v2

| Area | v2 | v3 |
|---|---|---|
| `co_request` timeout | Wrap the callback `request` because `co_request` has no timeout | **Correction: no patch or wrapper needed.** Both use the cluster-wide 60 s timeout (§2.1) |
| DECtalk callback | Pass `this` as the instance parameter | **Correction: that truncates on x64.** The callback's parameters are 32-bit (§2.2) |
| DECtalk synthesis | In-memory first, streaming later | **Streaming first** (§12.3) |
| `[:play]` / `[:log]` | Always stripped | **Allowed for admins**, silently stripped for everyone else (§12.5) |
| TTS control | None | **`/tts stop`** and a duration cap (§12.7) |
| Custom voices | Not planned | **Saved custom voices + a "voice lab"** for quick tuning (§12.6) |
| `/nickname` attribution | Not covered | **Attributed to the person who ran the command** (§8.1) |
| Nickname import | Assume `America/Chicago` | **US Central with DST handled per date**, with edge-case rules (§8.3) |
| Embed failure | Retries capped at the number of alternates | **2 attempts per alternate**; on failure, keep our message with a **Retry** button (§9.3) |
| Reaction stats | Live events only | **Historical backfill** with a date bound, plus **emoji aliasing** (§9.6) |
| Simple triggers | Fixed cooldown | **Cooldown configurable, 0 allowed**; match mode per trigger (§11) |
| Voice replies | Same-VC heuristic | **Explicit voice session**: the bot joins your VC, routes TTS there, and auto-leaves (§13.2) |
| LLM triggers | Shared with simple triggers | **Separate "advanced triggers"** with a per-trigger context and a shared style text (§14.3) |
| LLM context settings | `config.json` only | **Admin-editable at runtime** (§14.5) |
| Personality | Not planned | **Versioned personality + system-instruction documents** (§14.6) |
| Testing | Not planned | **Catch2 unit tests, SQLite tests, golden audio tests, fakes, CI** (§17) |
| `java-reference/` | Commit decision open | **Excluded from git** (done, §1) |

---

## 1. Workspace changes this round

- `.gitignore` now excludes `java-reference/`, per your answer to v2 decision 1.
  - **Side effect:** the porting plans live in that folder, so they're now
    untracked as well. v3 decision 1 (§20) proposes moving them to `docs/plans/`.

> Sure, i've moved the plan documents to 'docs/porting/'.

- Nothing else changed.
  - The test framework, CTRE and FTS5 are **not** installed yet. §18 lists the
    exact changes, ready to apply once you confirm §20.
  - Nothing has been committed yet.

---

## 2. Findings and corrections

### 2.1 Correction: `co_request` already has a usable timeout

v2 said `co_request` was stuck on a 5-second timeout and the callback
`request(...)` took a timeout parameter. Reading DPP 10.1.6's source directly,
neither is true:

- `cluster::request(url, method, callback, postdata, mimetype, headers, protocol)`
  has **no** timeout parameter. `co_request` is a thin wrapper around it.
- Both end up in `http_request::run` (`queues.cpp`), which passes
  **`owner->request_timeout`**. That's a cluster-wide setting, **default 60 s**,
  changeable with `cluster::set_request_timeout(seconds)`. The 5 s value v2
  quoted is only the default argument of the low-level `https_client`
  constructor, which the cluster never relies on.
- The same setting also applies to Discord REST calls. Raising it only makes a
  hung Discord request take longer to fail, which is harmless here.

**So:** use `co_request` directly. No patch, no fork, no wrapper. 60 s is plenty
for Haiku with a capped `max_tokens`. If slow tool loops ever need more, call
`set_request_timeout(120)` once at startup.

Also, `co_request` requests go through DPP's `raw_rest` queue, which is
separate from the Discord REST queue. A slow LLM call won't hold up Discord
calls.

> Great. Approved. 

### 2.2 Correction: DECtalk's callback is 32-bit on x64

The callback signature is `void (*)(LONG, LONG, DWORD, UINT)`, and the instance
parameter is a `LONG`. On 64-bit Windows, `LONG` and `DWORD` are **32 bits**.
Two consequences:

- **v2's `reinterpret_cast<LONG>(this)` would have truncated the pointer**,
  which would crash or corrupt memory. Instead, pass a small integer engine id,
  or skip the parameter entirely, since there's one engine (§12.4).
- **The buffer pointer in the buffer-ready callback is truncated too.** In
  `ttsapi.c` the engine calls
  `Report_TTS_Status(phTTS, uiID_Buffer_Message, 0, (LPARAM)pTTS_Buffer)`,
  and `Report_TTS_Status` takes `long lParam2`. So the callback receives only
  the low 32 bits of the buffer address.
  - **Workaround, no DECtalk changes needed:** we allocate and queue the
    buffers ourselves, and the engine returns them **in the order they were
    added**. We keep a `std::deque` of the buffers we've submitted and take
    the front one whenever a buffer message arrives.
  - The truncated value can still be compared against the low 32 bits of the
    front buffer's address as a sanity check. A mismatch gets logged and
    handled as an engine error.

This is exactly the kind of thing the §17 tests will pin down: a
streaming test that checks every returned buffer matches the one we expected.

> Looks good. And yes, the testing of this is a must. Approved. 

### 2.3 Streaming is unblocked

You asked to reevaluate streaming vs buffered, since v1 only put streaming
second because of the open question about how buffer messages arrive. That
question is now answered:

- The engine calls our function directly on its own thread. No window or
  message loop is involved.
- The message ID is `RegisterWindowMessage("DECtalkBufferMessage")`.
- Buffers come back in FIFO order.

So **streaming is now the primary design** (§12.3). "In-memory" and
"streaming" turned out to be the same API (`OpenInMemory` + `AddBuffer`). The
only difference is whether we pass each buffer on as soon as it arrives or
wait for `Sync`. `/chat` voice messages still collect the whole thing, since
they need it all anyway.

> Approved.

### 2.4 What the risky DECtalk inline commands actually do

After reading the command handlers (`cm_copt.c`, `cmd_wav.c`, command table in
`c_us_cde.h`):

| Command | What it really does | Normal users | Admins |
|---|---|---|---|
| `[:play "path"]` | Opens **any** `.wav` path on the host with `mmioOpen` and plays it | stripped | allowed |
| `[:log ...]` | Writes fixed file names **`log.txt` / `dbglog.txt` in the working directory** (not an arbitrary path, as v2 implied) | stripped | allowed |
| `[:debug n]` | Sets engine debug flags. Prints diagnostics to the bot's **stdout** | stripped (recommended) | allowed |
| `[:loadv n]` / `[:setv n]` | Stores and replays a command macro. The source itself says `loadv` "will probably crash and burn if a flush happens in the middle", and `/tts stop` flushes | stripped (recommended) | allowed |
| `[:dv save]` | Makes the current voice edits permanent **for the engine handle** | stripped for everyone; custom voices are stored by us instead (§12.6) | stripped |
| `[:tone]`, `[:dial]`, `[:pause]` | Generate tones and silence of any length | allowed | allowed |

Two more things:

- **Engine state persists between requests.** `[:rate]`, `[:dv ...]`,
  `[:mode]`, `[:phoneme on]`, `[:punct]` and so on stay set on the handle. One
  user's `[:rate 75][:dv ap 300]` would carry into the next person's `/speak`.
  So every request starts with a **reset preamble** (§12.5). The spike checks
  whether `TextToSpeechReset` alone is enough.
- The duration cap (§12.7) is what stops `[:tone]` and `[:pause]` from holding
  the channel.

> Looks good. Approved.

### 2.5 Custom voices are built into DECtalk

`[:dv <param> <value> ...]` (alias `[:define_voice]`) edits the current
speaker. There are about 35 parameters. The ones that matter most for tuning:

| Group | Parameters |
|---|---|
| Identity | `sx` sex, `hs` head size %, base voice (`[:np]`, `[:nb]`, ... the 11 built-ins) |
| Pitch | `ap` average pitch (Hz), `pr` pitch range %, `as` assertiveness, `hr` hat rise, `sr` stress rise, `bf` baseline fall |
| Voice quality | `br` breathiness, `ri` richness, `sm` smoothness, `la` laryngealization, `lx` lax breathiness, `qu` quickness |
| Formants | `f4 b4 f5 b5 f7 f8` |
| Gains | `gf gh gv gn g1`–`g5` (`g5` is loudness) |

So a custom voice is just a base voice plus a list of `[:dv]` pairs, which we
store and prepend. That's what the voice lab edits (§12.6).

Note: the voice table has up to 11 base voices, not the 9 v2 mentioned. `val`
is always there, and `chris` depends on a build flag, which Phase 4 will
check.

> Sounds good.

### 2.6 Old replacement messages don't record who posted the link

The Java bot sent `🔗[_](<replaced url>)`, wrapped in `||...||` for spoilers,
as a **plain new message**: not a reply, no mention, no author name. Your
recompute request (§9.6) needs the original poster to credit reactions. For
history, that means a heuristic: look at the preceding messages in the same
channel for the link we replaced. From now on, we record it and also send the
replacement **as a reply** to the original, without a ping (§9.3,
decision in §20).

> When originally testing the feature, we decided we didnt like having the bot send the message as a reply. Keep it as just a message sent with no reply. It should be valid in pretty much all cases to take the proceding message that has a link in it as the original poster. There are rare cases where someone sends a message just after someone sends a link that gets replaced, so ensuring the proceding message has a link in it and skiping over ones that dont should cover 99.9% of cases.

### 2.7 A public repo changes a few defaults

You said the repo will be public, as a portfolio piece:

- Real `nicknames.json` / `UrlReplacements.txt` go in `data/import/`, which is
  gitignored. **Tests use synthetic data only**, never real IDs or names.
    > Yes, i forgot to clarify this.
- The CI pipeline (§17.7) gets a **secret scan**, and GitHub push protection
  should be turned on for the repo.
    > sure.
- DECtalk stays a submodule (not redistributed), as decided.
- A clean test suite and CI badge are also a good portfolio signal.
    > yes.

### 2.8 "Administrator" is a per-server permission

Allowing `[:play]` for admins means **Administrator in whichever server the
command came from**. On your server that's you and whoever you've given it to.
If the bot is ever added to another server, that server's admins could play
any `.wav` file on your machine and write `log.txt` into the bot's folder.
§20 asks whether to add a config allowlist of trusted servers or users.

> yes, a trusted list in the bot config or server and users would be good.

**LLM output** is always treated as coming from a normal user, even when an admin
asked the question. Otherwise a prompt-injected message could get the model to
emit `[:play "C:\..."]`.

> yes, probably for the best.

---

## 3. Decisions locked in

Everything from v2's §3, plus your §19 answers:

| # | Decision |
|---|---|
| Repo | `java-reference/` not in git; submodules for DPP and DECtalk; repo will be public (portfolio) |
| C++ | Stay on C++20 |
| Data | Own thin SQLite wrapper; one connection + mutex, WAL mode |
| Import | `nicknames.json` times are US Central, DST resolved per date |
| Nicknames | Self-changes are audit-logged (you confirmed); unmatched rows are recorded as **unknown** |
| URL scanner | **CTRE** |
| URL panel | Approved as designed; revisit after using it |
| Triggers | Match mode per trigger; `YesNoAnswers.txt` dropped |
| DECtalk | **DLL**; denylist sanitizer; dangerous commands admin-only |
| LLM | Haiku 4.5 default; $20/month cap; model-managed memory with tools; bot-to-bot limits as proposed |

---

## 4. Target project structure

Updated for testability. The main change: all bot code goes in a **static
library** that both the bot and the tests link (§17.2).

```
CMakeLists.txt
CMakePresets.json            (new) our presets: msvc, ninja-tidy, asan
conanfile.py
.clang-format                (new)
.github/workflows/ci.yml     (new, §17.7)
cmake/
  dectalk.cmake
docs/plans/                  (proposed) porting plans, tracked in git
src/
  main.cpp                   tiny: parse config, build bot, run
  core/ ...                  (see below; built as latibot_core)
    bot.*  config/  db/  discord/  commands/  events/  ui/
    audio/  llm/  util/
    ports/                   (new) interfaces the core talks through (§17.3)
      clock.hpp  discord_gateway.hpp  http_client.hpp  tts_engine.hpp
tests/                       (new)
  CMakeLists.txt
  unit/                      pure logic, one file per module
  db/                        SQLite + migrations + importers
  golden/                    DECtalk audio and WAV output comparisons
  fakes/                     fake_clock, fake_discord, fake_http, fake_tts
  fixtures/                  synthetic JSON, recorded LLM responses
  live/                      opt-in, against a test Discord server
third_party/  DPP/  dectalk/
data/                        runtime + import/ (gitignored)
```
> docs are now in "docs/porting/"
> Lets use the term "mocks" instead of "fakes"
---

## 5. Core infrastructure

Approved in v2; changes only:

- **§5.1 Config:** the LLM context and guard settings move into `guild_settings`
  and are editable at runtime (§14.5). Secrets still come only from
  environment variables.
- **§5.2 / 5.4 SQLite:** our own wrapper, **one connection guarded by a
  mutex**, WAL mode (decided). The `database` class takes a path, so tests
  open `":memory:"` (§17.4).
- **§5.5 Command registry:** handlers stay thin. They turn the DPP event into
  a plain struct, call a core function that returns a result, then act on it.
  That split is what makes most features testable without Discord (§17.3).
- **§5.6 Pipeline:** unchanged. As you said, which stages consume the message
  gets settled once it runs. Stage order is data (a list), so reordering is
  cheap.
- **§5.7 Raw-API helper:** unchanged.
- **Minor correction:** `CMAKE_EXPORT_COMPILE_COMMANDS` does nothing under the
  Visual Studio generator Conan's preset uses. It only matters for the Ninja
  preset used by clang-tidy (§17.6).

> Approved.

---

## 6. Basic commands

Unchanged from v2: `/ping`, `/say`, `/shutdown`, `/status`, `/join`, `/leave`,
and the configurable goodbye phrase. `/leave` also ends any voice session
(§13.2).

## 7. Permission preflight

Unchanged from v2. Add **`MANAGE_NICKNAMES`** (for `/nickname`) and
**`READ_MESSAGE_HISTORY`** (for the backfill in §9.6) to the declarations.

---

## 8. Nickname tracking

### 8.1 Attribution (event-driven), now including `/nickname`

Steps 1–3 from v2 are unchanged: record immediately with `changed_by = NULL`,
match the `aut_member_update` audit entry, and fall back to a one-off audit-log
query after about 10 s. You confirmed self-changes are logged, so an unmatched
row stays **`unknown`**.

**New: changes made through `/nickname`.** Discord logs these as done **by the
bot**, which would lose who ran the command. So:

1. Before calling the API, the command writes the history row itself:
   `changed_by = <invoker>`, `source = 'command'`. It also registers a
   short-lived **pending expectation** `(guild, target, new_nick)` in memory,
   expiring after about 30 s.
2. When `on_guild_member_update` sees a change that matches a pending
   expectation, it **doesn't insert a second row**. It just clears the
   expectation.
3. When an audit entry's actor is **the bot itself**, it never overwrites
   `changed_by`. The command row already has the right person.
4. If the API call fails, the command row is deleted and the user gets an
   error reply.

**Why not use DPP's `set_audit_reason`?** That would make Discord's own audit log
show "via /nickname by X". But `set_audit_reason` sets a **single cluster-wide
value** that the next REST call from any thread picks up. With coroutines
running concurrently, the reason can end up on the wrong request. If you want
the audit-log reason as well, the raw-API helper can send the
`X-Audit-Log-Reason` header on this one request, with no race. Listed in §20.

The Java owner-confirmation flow was dropped in v1 and stays dropped. The
server owner's nickname can't be changed by bots, so `/nickname` just says so.

> looks good. Approved.

### 8.3 Importing `nicknames.json`: US Central with DST

The Java times are local wall-clock times in US Central. Conversion:

```cpp
using namespace std::chrono;
const auto* ct = locate_zone("America/Chicago");   // US Central, with full DST history
local_seconds lt = parse_local(text);               // "yyyy-MM-dd HH:mm:ss"
sys_seconds utc = zoned_time{ct, lt, choose::earliest}.get_sys_time();
```

- **`America/Chicago` is the IANA name for US Central.** Its rule history is
  built in, so every date gets CST (UTC−6) or CDT (UTC−5) correctly, including
  the 2007 change to the DST dates.
- **Ambiguous times:** 1:00–1:59 on the November fall-back day happens twice.
  `choose::earliest` picks the first (CDT). There's no way to know which one
  was meant, and it's at most an hour off, only on that one day.
- **Nonexistent times:** 2:00–2:59 on the March spring-forward day don't exist,
  and the conversion throws `nonexistent_local_time`. We catch that and shift
  forward one hour, which is what the clock showed.
- **Each row keeps the original text** in an `imported_raw` column, so the
  conversion can be redone later if needed.
- **Unit tests** cover a CST date, a CDT date, both edge cases on a real
  transition day, and pre-2007 rules (§17.5).

8.2, 8.4, 8.5 and 8.6 are unchanged from v2.

> Approved.

---

## 9. URL replacement

### 9.1 Scanner on CTRE

**CTRE** (Compile-Time Regular Expressions, header-only, C++20) was chosen.
- The pattern is compiled into ordinary C++ when we build, so a typo in it is
  a compile error rather than a runtime surprise. Matching uses no heap.
- We use `ctre::search_all<pattern>(message)` to walk every URL. The host part
  goes through the rule lookup, then we splice the result into the output
  using match offsets (the multi-link fix from v1).
- It isn't recursive the way MSVC's `std::regex` is. Even so, a test feeds a
  100 KB message with thousands of URL-like fragments and checks it finishes
  quickly and without crashing (§17.5). That test checks that the fix works,
  not just that the code compiles.
- The spoiler-counting fix is unchanged.

> Approved.

### 9.3 Embed verification, retries and failure handling (redesigned)

**Attempt schedule.** Per link, each alternate gets **2 attempts**:
`alt1, alt1, alt2, alt2, ...`. A link counts as embedded when an
`on_message_update` for our message shows an embed for it. If no such update
arrives, a fallback timeout of about 6 s triggers the next attempt, which
edits our message to the next URL in the schedule. Messages with several
links track each link separately.

**When every attempt fails**, instead of deleting as the Java bot did:
1. **Un-suppress the embed** on the original message, so the original link
   shows its normal preview again.
2. **Keep our message**, edited to a short failure note (for example
   "🔗 Couldn't get an embed for this link (tried 3 mirrors)") with a
   **Retry** button (`custom_id = urlretry:<our_message_id>`). The link text is
   kept, and our message's own embeds are suppressed so it doesn't show a
   broken preview.
3. **Retry** runs a single pass: **one attempt per alternate**, no doubled attempts.
   (I read "single shot" as one pass. If you meant one attempt total, see §20.)
   - **If it works:** our message is restored to the normal replacement, the
     original's embed is **suppressed again**, and the button is removed.
   - **If it fails:** the failure note is updated with the time it was
     retried, and the button stays.
4. **Who can press Retry:** the person who posted the original link, or anyone with
   Manage Messages. Others get an ephemeral "not yours" reply.

> Looks good, anyone should be able to retry though.

State needed for this (original message ID, link, alternates tried) lives in
`replacement_messages`. That way Retry still works after a restart.

**Going forward, our message is sent as a reply** to the original, with
`allowed_mentions` set so the author isn't pinged. That makes the link between
the two messages visible in Discord and recoverable later without guesswork
(decision in §20).

> No, I'd rather it just be sent as a normal message. The bot replys fast enough to pretty much always have it's replacement link be the next message. We didnt like the look of the msg when its a reply. The channels its used in generally do not have messages sent that quickly, so its not a concern. See me note above on how to link back to the original message in the recalculate section.

### 9.6 Reaction statistics, backfill and emoji aliases

**Live tracking** is unchanged: kept forever. The schema is refined so that
backfill and live events can coexist without double counting:

```sql
reactions(message_id, user_id, emoji_key, first_seen_at NULL, removed_at NULL,
          source)            -- source: live | backfill
reaction_log(message_id, user_id, emoji_key, action, at)   -- live add/remove, append-only
```

`reactions` holds each message's current reactions and is what stats queries
read. `reaction_log` keeps the full live history, as v2 wanted. `emoji_key`
is `u:<unicode>` or `c:<custom emoji id>`.

**One-shot recompute: `/linkstats recompute since:<date> [until:<date>] [channel]`**
- **Who:** admins only.
- **What it does:** walks channel history backwards, 100 messages per request.
  It uses `co_messages_get` with `before` and stops once it passes `since`.
  - It keeps only messages **authored by the bot's own user**.
  - Each of those goes to a **tolerant legacy parser**, which recognises every
    format the bot has used: `🔗[_](url)`, spoiler-wrapped, webhook-era
    `<url> [_](url)`, and whatever older formats you can give me samples of
    (see §20). Messages it can't parse are counted and logged with their
    IDs. We never guess at them.
- **Who reacted:** `message.reactions` only gives counts, so the recompute calls
  `co_message_get_reactions(msg, emoji, ...)` for each emoji, paged 100 at a
  time.
  - Discord doesn't say *when* a reaction was added, so backfilled rows get
    `first_seen_at = NULL` and `source = 'backfill'`.
- **Crediting the original poster (old messages):** we look at up to about 10
  messages immediately before ours in the same channel, within about 60 s, for
  a non-bot message containing a URL with the **same path** once the domain
  is mapped back.
  - If there's exactly one match, that's the author.
  - Otherwise the author is left `NULL`. The reactions still count toward
    emoji stats, but not toward any poster.
  - The result also reports how many messages were attributed and how many weren't.
- **Re-running it is safe:** for each message it rebuilds the backfill rows from
  what Discord shows now, and never touches `live` rows.
- **Runs as a background job:**
  - A progress message is updated every few hundred messages.
  - `/linkstats recompute cancel` stops it.
  - A per-channel checkpoint means an interrupted run carries on where it
    stopped.
  - DPP handles Discord's rate limits. A few years of history may take a
    while, which is fine.

**Emoji aliases.** An `emoji_aliases(guild_id, emoji_key, canonical_key)` table
merges emojis that should count as one:
- the same emote from different servers;
- an emote that was deleted and re-added, which gets a new ID.

Stats queries count by `COALESCE(canonical_key, emoji_key)`. Aliases are
applied **when stats are read**, not when reactions are stored, so adding or
removing one takes effect immediately on all history.

Commands:
- `/linkstats alias add emoji:<e> same_as:<e>`
- `/linkstats alias remove`
- `/linkstats alias list`

Custom emoji are matched by ID; names are only for display.

**Management helper:** `/linkstats emojis` lists emoji keys that look like
duplicates, such as same name with different IDs, so you can spot alias
candidates quickly.

> Some clarifications about the reaction tracking:
> - The purpose of the reaction tracking on replaced links is to be able to have stats like "which user gets the most reactions of a single type, or overall?" and etc. This is tracking the reactions that a user recieves on the things they send (that the bot provides link replacement for)
> - But i also want to track the reactions a user gives to others to have stats like "the top 3 reactions give by user x are..." at etc. This is tracking reactions a user gives on things other people send (that the bot provides link replacement for). Note that a user's reactions on their own posts can still be recorded, but shouldnt be included in calculations of their statistics (it would be funny to track that seperately as "who likes their own posts the most" and etc lol).
> - for the purposes of backfilling, it should be identical to the current tracking as all of the information needed is still present, so long as we can correctly identify a link replacement msg and its original message that had the link the bot replaced (see next item for details on that). The user that posted the original msg, and the reactions on the link replacement with the user who added them and relevant timestamps should all still exist to be backfilled into the database just like they were sent as new messages. I see no need to make a distinction between backfilled entries or not. 
> - For identifying link replacement messages, the bot has normally been fast enough so that in the majority of cases, the original message and the bot's replacement message are sequential. There are occasional instances where someone sends a different message directly after an original post message, but before the bot's replacement message, which should be simple to detect and link back to the original poster message.  
> - there are already 1000s of existing bot messages for link replacements and in the original development of the feature, I tired a few styles, including using a message reply. We decided we didnt like the look of the link replacement being a reply when the original and replacement messages were pretty always going to be sequential anyway.

---

## 10. Midnight

Unchanged from v2. The scheduling decision ("should entry X fire now?") is a
pure function of (entry, now), which makes it easy to unit test across DST
changes (§17.5).

> Approved.

---

## 11. Simple trigger responses

Updated with your comments:
- **Match mode per trigger:** whole word or substring. No user-defined regex.
- **Cooldown is configurable per trigger,** per channel. **0 (no cooldown) is
  allowed**, but new triggers default to a non-zero value (proposed: 30 s).
- **`YesNoAnswers.txt` is dropped.**
- **Management:** both commands (`/trigger add | edit | remove | list`) and a
  panel reusing the §9.4 panel code.
- Seeded with the Java defaults (`420`, `4:20`, `69` → "nice") on first run.

These stay separate from LLM "advanced triggers" (§14.3): different tables,
different panels.

> Approved.

---

## 12. DECtalk

### 12.1 Build

Our own CMake in `cmake/dectalk.cmake`, built as a **DLL** (decided), plus the
dictionary compiler and dictionary build step, as in v2.

### 12.2 Startup

```cpp
TextToSpeechStartupExFonix(&handle, WAVE_MAPPER, DO_NOT_USE_AUDIO_DEVICE,
                           &on_dectalk_message, /*instance*/ 0,
                           absolute_path_to("dtalk_us.dic").c_str());
```

The instance parameter is **not** a pointer (§2.2). With one engine, the
callback reaches it through a static. If we ever run several engines, it
becomes a small integer id looked up in a table.

> Sounds good.

### 12.3 Synthesis: streaming first

1. `TextToSpeechOpenInMemory(handle, WAVE_FORMAT_1M16)` once at startup. It
   stays open.
2. Keep a ring of about 4 buffers of about 0.25 s each (11025 Hz mono 16-bit
   ≈ 5.5 KB each) queued with `AddBuffer`. Each is also pushed onto our
   `std::deque` (§2.2).
3. `TextToSpeechSpeak(handle, text, TTS_FORCE)`.
4. In the callback, on the buffer message:
   - take the front buffer from the deque;
   - hand its PCM to the worker thread (don't do heavy work on DECtalk's thread);
   - requeue it with `AddBuffer`.
5. The worker resamples each chunk to 48 kHz stereo and feeds the mixer, so
   audio starts after the first buffer (about 0.25 s of speech).
6. **End of utterance:** after `Speak`, the worker calls `TextToSpeechSync`,
   then `ReturnBuffer` to flush the last partial buffer.

`/chat` voice messages run the same path but collect the chunks instead of
streaming them (they need the complete file).

*Spike checks:*
- whether `AddBuffer` may be called from inside the callback, or must be
  called from our worker (default: our worker);
- buffer size vs start-up latency;
- that the FIFO assumption holds under `TextToSpeechReset`.

> Looks good.

### 12.4 Engine threading

Unchanged: one worker thread owns the handle, and requests come through a
queue. Since everything is serialised through the queue, one request's
engine state never overlaps with another's.

### 12.5 Sanitizer and per-request reset

Every request passes through `dectalk_sanitizer::clean(text, caller)`, where
`caller` is `user`, `admin` or `llm`:

| | `user` | `admin` | `llm` |
|---|---|---|---|
| `[:play]`, `[:log]` | stripped silently | **kept** | stripped |
| `[:debug]`, `[:loadv]`, `[:setv]` | stripped (proposed) | kept | stripped |
| `[:dv save]` | stripped | stripped | stripped |
| everything else | kept | kept | kept |

- It's a **denylist**, as you chose.
- "Stripped silently" means the command is removed and the rest is spoken, with
  no error. That's what you asked for.
- **Parsing follows DECtalk's own rules.** Commands are `[:name args]`,
  case-insensitive, and accepted as soon as the prefix typed so far is unique
  (`cm_cmd_match_comm`). Several can be
  chained with `:` inside one bracket. So `[:PLA "x"]` and
  `[:rate 200 :play "x"]` are caught too. This is one of the most heavily
  tested pieces (§17.5), including fuzzing.
- **Reset preamble:** every request starts from a known state. It's either
  `TextToSpeechReset(handle, FALSE)` or an explicit preamble that selects the
  requested voice (built-in or custom, §12.6) and resets rate, mode and
  punctuation. The spike picks whichever one also clears `[:dv]` edits.
- **Length limit:** input text is capped (proposed: 1000 characters for
  `/speak`, configurable per server).

  > Looks good.

### 12.6 `/speak`, custom voices and the voice lab

**`/speak text:<...> [voice] [rate] [volume]`**
- `voice` autocompletes the 11 built-in voices plus this server's saved
  custom voices.
- Volume uses `TextToSpeechSetVolume`.

**Custom voices:** stored per server in
`tts_voices(guild_id, name, base_voice, params, created_by, updated_at)`.
- `params` is an ordered list of `[:dv]` pairs.
- At speak time this becomes the preamble `[:n<base>][:dv ap 122 pr 140 hs 95 ...]`.
- Values are clamped to each parameter's documented range. The ranges get
  collected from the DECtalk docs during Phase 4.

**Voice lab: `/voice lab [start_from]`** opens an ephemeral panel for quick
tuning:
- **Display:** the base voice and current parameters, grouped as in §2.5.
- **▶ Test** button: speaks the test phrase in the bot's current voice
  channel. The panel stays open, so the loop is edit → test → edit.
- **Edit buttons** (Pitch, Quality, Formants, Gains, Test phrase): each opens a
  modal with ≤5 short text inputs (modals allow 5 components). The ~35
  parameters are grouped so the common ones sit on the first two modals.
- **Raw** button: a modal with one paragraph input, to paste or copy a whole
  `[:dv ...]` string. Handy for sharing voices.
- **Save as…**: a modal asking for a name. Saves to `tts_voices`.
- **Reset:** back to the base voice.
- The draft is kept in memory per user, expiring after about 30 minutes, so
  closing the panel by accident doesn't lose it.

Who can create and delete custom voices is a §20 decision (proposed: anyone
creates; the creator or an admin deletes).

> Looks good. Approved.

### 12.7 `/tts stop` and the duration cap

- **`/tts stop`** (admins, plus whoever started the current utterance):
  1. flushes the engine (`TextToSpeechReset(handle, TRUE)`);
  2. clears the TTS queue;
  3. drops mixer audio that's already queued and calls the voice client's
     `stop_audio`.
  - Music, if any, resumes as usual.
- **`/tts skip`:** stops only the current utterance; the rest of the queue plays.
- **Duration cap:** each utterance stops after N seconds of generated audio
  (proposed: 60 s, configurable per server). This catches `[:rate 75]` on long
  text, long `[:pause]`/`[:tone]`, and any similar trick without needing to
  know about it in advance.

> Approved.

### 12.8 `/chat` voice message

Unchanged from v2 (WAV + waveform + duration, sent through the raw-API
helper), except that it now uses the §12.3 path in collect mode.

---

## 13. Voice mixer and voice sessions

### 13.1 Mixer

Unchanged: one per guild, music pauses during TTS and resumes afterwards.
Ducking or overlay can be configured later.

### 13.2 Voice sessions (new, replaces v2's voice-reply rule)

You don't use voice-channel text chat, so v2's rule (reply by voice when the
message came from the same voice channel) wouldn't have fired. v3 uses an
explicit session instead:

- **`/voice start`**:
  - The bot joins **the voice channel you're in** (you must be in one). If
    it's already in another voice channel in that server, it moves.
  - It records a session: `{guild, voice_channel, text_channel = where you ran
    it, started_by}`.
- **While the session is active:**
  - LLM replies to messages in that text channel are **spoken in the voice
    channel** (§14.2).
  - `/speak` from anywhere in the server goes there as well.
- **The session ends when:**
  - someone runs `/voice stop` or `/leave`;
  - the bot is disconnected or moved out;
  - **no humans are left in the voice channel**, after a grace period
    (proposed: 30 s, so a quick rejoin doesn't kill it). When that happens the
    bot also leaves.
  - These are detected with `on_voice_state_update`.
- The auto-leave also applies after a plain `/join`, not just in sessions.
  Otherwise a bot sitting alone in a voice channel stays there forever.

Spoken text and text replies: see §20 (proposed: both, so there's a written
record and people outside VC can follow).

> Lookgs good. Approved.

---

## 14. LLM integration

### 14.1 Providers and models

Approved. Haiku 4.5 (`claude-haiku-4-5`) is the default.

### 14.2 Reply mode

- **Text by default.**
- **Spoken when a voice session is active** in that server and the message is in
  the session's text channel (§13.2).
- Voice replies use the DECtalk-aware prompt (§14.7) and the `llm` sanitizer
  column (§12.5).

### 14.3 When the bot responds: addressed, and advanced triggers

**Addressed** (unchanged): @mention, a reply to one of the bot's messages, or a
message starting with its name.

**Advanced triggers (new, separate from simple triggers):**

```sql
llm_triggers(id, guild_id, pattern, match_mode, context_prompt,
             probability, cooldown_s, enabled, created_by)
```

- `pattern` and `match_mode` work like simple triggers.
- **`context_prompt`** is a short, user-written instruction about *what* to
  say, for example "Someone mentioned pineapple pizza. Defend it with
  unreasonable passion."
- A **shared "trigger style" document** (§14.6), edited by admins, sets *how* every
  advanced-trigger reply is written: length limit, style, what to avoid. So each
  trigger's prompt can stay short.
- The request is built as fixed safety rules + system instructions +
  personality + trigger style + this trigger's `context_prompt` + a **short**
  window of recent messages (proposed: 5).
- `probability` (0–1) and a per-channel `cooldown_s` limit how often it fires.
- **Management:** `/llm trigger add | edit | remove | list` and a panel, both
  admin-only. A modal suits this well, since the context prompt is a paragraph.
- Blacklists (§14.8) are checked first. The spend cap applies.
- When both a simple and an advanced trigger match the same message: undecided
  until testing, like the other consume-or-not questions in §5.6.

> Looks good.

### 14.4 Bot-to-bot

Approved as in v2; you'll tune the limits after implementation.

### 14.5 Memory and runtime-configurable context

**Short-term context: now configurable at runtime by admins,** without a
restart. You changed your mind on this from v2.
- The settings live in `guild_settings`, and `config.json` only holds the
  defaults. They're read on every request, so a change applies to the next
  message.
- Settings:
  - context message count
  - context token budget
  - max output tokens
  - per-user and per-channel rate limits
  - advanced-trigger context size
- **UI:** `/llm settings` shows the current values in an admin-only panel with
  an **Edit** button that opens a modal, which fits 5 fields at a time. Values
  are validated with clear min/max limits, and out-of-range input is rejected
  with the allowed range shown.

**Long-term memory:** approved as designed. The model manages it with the
`remember` / `recall` / `forget` tools, backed by SQLite FTS5.
- The tool loop is built as a **general tool framework**: a registry of tools
  with a name, JSON schema and handler. That's so the future features you
  mentioned can add tools without touching the loop.
- The loop has a maximum number of rounds (proposed: 4) so a confused model
  can't run up costs.

> Approved.

### 14.6 Personality and system instructions as living documents (new)

```sql
llm_documents(guild_id, kind, version, content, edited_by, edited_at, note)
-- kind: personality | system | trigger_style
```

- **Every edit is a new version.** The newest one is used. Nothing is
  overwritten, so a bad edit is one command away from being undone.
- **Commands:** `/llm personality view | edit | history | diff | revert <version>`,
  and the same for `system` and `trigger_style`.
- **Editing:** a modal opens pre-filled with the current text.
  - A modal text input holds up to 4000 characters, and a modal can have up to
    5 inputs. So a document can be split into up to 5 named sections, 20,000
    characters in total.
  - For something longer, `edit` also accepts a `.txt`/`.md` attachment.
- **Who can edit what:**
  - **`system`** and **`trigger_style`**: admins only.
  - **`personality`**: a configurable role (who gets it by default is in §20).
- **Prompt order and priority:**
  1. fixed rules in code that nobody can edit (always sanitize, never claim to
     be human, obey blacklists, ...)
  2. `system` (admin)
  3. `personality` (users)
  4. relevant memories
  5. conversation
  - The personality section is explicitly labelled as style guidance that
    can't override the sections above it. That limits what an edited
    personality can do, including someone trying to use it to jailbreak the
    bot.
- **Size budget:** each document's token count is estimated on save, and saving
  warns when it's large. A long personality is sent on every request, and
  that directly raises the per-message cost. Prompt caching (Anthropic
  `cache_control` on the stable prefix: rules + system + personality) cuts
  repeated-input cost a lot and should be on from the start.

> Looks good. Approved.

### 14.7 DECtalk-aware prompting

Approved as in v2.

### 14.8 Guards

Approved as in v2. The spend cap is **$20 per month**. v3 also proposes a daily
cap of about $2, so one bad day can't use up the whole month (§20).

> Likely will fine tune after implementation.

### 14.9 HTTP

`co_request` directly, with the cluster's 60 s timeout (§2.1). Typing indicator
while waiting. No streaming responses.

---

## 15–16. Music, emote stats (later)

Unchanged from v2.

---

## 17. Testing strategy (new)

You asked for testing to be worked out before any porting starts, and said
you're open to suggestions. This section recommends a set of tools, explains
how the code will be structured so it can be tested, and lists what gets
tested in each phase.

### 17.1 Framework recommendation: Catch2 v3

The three mainstream C++ unit-testing frameworks are all on Conan and all
integrate with CMake/CTest and VS Code's Testing sidebar:

| | **Catch2 v3** | GoogleTest | doctest |
|---|---|---|---|
| Assertion style | `REQUIRE(a == b)`, and failures print both sides | `EXPECT_EQ(a, b)` family of macros | like Catch2 |
| Shared setup | `SECTION`s inside one test | fixture classes | `SUBCASE` |
| Table tests | `GENERATE(...)` | parameterised fixtures (more boilerplate) | limited |
| Mocking | none built in (pair with trompeloeil if needed) | **gMock included** | none |
| Micro-benchmarks | **built in** (`BENCHMARK`) | no | no |
| Compile speed | medium | medium | fastest |
| Recognition | very common | the most common in industry | less common |

**Recommendation: Catch2 v3.**
- It's the easiest to read and write when you're new to C++ testing.
- Its built-in benchmarking fits this project: the URL scanner runs on every
  message, and the resampler runs in real time.
- We don't need gMock, because the design below uses a few small hand-written
  fakes rather than mocking frameworks. That's usually clearer anyway.
- GoogleTest is the reasonable alternative, if you'd rather learn the most
  widespread framework for your portfolio. Either works; §20 asks which.

What a test looks like:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "core/util/url_scan.hpp"

TEST_CASE("replaces every link in a message, not just the last", "[url]") {
    const url_rules rules = {{"x.com", {"fixupx.com"}}};
    const auto out = rewrite("a https://x.com/1 b https://x.com/2", rules);

    REQUIRE(out.links.size() == 2);
    CHECK(out.links[0].replaced == "https://fixupx.com/1");
    CHECK(out.links[1].replaced == "https://fixupx.com/2");
}

TEST_CASE("links inside a spoiler stay spoilered", "[url]") {
    auto [input, spoilered] = GENERATE(table<std::string, bool>({
        {"||https://x.com/1||",         true },
        {"https://x.com/1",             false},
        {"|| a || https://x.com/1",     false},
        {"|| a || || https://x.com/1||", true },
    }));
    CHECK(rewrite(input, rules()).links[0].spoilered == spoilered);
}
```

> Looks good. Lets go with Catch2.

### 17.2 Build layout: a core library that both the bot and the tests use

```cmake
add_library(latibot_core STATIC ${CORE_SOURCES})     # all of src/core
target_link_libraries(latibot_core PUBLIC dpp SQLite::SQLite3 ctre::ctre)

add_executable(LatiBot src/main.cpp)                 # a few dozen lines
target_link_libraries(LatiBot PRIVATE latibot_core)

option(LATIBOT_BUILD_TESTS "Build unit tests" ON)
if(LATIBOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)                          # latibot_tests links latibot_core + Catch2
endif()
```

- **Running tests:** `ctest --test-dir build -C Debug` runs everything.
  `catch_discover_tests` registers each `TEST_CASE` separately, so
  VS Code's **Testing** sidebar (via the CMake Tools extension you already
  use) lists and runs them one by one, with a click to jump to a failure.
- **Tags** (`[url]`, `[db]`, `[golden]`, `[live]`) let you run a subset:
  `latibot_tests.exe "[url]"`.
- **Live tests** (`[live]`, §17.5) are excluded by default and only run when a
  test token is set.

> Looks good. I like the static core approach that can be used by both test and deploy.

### 17.3 Designing for testability: ports and fakes

The hard part of testing a Discord bot is that most code reacts to Discord events
and calls Discord. The plan keeps that at the edges:

**1. Functional core, thin shell.** Features are written as functions that take
plain data and return **decisions** as plain data. For example,
`url_replacer::on_message(msg, rules, opt_outs) → std::optional<reply_plan>`,
or `midnight::due(entry, now) → bool`. The DPP event handler (the shell) just
converts the DPP event to input, calls the function, and carries out the
result. Most logic is tested with no Discord, no network and no threads.

**2. Ports for the outside world.** Where a feature needs I/O in the middle of
its logic, it goes through a small interface in `src/core/ports/`:

| Port | Real implementation | Test fake |
|---|---|---|
| `clock` | `system_clock` | `fake_clock`, set and advanced by the test (cooldowns, midnight, spend-cap day rollover, voice-session grace period) |
| `discord_gateway` | wraps the handful of `dpp::cluster` calls features use (send, edit, suppress embeds, get messages, get reactions, ...) | `fake_discord`: records calls; returns scripted results and scripted pages of message history |
| `http_client` | `co_request` | `fake_http`: returns recorded fixture responses (LLM) |
| `tts_engine` | DECtalk worker | `fake_tts`: returns a tone of the right length (mixer tests without DECtalk) |

These are ordinary abstract classes with a handful of virtual functions each.
There's no mocking framework: a fake is a small class you can read in one
screen.

> Looks good. Id like to refer to them as 'mocks' instead of 'fakes' still to be consistent even though we are not using a proper mocking library.

**3. Coroutines in tests.** DPP's `dpp::task` has `sync_wait_for(timeout)`, so a
test can run a coroutine to completion:
`REQUIRE(flow.run(fake).sync_wait_for(2s))`. The timeout means a broken
coroutine fails the test instead of hanging it. Fakes return results that are
already complete, so no event loop is needed.

### 17.4 Database tests

- Each test opens a fresh **`":memory:"` database** and runs the real migrations.
  That's fast (milliseconds), isolated, and uses the actual SQL.
- **Migration tests:**
  - migrating from 0 to the latest version works;
  - every migration is idempotent where it claims to be;
  - once there's a v1 schema in use, a stored v1 fixture database migrates
    forward without losing data.
- **Importer tests** use synthetic `nicknames.json` / `UrlReplacements.txt`
  fixtures in `tests/fixtures/`. They cover empty, malformed and edge-case
  timestamps. **Never the real files** (public repo).
- **Backup test:** make a backup while a write transaction is open, then check
  the copy opens and passes `PRAGMA integrity_check`.

### 17.5 What gets tested, per feature

| Feature | Unit tests (every build) | Other |
|---|---|---|
| URL scanner/rewriter | multiple links, spoilers (odd/even `\|\|`), `/en` with query/fragment/trailing slash, unknown domains, opt-outs, attempt schedule (alt1, alt1, alt2, ...) | 100 KB pathological input under a time limit; `BENCHMARK` |
| Embed flow | coroutine test with `fake_discord` + `fake_clock`: success, retry, all-fail → un-suppress + Retry button, Retry success → re-suppress | live checklist |
| Legacy parser (backfill) | each known historical format; unknown → reported, never guessed; author heuristic: 1 match, 0, 2+ | fuzz |
| Emoji aliases | counts merge, remove alias splits them again | db |
| Nicknames | pending-expectation matching, bot-as-actor never overwrites, 10 s fallback (fake clock), unmatched → unknown | db |
| CT import | CST, CDT, ambiguous 1:30 (fall back), nonexistent 2:30 (spring forward), pre-2007 dates | db |
| Midnight | `due()` across DST changes, suspend/resume gap, no double fire after restart | |
| Triggers | whole word vs substring, cooldown 0 vs N (fake clock), per-channel isolation | |
| Paginator | `custom_id` encode/decode round-trip, 100-char limit, bounds | |
| DECtalk sanitizer | the whole §12.5 table per caller type; abbreviations (`[:pla]`), chained commands, mixed case, unterminated brackets, nested/odd brackets | **fuzz** |
| DECtalk engine | streaming buffers arrive in order (§2.2), reset clears `[:rate]`/`[:dv]` between requests, duration cap, stop | **golden audio** |
| Resampler / WAV / waveform | known sine in → expected length and frequency out; WAV header bytes; waveform of silence/tone | `BENCHMARK` |
| LLM request builder | model-aware fields (no `temperature` for Sonnet 5), prompt order (§14.6), cache_control placement, token budget trimming | recorded fixtures |
| LLM response/tool loop | text, tool_use → tool_result round, max rounds, 429/529/overloaded, malformed JSON | recorded fixtures |
| Spend cap | cost math per model, daily/monthly rollover (fake clock), auto-disable | |
| Bot-to-bot | turn counter, human resets, delay, daily cap | |
| Voice session | grace period, auto-leave when empty, move vs join | `fake_discord` |

**Golden audio tests** check DECtalk output. The same text and voice should
produce the same samples every time. The spike confirms DECtalk is
deterministic.
- Tests store a hash plus the sample count for a handful of phrases, and fail
  if the output changes.
- A changed hash after a deliberate change is updated with one command. The
  test also writes the `.wav` so you can listen to the difference.

**Fuzz tests (optional, Phase 4+).** MSVC supports libFuzzer
(`/fsanitize=fuzzer`). It generates millions of random inputs and reports
crashes. It's the right tool for the three parsers that see untrusted text:
- the URL scanner;
- the DECtalk sanitizer;
- the legacy message parser.

It needs the AddressSanitizer component (§17.6), and it runs on demand, not
on every build.

> Syre lets go ahead and add the fuzz testing and AddressSanitizer components. They will be good practice.

**Live tests (opt-in, `[live]`)** cover the things only real Discord can answer.
They need:
- a **separate test server**;
- a **separate test bot application**, with its token in `LATIBOT_TEST_TOKEN`.

They cover:
- the modal-submit spike (§9.4);
- audit-log event timing;
- whether embeds actually appear for each mirror;
- voice.

The rest stays a short **manual checklist** in `tests/live/CHECKLIST.md`,
run before a release, mostly for UI such as panels and modals.

> Sure, but this is less important that the other tests.

### 17.6 Tooling around the tests

| Tool | Purpose | Status |
|---|---|---|
| **CTest + CMake Tools** | run tests from the terminal or VS Code's Testing sidebar | built in |
| **AddressSanitizer** (`/fsanitize=address`) | catches use-after-free, buffer overflows and similar bugs at runtime; very relevant with 1990s C (DECtalk) and raw buffers | needs one extra Build Tools component: `Microsoft.VisualStudio.Component.VC.ASAN`. Run as a separate `asan` preset. It doesn't work together with `/RTC1` (on in Debug by default), so the preset turns that off |
| **clang-tidy** | static analysis: bug patterns and modern-C++ suggestions | ✅ already installed with Build Tools. Needs `compile_commands.json`, which only the **Ninja** generator produces, so it gets a separate `ninja-tidy` preset. It checks our code only (not DPP or DECtalk) |
| **clang-format** | consistent formatting; VS Code formats on save | ✅ already installed. Add a `.clang-format` file |
| **OpenCppCoverage** | line coverage of the test run, HTML report | `winget install OpenCppCoverage.OpenCppCoverage`. No hard coverage target; used to find untested logic in `core/` |
| **Warnings as errors** | `/W4 /WX /permissive-` on **our** targets only | turn on once Phase 0 compiles cleanly |

### 17.7 Continuous integration (GitHub Actions)

Since the repo will be public, GitHub Actions on `windows-latest` is free.

- **Runs:** on every push and PR to `main`.
- **Steps:**
  1. checkout with submodules;
  2. restore caches (Conan packages, keyed on `conanfile.py`, and DPP's
     build, keyed on the submodule commit);
  3. `conan install` (Debug);
  4. configure, build, and run `ctest`, excluding `[live]`;
  5. upload the test report.
- **Secret scan:** gitleaks runs on every push.
- **Timing:** the first build is slow (DPP takes 10+ minutes); cached builds
  are much faster.
- **Separate jobs:** clang-tidy and ASan run on PRs only, or nightly, to keep
  pushes fast.
- **Portfolio:** a status badge in the README.

It's also important to know what CI **can't** do: it has no Discord token and
no voice. That's intentional, and it's why §17.3 keeps logic away from Discord.

> Approved. I've added the origin remote for the current repo. Nothing has been commit yet.

### 17.8 Working rules

- **Each feature's tests are part of its definition of done.** A phase isn't
  finished until its §17.5 rows are green in CI.
- **Bug fixes start with a failing test** that reproduces the bug. That's
  especially relevant for the four URL bugs from the Java version: each gets
  a test first.
- **Only synthetic data** in `tests/` (public repo).

---

## 18. Dependencies

| Dependency | Source | Status |
|---|---|---|
| DPP 10.1.6 | submodule | ✅ done |
| OpenSSL 3.6.4, zlib 1.3.2, opus 1.6.1 | Conan | ✅ done |
| SQLite 3.53.4 | Conan | ✅ done. **To add:** `default_options = {"sqlite3/*:enable_fts5": True}` (approved in §14.5) |
| CTRE 3.11.0 | Conan, header-only | **To add** (decided) |
| Catch2 3.16.0 | Conan, `test_requires` | **To add** once §20 confirms the framework |
| DECtalk (develop) | submodule, our CMake | vendored; build in Phase 4 |
| clang-tidy / clang-format | Build Tools (LLVM component) | ✅ installed |
| ASan | Build Tools component `VC.ASAN` | to install (one `winget`/installer modify command, added to README) |
| OpenCppCoverage | winget | optional |
| Ninja | winget (`Ninja-build.Ninja`) or the copy bundled with Build Tools' CMake component | for the clang-tidy preset |

All of these fit the existing setup: one more Conan requirement, one test
requirement, one option, and tools added to the README.

> Looks good.

---

## 19. Build order

**Phase 0: Foundations and test harness**
- ✅ Git, submodules, C++20, DPP from source, Conan dependencies, README
- **0a. Test harness first:**
  - Split into `latibot_core`, `LatiBot` and `latibot_tests`.
  - Add Catch2 and CTRE, enable FTS5.
  - Add `CMakePresets.json` (msvc / ninja-tidy / asan), `.clang-format`, and
    one trivial passing test.
  - Add the CI workflow, and check it goes green.
- **0b.** SQLite wrapper, migrations and backups, *with the db tests*.
- **0c.** Bootstrap config and per-guild settings; the `clock`,
  `discord_gateway` and `http_client` ports with their fakes.
- **0d.** Command registry and raw-API helper.

**Phase 1: Framework proof**
- Basic commands and goodbye phrase; permission preflight.
- Message pipeline and simple triggers, plus tests.

**Phase 2: Data features**
- Nickname import (CT/DST tests), tracking and attribution (including
  `/nickname`), reconciliation, `/nicknames` and the paginator.
- Midnight.

**Phase 3: URL replacement**
- CTRE scanner and rewriter, with the four Java bugs each reproduced as a test
  first.
- Embed flow with Retry; reply-to-original.
- Reaction tracking and `/linkstats`; emoji aliases; recompute/backfill.
- Commands, then the panel/modal UI.

**Phase 4: Voice**
- DECtalk CMake and dictionary.
- Streaming engine spike: check FIFO order, reset behaviour, determinism, and
  latency.
- Sanitizer (with fuzzing), `/tts stop`, the duration cap.
- Resampler and mixer, `/speak`, voice sessions.
- Custom voices and the voice lab.
- `/chat` voice message.

**Phase 5: LLM**
- Provider, tool framework, text replies, guards, spend cap, prompt caching.
- Runtime settings panel; personality/system/trigger-style documents.
- Short-term and long-term memory.
- Advanced triggers.
- Voice-session replies with DECtalk prompting.
- Bot-to-bot.

**Later:** music, emote stats, appearance tracking.

---

## 20. Open decisions

**Repository and testing**

1. **Where the plans live.** `java-reference/` is now ignored, so these plan
   files are untracked. Move them to `docs/plans/` (tracked)? I'd move them:
   they show how the port was planned, which is good material for a
   portfolio.

  > I've moved them to "docs/porting/".

2. **Test framework:** Catch2 v3 (recommended) or GoogleTest?

  > Catch2 v3.

3. **Tooling scope for Phase 0a:** set up everything in §17.6–17.7 now (CI,
   clang-tidy preset, ASan preset, clang-format), or just Catch2 + CTest now and
   the rest when first needed? I'd do Catch2, CTest, clang-format and CI now
   (cheap, and useful from the first commit), and add ASan and clang-tidy in
   Phase 4 when the C code arrives.

  > Lets go ahead and get everything setup now. 

4. **A test Discord server + test bot application** for `[live]` tests: are you
   OK creating one? It keeps experiments away from your real server.

  > I have a test bot created already as the real bot account is in use by the java version already, and will provide a test server when needed.


**URL replacement**
5. **Send replacements as replies** to the original (no ping)? Recommended: it
   makes the pairing visible and recoverable.

  > No, keep it as a normal message. They bot is fast enough to have the message be sequential a majority of the time. We did like the look when the bot used replys.

6. **Retry semantics:** is "single shot" one pass over all alternates (my
   reading), or exactly one attempt?

  > Correct.

7. **Old message formats:** can you share a few examples of each older format
   the bot used? Copy the raw text; an ID is enough if you're not sure. The
   legacy parser gets a test per format.

  > - The first iteration used to copy the entire message contents exactly and used a reply.
  > - Next iteration we tested out the webhook mode for a bit, which would delete the origial message and send a message that copies the original sender's username/nickname and icon (at that time) and sends a message of the form `\<<originl link>\> [.](<replaced link>)` (note the "\>" denote literal "<" and ">" character). I think this one can just be igrnored due to it deleting the original messages. There are not many of them like this that even have reactions on them.
  > - Then is goes to normal message (not a reply) copying exact msg content but replacing the link.
  > - Then we used markdown to hide the link with `[.](<replacement link>)`.
  > - Then format changed to `:link: [.](<replacement link>)`.
  > - Then finally we have settled on `:link: [_](<replacement link>)`.

8. **Backfill author heuristic** (§9.6): is "same path, within about 10
   messages and 60 s, exactly one match" acceptable, with the rest left
   unattributed?
  > yes, probably. See my other comment above (in 9.6) detailing the backfill logic. 

**Nicknames**
9. **Audit-log reason for `/nickname`:** also send `X-Audit-Log-Reason` through the
   raw-API helper, so Discord's own audit log shows who used the command? Cheap;
   recommended.

   > Sure, that works.

**DECtalk**
10. **Who counts as "admin" for `[:play]`/`[:log]`:** Administrator in any server
    (what you said), or Administrator **and** the server is on a
    `trusted_guilds` list in `config.json`? I'd add the list, for the reason
    in §2.8. It costs nothing while the bot is only on your server.
  
  > lets also include the trusted_guilds as well.

11. **`[:debug]`, `[:loadv]`, `[:setv]`:** admin-only as well (proposed)?

  > Yes.

12. **Custom voice permissions:** anyone creates, creator or admin deletes?

  > Yes.

13. **Defaults:** 60 s max utterance, 1000-character `/speak` limit, 30 s
    voice-session grace period. OK as starting values?

  > Yes, will fine tune after implementation.

**LLM**
14. **Personality editors:** which role can edit `personality` by default?
    Everyone, a specific role you pick, or Manage Server? And should
    personality edits be announced in a log channel, so changes are visible?

  > Lets add a config for the role that can edit personality. Default to @everone. No need to notify on personality change beside internal logs.

15. **Voice sessions:** speak **and** post the text, or speak only?

  > Both speak and text. Text can inclued inclinded commands for DECTalk to see what it was trying to do.

16. **Daily spend cap:** add about $2/day alongside the $20/month?

  > fine for now. will tweak after implementation.

17. **When a simple and an advanced trigger both match:** leave it until testing
    (as with §5.6), or pick a default now? I'd leave it.

    > lets go with picking the simple trigger over the advanced. Will tweak after implementation.

---

> Heres an idea that i want to document, but put on the back burner:
> It would be cool to transition into using a tool like "cobalt" (https://github.com/imputnet/cobalt) to download the content from the (supported) links and create our own hosted embed based on the downloaded content. This would be an ideal as all of the content is now hosted by me and isnt lost should the original user delete their post or the service block these embed fixer services in the future. Not somthing i want to implement now but i also dont want to forget about it. Maybe make a seperate doc noting this idea down and doing a bit of premliminary feasibility analysis of what would be needed to do this and etc.