# Voice channels

How the bot gets into a voice channel, what plays there, and how it leaves.
`/join` and `/leave` move it in and out. A **voice session** (`/voice start`)
ties it to a text channel, so `/speak` from anywhere in the server and the
language model's replies in that channel are heard there. The bot also
**leaves on its own** once it has been alone for a while. What it says is
[Speech.md](Speech.md).

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](README.md#join--leave) has
`/join`, `/leave` and [`/voice`](README.md#voice), with every reply.
[Classes.md §9](../architecture/Classes.md#9-speech) draws the classes.

| | |
|---|---|
| **Module** | `voice`, built when dectalk or music is: [its README](../../src/modules/voice/README.md) lists what it owns |
| **Code** | `src/modules/voice/`: `include/voice/{services,voice_mixer,voice_output,pcm,voice_sessions,voice_state}.hpp`, `src/{join_command,voice_command,dpp_voice_output,module}.*`; speech's queue is dectalk's (`src/modules/dectalk/src/speech_queue.*`) |
| **Tests** | `src/modules/voice/tests/` (`latibot_voice_tests`), `tests/mocks/mock_voice.hpp` |
| **Tables** | `voice_grace_seconds` per server in `guild_settings` |
| **Plan** | Replaces plan §13, and the voice half of §6 |
| **Status** | `/join` and `/leave` built in phase 1; sessions, the queue and auto-leave in phase 4 (2026-09-26). **Not yet run in Discord**: see §5 |

## Contents

1. [Intent](#1-intent)
2. [Behaviour](#2-behaviour)
3. [How it works](#3-how-it-works)
4. [Decisions](#4-decisions)
5. [Limits, and what is still to check](#5-limits-and-what-is-still-to-check)

## 1. Intent

In the Java bot, speech was a `.wav` queued as a music track on LavaPlayer,
and the bot stayed in a channel until told to leave. The port keeps speech
and [music](Music.md) apart: both go through a **mixer**, which is the only
thing that writes to the connection, and speech always comes first. A voice session exists so that a conversation with
the bot can happen in text and be heard in voice, without anyone having to
be in the right text channel to use `/speak`. Auto-leave exists because a
bot sitting alone in a channel for hours looks broken.

## 2. Behaviour

### 2.1 `/join [user]` and `/leave`

Speak, by default; the bot needs Connect and Speak.

- `/join` joins **your** channel, or with `user`, theirs. If the bot is
  already elsewhere in the server it **moves** rather than refusing.
- `ok joining @name`, `ok moving to @name` and `ok bye` are **public** and
  silent, as in the Java bot: the room sees the bot come and go. The name is
  a mention sent with mentions off, so it pings nobody.
- The refusals are private: already there, the target not in voice, or the
  gateway unavailable.

### 2.2 Voice sessions

| Subcommand | Who | Does |
|---|---|---|
| `/voice start` | Speak | Joins **your** voice channel (moving if needed) and ties the session to the text channel it was run in |
| `/voice stop` | Speak | Ends the session and leaves |
| `/voice grace [seconds]` | Speak to see; Manage Server to change | How long the bot stays once alone, 0–600 s, 30 by default |

While a session lasts:

- `/speak` from **any** channel in the server speaks in the session's voice
  channel;
- the [language model](Language_Model.md#25-replies-in-a-voice-session)'s
  replies in the session's **text** channel are spoken as well as posted.

One session per server. It ends on `/voice stop`, `/leave`, a disconnect, or
auto-leave. `start` and `stop` answer publicly and silently; the rest
privately.

### 2.3 Leaving an empty channel

Once no **people** are left in the bot's channel, it waits the server's
grace period, then leaves. Other bots do not count as company, as far as
DPP's cache knows who is a bot. It applies however the bot got there: by
`/join`, `/speak` or `/voice start`. Grace 0 leaves at once. Someone who
drops out and rejoins within the grace does not lose the session.

**Leaving by any route** ends the server's voice session and drops whatever
it was about to say. That includes a moderator disconnecting the bot, since
the shell sees the bot's own voice state change, whatever caused it.

## 3. How it works

**One connection per server.** DPP gives one `discord_voice_client` per
guild. `discord/voice_state` answers "which channel is this member in" from
the cache, and `plan_join(target, bot_channel)` decides connect, move,
already there, or nobody to follow, as plain data.

**The mixer.** `audio::voice_mixer` sits between everything that plays and
DPP's connection. Music is fed to it a few seconds at a time; when speech
arrives, the music not yet heard is taken back and the speech plays at once,
and the music resumes afterwards from exactly where it stopped. DPP keeps one
queue per connection, so it only ever holds one of the two.
[Music.md §4.2](Music.md#42-the-mixer-and-why-pause_audio-cannot-do-this)
has the detail. Towards speech, the mixer is a `voice_output`, so the speech
queue below did not change when music arrived.

**The speech queue.** `audio::speech_queue` holds each server's utterances,
behind the `voice_output` port (the mixer in the bot, `mock_voice` in
tests):

- An utterance is queued on the connection **whole**, followed by a DPP
  **track marker** naming it. Playback reports each marker as it passes, so
  the queue knows which utterance finished and whose is playing now, which
  `/tts stop`'s permission check needs.
- `skip` drops exactly one, with `skip_to_next_marker`.
- A connection still being set up **holds** the audio (`waiting`), and
  `on_ready` plays it once DPP says the voice connection is ready.
- **Tickets.** `/speak` takes a ticket from the queue **before**
  synthesizing. A `stop` makes older tickets stale, so speech still being
  made when `/tts stop` arrives is dropped when it is done, rather than
  played.
- `forget` clears a server whose connection went away, without asking the
  connection to stop.
- **Leaving** also empties the server's music queue, and the mixer forgets
  it.

**Sessions** are `events::voice_sessions`, in memory, at most one per
server: `{guild, voice_channel, text_channel, started_by}`. `moved` follows
the bot to another channel in the server.

**Auto-leave** is `events::auto_leave`. The shell calls
`observe(guild, in_voice, humans)` whenever voice states change, which
records when the bot was **first** seen alone. A **5 s** timer asks
`due(grace_for)` which servers have been alone past their grace, and leaves
those. Each is returned once and forgotten, and being seen alone again does
not restart the clock.

**Grace** is `voice_grace_seconds` in `guild_settings`, clamped to 0–600
when read.

All three are thread-safe: commands, DPP's voice events and timers all
reach them.

## 4. Decisions

| Date | Decision | Why |
|---|---|---|
| plan v4 | `/join` and `/leave` stay real without music | Speech needs them |
| plan v4 | `/join` moves rather than refusing when the bot is elsewhere in the server | Asking it to come is asking it to come |
| plan v4 | A session ties a voice channel to a text channel; one per server | `/speak` from anywhere, and model replies heard |
| plan v4 | Auto-leave after a 30 s grace, per server; people only | Long enough that dropping out and back does not lose it |
| 2026-09-25 | `/join`, `/leave` public; `/join` names whom it followed, with mentions off (cleanup decision 1) | The room sees the bot arrive; Java said who |
| 2026-09-26 | Utterances queued whole with a track marker each; no mixer yet | Markers give per-utterance finish and skip; a mixer would be an abstraction with one user until music (plan §21.5) |
| 2026-09-26 | Tickets taken before synthesizing | `/tts stop` must also stop what is still being made |
| 2026-09-26 | Auto-leave applies after `/join` and `/speak`, not only sessions | The bot should never sit alone in a channel |
| 2026-09-26 | Leaving by any route ends the session and drops speech | The bot's own voice state is the one signal every route shares |
| 2026-09-30 | A mixer between speech, music and the connection; leaving also empties the music queue | Music arrived; DPP's one queue per connection cannot hold both (Music.md §4.2) |

## 5. Limits, and what is still to check

- Sessions and grace timers live in memory. A restart leaves the voice
  channel and forgets the session.
- The wiring is untested by design: that DPP's voice-ready, track-marker and
  voice-state events reach the queue and sessions, that the bot's own voice
  state ends a session, and that `dpp_voice_output` queues audio DPP plays.
- **Still to check in Discord:** joining and playing, with the ready event
  flushing what waited; skip and stop; auto-leave after the grace; a
  moderator's disconnect ending the session.
