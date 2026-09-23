# LatiBot Stage 2 — Detailed C++ Implementation Plan

Follow-up to [Porting_Initial_Analysis.md](Porting_Initial_Analysis.md), incorporating the keep/skip/later
marks and inline comments. This is the technical "how", not the "what" — each
feature section covers data structures, the specific DPP APIs involved,
algorithms, known bugs being fixed, and open questions.

Everything below was checked against the **actually installed** versions
(DPP 10.0.35 headers in the Conan cache, the DECtalk `ttsapi.h` in the reference
project), not from memory. Where something still needs verifying at
implementation time, it says so explicitly.

---

## 1. Research findings that change the plan

These came out of digging into the DECtalk API and the installed DPP headers
while writing this. Several directly answer open questions you raised.

### 1.1 DECtalk can absolutely avoid the WAV file ✅

Your §2/§3 comments both hedged on whether skipping the `.wav` intermediate was
feasible. It is — `ttsapi.h` exposes an in-memory output API that the old JNI
shim never touched:

```c
MMRESULT TextToSpeechOpenInMemory ( LPTTS_HANDLE_T, DWORD dwFormat );
MMRESULT TextToSpeechAddBuffer    ( LPTTS_HANDLE_T, LPTTS_BUFFER_T );
MMRESULT TextToSpeechReturnBuffer ( LPTTS_HANDLE_T, LPTTS_BUFFER_T* );
MMRESULT TextToSpeechCloseInMemory( LPTTS_HANDLE_T );
```

`TTS_BUFFER_T` hands back `lpData` + `dwBufferLength` (raw PCM samples), plus
phoneme and index-mark arrays as a bonus. So the pipeline becomes
**text → PCM in RAM → Discord**, with no filesystem involvement at all. That
removes the whole "retention and auto-delete of stale wav files" problem you
raised in §3 — there's nothing to clean up.

Two levels of ambition here, worth separating:

| Goal | Approach | Difficulty |
|---|---|---|
| **No temp files** (what you actually asked for) | `OpenInMemory` → `AddBuffer` ×N → `TextToSpeechSpeak` → `TextToSpeechSync` (blocks until done) → collect buffers | Low — no async machinery needed |
| **True low-latency streaming** (start playing before synthesis finishes) | Same, but consume buffers via the async completion notification as they fill | Medium — see caveat below |

Recommend doing the first now; it solves the stated problem. The second is a
later optimization that only matters if you notice a lag before speech starts.

**Caveat to verify at implementation time:** how the "buffer is full" notification
is delivered on Windows. The header documents two mechanisms — a Win32 window
message (`RegisterWindowMessage("DECtalkBufferMessage")`, requiring an `HWND`,
which is what the Win32 `TextToSpeechStartup(HWND, ...)` overload wants) and a
plain C callback via `TextToSpeechStartupEx(..., void (*cb)(LONG,LONG,DWORD,UINT), LONG)`.
The `TTS_MSG_BUFFER` constant is only `#define`d in the non-Windows branch of the
header, which suggests the callback path may be Unix-only in this vintage of the
API. The modern `dectalk/dectalk` repo may well have unified them. If the
callback path doesn't work on Windows, the fallback is a hidden message-only
window (`HWND_MESSAGE`) pumped on a dedicated thread — a standard Win32 pattern,
just more code. **None of this blocks the "no temp files" goal**, which only
needs the synchronous `Sync`-then-collect path.

> That all sounds good. I like the recommendation, lets start with the simple buffer approach and ill evaluate if the delay time before the audio starts is to long. Then we can look into the true streaming approach.
> But, i think you should clone the dectalk repo into this workspace regardless and analyze the real headers from there as my header from the reference project may not match and be old version.

### 1.2 ffmpeg is not needed for TTS at all

Your §2 comment assumed ffmpeg would do TTS playback. That assumption came from
the file-based design — if DECtalk hands us raw PCM in memory, there's no
container to demux and no codec to decode. All that's left is a **sample-rate
conversion**, which is ~40 lines of code:

- DECtalk output: 11025 Hz, mono, 16-bit signed (`WAVE_FORMAT_1M16`, what the old
  code used; confirm the engine's actual rate via `TextToSpeechGetCaps()` →
  `dwSampleRate`).
- What DPP wants: **48000 Hz, stereo, 16-bit signed**, in frames of
  `dpp::send_audio_raw_max_length` = 11520 bytes (= 2880 frames × 2ch × 2 bytes
  = exactly 20 ms).

11025 → 48000 isn't an integer ratio (4.3537…), so it needs a fractional-position
resampler rather than simple sample duplication. Linear interpolation is
perfectly adequate for lo-fi robotic speech. Mono → stereo is just writing each
sample twice.

So: **ffmpeg becomes a music-only dependency** (§11), needed for demuxing/decoding
whatever yt-dlp produces. It's available via Conan (`ffmpeg/9.0.1` and several
8.x) if we want libav* rather than the CLI. Decide that when music gets built.

> Perfect, sounds good.

### 1.3 DPP has a built-in track-marker queue

Relevant to your "the track sequencing implementation is not very good, needs a
redesign" note. `discord_voice_client` already provides:

```cpp
insert_marker(metadata);        // mark a boundary in the outgoing audio buffer
skip_to_next_marker();          // jump to it (this is "skip track")
get_marker_metadata();          // what's queued
stop_audio(); pause_audio(bool); is_playing();
```

plus a `cluster::on_voice_track_marker` event that fires when playback crosses a
marker. That means a chunk of the hand-rolled `TrackManager`/`SongQueue`
bookkeeping (what's playing, advancing on track end) can be delegated to DPP
instead of reimplemented. Worth building the redesign around.

> Thats great, lets use that in our implementation.

### 1.4 The "separate TTS from music" requirement has a physical constraint

You asked (§2) to separate voice synthesis playback from music/track management.
Agreed on separating the *management*, but note: **there is exactly one
`discord_voice_client` per guild**. Both systems are feeding the same socket, so
they can't be fully independent — something has to arbitrate who holds the mic.

Proposed shape: a thin `voice_mixer` that owns the connection and accepts audio
from prioritized sources. TTS is high priority and preempts; music is low
priority and auto-pauses (or ducks) while TTS plays, then resumes. The music
queue and the TTS engine stay completely separate modules that don't know about
each other — they both just talk to the mixer. That gives you the separation you
want without two systems fighting over one socket.

> Yes, this is what i figured would be the case. Having the music pause, duck, or simply play over the tts should ideally be configurable, but lets keep it simple to start and just auto pause and resume the music when tts is going.

### 1.5 Coroutines are unavailable in the current setup

DPP's `co_await` support is gated behind `DPP_CORO`, which requires C++20. The
Conan package is built with `compiler.cppstd=17` and the ConanCenter recipe
exposes no option to enable coro, so **all async work must use callbacks**, not
`co_await`. This affects how the message-history walk (§12) and the embed-retry
logic (§8) get written.

Changing this would mean building DPP from source with a custom recipe — not
worth it now. Callback style is fine; it's what the Java version effectively did
with `.queue()` anyway.

> This was actually going to be a point i was going to bring up on my own. I want to use a more modern cpp standard that 17 for my own learning experience. Lets go ahead and take care of that by changing DPP from being handled by conan to being vendored into this workspace and built from source via our CMake. This gives us the flexibility needed to to move to cppstd 20 or potentially 23. 

### 1.6 Voice-message *sending* is a genuine gap in DPP

For §3. DPP models received voice messages (`attachment::duration_secs`,
`attachment::waveform`, the `m_is_voice_message` flag, `message::is_voice_message()`),
but `message::add_file(filename, content, mimetype)` has **no parameter for
`duration_secs`/`waveform`** — so there's no first-class way to *send* one. JDA's
`FileUpload.asVoiceMessage(...)`, which the Java version relied on, has no DPP
equivalent.

Workarounds, in order of preference:
1. Hand-build the request via `cluster::post_rest_multipart(...)`, which takes a
   `payload_json` string and a `std::vector<message_file_data>` — we control the
   JSON, so we can include `flags: 8192` and the `attachments[0]` object carrying
   `duration_secs` + `waveform`.
2. Fall back to a plain audio attachment (plays inline as a file, just not as a
   native voice message with the waveform UI).

Flagging this as the one place in the whole port where DPP is materially behind
JDA. Option 1 should work but needs a spike to confirm.

> Option 1 seems reasonable and is likely not to difficult given discords api documentation.

### 1.7 The old waveform-sampling code was producing garbage

While we're here — `ChatTestCmd`'s waveform sampler sums **raw signed bytes of
the WAV file** (header bytes included) and averages them into a `byte`. Since
16-bit PCM bytes are roughly symmetric around zero, every bucket averages to
approximately zero. The waveform Discord displayed was effectively noise.

The correct version: skip the RIFF header, read `int16` samples, take the peak
(or RMS) absolute amplitude per bucket across 256 buckets, normalize to 0–255,
base64 the result. Worth doing properly since we're rewriting it anyway.

> Approved.

### 1.8 Smaller finds

- **`TextToSpeechSetVolume(handle, VOLUME_MAIN, level)`** exists — that's the fix
  for the `make dectalk louder lol` TODO in `LatiBot.java`.
- **Speaker and rate control**: `TextToSpeechSetSpeaker` (PAUL, BETTY, HARRY,
  FRANK, DENNIS, KIT, URSULA, RITA, WENDY) and `TextToSpeechSetRate`. Cheap to
  expose as `/speak` options; the Java version never did.
- **`cluster::request(url, method, callback, postdata, mimetype, headers, protocol, request_timeout)`**
  — a general-purpose HTTP client is already available, including a settable
  timeout. This means the LLM integration (§10) needs **no new HTTP dependency**.
- **`cluster::start_timer(on_tick, seconds, on_stop)`** / `stop_timer` — use for
  the midnight scheduler instead of a hand-rolled thread.
- **`on_autocomplete` and `on_form_submit`/`interaction_modal_response` both
  exist** — relevant to fixing the clunky URL-replacement command UX (§8).
- **The DECtalk repo is not CMake.** `dectalk/dectalk` uses autotools on
  Linux/macOS and a **Visual Studio 2022 solution** on Windows; the `develop`
  branch ships prebuilt working binaries for Windows x86_64. See §13 for how to
  vendor it.

> all these points seem reasonable. approved.

---

## 2. Decisions locked in from your review

| § (Stage 1) | Feature | Verdict | Notes |
|---|---|---|---|
| 1 | Music playback | **Later** — stub now | yt-dlp + ffmpeg when built; queue gets redesigned, not ported |
| 2 | DECtalk TTS in voice | **Keep** | Link `DECtalk.lib` directly; vendor `dectalk/dectalk@develop`; stream, no wav |
| 3 | TTS voice message (`/chat`) | **Keep**, promote to real command | In-memory upload; DPP gap, see §1.6 |
| 4 | Nickname history | **Keep** + pagination fix | Profile/appearance tracking is a later extension |
| 5 | Owner-confirm nickname flow | **Skip** | But attribution still needed — see §7 |
| 6 | Emote stats | **Later** | Needs a redesign, not a port |
| 7 | Emote export | **Skip** | Dropped entirely |
| 8 | URL replacement | **Keep** — redesign | 4 known bugs + reaction stats + `/en`; webhook mode dropped |
| 9 | Reaction refresh | **Skip** | Dropped entirely |
| 10 | Midnight message | **Keep** + expand | Per-timezone config, management commands, random-fire bug fixed |
| 11 | Basic commands | **Keep all** | Plus "say goodbye latibot" admin phrase |
| 12 | Permission check | **Generalize** | Per-command declared permissions, checked across all guilds |
| — | LLM integration | **New build** | OpenAI *and* Anthropic; can speak via DECtalk |
| — | Trigger word responses | **New build** | Generalizes the hardcoded 420/69 → "nice" |

> These items look good. Only addition is for the LLM integration, it should also be able to respond via text chat (which should likely be the default mode, unless the bot is in a voice channel or etc).

---

## 3. Target project structure

```
src/
  main.cpp                     entry point, config load, cluster construction
  bot.hpp/.cpp                 owns cluster + subsystems; replaces static globals
  config.hpp/.cpp              JSON config + env overrides, hot-reloadable subset

  commands/
    registry.hpp/.cpp          command interface, dispatch, permission declaration
    basic.cpp                  ping, say, shutdown, status
    voice.cpp                  join, leave
    nickname.cpp               nickname, nicknames (+ pagination buttons)
    urlrepl.cpp                url replacement management + autocomplete
    midnight.cpp               midnight config management
    speak.cpp                  /speak (voice channel TTS)
    chat.cpp                   /chat (voice message TTS)
    ask.cpp                    LLM commands

  events/
    message_pipeline.hpp/.cpp  ONE on_message_create handler, ordered stages
    nickname_tracker.hpp/.cpp  on_guild_member_update + history store
    reaction_stats.hpp/.cpp    on_message_reaction_add/remove

  audio/
    voice_mixer.hpp/.cpp       per-guild connection arbiter (TTS vs music)
    resample.hpp/.cpp          11025 mono -> 48000 stereo
    dectalk.hpp/.cpp           RAII wrapper over the DECtalk C API
    music/                     stubs for now

  llm/
    provider.hpp               abstract interface
    anthropic.cpp  openai.cpp  two implementations

  util/
    json_store.hpp/.cpp        atomic load/save, snowflake-safe
    scheduler.hpp/.cpp         wall-clock-correct recurring tasks
    text.hpp/.cpp              spoiler/markdown handling, TTS sanitization

third_party/dectalk/           vendored, see §13
data/                          runtime state (gitignored)
```

> Looks good. Main addition is my comment about vendoring DPP into this workspace as well so it can be built from source via cmake. This is to facilitate the use of a newer cppstd like cpp 20 or 23. Also make note that this repo needs to be updated for cppstd 20 at minimum. 

---

## 4. Core infrastructure (build first — everything depends on it)

### 4.1 Configuration

Replaces `token.txt`, `openai_key.txt`, and every hardcoded snowflake
(guild `142409638556467200`, the midnight channel, the `smwOK`/`smwNO` emoji IDs).

- `config.json` for non-secrets (channels, timezones, trigger words, feature toggles).
- **Secrets via environment variables only** (`DISCORD_BOT_TOKEN`,
  `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`), never in a committed file. The Java
  version bundled `token.txt` into the jar resources; don't repeat that.
- Loud failure at startup if the token is missing, rather than a null deref.

> approved.

### 4.2 Thread safety — a real difference from Java

**This matters more in C++ than it did in Java.** DPP dispatches events from
multiple threads. The Java version shares plain `HashMap`s across gateway threads
and command handlers (`MessageListener.domains`, `NicknameListener.nicknamesHistory`,
`hashes`, `userBlacklist`) with no synchronization — in Java that's a
correctness hazard that usually gets away with it; in C++ a concurrent
`std::unordered_map` read during a rehash is **undefined behaviour and will
eventually crash**.

Every shared store gets a `std::shared_mutex` (shared lock for reads, unique for
writes), or lives behind a class that enforces it. Not optional.

> Absolutly.

### 4.3 Persistence helper

`nicknames.json` is irreplaceable data (years of history). The Java version
truncates and rewrites the whole file on every single nickname change, on the
event thread — a crash mid-write loses everything.

- **Atomic writes**: serialize to `foo.json.tmp`, `fsync`, then
  `std::filesystem::rename` over the original. Rename is atomic on NTFS.
- **Keep snowflakes as JSON strings.** They exceed 2^53 and will silently
  corrupt if any parser treats them as doubles. The existing file already
  stores them as strings — preserve that.
- Consider debouncing writes (coalesce rapid changes) and keeping a rolling
  backup of the last known-good file.

> Atomic writes is very important. using json strings as well for snowflakes.
> Sure, debouncing would be good and keeping a couple of backups of the file as well is fine.

### 4.4 Command registry

Mirrors `BaseCommand`/`CommandRegistry` but adds what §12 needs:

```cpp
struct command {
    std::string name, description;
    std::vector<std::string> aliases;          // nowplaying->np, queue->q
    uint64_t required_bot_permissions = 0;     // NEW: drives the startup check
    dpp::permission default_member_permissions;
    bool guild_only = true;
    virtual void execute(const dpp::slashcommand_t&) = 0;
    virtual dpp::slashcommand build(const std::string& name_or_alias) const = 0;
};
```

Registration is a static list like `Commands.java`, but the aliases mechanism
should just build a second `dpp::slashcommand` with a different name pointing at
the same handler (same as the Java approach — it worked fine).

> approved.

### 4.5 The unified message pipeline ⚠️

Right now, several features each want a look at every incoming message: trigger
words, URL replacement, LLM auto-response, and "say goodbye latibot". The Java
version's `onMessageReceived` handles this with early `return`s, which causes a
real bug: **a message containing both "420" and a link gets "nice" but no link
replacement**, because the trigger check returns before the URL logic runs.

Build one ordered pipeline instead, where each stage declares whether it
consumes the message:

```
on_message_create
  └─ ignore bots/self
  └─ stage: admin shutdown phrase   (consumes)
  └─ stage: url replacement         (does NOT consume)
  └─ stage: trigger word responses  (does NOT consume)
  └─ stage: LLM addressed/auto      (consumes)
```

Making "consumes or not" explicit per stage is what fixes the class of bug,
rather than fixing the one instance.

> Yes this is a better approach that is much more flexible. Approved.

---

## 5. Basic commands (Stage 1 §11)

Direct ports, no design questions. `/ping`, `/say`, `/shutdown`, `/status`,
`/join`, `/leave`.

Notes:
- `/ping`: `e.reply()` then edit with elapsed ms — same trick as Java, using
  `dpp::interaction_create_t::edit_original_response`.
- `/say`: keep the optional reply-to-message-id. Guard against the message ID
  not existing (Java did handle this).
- `/shutdown`: must shut down DECtalk (`TextToSpeechShutdown`) and close voice
  connections before exiting, same as Java.
- `/join` / `/leave`: these are needed **now**, not later — TTS playback depends
  on them even though music is stubbed. `guild::connect_member_voice()` or
  `discord_client::connect_voice(guild_id, channel_id)`.

### New: "say goodbye latibot"

A pipeline stage (§4.5) matching the phrase case-insensitively, then verifying
the author has `dpp::p_administrator` in that guild before shutting down.

Two things to get right: check the permission on the **member in that guild**,
not the user globally; and require the phrase to be the whole message (or
close to it) rather than a substring, so quoting it in conversation doesn't
kill the bot. Probably worth a goodbye message + a short delay so it doesn't
just vanish mid-sentence.

> yes a goodbye message should be configurable in the config file.

---

## 6. Permission preflight (Stage 1 §12)

Your described intent — each feature declares what it needs, startup verifies —
becomes striaghtforward with `required_bot_permissions` on the command struct
(§4.4).

Implementation:
- On `on_ready`, enumerate guilds; for each, resolve the bot's effective
  permissions (`guild::base_permissions(member)`), OR together the requirements
  of all registered commands plus the standing needs of passive features
  (message pipeline needs `MESSAGE_SEND`/`MANAGE_MESSAGES`, nickname tracking
  needs `MANAGE_NICKNAMES`, etc.).
- Report **per guild** what's missing, at WARN level — and critically, **do not
  exit**. The Java version calls `System.exit(-3)` if anything is missing in the
  one hardcoded guild; with multi-guild support, one under-permissioned server
  shouldn't take the whole bot down. Degrade that feature there instead.
- Optionally post the summary to the guild's system channel like Java did.

Also note: `on_guild_create` fires when joining a new guild later, so the same
check should run there, not just at startup.

> Approved.

---

## 7. Nickname tracking (Stage 1 §4, §5)

### 7.1 The attribution problem

Dropping the owner-confirmation flow (§5) is straightforward, but it removes the
SHA-256 hash machinery that also served a *second* purpose: correlating the
`/nickname` command with the resulting `GUILD_MEMBER_UPDATE` event, so history
could record **who** changed the name.

Two ways to restore that, much more simply:

**(a) Pending-attribution map (baseline, recommended).** Before calling the edit,
`/nickname` inserts `{guild_id, user_id, new_nickname} → {actor_id, expiry}` into
a short-TTL map. The `on_guild_member_update` handler looks it up; a hit
attributes to the actor, a miss means self-change. No hashing, no confirmation
dance. Keying on the new nickname (rather than just the user) handles rapid
successive changes.

**(b) Audit log lookup (better, more work).** On member update, query the guild
audit log for a recent `MEMBER_UPDATE` entry targeting that user.

Worth knowing: **(b) fixes a real correctness bug in the current version.** Today,
any nickname change made through the Discord UI by a moderator is recorded as if
the target changed it themselves, because there's no hash for it. Only changes
made through `/nickname` get correct attribution. Audit log is the only way to
catch the rest. Costs one API call per change and needs `VIEW_AUDIT_LOG`.

Suggest shipping (a) first since it's a small amount of code, then adding (b) as
a refinement.

> This should be updated to use the audit log. Not sure why i didnt do that originally, sounds like a much better approach.

### 7.2 Storage

Keep the existing `nicknames.json` schema exactly — it holds years of data and
there's no reason to migrate it:

```json
{ "<guild id>": [ { "guild": "...", "member": {"id": "...", "username": "..."},
                    "nicknames": [ {"nickname": "...", "changedById": "...",
                                    "datetime": "yyyy-MM-dd HH:mm:ss"} ] } ] }
```

One internal change: the Java `NicknameHistory.fromJson` resolves live `Guild`
and `Member` objects at load time, which is why loading has to wait for the
gateway to be ready and why it breaks for members who have left. **Store raw
snowflake IDs and resolve lazily at display time** instead. That removes the
startup ordering constraint and handles departed members gracefully.

Other edge cases the Java version mishandles, worth fixing in the port:
- `getLatestNickname()` does `nicknames.get(size() - 1)` with no empty check →
  throws on an empty history list.
- Null nicknames (a user clearing their nickname) get stored as null.
- `getMemberById` returning null for departed members → NPE during load.

> Reasonable changes. Approved.

### 7.3 Startup reconciliation

Same idea as `ReadyListener`: on ready, compare each tracked member's stored
latest nickname to their current one and record any change that happened while
offline. Attribute these to "unknown" rather than to the member themselves —
the Java version guesses "self", which is often wrong.

> sure, makes sense.

### 7.4 `/nicknames` pagination

The current version gives up entirely past 2000 characters, and you noted most
histories are well past that now.

Recommended: **embed + button pagination.** Render N entries per page into a
`dpp::embed`, with ◀/▶ buttons. Page state encodes into the button `custom_id`
(e.g. `nick:<user id>:<page>`), handled in `on_button_click` — no server-side
session state needed, so it survives restarts.

Watch out for: `custom_id` has a **100-character limit**; embed description caps
at 4096 chars and total embed at 6000. For genuinely enormous histories, offer a
`.txt` file attachment as an escape hatch.

> Approved.

### 7.5 Later: appearance/profile tracking

Your "nice to have" — tracking name color (including gradient roles), per-server
avatar, avatar decoration, and font alongside nicknames.

Deliberately deferred, but two notes so the schema doesn't box us in:
- Make the history entry an **open object** now (`{nickname, changedById, datetime, ...}`)
  so appearance fields can be added later without a migration.
    > This is fine, but i am also okay with migrating the existing data to a new format when we get to this implementation in the future.

- Name color isn't a member property — it derives from the highest colored role.
  Gradient/holographic role styles are newer API surface; **verify DPP 10.0.35
  actually exposes role colors as gradients** before promising this. May need a
  DPP upgrade or a raw API call.
    > Yes, need to verify this. It likely will need to be handled via raw api calls. Maybe consider a generalized util for making these raw api calls to fill the gaps of unsupported/missing features in DPP.

- Rendering "what they looked like" in an embed is mostly a presentation problem
  (embed color = name color, thumbnail = server avatar). Gradients can't be
  represented in an embed color, so that needs thought — possibly generating a
  small image.
    > Yes, this may need to be a generated image placed in the embed rather than in the embed itself. Something to evaluate later when we get to it. 

---

## 8. URL replacement (Stage 1 §8) — the big redesign

Your most-used feature, and the one with the most accumulated problems. Each of
your four reported bugs has an identifiable root cause in the Java code.

### 8.1 Bug: multiple links in one message

**Root cause.** The regex wraps the URL pattern in greedy `(?<before>.*)` and
`(?<after>.*)` groups. On a single-line message with two links, the greedy
`before` consumes as far as possible, so the *first* `find()` swallows the entire
string and matches only one URL — the loop then exits. Compounding it,
`index = index % replacements.size()` reassigns the shared `index` variable
inside the loop, corrupting alternate-selection across links.

**Fix.** Drop the `before`/`after` capture groups entirely. Match only the URL
itself, iterate all matches collecting `(start, end, domain)` offsets, then
rebuild the output by splicing replacements into the original string. Track
alternate index per-link, not globally.

> Approved.

### 8.2 Bug: spoilers not preserved

**Root cause.** `matcher.group("before").split("||")` — Java's `String.split`
takes a **regex**, and `"||"` as a regex is `empty|empty|empty`, which matches
the empty string everywhere and splits into individual characters. So
`.split("||").length % 2 == 0` is really testing "is the character count even",
which is meaningless. The spoiler check never worked as intended.

**Fix.** Count literal `||` occurrences before the match position; an odd count
means the link sits inside an open spoiler, so the replacement must be wrapped
in `||…||` too. In C++ this is a plain `std::string::find` loop — no regex
subtlety to trip over.

> Approved.

### 8.3 Bug: unreliable embed verification

**Root cause.** After posting, it schedules a check 5 s later, re-fetches the
message, and treats `embeds.size() < replaceCount` as failure — up to 10 retries,
editing the message each time. Problems: 5 s is often not enough on a slow
network (false failure); some links legitimately never produce an embed (false
failure forever); `replaceCount` is wrong when multiple links are involved (see
8.1); and each retry edits the visible message, causing flicker.

**Fix — go event-driven instead of polling.** Discord fires `MESSAGE_UPDATE` when
it attaches an embed to an existing message, and DPP surfaces this as
`cluster::on_message_update`. So:

1. Post the replacement, record its message ID in a pending map.
2. Listen on `on_message_update`; if the ID matches and embeds are now present →
   success, drop it from the map. No polling, no fixed delay, correct even if
   Discord takes 30 s.
3. Keep a **single** fallback timeout (~10 s) via `cluster::start_timer`. Only on
   timeout, try the next alternate domain.
4. Cap retries at the **number of configured alternates**, not an arbitrary 10.

This eliminates the false-negative class entirely, since slowness no longer
looks like failure.

> Approved.

### 8.4 Bug: clunky management commands

Your note says this needs discussion — here's a concrete proposal to react to.

Current: `/replaceurl add|remove|list`, with "edit" requiring remove-then-re-add,
and alternates managed by a confusing `as_alternate` boolean.

Proposed:

| Command | Purpose |
|---|---|
| `/urlrepl list` | Paginated embed of all rules |
| `/urlrepl set <domain> <replacements>` | Set the **entire ordered** alternate list in one call (comma-separated). Replaces add/edit/as_alternate with one obvious operation |
| `/urlrepl remove <domain>` | Delete a rule |
| `/urlrepl test <url>` | **Dry run** — show exactly what the bot would post, without posting. Makes debugging the above bugs possible |

Two DPP features worth using here, both confirmed present:
- **`on_autocomplete`** on the `domain` parameter — suggests existing domains as
  you type. This is the single biggest usability win; it eliminates typos and the
  "what did I call it again" problem.
- **`on_form_submit` / `interaction_modal_response`** — a modal with a multi-line
  text box is a much nicer editor for a long alternate list than a slash
  parameter. Optional, but it fits this exact use case.

> Looks good. I dont believe the feature allowing dialog creation existed when i original made the java version. Consider potentially having it be fully dialog driven with a editable list that for even better useability. I am not familiar at all with the capabilities of these dialogs, so need to verify if this is at all feasible. if so, it would be preferable to a command only interface.

### 8.5 New: `/en` translation suffix

Some embedders (fxtwitter et al.) support an `/en` path suffix to auto-translate.
This makes the replacement target a **structured rule**, not just a hostname:

```json
{ "x.com": { "alternates": [
      {"host": "fxtwitter.com", "translate_suffix": "/en"},
      {"host": "vxtwitter.com"} ] } }
```

Appending the suffix correctly means handling existing query strings and trailing
slashes — parse the URL rather than doing string concatenation. Whether it's
always-on or a per-rule toggle is your call.

> Lets just go with always on for now.

### 8.6 New: reaction statistics

Track reactions on replacement messages, attributed to whoever posted the
original link.

- Persist `replacement_message_id → {original_author_id, channel_id, domain, timestamp}`.
  This **must** survive restarts, or reactions on older messages are lost —
  so it's a file, not an in-memory map.
- Handle both `on_message_reaction_add` and `on_message_reaction_remove` so
  counts stay accurate.
- Retention policy needed: this grows without bound. Suggest aging out entries
  past N days, or capping total size.
- A `/linkstats` command can then answer "who gets the most reactions" and
  "which reactions are most popular".

**This is the point where a real database starts to make sense** — nickname
history, reaction stats, URL rules, and midnight config are four separate JSON
files with hand-rolled locking. SQLite is available via Conan and would
consolidate them with real queries and transactional writes. Recommend staying
with JSON for now (matches existing data, no new dependency) but revisiting if
reaction stats get big — flagging so it's a conscious choice.

> I think it makes sense to just do the database migration now. Now is the time to do all that work to do things more correctly. SQLite is probably fine, but may be worth having a bit more discussion.

### 8.7 Other fixes while in here

- **Webhook mode removed** entirely per your note — deletes ~40 lines and the
  `MANAGE_WEBHOOKS` permission requirement.
    > Approved.
- **Persist the per-user opt-out list.** `/toggle` currently writes to an
  in-memory `ArrayList` that resets on every restart, so the command silently
  forgets. Should be in the config file.
    > Approved.
- **Don't crash on a missing config file.** The Java static initializer throws
  `RuntimeException` if `UrlReplacements.txt` is absent, which kills the bot at
  class-load. Start with an empty ruleset and log a warning.
    > Approved.
- **Migrate the config format** from pipe/caret text to JSON, with a one-time
  importer for the existing `UrlReplacements.txt`.
    > Approved.
---

## 9. Midnight message (Stage 1 §10)

### 9.1 The random-fire bug — diagnosed

You described the bot occasionally posting "midnight" at a random time of day,
then behaving for a while. The cause is almost certainly this:

`ScheduledExecutorService.schedule(task, delay, SECONDS)` measures its delay
against a **monotonic clock**, but the delay is *computed* from the **wall
clock**. Those diverge when the machine sleeps/hibernates or the system clock
jumps (NTP correction, DST-adjacent weirdness). On resume, the pending task
fires more or less immediately — at whatever wall-clock time that happens to be.
The handler then recomputes the next run correctly from the wall clock, which is
exactly the "then goes back to normal for a while" behaviour you observed.

(Also worth noting: the `if (now.isAfter(nextMidnight))` branch that logs `"huh???"`
is unreachable — `nextMidnight` is always constructed as tomorrow.)

**Fix: poll the wall clock instead of sleeping on a computed delay.**

```
cluster::start_timer(tick, 30 /* seconds */)
  for each configured timezone entry:
      local_now = now in entry.timezone
      if local_now.date() != entry.last_fired_date and local_now >= 00:00:05:
          send message; entry.last_fired_date = local_now.date(); persist
```

A short repeating tick comparing against wall-clock time is immune to
suspend/resume, clock jumps, and DST shifts. The `last_fired_date` guard makes
double-firing impossible even if several ticks land in the same second, and
persisting it means a restart at 00:00:30 won't re-post.

> Sounds good. Approved.

### 9.2 Multi-timezone support

Config becomes a list:

```json
{ "midnight": [
    { "timezone": "America/Chicago", "channel_id": "…", "message": "midnight", "enabled": true },
    { "timezone": "Europe/London",   "channel_id": "…", "message": "…",        "enabled": true } ] }
```
 
The polling design above handles N entries naturally — no extra machinery.

**Dependency decision:** C++17 has no IANA timezone support.
- **Option A — bump the project to C++20.** MSVC's `<chrono>` provides
  `std::chrono::locate_zone`/`zoned_time` (needs the ICU-backed tzdb, present on
  Win10 1903+, so fine on your Win11). Note this changes `compiler.cppstd` in the
  Conan profile, which changes package IDs → **all dependencies rebuild**.
- **Option B — Howard Hinnant's `date` library** (Conan `date/3.x`), which is
  where `std::chrono`'s tz support came from. Works on C++17, adds a dependency.

Lean toward A — it's where the code is going anyway and avoids a dependency, and
the rebuild is a one-time cost. Your call.

> yes, option A. See notes above about updating cpp std.

### 9.3 Management commands

`/midnight list | set-channel | set-message | add-timezone | remove-timezone | toggle`,
writing to the config file. Autocomplete on the timezone parameter would be a
nice touch given how many IANA zone names there are.

> Approved.

---

## 10. LLM integration (new build)

Replaces the never-wired-up `ApiDriver`. Requirements from your comment: support
**both** OpenAI and Anthropic; respond when addressed; optionally auto-respond to
trigger words with a random chance; able to speak via DECtalk in voice.

### 10.1 Provider abstraction

```cpp
struct llm_message { std::string role, content; };
struct llm_request  { std::string system;
                      std::vector<llm_message> messages;
                      int max_tokens; };
struct llm_provider {
    virtual void complete(const llm_request&,
                          std::function<void(std::string, bool ok)>) = 0;
};
```

Two implementations, selected by config. **No new HTTP dependency is needed** —
`cluster::request(url, method, callback, postdata, mimetype, headers, protocol, request_timeout)`
covers it, and the timeout is a parameter (important: its default is 5 s, far too
short for an LLM call — pass 30–60 s). JSON via `nlohmann::json`, already
available through DPP.

> Seems good.

### 10.2 Anthropic

```
POST https://api.anthropic.com/v1/messages
Headers: x-api-key: <key>
         anthropic-version: 2023-06-01
         content-type: application/json
Body:    {"model": "...", "max_tokens": N, "system": "...",
          "messages": [{"role":"user","content":"..."}]}
```

Response: `content` is an **array of blocks** — filter to `type == "text"` and
read `.text` (not a single string field). Also check `stop_reason`.

Current model IDs — use these exactly, **never append a date suffix**:

| Model | ID | Input $/1M | Output $/1M |
|---|---|---|---|
| Claude Opus 5 | `claude-opus-5` | $5.00 | $25.00 |
| Claude Sonnet 5 | `claude-sonnet-5` | $2.00 | $10.00 |
| Claude Haiku 4.5 | `claude-haiku-4-5` | $1.00 | $5.00 |

**Two porting gotchas that break a direct translation of the Java prompt config:**

1. **`temperature` is rejected with a 400** on current Claude models
   (Opus 5 / Sonnet 5 / the 4.6+ family). The Java `ApiDriver` sets
   `temperature(1.5)` to get varied responses — that value cannot carry over.
   Get variety through the system prompt instead (which the existing prompt
   already attempts: *"make sure that you have varied responses and are not
   repeating 'Yes' or 'No' over and over"*).
2. **`max_tokens` must cover thinking, not just the reply.** Thinking is on by
   default on Opus 5, and thinking tokens count toward the output budget. The
   Java version's `maxCompletionTokens(100)` would truncate. Either give it real
   headroom (~1024) with `output_config: {"effort": "low"}` for terse, cheap
   replies, or use a model without adaptive thinking. Don't set
   `thinking: {"type":"disabled"}` — it has documented failure modes (leaked
   `<thinking>` tags, tool calls written into visible text); low effort is the
   better lever.

Assistant prefill also returns 400 on current models, in case that pattern was
being considered for response shaping.

> The reference implementation can be discarded. This system should be started fresh. I would perfer to keep this feature to lesser models as i dont want to spend a ton of money on this. It should still be configurable but i doubt i will be choosing smthing like opus 5.

### 10.3 OpenAI

```
POST https://api.openai.com/v1/chat/completions
Headers: Authorization: Bearer <key>
Body:    {"model": "...", "messages": [...], "max_completion_tokens": N}
```

Response: `choices[0].message.content`. Model choice is yours (the Java version
used `gpt-4o`).

> Same thing. Discard reference implementation and start fresh.

### 10.4 Behaviour

- **Addressed**: bot is mentioned, or the message replies to one of its messages.
  Pipeline stage (§4.5), consumes the message.
- **Auto-response**: configured trigger words with a per-trigger probability
  (`0.0`–`1.0`). Needs a cooldown per channel so it can't spiral, and must never
  respond to its own or other bots' messages.
    > There is actually a case where i want to to respond to a specific bot. A detail for later but the gist is that a 2nd other bot will also have llm integration and i want them to be able to talk to each other. Conditionally of course. 
- **Conversation context**: decide how much history to include. Simplest is a
  short rolling window per channel; the Java version was single-shot. Note the
  APIs are stateless — full history is resent each call, so this directly drives
  cost.
    > id like this to be configurable. doesnt have to be exposed via commands. 
- **Streaming**: both APIs support SSE, but `cluster::request` delivers one
  completion callback rather than a stream. Non-streaming is the right call for
  a Discord bot (you can't edit a message per token without hitting rate limits
  anyway). Send a typing indicator while waiting.
    > makes sense.
- **Speaking via DECtalk**: pipe the reply text into the TTS path (§11). Needs
  sanitization first — strip markdown, custom emoji, and mentions, and cap length,
  or DECtalk will happily read `<@1234567890>` aloud digit by digit.
    > Yes sanitization needed. Also need to consider that when tts is being used, the system prompt (or equiv) should direct the model on how to use the inline control commands DECtalk understands to change the voice properties, pitch, and etc. Not certain how well this will work but im interestd in trying.    

### 10.5 Safety/cost guards

Per-user and per-channel rate limits, a max tokens cap, and a kill switch toggle.
Keys from environment variables only.

> Yes, administrators should also be able to blacklist users or roles. 

---

## 11. DECtalk TTS (Stage 1 §2, §3)

### 11.1 Vendoring

Per your note, use `github.com/dectalk/dectalk` @ `develop` rather than the
recovered DLL in the reference project.

Practical notes:
- The `develop` branch **ships prebuilt Windows x86_64 binaries**, so a build from
  source may not even be necessary to start.
- It is **not a CMake project** — Windows builds use a Visual Studio 2022 solution
  (plus legacy VS6 batch scripts); Linux/macOS use autotools. Three integration
  options, in order of recommendation:
  1. **Vendor the prebuilt `.lib`/`.dll` + headers** into `third_party/dectalk/`,
     link directly, document the rebuild steps. Simplest, matches "vendored into
     this workspace".
  2. Write our own `CMakeLists.txt` over their C sources — makes it a first-class
     CMake target, but it's a large, old C codebase.
  3. `ExternalProject_Add` driving MSBuild on their `.sln` — automated but
     fragile.
- **`git` is not installed on this machine** (neither is `ffmpeg` or `yt-dlp`, and
  the workspace isn't a git repo). Vendoring needs either `winget install Git.Git`
  or a zip download. See §13.
    > lol i forgot about that. Git is now installed on the system.
- Dictionary/data files (`dtalk_us.dic`, `dic_*.txt`) need to be findable at
  runtime — copy them next to the executable via a CMake post-build step, same as
  we already do for `dpp.dll`.
    > I never figured out how to get the DecTalk dll to use or compile these dictionary files in my original implementation.

> I would like to roll our own CMake for dectalk based off the existing build system ideally.

### 11.2 Engine wrapper

An RAII class around the C API, with all the calls the JNI shim never exposed:

```cpp
class dectalk_engine {
public:
    bool  startup();                                 // TextToSpeechStartup(Ex)
    void  shutdown();                                // TextToSpeechShutdown
    std::vector<int16_t> synthesize(std::string_view text);   // in-memory PCM
    void  set_speaker(speaker);   // PAUL/BETTY/HARRY/FRANK/DENNIS/KIT/URSULA/RITA/WENDY
    void  set_rate(uint32_t);
    void  set_volume(int);        // fixes the "make dectalk louder" TODO
    uint32_t sample_rate() const; // from TextToSpeechGetCaps
};
```

`synthesize` implements the in-memory path from §1.1. Two things to be careful
about:
- **The engine is a single global handle and is not thread-safe.** Serialize all
  access behind a mutex, or better, run synthesis on a dedicated worker thread
  with a request queue — synthesis is CPU work and must not block a DPP event
  thread.
    > Yes.
- Keep the `[:phoneme arpabet speak on]` prefix support that `ChatTestCmd` used,
  and consider exposing DECtalk's inline command syntax generally (it's how you'd
  do the "dectalk copy pastas" idea from the old TODO list).
    > Yes, this is required. being able to control with the inline commands must be supported by default.

### 11.3 `/speak` — voice channel playback

Flow: text → `dectalk_engine::synthesize` → resample 11025 mono → 48000 stereo
(§1.2) → chunk into 11520-byte frames → `voice_mixer` → `send_audio_raw`.

Per §1.4, `/speak` does **not** go through any music queue — it goes to the mixer
as a high-priority source that preempts music.

> Approved.

### 11.4 `/chat` — voice message attachment

Promoted to a real command per your note.

- Synthesize to PCM in memory, wrap in a RIFF/WAV header in memory (Discord needs
  a real audio file, and a WAV header is 44 bytes we can write ourselves).
- Compute `duration_secs` from sample count ÷ sample rate.
- Compute the 256-byte waveform **correctly** this time (§1.7): per-bucket peak
  amplitude of the int16 samples, normalized 0–255, base64-encoded.
- Upload via `post_rest_multipart` with hand-built JSON including `flags: 8192`
  and the attachment metadata (§1.6) — **needs a spike to confirm**, with a plain
  attachment as the fallback.

**No temp files anywhere in this path**, which resolves the retention/cleanup
concern from your §3 comment entirely.

> Approved.

---

## 12. Deferred features

### 12.1 Music (Stage 1 §1) — stubbed now

For this phase: **don't register the queue commands at all** rather than
registering stubs that reply "not implemented" — fewer dead commands cluttering
the slash menu. Keep `/join` and `/leave` real, since TTS needs them.

Design sketch for when it's built, incorporating your "needs a redesign" note:
- `audio_source` interface with two implementations: TTS (PCM already in memory)
  and streamed media (yt-dlp resolves the URL → ffmpeg decodes to PCM → pipe).
- Queue owns ordering/metadata; DPP's marker API (§1.3) owns playback position.
- Both sources feed the `voice_mixer` (§1.4), which handles TTS-preempts-music.
- yt-dlp and ffmpeg become external runtime prerequisites (or libav* via Conan).

> Approved.

### 12.2 Emote stats (Stage 1 §6) — later

Brief note for when it comes up: the expensive part is walking full channel
history via paginated `messages_get`, with many channels in flight. Without
coroutines (§1.5) that's a callback-chained pager with a bounded concurrency
limit. The `commons-collections4 Bag` is just a multiset →
`std::unordered_map<dpp::snowflake, int>`. Worth designing around incremental
caching (store a per-channel high-water mark, only scan new messages) rather than
re-walking everything each invocation, which is what made the original so slow.

> Sure. remember.

---

## 13. Prerequisites and dependencies

### Missing from this machine right now

| Tool | Status | Needed for |
|---|---|---|
| `git` | **not installed** | Vendoring the DECtalk repo |
| `ffmpeg` | not installed | Music only (§12.1) — not TTS |
| `yt-dlp` | not installed | Music only (§12.1) |

Also: **the workspace is not a git repository.** Worth initializing before the
port starts in earnest, given the amount of code about to be written — and the
`.gitignore` is already in place from the initial setup.

> I've installed git. Yes, initalizing the git repo is fine.

### Conan dependencies

| Package | Status | For |
|---|---|---|
| `dpp/10.0.35` | already installed | Everything |
| `nlohmann_json` | already present (via DPP) | All persistence, LLM JSON |
| `date/3.x` | **maybe** | Timezones, only if staying on C++17 (§9.2) |
| `ffmpeg/9.x` | later | Music, if going the libav* route |
| `sqlite3` | maybe later | Only if consolidating storage (§8.6) |

No HTTP library needed — `cluster::request` covers the LLM calls (§10.1).

> date/3.x can be ignored, will be upgrading to cpp20. Yes lets go with sqlite3.

### Build configuration change to decide

Moving to **C++20** (§9.2) would provide native timezone support and remove the
`date` dependency. It requires updating `CMAKE_CXX_STANDARD` and
`compiler.cppstd` in the Conan profile, which changes package IDs and triggers a
full dependency rebuild — a one-time cost. It would *not* automatically enable
DPP coroutines, since those need DPP itself built with `DPP_CORO` (§1.5).

> Yes, update to at least cpp20

---

## 14. Proposed build order

Ordered by dependency and by how much each unblocks, not purely by difficulty.

**Phase 0 — foundations** (nothing works well without these)
1. Config system + secrets from environment (§4.1)
2. Bot context object, replacing static globals (§4.2 thread safety baked in)
3. Command registry with declared permissions (§4.4)
4. Atomic JSON persistence helper (§4.3)
5. `git init`

**Phase 1 — quick wins that prove the framework**
6. Basic commands (§5) — ping, say, shutdown, status, join, leave
7. Permission preflight (§6)
8. Message pipeline skeleton + "say goodbye latibot" + trigger words (§4.5)

**Phase 2 — the data features**
9. Nickname tracking + attribution + storage (§7.1–7.3)
10. `/nicknames` with button pagination (§7.4)
11. Midnight rewrite: wall-clock scheduler, multi-timezone, commands (§9)

**Phase 3 — the most-used feature, done properly**
12. URL replacement core: multi-link + spoiler fixes, JSON config migration (§8.1–8.2)
13. Event-driven embed verification (§8.3)
14. Command UX with autocomplete + `/urlrepl test` (§8.4)
15. `/en` suffix support (§8.5)
16. Reaction statistics (§8.6)

**Phase 4 — voice**
17. Vendor DECtalk, get it linking and synthesizing to memory (§11.1–11.2)
18. Resampler + `voice_mixer` (§1.2, §1.4)
19. `/speak` in voice channels (§11.3)
20. `/chat` voice message — **spike the DPP gap first** (§11.4, §1.6)

**Phase 5 — LLM**
21. Provider abstraction + Anthropic + OpenAI (§10.1–10.3)
22. Addressed/auto-response behaviour + guards (§10.4–10.5)
23. LLM → DECtalk → voice channel (§10.4)

**Later, unscheduled:** music (§12.1), emote stats (§12.2), appearance tracking (§7.5).

Phases 1–3 deliver a bot that's already more capable and less buggy than the Java
one, before touching any of the hard audio work.

> Sounds good, may need to update based on all my comments above.

---

## 15. Open questions for you

1. **C++20 or stay on C++17 + `date`?** (§9.2, §13) — leaning C++20.
    > move to cpp20, see comments about vendoring dpp into workspace to build from source.
2. **URL command UX** — does the `/urlrepl set` + autocomplete + `test` proposal
   in §8.4 fit how you actually use it, or do you want the modal editor?
    > Having the modal interaction is ideall, but the command interface is also useful.
3. **`/en` translation** — always-on where supported, or a per-rule toggle? (§8.5)
    > always on for now where supported.
4. **LLM default provider and model** — Anthropic or OpenAI as the default, and
   which model? Affects cost more than anything else. (§10.2)
    > Default to anthropic.
5. **LLM conversation memory** — single-shot like the Java version, or a rolling
   per-channel window? (§10.4)
    > I'd like to have a long term memory of some kind to remember important things and a short term for the context around individual conversations.
6. **Reaction stats retention** — how long to keep replacement-message records
   before aging them out? (§8.6)
    > Id like to keep them forever bc i like the data and dont think it will grow very quickly at all. Especially when moving to sqlite instead of json
7. **Nickname attribution** — ship the simple pending-map first, or go straight
   to audit-log lookup so moderator renames are attributed correctly? (§7.1)
    > use the audit log.
8. **Guild scope** — generalizing to all guilds is agreed, but should per-guild
   config (midnight channels, URL rules, opt-outs) be split per guild now, or
   stay global until a second guild actually exists?
    > Relevant config should be split per guild.
