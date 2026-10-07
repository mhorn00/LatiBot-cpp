# Speech

The bot speaks in **DECtalk**'s voice, the 1980s synthesizer people know
from Moonbase Alpha, inline commands and all. `/speak` says something in a
voice channel, `/chat` answers with a Discord voice message, `/tts` stops
or skips speech and sets its limits, and a server can build and save
**custom voices** in the voice lab. How the bot gets into and out of a
voice channel is [Voice_Channels.md](../../voice/docs/Voice_Channels.md).

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](../../../../docs/User_Guide.md#speak) has the
commands and replies.

| | |
|---|---|
| **Module** | `dectalk`, which requires voice: [its README](../README.md) lists what it owns |
| **Code** | `src/modules/dectalk/src/`: `{dectalk_engine,dectalk_sanitizer,dectalk_speech,speech_queue,voice_params,voice_store,wav}.*`, `{speak_command,chat_command,voice_lab}.*`, `module.cpp`; `cmake/dectalk.cmake`, `cmake/dectalk_zeroed_heap.h`; PCM is voice's |
| **Tests** | `src/modules/dectalk/tests/` (`latibot_dectalk_tests`), `tests/golden/dectalk.txt`, `tests/fuzz/fuzz_dectalk_sanitizer.cpp` |
| **Tables** | `tts_voices`, the module's schema version 1 (was migration 10); `tts_max_characters` and `tts_max_seconds` per server in `guild_settings` |
| **Config** | `trusted_users` and `trusted_guilds` in `config.json` |
| **Plan** | Replaces plan §2.2, §2.4, §12, §21.16 and §21.17 |
| **Status** | Built in phase 4 (2026-09-26). **Not yet run in Discord**: see §6 |

## Contents

1. [Intent](#1-intent)
2. [Behaviour](#2-behaviour)
3. [Custom voices and the voice lab](#3-custom-voices-and-the-voice-lab)
4. [How it works](#4-how-it-works)
5. [Decisions](#5-decisions)
6. [Limits, and what is still to check](#6-limits-and-what-is-still-to-check)

## 1. Intent

The Java bot spoke through DECtalk too, through a JNI shim. It wrote every
utterance to a `.wav` file and queued it as a music track. Its dictionary
never loaded, since DECtalk looked for it relative to whatever the working
directory was, and one person's `[:rate 75]` carried into the next person's
speech. It had no volume control ("make dectalk louder lol"). Its test
command `/chat` sent a voice message whose waveform was noise.

The port calls the DLL directly and keeps audio in memory end to end. Every
utterance starts from DECtalk's defaults, and the inline commands that
touch the host machine are **for trusted users only**. Being an
administrator of whichever server the bot was added to is not enough: that
would let a stranger's server play any `.wav` on the host.

## 2. Behaviour

### 2.1 `/speak text [voice] [rate] [volume]`

Speak, by default. It speaks where the bot already is: a voice session's
channel, or wherever `/join` put it. So it works from any text channel in
the server. If the bot is not in voice, it joins **your** channel first, as
the Java bot did. [Music](../../music/docs/Music.md) playing there stops for the speech and
carries on afterwards from where it was.

- `voice`: the ten built-in voices (Paul, Betty, Harry, Frank, Dennis, Kit,
  Ursula, Rita, Wendy, Val), then this server's custom voices. Paul when left
  out.
- `rate`: 75–600 words a minute, 200 by default.
- `volume`: 0–200 %, a gain applied to the samples.
- The answer is private: `ok`, or `ok, but it's cut off at 60s (this
  server's limit)`, or a refusal.

### 2.2 Inline commands, and who may use which

DECtalk's inline commands work: `[:rate 120]`, `[:dv ap 200]`,
`[:tone 440 500]`, `[:dial 555]`, `[:phoneme on]` and the rest. Some are
removed **silently**, depending on who is asking; the rest of the text is
spoken.

| Command | Anyone | Trusted | The language model |
|---|---|---|---|
| `[:play]`, `[:log]`, `[:debug]`, `[:loadv]`, `[:setv]` | removed | kept | removed |
| `[:pause]`, `[:resume]` | removed | removed | removed |
| `save` in `[:dv … save]` | removed | removed | removed |
| everything else | kept | kept | kept |

What the first row does, from DECtalk's source:

| Command | Does |
|---|---|
| `[:play "path"]` | opens **any** `.wav` on the host and plays it |
| `[:log …]` | writes `log.txt` or `dbglog.txt` into the working directory |
| `[:debug n]` | engine debug flags, printed to the bot's stdout |
| `[:loadv n]` / `[:setv n]` | store and replay a command macro; the source says `loadv` "will probably crash and burn if a flush happens in the middle", and `/tts stop` flushes |

**Trusted** means a user in `trusted_users`, or a user with Administrator in
a server listed in `trusted_guilds`. **Model output is never trusted**,
whoever asked, so a prompt-injected message cannot make it write
`[:play "C:\…"]`.

`[:pause]` pauses a sound card the bot does not have, so all it does is hold
the engine, and everyone's speech behind it, for the time given. `save`
would make voice edits permanent on the engine; custom voices are stored by
the bot instead (§3).

Commands are recognised the way DECtalk recognises them: in any case, by
any unique prefix (`[:PLA "x"]` is `[:play]`), several to a bracket
(`[:rate 200 :play "x"]`), with spaces and extra `[` allowed before the `:`.

### 2.3 Limits

| Limit | Default | Range | Set with |
|---|---|---|---|
| Characters per `/speak` or `/chat` | 1,000 | up to 4,000 | `/tts limits characters:` |
| Seconds of audio per utterance | 60 | up to 600 | `/tts limits seconds:` |
| Wall time to make one utterance | 10 s | — | a constant |

Past the character limit, the text is refused. Past the duration, the audio
is cut off and the reply says so. The wall-time limit covers what the
duration cannot: commands that wait instead of making audio, and a
`[:tone]` long enough to stall the engine.

### 2.4 `/tts`

| Subcommand | Who | Does |
|---|---|---|
| `stop` | whoever asked for what is playing, an administrator, or a trusted user | Silences the bot and drops everything waiting, **including speech still being made**. Music is not touched: it carries on once speech is over ([Music.md §3.3](../../music/docs/Music.md#33-speech-and-music-together)) |
| `skip` | the same | Drops only what is being said now |
| `limits [characters] [seconds]` | Speak to see; Manage Server to change | Shows or changes §2.3's limits |
| `voices lab [voice]`, `voices list`, `voices delete voice` | Speak; replacing or deleting a voice is for whoever made it, or an administrator | The custom voices (§3) |

### 2.5 `/chat text [voice] [rate]`

Says something as a Discord **voice message**, the kind with a play button
and a waveform, with no voice channel involved. The voice message is the
answer to the command, public and silent, as in the Java bot. Inline
commands and limits work as for `/speak`, and **phoneme input is on**
(`[:phoneme arpabet speak on]`), as it was in the Java bot's `/chat`, so
`[hxeh'low]` is spoken as phonemes. The waveform is the audio's own.

## 3. Custom voices and the voice lab

A custom voice is a built-in voice plus DECtalk `[:dv]` edits: an ordered
list of `parameter value` pairs over the **29** parameters this build's
synthesizer reads, each clamped to DECtalk's own limits from
`ph/ph_vdefi.c`. So a voice is stored as `base_voice` plus text such as
`ap 200 pr 150`.

- **Names** are 1–32 lowercase letters, digits, `-` and `_`, and never a
  built-in voice's name. A server keeps up to **100**.
- **Anyone can create one.** Replacing or deleting one is for whoever made
  it, or an administrator.
- `/tts voices list` shows them with what each is built on and who made
  it. `/tts voices delete voice` removes one.
- A saved voice's edits are **read back and written out again** when speech
  is built, never pasted in, since that part of the text is not sanitized.

**`/tts voices lab [voice]`** opens a private panel for building one:

- The first line says what is being edited (a new voice, or a saved one)
  and whether it has **unsaved changes**.
- The edits, grouped: Pitch, Character, Breath, Formants, Parallel formants
  and tilt, Source gains, Formant gains. Each group has at most five
  parameters, a modal's limit, and a menu opens each group's form. An empty
  box goes back to the base voice's value, and a value out of range is
  clamped and the panel says so.
- **As [:dv] text** opens the whole voice as text, to copy or paste.
- **Built on** picks the base voice.
- **Open a saved voice** lists this server's first 25, always including the
  one being edited.
- **▶ Test** says "Hello! This is how I sound now. What do you think?" in
  the voice channel, joining yours if needed.
- **Save as…** or **Save…** keeps it; **New voice** starts again from Paul.

The draft is kept per person for **30 minutes** after it was last touched,
so closing the panel by accident loses nothing.

## 4. How it works

### 4.1 The engine

`audio::dectalk_engine` implements the `tts_engine` port. **Each utterance
gets an engine of its own**, started for it and shut down after it, about
45 ms in all:

1. `TextToSpeechStartupExFonix`, with an **absolute** path to
   `dtalk_us.dic` beside `dectalk.dll`, so neither the registry nor the
   working directory matters. The file is checked first, because a failed
   dictionary load breaks every later start in the process.
2. `TextToSpeechOpenInMemory(WAVE_FORMAT_1M16)`: 11,025 Hz mono, 16-bit.
3. Four buffers of 8,192 samples, queued with `AddBuffer` and on our own
   `std::deque`.
4. `TextToSpeechSpeak(preamble + text, TTS_FORCE)`. The preamble selects the
   voice, the rate and a custom voice's `[:dv]` edits.
5. On each buffer message: take the **front** buffer from the deque, keep
   its samples, and requeue it from inside the callback. The callback's
   buffer pointer is 32 bits on a 64-bit build, so it is only compared with
   the low 32 bits of the front buffer, as a sanity check.
6. `TextToSpeechSync` on a thread of its own, while the worker watches the
   duration and wall-time limits; `ReturnBuffer` hands back the part-filled
   tail.
7. Close and shut the engine down. Trailing silence is cut to 50 ms (DECtalk
   ends everything with about 400 ms), and the volume is applied.

The callback's instance parameter is 32 bits too, so it is not a pointer.
There is only ever one utterance in progress, and the callback reaches it
through a static.

**One worker thread** takes requests in order and completes each as a DPP
promise, so commands `co_await` without blocking DPP's threads. The code
after the `co_await` runs on the worker until it next suspends, which is why
callers only hand the audio on and reply. `stop()` fails everything queued
before it, and the worker itself completes every request, so no coroutine
resumes on the caller's thread.

**Past the duration cap** the engine is **kept fed** and the audio thrown
away, because an engine starved of buffers can never be shut down.

**Resampling.** `audio::to_discord` converts 11,025 Hz mono to 48 kHz stereo
by linear interpolation, with the fractional positions kept exactly as
integers, writing each sample twice. ffmpeg is not needed for speech.

### 4.2 The build

`cmake/dectalk.cmake` builds the untouched submodule: the `dectalk` DLL (86
C files, exports from `DECTALK.DEF`, linking `winmm`), the `dectalk_dic`
host tool, and a step compiling `dtalk_us.dic` into the folder the DLL lands
in. It is built with `/W0` and none of our warnings. Every DECtalk source is
compiled with `cmake/dectalk_zeroed_heap.h` force-included, which turns
`malloc` into `calloc` and `realloc` into `_recalloc`. DECtalk reads heap
memory it never wrote, and without that a Release build does not say the
same thing twice (§5, 2026-09-26).

DECtalk is proprietary Fonix code, and the repository is public. As a
submodule, it is referenced and never redistributed.

### 4.3 The sanitizer

`audio::sanitize_speech(text, trust)` **rebuilds** the text rather than
editing it:

- plain text, with no `[` in it;
- phoneme brackets, with no `[` inside;
- each command it keeps, written out by itself as `[:name parameters]`, with
  parameters limited to characters that cannot open or close anything.

DECtalk only ever sees commands the sanitizer wrote. A bracket the sanitizer
reads differently from DECtalk can cost some text, but cannot let a command
through. Control characters go before anything is read. It is fuzzed against
a checker that reads the output the way DECtalk would, and removed commands
are logged at `debug`.

### 4.4 `/chat`'s voice message

DPP can read voice messages but has no way to send one. So `/chat`
synthesizes, wraps the audio in a 44-byte WAV header, and computes:

- the duration;
- a waveform: the peak absolute sample in each of 256 buckets, normalised to
  0–255 and base64-encoded.

It then answers the interaction through `post_rest_multipart`, with a
hand-built `payload_json` carrying `flags: 8192` and the attachment's
`duration_secs` and `waveform`. It answers directly rather than deferring,
so the voice message is the reply itself, and no temporary file is written.

The Java waveform summed the **raw signed bytes of the file**, header
included, into an average. Sixteen-bit samples are roughly symmetric around
zero, so every bucket averaged to about zero, and the shape Discord showed
meant nothing.

### 4.5 Storage

`tts_voices(guild_id, name, base_voice, params, created_by, updated_at)`,
keyed by `(guild_id, name)`. The limits are `guild_settings` rows, clamped
to their ranges when read. Lab drafts live only in memory.

## 5. Decisions

| Date | Decision | Why |
|---|---|---|
| plan v4 | Call the DLL directly; audio stays in memory | The in-memory API removes the `.wav` files and their cleanup |
| plan v4 | An absolute dictionary path through `TextToSpeechStartupExFonix` | DECtalk otherwise reads the path from the registry or the working directory, which is why the Java bot's dictionary never loaded |
| plan v4 | Host commands for trusted users and servers from `config.json` only; model output never | Administrator is per server, and the bot could be added anywhere |
| plan v4 | A denylist, and stripping is silent | DECtalk has many harmless commands worth keeping |
| plan v4 | Volume as a gain on the samples | `TextToSpeechSetVolume` only affects a sound card |
| plan v4 | The `/chat` waveform from peak samples over 256 buckets | The Java waveform was noise |
| plan v4 | Custom voices stored by the bot, replayed as `[:dv]` edits; anyone creates, creator or admin replaces | `[:dv save]` is engine state, and voices are a server's to share |
| 2026-09-26 | A fresh engine per utterance, and whole utterances rather than streaming | A spike showed 400× real time, and that reset leaves a memory engine silent (plan §21.16) |
| 2026-09-26 | `[:pause]` and `[:resume]` stripped for everyone, and a 10 s wall-time limit | Pause holds the engine without making audio, and a huge `[:tone]` stalls (plan §21.16) |
| 2026-09-26 | Keep feeding a capped engine and discard the audio | An engine starved of buffers cannot be shut down |
| 2026-09-26 | Check the dictionary exists before DECtalk sees the path | A failed load breaks every later start in the process |
| 2026-09-26 | Zero DECtalk's heap with a force-included header | Release builds said the same thing differently under load; the submodule stays untouched (plan §21.17) |
| 2026-09-26 | Trailing silence cut to 50 ms | DECtalk's 400 ms sat between queued utterances and at the end of every voice message |
| 2026-09-26 | The rebuilt-text sanitizer, fuzzed | DECtalk only runs commands the sanitizer wrote |
| 2026-09-26 | A missing dictionary's path goes to the log; the reply says only that it is missing | Engine errors are passed on to whoever asked, and this one named the host's folders |
| 2026-09-26 | `/chat` answers directly, as WAV | As in the Java bot; see §6 |
| 2026-09-29 | The lab shows and opens saved voices, and says whether the draft matches | A lab listing only the built-ins left that unclear (plan §21.21) |

## 6. Limits, and what is still to check

- One utterance is made at a time, bot-wide. At 400 times real time, that
  has not mattered.
- **Nothing here has run in real Discord yet.** In particular:
  - `/speak` joining and playing: the connection's ready event must flush
    what was waiting;
  - `/tts skip` and `stop`;
  - the voice lab's ▶ Test and forms, and whether a group menu stays stuck
    on the picked option after its form is cancelled (plan §21.21);
  - `/chat` showing as a voice message with its waveform. It sends WAV, as
    the Java bot did, and answers within Discord's three seconds. If Discord
    refuses WAV, the fallback is Ogg Opus, and opus is already linked through
    DPP.
