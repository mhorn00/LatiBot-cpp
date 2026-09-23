# LatiBot Java → C++/DPP Porting Plan

This document breaks the existing Java bot (`java-reference/LatiBot v1.1`) down into
individual features. For each one: **mark it `Keep`, `Skip`, or `Later`** in the
checkbox, and add notes inline if you want something done differently. Nothing
gets implemented until you've been through this list — this is purely the
analysis/planning pass.

Legend: 🟢 Low complexity · 🟡 Medium · 🔴 High (new architecture needed)

---

## 0. Architecture summary of the Java bot

- **Framework:** JDA 5, one `JDA` instance, event listeners (`ListenerAdapter`
  subclasses) + a hand-rolled slash-command registry (`BaseCommand`,
  `CommandRegistry`, static `Commands` holder).
- **Global mutable state** lives in static fields on `LatiBot` (`jdaInst`,
  `audioPlayerManager`, `audioPlayer`, `tm` (TrackManager), `dectalk`). Commands
  and listeners reach into these statics directly rather than through
  dependency injection.
- **Startup** (`LatiBot.main`): build JDA with specific intents/cache flags,
  await ready, hard-check a fixed list of `Permission`s **against one hardcoded
  guild ID** (`142409638556467200`, "LATV") and exit if missing, register all
  slash commands globally, wire up the audio player, schedule the midnight
  task.
- **Persistence** is flat files next to the working directory, not a database:
  `nicknames.json`, `UrlReplacements.txt`, plus runtime-generated `tts/*.wav`
  and `emotes/*`.
- **Audio:** LavaPlayer resolves/streams tracks (YouTube, direct links, etc.)
  and DECtalk (a native Windows TTS DLL, called via JNI) synthesizes speech to
  a WAV file that's then queued as just another "track".

### Proposed C++ mapping

| Java concept | C++/DPP equivalent |
|---|---|
| `ListenerAdapter` subclasses | `dpp::cluster::on_*` event handlers (lambdas or free functions registered in one place) |
| `BaseCommand` / `CommandRegistry` / `Commands` | Similar registry: a small `command` interface + a map keyed by name, or just register `dpp::slashcommand` objects directly with a dispatch table in `on_slashcommand` |
| Static globals on `LatiBot` | A single `Bot` struct/class holding `dpp::cluster`, current `TrackManager`, TTS engine handle, etc., passed by reference — avoids the "everything is a static" pattern without over-engineering it |
| `nicknames.json` / `UrlReplacements.txt` | Same file formats, parsed with `nlohmann::json` (already pulled in transitively by DPP) — no need to invent a new format |
| LavaPlayer | **No direct equivalent — needs a real decision, see §1** |
| DECtalk via JNI | Direct native call — actually *simpler* in C++ since there's no JNI boundary; either link `DECtalk.lib` directly or keep the `LoadLibrary`/`GetProcAddress` pattern (still Windows-only either way) |
| logback | DPP's built-in logger (`cluster.on_log`) is already wired up in the skeleton from setup |

---

## 1. Music playback — 🔴 High — the one open architectural question

**What it does:** `/play [link] [type: normal|next|now] [silent]`, `/queue`
(alias `/q`), `/nowplaying` (alias `/np`), `/pause`, `/skip`, `/repeat`,
`/shuffle`, `/clear`, plus `/join` and `/leave` to manage the voice connection.
Backed by `TrackManager` (a linked-list `SongQueue` + repeat flag) and
LavaPlayer, which does URL resolution, format decoding, and Opus encoding for
YouTube/direct-link/etc. sources.

**Files:** `TrackManager.java`, `AudioTrackInfo.java`, `AudioSendingHandler.java`,
`command/commands/audio/*.java` (8 files), `command/commands/bot/JoinVoiceCmd.java`,
`LeaveVoiceCmd.java`.

**Why this is the hard one:** LavaPlayer is a JVM library with no C++
equivalent — it bundles YouTube extraction logic, container demuxers, and an
Opus encoder. DPP handles the Discord voice *transport* (it can send raw PCM
or Opus frames once connected) but does nothing for source resolution/decoding.
Realistic options, roughly in order of how much you'd normally want:

1. **Shell out to `yt-dlp` + `ffmpeg`**: resolve the URL with `yt-dlp -g` (or
   have it write directly), pipe audio through `ffmpeg` to raw PCM, feed DPP's
   voice connection (`dpp::discord_voice_client::send_audio_raw`). This is the
   most common approach other C++/native Discord bots use and avoids
   reimplementing a media pipeline. Requires `yt-dlp` and `ffmpeg` as external
   runtime dependencies (not Conan packages — installed separately or vendored).
2. **libavformat/libavcodec (FFmpeg libraries) directly** via Conan's `ffmpeg`
   package, skipping the `ffmpeg` CLI — more "native" but noticeably more
   integration work for not much practical benefit here.
3. **Drop general URL support, keep only direct audio file/stream URLs** — much
   simpler (just an HTTP fetch + decode), but loses the "paste a YouTube link"
   workflow that's presumably the main use case.

**Decision needed before this can be scoped further:** which sources actually
matter to you day-to-day (YouTube? Direct MP3/WAV links? Local files?) — that
determines which of the above is worth it. Recommend picking this once you've
been through the rest of the list, since it's independent of everything else.

- [ ] Keep — pick approach: 
- [ ] Skip entirely (drop music playback)
- [x] Later (stub the commands, implement last)

> I want to keep the music bot functionality, but its not the most important. For now lets just stub it and can come pack to it later when more of the bot is implemented. 
>
> When we do get to the implementation, we will be going with yt-dlp and ffmpeg. The ffmpeg part will be relevant for the next feature though.
>
> I do think there is some need to rethink and redesign how the music and track sequencing implementation will work though as the reference java implementation is not very good. I created it to get somthing working and never got around to cleaning it up. Plus, there will likely be small portions that can be reused from the DECTalk integration (more on that in the next section). Again for now, we can just stub or comment the commands and come back to it later.



---

## 2. Text-to-speech via DECtalk (voice channel playback) — 🟡 Medium

**What it does:** `/speak <text>` — synthesizes `text` with DECtalk to a WAV
file, then queues it through the *same* `TrackManager`/voice connection as
music (interrupts current playback via `queueNow`).

**Files:** `DecTalkWrapper.java` + `src/main/c/DecTalkWrapper.c`, `SpeakCmd.java`.

**Porting notes:** Actually gets *simpler* in C++ — the Java version goes
through JNI (Java → native shim → DECtalk DLL) purely because Java can't call
a C DLL directly. In C++ you call `DECtalk.lib`/`dectalk.dll` directly, no
shim layer needed. The existing `DECtalkWrapper.dll` project's `.c`/`.h` files
in this folder (`ttsapi.h`, `tts.h`) document the exported functions
(`TextToSpeechStartup`, `TextToSpeechSpeak`, `TextToSpeechOpenWaveOutFile`,
`TextToSpeechSync`, `TextToSpeechCloseWaveOutFile`, `TextToSpeechShutdown`) —
those can be called with `LoadLibrary`/`GetProcAddress` (matching the
existing pattern) or by linking `DECtalk.lib` directly.

This feature depends on whatever is decided for §1 (needs a working
audio-queue/voice-playback path to actually play the resulting WAV), but the
*synthesis* half is independent and could be built/tested standalone first
(e.g. just write the WAV to disk without playing it).

- [x] Keep
- [ ] Skip
- [ ] Later

> For DECTalk, we should just link directly to DECTalk.lib and skip the shim. The version of DECTalk that exists in the reference project is also the result of my best effort to recover assets from DECTalk based on what I was able to find on the internet (and with my at the time much more limited knowledge about cpp software developement). I would like to use the proper DECTalk git repo from `https://github.com/dectalk/dectalk`, using its 'develop' branch. This can just be vendored into this workspace. 
>
> Regarding the audio playback, I want to seperate the music and track management from the voice synthesis audio playback, as i plan to do much more with the voice systhesis and dont want it to collide with the music playing features. This should use ffmepg to to playback the audio. 
> Another issue I want to improve over the previous implementation is to skip creating a .wav file and instead stream the audio directly if possible. I had the previous implmentation create wav files as a compromise because i was not able to figure out how to do the direct streaming to java from the dll with my limited knowledge at the time. I am not sure of the feasibility on this as I am no longer familiar with the DECTalk api, so this will need some analysis.
>
> One of the enhancements I want to make is to allow the llm integration to use DECTalk to 'speak' in the voice channel. I will detail the llm integration part in a further seciton.

---

## 3. TTS as a Discord "voice message" attachment (`/chat`) — 🟢 Low

**What it does:** A separate, currently-hidden/test command (description
literally says "you dont see this lol") that synthesizes DECtalk speech and
uploads it as a Discord native voice-message attachment (with a waveform
sample and duration), **without** needing a voice channel connection at all.

**Files:** `ChatTestCmd.java`.

**Porting notes:** Reuses the DECtalk synthesis from §2 but sidesteps the
voice-connection/LavaPlayer complexity entirely — it's just "synthesize WAV,
compute a 256-byte waveform sample + duration, upload as a file with DPP's
voice-message attachment flag." Worth doing early as a way to validate DECtalk
interop without waiting on the §1 decision. Confirm whether you actually want
to keep this given it's marked as a throwaway test command in the original.

- [x] Keep (as a real command? or drop entirely as it was just a test)
- [ ] Skip
- [ ] Later

> Yes, this should be moved into a real command. this was a classic case of "the most permenent fix is a temporary one" as the test implementation worked well enough and i never went back to fully finish it. 
> I would like to make the same streaming improvement as the in voice channel feature instead of using generated wav files, but not sure if that is reasonable. The voice message attachement may only expect a file. If so, its fine to use the wav, but it would need some kind of retention and auto delete after a bit of time so i dont have to manually delete wav files after time passes. 

---

## 4. Nickname change tracking & history — 🟢 Low

**What it does:** `NicknameListener` records every nickname change (self- or
bot-initiated) to `nicknames.json`, keyed by guild → member → list of
`{nickname, changedById, datetime}`. On bot startup (`ReadyListener`), it
back-fills any nickname changes that happened while the bot was offline.
`/nicknames <user>` displays the history (currently silently gives up if the
formatted output would exceed 2000 characters — a real limitation, not a
feature).

**Files:** `NicknameListener.java`, `ReadyListener.java`, `NicknamesCmd.java`.

**Porting notes:** Straightforward JSON I/O with `nlohmann::json`, a
`GUILD_MEMBER_UPDATE`-equivalent DPP event
(`dpp::cluster::on_guild_member_update`), and a startup reconciliation pass.
The 2000-char output limit should probably be fixed while porting (paginate
or use an embed/file) rather than carried over as-is — flag if you'd rather
keep exact parity instead.

- [x] Keep
- [ ] Skip
- [ ] Later

> Yes, this one is important. The nickname history should defineitly be updated to use pagation instead of just giving up. i was just lazy in the previous impl while it wasnt a problem at the time. Most nickname histories are will past the 2000 character limit at this point.
>
> Tentatively, i want to start saving more than just nicknames. Since i created this feature, discord has introduced per server profile customization. My server that this bot will primarily be used in also has the fancy role color feature so we have gradient names in addition to single color. I want to be able to catalog how people's 'look' has changed in addition to their nickname by tracking, name color (including gradient if present), server specific icon, icon decoration, and font (if possible). Then, the nickname history command would then use an embed or similar to show what the user looked like throughout the history. This feature is absolutly a 'nice to have' and should not be a priority. Get the original implementation in with the pagation fix, and we will come back to this later after more of the bot has been ported.

---

## 5. Forced nickname change with owner-confirmation flow — 🟡 Medium

**What it does:** `/nickname <user> <new nickname>` force-changes someone's
nickname. **Special case:** if the target is the guild owner, the bot does
*not* actually change the nickname — instead it posts a message tagging the
owner with ✅/❌ buttons; only if the owner clicks ✅ does the change get
recorded into history (and even then, the nickname itself was apparently
already "faked" as set in the reply — worth re-reading this logic carefully
during implementation, it's the most convoluted piece of the whole bot).
Uses SHA-256 hashing of `(old, new, memberId)` as a correlation key between
the nickname-change event and the pending command, plus hardcoded custom
emoji IDs for the button icons.

**Files:** `NicknameListener.java` (hash/confirm machinery + inner classes),
`ActionListener.java` (button handler), `NicknameCmd.java`.

**Porting notes:** DPP supports message components (buttons) and button
interaction events (`on_button_click`) directly, so the mechanics translate
fine. The hardcoded emoji IDs (`smwOK`/`smwNO`, snowflakes
`1150941028291985478`/`1150941027360854067`) are specific to your server and
will need to either stay hardcoded or become configurable.

- [ ] Keep (as-is, including the owner special-case)
- [ ] Keep, but simplify (e.g. drop the owner special-case, always apply the change)
- [x] Skip
- [ ] Later

> this implementation was stupid. The real fix was that i made an alt account that i dont regularly use the server owner, and my real account just became an administrator so the bot could just change my nickname like it could for everyone else. can be dropped.

---

## 6. Emote usage statistics (`/emotestats`) — 🟡 Medium

**What it does:** Walks the **entire message history of every text channel**
in the guild, tallying custom-emoji reactions and in-message emoji mentions,
then reports counts above a cutoff. Explicitly warned in its own description
as slow. Uses `org.apache.commons.collections4.Bag` for counting, which is
just a multiset — trivial to replace with `std::unordered_map<snowflake, int>`.

**Files:** `EmoteStatsCmd.java`.

**Porting notes:** The real work here is fetching full channel history
efficiently via DPP's paginated `messages_get`, and managing many concurrent
per-channel fetches (Java version uses `CompletableFuture`s + a thread pool;
C++ side would use DPP's coroutine support or chained callbacks). No
conceptually new dependencies, just a bulk-history-walk pattern worth getting
right once since nothing else in the bot needs it.

- [ ] Keep
- [ ] Skip
- [x] Later

> Not a priority. It would be nice to get the statistics on emote useage eventually but there a lot of nuance to this that i did not cover in the original impl. The original impl is also not great.

---

## 7. Emote export (`/getemotes`) — 🟢 Low

**What it does:** Downloads every custom emoji the bot can see to
`emotes/<guild name>/<emote name>.png|gif`.

**Files:** `GetEmotesCmd.java`.

**Porting notes:** Simple HTTP download loop against emoji CDN URLs (DPP
exposes emoji IDs; the CDN URL format is well-known/static). No new
dependencies beyond an HTTP client, which DPP already has internally for its
own REST calls (though that's not necessarily exposed for arbitrary GETs —
may want a small HTTP client, e.g. via a Conan package like `cpr` or raw
`asio`/`httplib`, if DPP doesn't expose one usable for this).

- [ ] Keep
- [x] Skip
- [ ] Later

> this was a temp tool that helped me bulk download all of my emotes across multiple server so i could redistribute them back to the severs based on the usage from the emote stats command. I dont plan on doing this again, can be dropped. 

---

## 8. URL "embed fix" replacement (Twitter/TikTok/Reddit/Instagram links) — 🟡 Medium

**What it does:** Scans every non-bot message for links to specific domains
(`x.com`, `tiktok.com`, `reddit.com`/`old.reddit.com`, `instagram.com`) and
rewrites them to embed-friendly mirrors (`fxtwitter.com`, `vxtiktok.com`,
`rxddit.com`, `ddinstagram.com`, each domain having a ranked list of
alternates it cycles through on repeated failures). Two delivery modes:
- **Webhook mode:** deletes the original, reposts as a webhook impersonating
  the original author (name + avatar).
- **Reply mode (default):** suppresses the original message's embed, then
  sends a separate spoiler-tag-aware reply containing just the fixed link,
  with retry logic that polls Discord for a few seconds to confirm the embed
  actually rendered (up to 10 retries, trying the next alternate domain each
  time) before giving up.

Configurable via `/replaceurl add|remove|list` (persisted to
`UrlReplacements.txt`, pipe/caret-delimited), `/togglewebhooks` (switch
webhook vs. reply mode), `/toggle [user]` (per-user opt-out blacklist, not
persisted — resets on restart).

**Files:** `MessageListener.java`, `ReplaceUrlCmd.java`, `ToggleReplaceCmd.java`,
`ToggleWebhooksCmd.java`, `UrlReplacements.txt`.

**Porting notes:** The regex and retry/polling logic is the fiddly part (the
Java author's own comment calls it "getting really stupid" but working) —
this one is worth porting close to as-is rather than redesigning, since it
evidently took real trial-and-error to get Discord's embed rendering timing
right. Needs `on_message_create`, DPP webhook support (present), and
message-edit/suppress-embed calls (present).

- [x] Keep
- [ ] Skip
- [ ] Later

> This is a very important feature and is the most used one of this bot. It unfortunately has a lot of issues that need to be resolved so i belive a redesign is required. Here is an incomplete list of issues im aware of in no particular order:
> 
> - The command interface for managing the link replacement URLs is clunky, annoying, and missing functinoality. For example, theres no edit option, only remove and re-add. Not sure on how to improve this currently, will need some discussion.
> 
> - Sending a link wrapped in a spoiler, ||like this||, does not preserve the spoiler when the makes its replacement msg.
>
> - Sending multiple links in a single msg either breaks the bot, or only replaces the first link and ignores the rest.
>
> - the logic that checks that the embed worked is not reliable at all and sometimes thinks it fails if the network is being slow or smthin. Ideally needs a new implementation thats a bit more sophisticated.  
>
> I also have a few enhancements i want to make to this feature. 
> The bot should keep track of the reactions added to the link replacement messages and the user that posted the original link. This will let us have statistics of who gets the most reactions and what reactions are most popular and etc. 
> The bot should also add /en to embbeder urls that support it to auto translate things into english.
> 
> We can drop the webhook mode as we ended up not liking it and prefering the default mode.
---

## 9. Reaction-triggered message refresh — 🟢 Low (possibly drop)

**What it does:** If a 🔁 reaction is added to a message from a bot named
literally `"riggbot"` (a different bot, not LatiBot itself — the code has a
`TODO: replace riggbot with latibot`), it re-issues an edit with identical
content (a cheap trick to force Discord to re-render/refresh an embed) and
removes the reaction.

**Files:** `ReactionListener.java`.

**Porting notes:** Tiny, but only useful if "riggbot" is still a bot present
in your server, or you fix the noted TODO to target LatiBot's own messages
instead (which would actually make this generally useful, e.g. for
retriggering the URL-replacement embed).

- [ ] Keep as-is (targeting "riggbot")
- [ ] Keep, fixed to target LatiBot's own messages
- [x] Skip
- [ ] Later

> This was for testing the other bot (which does still exist but no longer interfaces with latibot like when this was relevnat), can be dropped.

---

## 10. Scheduled midnight message — 🟢 Low

**What it does:** Every day at 00:00:05 America/Chicago, sends a silent
"midnight" message to a hardcoded channel ID (same as the LATV guild ID).

**Files:** `MidnightManager.java`.

**Porting notes:** Trivial with a background thread computing the delay to
next midnight, or DPP's timer utilities. The hardcoded channel ID is worth
turning into a config value while porting rather than keeping hardcoded,
unless there's a reason to keep it as-is.

- [X] Keep
- [ ] Skip
- [ ] Later

> This is a personal favorite feature. I want to update it to have a command to: manage the channel it sends the msg to; set the midnight message content per timezone; and set the timezone to use for midnight with the option to say potentially different things for different timezones. 
> One issue the previous implementation has is seemingly randomly, the bot will send the midnight message at a random time of day once, then go back to noraml for a while. No idea why that happens.

---

## 11. Basic bot/utility commands — 🟢 Low

- `/ping` — latency check, replies then edits with round-trip ms.
- `/say <message> [reply_to_message_id]` — bot sends an arbitrary message,
  optionally as a reply to a specific message ID (admin-gated).
- `/shutdown` — gracefully stops TTS/voice, then exits the process.
- `/join [user]` / `/leave` — voice-channel connect/move/disconnect, with or
  without targeting a specific user's channel.
- `/status <text> [type]` — sets the bot's Discord presence/activity
  (playing/watching/listening/competing/custom).

**Files:** `PingCmd.java`, `SayCmd.java`, `ShutdownCmd.java`, `JoinVoiceCmd.java`,
`LeaveVoiceCmd.java`, `StatusCmd.java`.

**Porting notes:** All direct 1:1 mappings onto DPP APIs, no open questions.
`/join`/`/leave` do depend on whatever voice-connection plumbing gets built
for §1, but are otherwise trivial.

- [X] Keep all
- [ ] Keep, except: ______
- [ ] Later

> Fine to port as is.
> One addition i want to make is to have the bot shut down if a user with the administrator permission in the server says "say goodbye latibot". This is for the comedic value mostly, but should be functional without a slash command.

---

## 12. Startup permission/guild check — 🟢 Low (config decision)

**What it does:** On startup, hard-checks the bot has a specific list of
Discord permissions **in one specific hardcoded guild** (LATV,
`142409638556467200`) and exits with an error if not. This bakes in the
assumption the bot only ever runs in that one server.

**Porting notes:** Worth deciding now whether the C++ version should keep
this single-guild assumption (simplest, matches current reality) or
generalize to "check perms in every guild it's in" — doesn't affect other
features either way, just flagging it's a hardcoded value that'll need to
live somewhere (config file/env var) rather than in source, especially since
this repo may end up more widely visible than the original.

- [ ] Keep single-guild check (move guild ID to config)
- [x] Generalize to all guilds
- [ ] Drop the check entirely

> This bot is very very likely still going to be used in only a single guild, but i want to generalize it for the flexibility if that changes. The original idea with the permission check was that each command/feature would have a known set of permissions it requires in order to for the bot to function, and part of the bot initialization would check against the known permissions it needs and give an error if it doesnt have the permissions its expecting. Not the most important feature but still useful. 

---

## Not carried forward (dead/unused in the current Java version)

These exist in the source tree but aren't wired up to anything — flagging so
they don't get ported by accident, unless you actually want them built out:

- **`ApiDriver.java` (OpenAI ChatGPT integration):** fully implemented
  (`simple-openai` client, a canned "terse opinionated" system prompt) but its
  `init()` call is commented out in `LatiBot.main`, and no command actually
  calls `ApiDriver.ask(...)`. `ChatTestCmd` (`/chat`) sounds like it should use
  this but actually does DECtalk TTS instead (see §3) — the naming is
  misleading. If you want an actual chat/AI feature in the C++ version, it'd
  need to be designed fresh rather than ported, since the Java version was
  never finished/wired up.
- **`YesNoAnswers.txt`:** a weighted list of yes/no/maybe joke responses, not
  referenced by any code — looks like data prepared for a planned (never
  built) "ask the bot a yes/no question" command. The Java file's own TODO
  comments mention "Wordle stats," "reminders," "random sound effects," "fake
  quote cmd" as other never-built ideas, in case any of those are actually
  wanted now.

> The only feature from this that section that matters is the llm integration. It will need to be fully reimplemented and support either openai or cluade api requests. I want to also allow the llm to use the DECTalk voice synthesis to "speak" in voice channels. It should also allow the bot to respond when addressed, and potentiall respond automatically (optionally with a random chance) to certain trigger words.

> I also have a new simple feature that i want to add. The bot should have a few trigger words and phrases that it will respond to with a preset message. For example, if a message contains 420, the bot will say "nice." and etc.

---

## Data files to carry over as-is

- `nicknames.json` — existing nickname history, format described in §4.
- `UrlReplacements.txt` — existing domain replacement rules, format described
  in §8.
- `dic_*.txt`, `dtalk_us.dic`, `DECtalk.dll`/`.lib`, `DecTalkWrapper.*` — DECtalk
  runtime/dictionary files, needed as-is if §2/§3 are kept.
- `token.txt` / `openai_key.txt` (under `src/main/resources/`) — **credentials,
  not inspected as part of this analysis.** Whatever config mechanism the C++
  bot uses (env var, `.env`, config file — worth deciding, but out of scope for
  this plan) should read the bot token the same way, and these files should
  never be committed.

---

## Suggested next step

> you will need to update this seciton most likely as my comments may change this.

Once you've marked up the checkboxes above, the natural build order is:
**§11 → §4 → §9/§10 → §8 → §6/§7 → §5 → §1 (music) → §2/§3 (TTS)**, roughly
easiest/most-foundational to hardest, with the music-playback architecture
decision (§1) made as early as possible since it gates §2/§3/§11's
join/leave commands too. Happy to re-order based on what you actually care
about most, though.
