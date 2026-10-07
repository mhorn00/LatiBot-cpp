# What LatiBot does

Everything the bot does today, as it actually behaves — commands, options, who
may run them, what it replies, and the edge cases each one handles. Features not
built yet are in [Planned.md](Planned.md), written the same way.

This guide says how to use each feature. **Each feature's spec** says what it
is for, how it behaves and is built, and what was decided and why. The specs
are the documents to correct: a change agreed there is what the
implementation, and then this guide, follow. They are listed in
[Feature specs](#feature-specs) below, and in the last column of the table.
They take over from
[docs/porting/Porting_Plan_Final.md](../porting/Porting_Plan_Final.md), which
stays as the record of how the port was designed.

---

## At a glance

| | Feature | What it is for | Module | Spec |
|---|---|---|---|---|
| 💬 | [`/ping`](#ping) | Is the bot alive, and how far away is it | the core | [Basic commands](Basic_Commands.md) |
| 💬 | [`/say`](#say) | Post as the bot, optionally as a reply | the core | [Basic commands](Basic_Commands.md) |
| 💬 | [`/status`](#status) | Set the bot's presence | the core | [Basic commands](Basic_Commands.md) |
| 🔊 | [`/join`, `/leave`](#join--leave) | Move the bot in and out of a voice channel | [voice](../../src/modules/voice/README.md) | [Voice channels](Voice_Channels.md) |
| 🔊 | [`/speak`](#speak) | Say something in the voice channel, in DECtalk's voice | [dectalk](../../src/modules/dectalk/README.md) | [Speech](Speech.md) |
| 🔊 | [`/tts`](#tts) | Stop or skip speech, set its limits, and keep custom voices from the [voice lab](#voice-lab) | [dectalk](../../src/modules/dectalk/README.md) | [Speech](Speech.md), [Speech §3](Speech.md#3-custom-voices-and-the-voice-lab) |
| 🔊 | [`/voice`](#voice) | Voice sessions, and how long the bot stays once alone | [voice](../../src/modules/voice/README.md) | [Voice channels](Voice_Channels.md) |
| 🔊 | [`/chat`](#chat) | Say something as a voice message | [dectalk](../../src/modules/dectalk/README.md) | [Speech](Speech.md) |
| 🎵 | [`/music`, `/m`](#music) | Play music from a link, with a queue | [music](../../src/modules/music/README.md) | [Music](Music.md) |
| 🛑 | [`/shutdown`](#shutdown) | Stop the bot | the core | [Basic commands](Basic_Commands.md) |
| 🛑 | [`/goodbye`](#goodbye) | Configure the phrase that stops the bot | the core | [Basic commands](Basic_Commands.md) |
| 📋 | [`/logs`](#logs) | Post the bot's log in one channel | the core | [Log channel](Log_Channel.md) |
| 🗣 | [`/trigger`](#trigger) | Manage automatic replies to phrases | [triggers](../../src/modules/triggers/README.md) | [Triggers](Triggers.md) |
| 🤖 | [`/bots`](#bots) | Choose which other bots the bot may hear | the core | [Message pipeline](Message_Pipeline.md) |
| 🏷 | [`/nickname`](#nickname) | Change somebody's nickname, on the record | [nicknames](../../src/modules/nicknames/README.md) | [Nicknames](Nicknames.md) |
| 🏷 | [`/nicknames`](#nicknames) | Every nickname somebody has had here | [nicknames](../../src/modules/nicknames/README.md) | [Nicknames](Nicknames.md) |
| 🌙 | [`/midnight`](#midnight) | Post a message at midnight | [midnight](../../src/modules/midnight/README.md) | [Midnight](Midnight.md) |
| 🔗 | [`/links`](#links) | Turn link replacement on, and choose which links get posted again with a working preview | [links](../../src/modules/links/README.md) | [URL replacement](Url_Replacement.md) |
| 🔗 | [`/urltoggle`](#urltoggle) | Have your own links left alone | [links](../../src/modules/links/README.md) | [URL replacement](Url_Replacement.md) |
| 📊 | [`/linkstats`](#linkstats) | Who gets the most reactions on replaced links | [linkstats](../../src/modules/linkstats/README.md) | [Link stats](Link_Stats.md) |
| 🧠 | [`/llm`](#llm) | Turn the language model on, choose it, edit its personality, set its triggers | [llm](../../src/modules/llm/README.md) | [Language model](Language_Model.md) |
| 🧠 | [`/memory`](#memory) | What the language model remembers | [llm](../../src/modules/llm/README.md) | [Language model](Language_Model.md) |
| 🛑 | [The goodbye phrase](#the-goodbye-phrase) | Stop the bot by saying so, no slash command | — | [Basic commands](Basic_Commands.md#3-the-goodbye-phrase) |
| 🗣 | [Trigger responses](#trigger-responses) | The "420 → nice" behaviour, generalised | — | [Triggers](Triggers.md) |
| 🔗 | [URL replacement](#url-replacement) | Posts poor-preview links again on a mirror that previews properly, once a server turns it on | — | [URL replacement](Url_Replacement.md) |
| 📊 | [Reaction statistics](#reaction-statistics) | Counts reactions on those, three ways | — | [Link stats](Link_Stats.md) |
| 🏷 | [Nickname tracking](#nickname-tracking) | Records every nickname change, and who made it | — | [Nicknames](Nicknames.md) |
| 🌙 | [The midnight message](#the-midnight-message) | Posts once per local day, per timezone | — | [Midnight](Midnight.md) |
| 🔊 | [Leaving empty voice channels](#leaving-empty-voice-channels) | Never sits alone in a voice channel | — | [Voice channels](Voice_Channels.md#23-leaving-an-empty-channel) |
| 🧠 | [Talking to the bot](#talking-to-the-bot) | Answers when addressed, remembers, speaks in a voice session | — | [Language model](Language_Model.md) |
| 🔒 | [Permission warnings](#permission-warnings) | Says what it cannot do in a server, at startup | — | [Running the bot](Operations.md#6-permission-warnings) |

Commands reply **ephemerally** by default — only the person who ran it sees the
answer. The exceptions are called out below: `/say` posts a separate public
message; [`/nicknames`](#nicknames) and the [`/linkstats`](#linkstats) views
answer publicly, because those are things a room reads together;
[`/join`, `/leave`](#join--leave), [`/voice start` and `stop`](#voice) and
[`/shutdown`](#shutdown) answer publicly, because the room sees the bot come
and go and should see why; [`/music`](#music) answers publicly, since the
room hears it; and [`/chat`](#chat) answers with its voice message, which is
the point of it.

Each command decides this for three kinds of message, per subcommand where
they differ: its **result** (the answer it exists to give), a **refusal**
(a typo, a missing permission), and a **post** (an ordinary channel message
sent on its behalf, like what `/say` says). Refusals are always private.
Where that is set is described under [Behind the scenes](#behind-the-scenes).

If a command fails partway, it answers
`something went wrong on my end running that; it's in the log` rather than
leaving Discord to say *The application did not respond*. A command Discord
still offers after it was removed answers `i don't have that command any more`.

**Who may run what** is set as Discord's *default member permission*. Server
admins can override any of it per role or per channel in
**Server Settings → Integrations → LatiBot**, which is why these are defaults
rather than hard checks.

---

## Feature specs

One per feature. Each says what the feature is **for** (and, where the Java
bot had it, what went wrong there), how it **behaves**, how it is **built**,
the **decisions** behind it with when and why each was made, and what is
still open or unchecked in Discord. The header of each lists its code,
tests, tables, and the plan sections it replaces. Code comments cite specs
by file and section: `docs/features/Url_Replacement.md §2.4`. So keep a
spec's section numbers stable: add new sections after the existing ones, or
as subsections, rather than renumbering.

| Spec | Covers |
|---|---|
| [Basic commands](Basic_Commands.md) | `/ping`, `/say`, `/status`, `/shutdown`, `/goodbye` and the goodbye phrase |
| [Message pipeline](Message_Pipeline.md) | The stages every message passes through, who is heard, and `/bots` |
| [Triggers](Triggers.md) | `/trigger` and its panel, and the replies |
| [URL replacement](Url_Replacement.md) | `/links`, `/urltoggle`, the replacements, watching previews, Retry |
| [Link stats](Link_Stats.md) | `/linkstats`, reaction counting, the recompute, image posts, emoji copies |
| [Nicknames](Nicknames.md) | `/nickname`, `/nicknames`, tracking and attribution, the Java import |
| [Midnight](Midnight.md) | `/midnight` and the once-a-day post |
| [Speech](Speech.md) | DECtalk, `/speak`, `/tts`, `/chat`, the sanitizer, custom voices and the voice lab |
| [Voice channels](Voice_Channels.md) | `/join`, `/leave`, voice sessions, the speech queue, leaving empty channels |
| [Language model](Language_Model.md) | `/llm`, `/memory`, answering, memory, documents, advanced triggers, spend |
| [Log channel](Log_Channel.md) | `/logs` |
| [Commands and panels](Commands_and_Panels.md) | What every command and panel shares: registration, flags, `custom_id` state, forms, `--unregister-commands` |
| [Running the bot](Operations.md) | Startup, the command line, `config.json`, secrets, intents, the database, backups, permission warnings |
| [Music](Music.md) | `/music` and `/m`, the queue, yt-dlp and ffmpeg, and the mixer that pauses music for speech |

Features designed but not built are in [Planned.md](Planned.md).

---

## Commands

### `/ping`

Round-trip and gateway latency.

| | |
|---|---|
| **Module** | the core |
| **Options** | none |
| **Who** | everyone |
| **Where** | servers and DMs |
| **Bot needs** | nothing |

Replies `Pong!`, then edits that to
`Pong! (12 ms round trip, 40 ms gateway)`.

The two numbers answer different questions and both are worth having. **Round
trip** is how long a REST call to Discord and back took, measured around the
reply itself. **Gateway** is DPP's own websocket heartbeat measurement — the
latency of the persistent connection the bot receives events on. A healthy
gateway with a slow round trip means Discord's REST API is congested, not the
bot.

### `/say`

Posts a message as the bot.

| | |
|---|---|
| **Module** | the core |
| **Options** | `message` (required, 1–2000 characters) · `reply` (optional, a message id) |
| **Who** | Manage Messages, by default (the Java bot asked for Manage Roles) |
| **Where** | servers only |
| **Bot needs** | Send Messages, Read Message History |

The message is posted in the channel the command was run in. With `reply`, it is
posted as a reply to that message id.

| Situation | Reply |
|---|---|
| Normal | `ok`, then the message is posted |
| `message` is only whitespace | `that message is empty` |
| `reply` is not a number | `"abc" is not a message id` |
| `reply` names a message that is not in this channel, or was deleted | `couldn't find message 123 in this channel` |

The target message is **fetched before posting**. Without that, replying to a
deleted message or one from another channel fails at the API with nothing to
show the caller. Whitespace is rejected locally for the same reason: Discord's
own length limit does not catch `"   "`.

### `/status`

Sets the bot's presence.

| | |
|---|---|
| **Module** | the core |
| **Options** | `status` (required, 1–128 characters) · `type` (optional: Playing, Watching, Listening to, Competing in, Custom) |
| **Who** | Manage Nicknames, by default |
| **Where** | servers and DMs |
| **Bot needs** | nothing |

Replies `status set to: <text>`. Presence is global — the bot has one, across
every server it is in. It is **kept**, and set again whenever the bot connects,
so a restart or a dropped connection does not clear it.

`type` defaults to **Playing** when omitted or unrecognised, which is what the
Java version did. **Custom** is the odd one out: Discord takes a custom status's
text from a different field than the others and expects the activity itself to
be named "Custom Status", so a custom status shows just your text with no
"Playing" prefix.

### `/join` / `/leave`

Moves the bot in and out of a voice channel.

| | |
|---|---|
| **Module** | [voice](../../src/modules/voice/README.md) |
| **Options** | `/join`: `user` (optional — whose channel to join) |
| **Who** | Speak, by default |
| **Where** | servers only |
| **Bot needs** | Connect, and Speak for `/join` |

`/join` with no `user` joins the channel **you** are in; with one, it follows
that person. If the bot is already in a different channel in the same server it
moves rather than refusing.

| Situation | Reply |
|---|---|
| Joining | `ok joining @name`, naming whoever it followed |
| Already connected elsewhere in this server | `ok moving to @name` |
| Already in the right channel | `i'm already in your voice channel` / `i'm already in their voice channel` |
| The target is not in a voice channel | `you're not in a voice channel` / `they're not in a voice channel` |
| The gateway connection is unavailable | `i can't reach the gateway right now` |
| `/leave` when not connected | `i'm not in a voice channel` |
| `/leave` when connected | `ok bye` |

`ok joining`, `ok moving to` and `ok bye` are **public**, as they were in the Java
bot: the room sees the bot arrive and leave. The name is a mention sent with
mentions off, so it shows without pinging anyone. The refusals are private.

Once the bot has been alone in its channel for a while it leaves on its own;
see [Leaving empty voice channels](#leaving-empty-voice-channels).

### `/speak`

Says something in the voice channel, in DECtalk's voice.

| | |
|---|---|
| **Module** | [dectalk](../../src/modules/dectalk/README.md) |
| **Options** | `text` (required) · `voice` · `rate` (75–600 words a minute, 200 by default) · `volume` (0–200 %, 100 by default) |
| **Who** | Speak, by default |
| **Where** | servers only |
| **Bot needs** | Connect, Speak |

It speaks where the bot already is: a [voice session](#voice)'s channel, or
wherever `/join` put it, so `/speak` works from any text channel in the
server. If the bot is not in voice it joins **your** channel first, as the
Java bot did.

`voice` autocompletes DECtalk's ten built-in voices — Paul, Betty, Harry,
Frank, Dennis, Kit, Ursula, Rita, Wendy and Val — then this server's
[custom voices](#voice-lab).

**DECtalk's inline commands work**: `[:rate 120]`, `[:dv ap 200]`, `[:tone
440 500]`, `[:dial 555]`, `[:phoneme on]` and the rest. A few are removed
silently, depending on who is asking, and the rest of the text is spoken:

| Command | Anyone | Trusted |
|---|---|---|
| `[:play]`, `[:log]`, `[:debug]`, `[:loadv]`, `[:setv]` | removed | kept |
| `[:pause]`, `[:resume]`, and `save` in `[:dv … save]` | removed | removed |
| everything else | kept | kept |

The first row reads or writes files on the machine the bot runs on, so it is
for **trusted** users only: those listed in `trusted_users` in the bot's
config, or an administrator of a server listed in `trusted_guilds`. Being an
administrator somewhere the bot was added is not enough on its own. `[:pause]`
only pauses a sound card the bot does not use, so all it would do is hold
everyone else's speech up. Anything the language model writes is never
trusted, whoever asked.

Commands are recognised the way DECtalk recognises them, in any case and by
any unique prefix, so `[:PLA "x"]` counts as `[:play]`.

| Situation | Reply |
|---|---|
| Spoken | `ok` |
| Longer than this server's time limit | `ok, but it's cut off at 60s (this server's limit)` |
| Nothing but spaces | `there's nothing to say` |
| Longer than this server's character limit | `that's 1200 characters; this server's limit is 1000` |
| Nothing left once the commands you can't use are removed | `there's nothing left to say without the commands you can't use` |
| `voice` is neither a built-in nor a saved voice | `i don't know a voice called "robot"` |
| Neither the bot nor you are in voice | `i'm not in a voice channel, and neither are you` |
| `/tts stop` arrived while it was being made | `stopped before i got to it` |
| The speech engine failed | `couldn't say that: …` |

All of these are private. The limits are 1000 characters and 60 seconds by
default, per server; see [`/tts limits`](#tts). One person's `[:rate 75]` never
carries into the next person's speech: every utterance starts from DECtalk's
defaults.

### `/tts`

Stops or skips speech, sets the limits on it, and keeps this server's custom
voices.

| | |
|---|---|
| **Module** | [dectalk](../../src/modules/dectalk/README.md) |
| **Subcommands** | `stop` · `skip` · `limits` (`characters`, `seconds`) · `voices lab` (`voice`) · `voices list` · `voices delete` (`voice`) |
| **Who** | Speak, by default; changing the limits needs Manage Server |
| **Where** | servers only |
| **Bot needs** | Connect, Speak |

`stop` silences the bot and drops everything waiting to be said, including
speech still being made. `skip` drops only what is being said now. Either is
open to whoever asked for what is playing, an administrator, or a trusted
user.

`limits` with no options shows the server's limits; with `characters` (up to
4000) or `seconds` (up to 600) it changes them.

| Situation | Reply |
|---|---|
| `stop` | `stopped` |
| `skip` | `skipped` |
| Nothing is being said | `i'm not saying anything` |
| Somebody else's speech, and you are neither an admin nor trusted | `only whoever asked for this, or an admin, can stop it` |
| `limits`, shown | `/speak takes up to 1000 characters, and stops after 60s` |
| `limits`, changed | `/speak now takes up to 500 characters, and stops after 30s` |
| `limits` changed without Manage Server | `changing the limits needs Manage Server` |

All private. Before 2026-10, the custom voices were `/voice lab`, `/voice list`
and `/voice delete`.

#### Voice lab

`/tts voices lab` opens a private panel for building a custom voice: one of the
ten built-in voices plus DECtalk's `[:dv]` voice parameters. With `voice` it
starts from one of this server's saved voices; without, from where you left
off.

The panel's first line says what you are editing: a new voice not saved yet,
or a saved one, and then whether it still matches what is saved or has
**unsaved changes**. Under it is what has been changed, grouped (Pitch,
Character, Breath, Formants, Parallel formants and tilt, Source gains, Formant
gains), and the whole voice as inline commands, such as
`[:nh][:dv ap 200 pr 150]`, which can be pasted into any `/speak`. From it:

- **Open a saved voice** lists this server's saved voices (the first 25, and
  always the one being edited; `/tts voices lab voice:` opens any). Picking one
  replaces the draft, so picking the one you are editing throws away your
  changes to it. The menu only appears once the server has a saved voice.
- **Change a group of settings** opens a form for up to five of them, each
  with its range. An empty box goes back to the built-in voice's own value; a
  value out of range is brought into range, and the panel says so.
- **As [:dv] text** opens the whole voice as text, to copy, or to paste one in.
- **Built on** picks the built-in voice underneath.
- **▶ Test** says a test phrase in the voice channel, joining yours if the bot
  is not in one.
- **Save as…** (**Save…** once it has a name) keeps it under a name:
  lowercase letters, digits, `-` and `_`, up to 32 characters, and never a
  built-in voice's name. The name you opened it as is filled in; change it to
  keep a copy. `/speak voice:` then offers it.
- **New voice** starts again from Paul, with no name.

Your draft is kept for half an hour after you last touched it, so closing the
panel by accident loses nothing. Anyone may save a voice; replacing or
deleting one is for whoever made it, or an administrator. A server keeps up to
100.

`/tts voices list` shows the server's voices, what each is built on and who
made it, and `/tts voices delete` removes one:

| Situation | Reply |
|---|---|
| Deleted | ``deleted `robo` `` |
| No such voice | `this server has no voice called "robo"` |
| Somebody else's, and you are not an admin | ``only whoever made `robo`, or an admin, can change it`` |
| `list` with none saved | `this server has no custom voices yet; make one with /tts voices lab` |

### `/voice`

Voice sessions, and how long the bot stays once alone. Custom voices are
[`/tts voices`](#voice-lab).

| | |
|---|---|
| **Module** | [voice](../../src/modules/voice/README.md) |
| **Subcommands** | `start` · `stop` · `grace` (`seconds`) |
| **Who** | Speak, by default; changing `grace` needs Manage Server |
| **Where** | servers only |
| **Bot needs** | Connect, Speak |

`start` brings the bot into **your** voice channel, moving it if it is
elsewhere in the server, and ties the session to the text channel you ran it
in. While it lasts, `/speak` from anywhere in the server goes to that voice
channel, and the language model's replies in that text channel are spoken as
well as posted (see [Talking to the bot](#talking-to-the-bot)). `stop` ends the session and leaves. So does `/leave`, being
disconnected, or everyone leaving (see below).

| Situation | Reply |
|---|---|
| `start` | `ok, voice session started in #channel; /speak goes there from anywhere in the server` |
| `start` when you are not in voice | `you're not in a voice channel` |
| `stop` | `ok bye` |
| `stop` with no session and the bot not in voice | `there's no voice session to stop` |
| `grace`, shown | `once everyone has left, i stay 30s before leaving` |

`start` and `stop` answer publicly and silently, as `/join` and `/leave` do.
The rest answer privately.

### `/chat`

Says something as a Discord **voice message**, the kind with a play button and
a waveform. No voice channel is involved.

| | |
|---|---|
| **Module** | [dectalk](../../src/modules/dectalk/README.md) |
| **Options** | `text` (required) · `voice` · `rate` |
| **Who** | Speak, by default |
| **Where** | servers only |
| **Bot needs** | nothing |

The voice message is the answer to the command, public and silent, as it was
in the Java bot. Inline commands work as they do in `/speak`, with the same
limits and the same rules on who may use which, and phoneme input is on, so
`[hxeh'low]` is spoken as phonemes. Refusals are those of `/speak` and
private; one more, `couldn't send the voice message; it's in the log`, covers
Discord refusing the upload.

The waveform is the audio's own. The Java version averaged the raw bytes of
the whole file, header included, which comes out close to zero everywhere, so
the shape it showed meant nothing.

### `/music`

Plays music in the voice channel, from a link. `/m` is the same command, so
`/m play` works too. How it all works is in [Music.md](Music.md).

| | |
|---|---|
| **Module** | [music](../../src/modules/music/README.md) |
| **Subcommands** | `play` · `queue` · `nowplaying` · `pause` · `skip` · `repeat` · `shuffle` · `clear` · `stop` · `remove` · `volume` · `limit` |
| **Who** | Speak, by default, from anywhere in the server; changing `volume` or `limit` needs Manage Server |
| **Where** | servers only |
| **Bot needs** | Connect, Speak; and yt-dlp and ffmpeg on the machine it runs on |

**`play link [position]`** plays a link to a song, a video or a playlist,
from YouTube, SoundCloud, Bandcamp, a direct link to an audio file, or any of
the other sites yt-dlp knows. Only links: plain text is not searched for.
`position` is **At the end** (the default), **Next**, or **Now**, which plays
it at once and puts what was playing straight after it, to start over. The
bot plays wherever it already is; if it is in no voice channel it joins
yours, and you can queue music into its channel without being in it.

Age-restricted videos play only when whoever runs the bot has signed music
in to a YouTube account, with a Firefox profile or a cookies file
([Music.md §4.9](Music.md#49-signing-in-to-youtube)).
Then a video YouTube refuses signed out is quietly tried again signed in,
and only a second refusal is the reply; without an account, YouTube's
refusal is.

A playlist queues its first 100 tracks. The queue holds 500. A track longer
than this server's limit (an hour, unless `limit` says otherwise) is left
out; live streams have no limit and play until skipped.

| Subcommand | Does |
|---|---|
| `queue` | What is playing and how far in, then the queue, ten a page with ◀ / ▶ |
| `nowplaying` | The current track, its link, how far in, who queued it, and what is next |
| `pause` | Pauses, or carries on |
| `skip` | Skips to the next track, even when the track is repeating |
| `repeat [mode]` | **Off**, **This track** or **The whole queue**; left out, the next one in turn |
| `shuffle` | Shuffles the queue; the current track plays on |
| `clear` | Empties the queue; the current track plays on |
| `stop` | Stops the music and empties the queue |
| `remove position` | Takes the track at that number in `queue` out |
| `volume [percent]` | Shows the volume, or sets it, 0–200 %; 50 to begin with |
| `limit [minutes]` | Shows the longest a track may be, or sets it; 0 for no limit |

| Situation | Reply |
|---|---|
| Playing | `playing **Song** (3:20)` |
| Queued | `queued **Song** (3:20)`, or `playing **Song** (3:20) next` |
| A playlist | `queued 42 tracks from **Mix**`, and what was left out and why |
| Not a link | `that isn't a link; i only play links for now, starting with https://` |
| A link into a private network | `that link points into a private network, which i won't fetch from` |
| The site says no | `couldn't play that: ` and yt-dlp's reason |
| Neither the bot nor you are in voice | `i'm not in a voice channel, and neither are you` |
| yt-dlp or ffmpeg is missing | says which, and where to put it |
| A track fails while its turn comes | `couldn't play **Song**: ` and the reason, in the channel it was queued from; the next track plays |

Replies are public and silent; refusals are private. Nothing is posted when
a track starts: `nowplaying` says what is on.

**Speech comes first.** When the bot speaks, from `/speak`, the language
model in a voice session, or the voice lab's ▶ Test, the music stops at
once, the speech plays, and the music carries on from exactly where it was.
`/tts stop` and `/tts skip` stop speech, never the music.

**Leaving empties the queue**: `/leave`, `/voice stop`, being disconnected,
or leaving an empty channel. The queue is not kept across a restart either.

### `/shutdown`

Stops the bot.

| | |
|---|---|
| **Module** | the core |
| **Options** | none |
| **Who** | Administrator, by default |
| **Where** | servers only |
| **Bot needs** | nothing |

Replies `ok bye bye!`, publicly, and then shuts down. The reply is **awaited** rather than
queued, because the process is about to stop and an unanswered interaction shows
the caller an error instead of a goodbye. The shutdown is logged with the name
of whoever asked.

### `/goodbye`

Shows or changes [the goodbye phrase](#the-goodbye-phrase) for this server.

| | |
|---|---|
| **Module** | the core |
| **Options** | `phrase` (optional, 1–200 characters) · `off` (optional, true/false) |
| **Who** | Administrator, by default |
| **Where** | servers only |
| **Bot needs** | nothing |

| Used as | Effect |
|---|---|
| `/goodbye` | Shows the current phrase: `an administrator saying "say goodbye latibot" stops the bot` |
| `/goodbye phrase:<text>` | Sets it: `an administrator saying "<text>" now stops the bot` |
| `/goodbye off:true` | Turns it off: `the goodbye phrase is off; set one to turn it back on` |

The phrase is **per server**, and the default is `say goodbye latibot`.

Turning it off stores an empty phrase rather than deleting the setting. Deleting
it would fall back to the default the next time the bot read it, which is the
opposite of off.

### `/logs`

Posts the bot's own log in one Discord channel, as well as to its console.

| | |
|---|---|
| **Module** | the core |
| **Options** | see below |
| **Who** | only users in `trusted_users` in `config.json`; shown to Administrators, by default |
| **Where** | servers only |
| **Bot needs** | View Channel and Send Messages in the log channel |

| Subcommand | Options | Effect |
|---|---|---|
| `set` | `channel` (required, a text or announcement channel) · `level` (optional) | Posts a first message there, then `ok, my log goes to #channel from now on: info and above` |
| `level` | `level` (required) | `ok, my log channel gets debug and above from now on` |
| `off` | none | `ok, my log isn't posted anywhere now` |
| `show` | none | Where it goes, from which level, how many lines are waiting, and whether posting is failing |

`level` is one of `error`, `warn`, `info`, `debug` or `trace`, each taking
everything above it too. `set` keeps the current level when it is not given,
and uses `info` the first time.

There is **one log channel for the whole bot**, not one per server: `set`
from another server moves it there. That is why only `trusted_users` can use
it, and not the administrators of `trusted_guilds`: the log covers every
server the bot is in, so the channel's readers see all of it. Anyone else is
told `only the people listed in trusted_users in my config.json can choose
where my log goes, since it covers every server i'm in`.

The channel's level is its own, independent of the console's: a console at
`info` and a channel at `debug` each get their own share. Lines are gathered
and posted every two seconds, as few messages as fit, each a code block of
`12:34:56 [info] …` lines in UTC, silent and with no previews or mentions.

- **Bursts.** At most two messages go out each time, well inside Discord's
  limit of five a channel every five seconds. Up to 1000 lines wait; beyond
  that the newest are dropped and the next message says how many.
- **Posting fails** (the channel is deleted, or the bot loses access): what
  was being posted is lost, the console gets one warning, and the bot waits
  30 seconds before trying again, doubling each time up to 15 minutes. `show`
  gives the reason and when the next try is. Lines keep waiting meanwhile.
- **`set` checks first.** It posts its first message before it changes
  anything, so a channel the bot cannot post in is refused with Discord's
  reason and the old setting kept.
- **Secrets.** The bot's token and API keys are masked in anything posted,
  in case anything ever logs one.
- **`trace` is everything**: the content of every message the bot sees, in
  every server, and DPP's gateway traffic. It is a lot of messages, and it
  puts every server's conversations in the channel.
- The setting is kept across restarts. What was logged while starting up is
  posted once the bot has connected. Lines logged in the last moments before
  a shutdown may not make it.

### `/trigger`

Manages this server's automatic replies. See
[Trigger responses](#trigger-responses) for how they behave.

| | |
|---|---|
| **Module** | [triggers](../../src/modules/triggers/README.md) |
| **Who** | Manage Messages, by default |
| **Where** | servers only |
| **Bot needs** | Send Messages |

#### `/trigger add`

| Option | Required | Meaning |
|---|---|---|
| `pattern` | yes | The text to look for, 1–200 characters. Literal text, not a regular expression |
| `responses` | yes | One per line, up to 2000 characters. A line may start with `3 \| ` to weight it |
| `mode` | no | **Whole word** (default) or **Anywhere in the message** |
| `cooldown` | no | Seconds between replies in one channel, 0–86400. Default 30 |
| `bots` | no | Also answer allowed bots. Default off |
| `silent` | no | Reply without notifying anyone, what Discord calls @silent. Default on |
| `previews` | no | Show link previews in the reply. Default on |

Replies with the new trigger's id, for example *added trigger `4` for `420` with 2 responses*.

Refuses a pattern that is only whitespace (`a pattern of only whitespace would
match everything`) and responses that parse to nothing (`that leaves no
responses to pick from`) — a trigger that matches but has nothing to say is
worse than no trigger.

#### `/trigger edit`

Takes `id` (required, from `/trigger list`) plus any of the options above, and
`enabled` to turn one off without deleting it. **Every option is optional**: an
edit changes what was given and leaves the rest alone, so fixing a cooldown does
not mean retyping the responses. Replies with the updated trigger, or
*no trigger `7` in this server* if the id is wrong.

#### `/trigger remove`

Takes `id`. Replies *removed trigger `7`*, or says it does not exist. The
responses go with it.

#### `/trigger list`

Shows this server's triggers, 8 per page, with ◀ / ▶ buttons. Each line reads:

```
`4` **420** (whole word, 30s) -> 2 responses
`5` **69** (anywhere, no cooldown, disabled, answers bots, notifies, no previews) -> 1 response
```

Silent with previews is the default and goes unmentioned; `notifies` and
`no previews` appear only when a trigger differs.

#### `/trigger panel`

The same list with editing attached. Pick a trigger from the menu and these
buttons appear:

| Button | Does |
|---|---|
| **Edit** | Opens a form with the pattern, responses, mode and cooldown filled in |
| **Enable** / **Disable** | Toggles it. The label says what pressing it will do |
| **Answer bots** / **Ignore bots** | Toggles whether it replies to allowed bots |
| **Reply silently** / **Reply with notifications** | Toggles whether its replies notify anyone, on a row of their own |
| **Hide link previews** / **Show link previews** | Toggles the previews on links in its replies |
| **Delete** | Asks to confirm in the panel itself, so nothing is left behind if ignored |

**Add** opens the same form, empty.

The form's mode takes `word` or `anywhere` (`whole word`, `whole_word` and
`substring` work too). It keeps fields it cannot read rather than resetting
them, and says so under the list: typing "thirty" into the cooldown box leaves
the cooldown you had. A blank pattern or no responses is refused with an
explanation, and nothing is saved. After a save the panel shows the trigger
picked, on whichever page it landed.

The panel survives restarts and has nothing to expire, because which page and
which trigger is selected are carried in the buttons themselves rather than
remembered on the bot's side. It is ephemeral, so two people can each have their
own open.

### `/bots`

Chooses which other bots LatiBot may hear. See
[Trigger responses](#trigger-responses) for why this exists.

| | |
|---|---|
| **Module** | the core |
| **Who** | Manage Server, by default |
| **Where** | servers only |
| **Bot needs** | Send Messages |

| Subcommand | Options | Reply |
|---|---|---|
| `allow` | `bot` (required) | `ok, i'll listen to DiceBot now`, or `i was already listening to DiceBot` |
| `deny` | `bot` (required) | `ok, back to ignoring DiceBot`, or `i was not listening to DiceBot anyway` |
| `list` | none | The allowed bots, by name |

`allow` refuses a human — `X is not a bot, and i already hear everyone else` —
because storing one would look like it worked while doing nothing; the list is
only consulted for messages from bots. It also refuses LatiBot itself.

`list` shows names where the bot is still in the server, and the raw id marked
`(not in this server any more)` where it is not, so a stale entry can still be
seen and removed. The empty state says so explicitly, since "no bots" and "bots
are off" look identical in a list.

### `/nickname`

Changes somebody's nickname, and records **who ran the command**.

| | |
|---|---|
| **Module** | [nicknames](../../src/modules/nicknames/README.md) |
| **Options** | `user` (required) · `nickname` (optional, up to 32 characters — leave it out to clear theirs) |
| **Who** | Manage Nicknames, by default |
| **Where** | servers only |
| **Bot needs** | Manage Nicknames |

| Situation | Reply |
|---|---|
| Normal | `ok, worm is now **worm scientist**` |
| No `nickname` given | `ok, cleared worm's nickname` |
| The target owns the server | `worm owns this server, and Discord will not let me touch the owner's nickname.` |
| Refused by Discord | `Discord says no: they are probably above me in the role list, or i am missing Manage Nicknames.` |
| Any other failure | Discord's own explanation, rather than a guess |

The point of the command is the record, not the rename: Discord's audit log
attributes the change to **the bot**, because the bot is what called the API.
This writes the row itself, with the invoker on it, before asking Discord —
and removes it again if Discord refuses, so the history never claims something
that did not happen.

Bots cannot change the server owner's nickname at all, which Discord enforces.
Saying so beats a refusal that reads like a permissions problem.

### `/nicknames`

Every nickname somebody has had in this server.

| | |
|---|---|
| **Module** | [nicknames](../../src/modules/nicknames/README.md) |
| **Options** | `user` (required) |
| **Who** | everyone |
| **Where** | servers only |
| **Bot needs** | Send Messages |

**The reply is public**, unlike every other list here. A nickname history is
something a room reads together, and half the point of it is showing somebody
their own. Anybody who can see the message can page through it, and the pages
change for everyone — which is what a shared message should do.

Newest first, ten to a page, with ◀ / ▶ the way [`/trigger list`](#trigger)
pages. Each line is the nickname, when it changed, and who changed it:

```
**Nickname history for @worm**
**worm scientist** — 25 November 2023 01:58 by @latios
**worm** — 2 October 2023 14:07 by unknown
*(cleared)* — 27 September 2023 09:31
```

Times use Discord's own timestamp markup, so **everybody sees them in their own
timezone** rather than in the bot's.

**Who** is one of three things, and the difference matters:

| Shown | Means |
|---|---|
| a name | somebody was identified: the command's invoker, or the audit log's actor |
| **unknown** | the change was seen, and nothing could attribute it |
| nothing at all | imported, and the old bot's guess was not worth keeping |

Cleared nicknames show as *(cleared)* rather than as a blank line. People who
have left the server still appear; Discord resolves the mention to a name where
it can, and shows the id where it cannot. Mentions in the reply **never ping
anyone**.

Past a hundred entries the whole history comes back as a `.txt` attachment
instead of ten pages of buttons, with plain UTC timestamps. The Java version
simply gave up past 2000 characters, which by now is most histories.

### `/midnight`

Posts a message at midnight. Any number per server, each with its own timezone,
channel and text.

| | |
|---|---|
| **Module** | [midnight](../../src/modules/midnight/README.md) |
| **Who** | Manage Server, by default |
| **Where** | servers only |
| **Bot needs** | Send Messages |

| Subcommand | Options | Reply |
|---|---|---|
| `list` | none | Each entry with its id, channel, timezone, text and the date it last posted |
| `add` | `timezone` (required, autocompleted) · `channel` (required) · `message` (required) · `silent` · `previews` | `ok, that posts in #general at the next midnight in America/Chicago` |
| `edit` | `id` (required) plus any of `timezone`, `channel`, `message`, `silent`, `previews` | The entry as it now stands |
| `remove` | `id` (required) | `gone: midnight message 4` |
| `toggle` | `id` (required) | `midnight message 4 is off` |

`timezone` autocompletes from the machine's own timezone database as you type,
matching anywhere in the name — typing `chicago` finds `America/Chicago`.
Discord allows 25 suggestions at a time. A timezone the machine does not know
is refused rather than stored.

**Adding one means "from the next midnight".** The entry is recorded as having
already posted for today, where it lives, so adding one at three in the
afternoon does not post it half a minute later.

`edit` changes only what is given, and never disturbs the date an entry last
posted for — otherwise fixing a typo would post it again the same day.

`silent` and `previews` work as they do for [triggers](#trigger-add): a
midnight message posts without notifying anyone and with link previews unless
told otherwise, and the list says `notifies` or `no previews` when one differs.

### `/links`

Manages which sites' links are posted again on a mirror, and whether that
happens in this server at all. See [URL replacement](#url-replacement) for
what happens to a link. Before 2026-10 it was `/urlrepl`.

| | |
|---|---|
| **Module** | [links](../../src/modules/links/README.md) |
| **Who** | Manage Server, by default |
| **Where** | servers only |
| **Bot needs** | Send Messages, Embed Links, Manage Messages |

**It is off in every server until somebody turns it on.** A server that invited
the bot for something else should not find its links rewritten, so rules can
be added, imported and tried with `test` first; nothing is replaced until
`enable`. The choice is kept in the database, per server, so it survives a
restart.

| Subcommand | Reply |
|---|---|
| `enable` | `Link replacement is on in this server. Its 3 rules apply from now on; /links list shows them.` With no rules yet it says so and points at `set`. |
| `disable` | `Link replacement is off in this server. The rules are kept, so /links enable picks up where it left off.` |
| either, already that way | `Link replacement was already on here.` / `…off here.` |

Turning it off stops new replacements and Retry presses. Replacements already
posted stay, and their reactions are still counted.

A **rule** is a site and its mirrors, in the order to try them. A mirror is a
host, optionally followed by a path that is added to every link — `/en`, for
mirrors that translate a post when asked. So `fxtwitter.com/en` is fxtwitter
with translation on, and needs no option of its own.

| Subcommand | Options | Reply |
|---|---|---|
| `list` | none | Whether replacement is on, then the rules, five a page: **x.com** → fxtwitter.com/en, vxtwitter.com |
| `set` | `domain` (required, autocompleted) · `mirrors` (required) | `Added: links to x.com now go to fxtwitter.com/en, vxtwitter.com`, or `Updated: …` |
| `remove` | `domain` (required, autocompleted) | `Links to x.com will be left alone from now on.` |
| `test` | `text` (required: a whole message, or just a link) | A dry run, below |
| `panel` | none | The rules with editing attached, below |

`set` replaces a site's whole list, which is also how reordering works.
`mirrors` may be separated by spaces, commas or new lines. The site is reduced
to what links are matched by, so `https://www.X.com/home` sets the rule for
`x.com`. `domain` suggests the sites that already have a rule, which is most of
what makes `remove` usable. A rule that could not work is refused with the
reason:

| Refused | Because |
|---|---|
| `a rule needs at least one mirror to send links to` | no mirrors |
| `x.com can't be its own mirror` | a mirror that is the site itself would "replace" a link with the same link |
| `"localhost" doesn't look like a site; …` / `"abc" doesn't look like a mirror; …` | no dot in it |
| `that's 9 mirrors; 8 is the most one rule takes` | each mirror gets two tries of six seconds; nobody waits for more |

A mirror listed twice is kept once, in its first place.

**`test`** posts nothing. It shows the message the bot would post, as text, and
then what happened to **every** link in what you gave it and why:

```
Would post:
🔗 ||[_](https://fxtwitter.com/a/status/1)||
- https://x.com/a/status/1: replaced using the x.com rule, trying fxtwitter.com, vxtwitter.com (spoilered, so the replacement is too)
- https://example.com/b: no rule for example.com
- https://x.com/c: written as <link>, which turns its preview off
```

It is the same code the real thing runs, so the two cannot disagree. It works
while replacement is off, and says that nothing is posted until `enable`. If
you have opted out with [`/urltoggle`](#urltoggle) it says so, since that would
explain a link of yours being left alone.

**`panel`** says whether replacement is on and lists a page of rules with a
menu to pick one, then **Edit** and **Delete** for it. The last row has
**Add rule**, **Turn replacement on** (or **off**), and paging. Edit and Add
open a form with the site and **the mirrors one per line** in the order they
are tried, so reordering is rewriting lines; changing the site renames the rule
rather than adding a second one. Neither will save over another site's rule:
adding a site that already has one, or renaming onto one, is refused, and the
existing rule is left as it was. Delete asks to confirm in the panel itself.
Like the trigger panel it keeps nothing on the bot's side, survives restarts,
and is ephemeral.

`/links` needs a subcommand; Discord does not let a command with
subcommands run without one, so the panel is `/links panel` rather than
`/links` on its own.

### `/urltoggle`

Stops the bot replacing your links in this server, or starts it again.

| | |
|---|---|
| **Module** | [links](../../src/modules/links/README.md) |
| **Options** | `user` (optional — somebody else, which needs Manage Server) |
| **Who** | everyone, for themselves |
| **Where** | servers only |
| **Bot needs** | nothing |

| Situation | Reply |
|---|---|
| Opting yourself out | `Your links will be left alone here from now on. Run this again to undo it.` |
| Opting back in | `Your links will be replaced again.` |
| For somebody else | `Links from @worm will be left alone from now on.` (nobody is pinged) |
| For somebody else, without Manage Server | `changing that for somebody else needs Manage Server` |
| Any of these, while replacement is off here | the same, then `Link replacement is off in this server at the moment, so nobody's links are being replaced.` |

The choice is **kept**, per server. The Java bot's `/toggle` kept it in memory,
so every restart quietly undid everyone's choice, and it let anyone toggle
anyone.

### `/linkstats`

Reactions on the bot's replacement messages, and on the images and videos
people post in a server that counts them. See
[Reaction statistics](#reaction-statistics) for what is counted, and
[Link_Stats.md](Link_Stats.md) for how it all works and why.

| | |
|---|---|
| **Module** | [linkstats](../../src/modules/linkstats/README.md) |
| **Who** | everyone for the views; Manage Server for aliases, `recompute` and `images` |
| **Where** | servers only |
| **Bot needs** | Send Messages, and Read Message History for `recompute` |

The views answer **publicly**, and nobody is pinged by appearing in one.
`duplicates` answers privately.

`top` and `reactions` also take `source`: **Links and images**,
**Replaced links**, or **Images and videos**. Left out, it is links and images
in a server that counts images, and links alone in one that does not. Titles
say which: "on replaced links", "on images", "on links and images". A
`domain` means links alone, since an image has no site.

Both take `per_page` too, from 1 to 200: how many people or emojis a page
shows. A number whose pages would not fit in one Discord message (2,000
characters) is **refused privately**, saying the most that fits, so `100` works
for a list of short Unicode emojis and not for one of long custom emoji names.
The ◀ / ▶ buttons keep the size.

#### `/linkstats top`

A leaderboard, ten a page (twenty for **Most used emojis**) with ◀ / ▶ that
anybody can use.

| Option | Meaning |
|---|---|
| `by` | **Reactions received** (default), **Reactions given**, **Reactions to your own links**, or **Most used emojis** (the same list as `/linkstats reactions`) |
| `emoji` | Only this emoji. Autocompleted from the ones used here; typing a name like `skull` also works |
| `since` / `until` | `YYYY-MM-DD`, in UTC. `until` includes the day typed |
| `domain` | Only links to this site. Autocompleted |
| `per_page` | How many to a page |

```
Most 💀 received on replaced x.com links since 2025-01-01
1. @worm 42
2. @latios 17
```

#### `/linkstats reactions`

Every emoji, most used first, twenty a page with ◀ / ▶, and how many reactions
and different emojis there are in all.

| Option | Meaning |
|---|---|
| `user` | Only this person's. Everyone's if left out |
| `side` | **Reactions received** (default), or **Reactions given** |
| `since` / `until` / `domain` | As for `top` |
| `per_page` | How many emojis to a page |

```
Reactions @worm received on replaced links
120 reactions with 14 different emojis
1. 💀 40
2. 😂 30
3. 🔥 12
```

Received counts the reactions on links somebody posted, leaving out their own.
Given counts every reaction somebody added, apart from on their own links,
including on replacements nobody could be credited with. What one person
reacted to their own links is `top by:Reactions to your own links`.

#### `/linkstats duplicates`

Custom emojis with the **same or nearly the same name**, a group at a time:
usually one emote uploaded twice, uploaded to two servers, or deleted and
uploaded again, which Discord treats as a brand new emoji. Names are alike when
they match ignoring case, or are a letter or two apart: none for names of up to
3 letters, one up to 5, two from 6. `kekw`, `KEKW` and `kekw2` are one group;
`ok` and `no` are not.

Anybody can look. For somebody with **Manage Server** the list has two menus:
pick the one to **keep**, then pick what to **merge** into it, one at a time
or all of them. Merging is the same as [`alias add`](#linkstats-alias), and
`alias remove` undoes it. An emoji already merged counts as the one it was
merged into, so it drops out of the list.

#### `/linkstats alias`

| Subcommand | Options | Reply |
|---|---|---|
| `add` | `emoji` · `as` (both required, autocompleted with emojis that are not aliases already) | `💀 counts as ☠️ now, in every statistic back to the start.` |
| `remove` | `emoji` (autocompleted with the aliases) | `💀 counts as itself again.` |
| `list` | none | Every alias here |

Aliases apply **when statistics are read**, so adding one changes all of
history at once and removing it puts history back. An alias of an alias is
pointed straight at the end of the chain, and one that would make two emojis
count as each other is refused. So is aliasing an emoji that is **already an
alias** ("skul is already aliased to skull"), since moving it would quietly
undo the first; remove that alias first.

#### `/linkstats recompute`

Reads channel history back into the statistics, so they start with years of
reactions rather than from the day the bot began counting.

| Subcommand | Options |
|---|---|
| `start` | `since` (required, `YYYY-MM-DD`) · `until` · `channel` (every text channel if left out) · `fresh` |
| `cancel` | none |

In a server that counts images, it also finds the images and videos people
posted, and their reactions. Everywhere, it also counts the
[emotes sent as reactions](#reaction-statistics) after each post, from the
messages it reads anyway.

It replies `Started. Progress goes in this channel.` and posts a progress
message there, updated every five hundred messages; an interaction's reply
stops being editable after fifteen minutes, and a recompute can take hours.
The finished message reports channels, messages scanned, replacements found,
how many were credited to whoever posted the link and how many were not, any
webhook replacements skipped, reactions recorded, emotes sent as reactions,
any old mirrors it recognised, and any channel it could not read. Messages worth a look — ones it
did not understand, ones it would not credit, ones whose reactions it could not
read — are **linked**, so a click jumps to each.

When it is over, the bot **replies to that message and pings whoever started
it**, since by then the report is far up the channel. The reply says how it
went, and carries `recompute-issues.txt` with a link to every message worth a
look whenever there are any, since the report only has room for three of
each.

**It is safe to run again.** Each replacement's reactions are rebuilt to match
what Discord shows now rather than added to, and a reaction the bot saw being
added keeps the time it saw. How far it got is saved per channel, so running it
again with the same dates carries on from where it stopped — after
`cancel`, or a restart — and `fresh:true` starts every channel over. One runs
per server at a time. Threads are not scanned.

#### `/linkstats images`

| Subcommand | Does |
|---|---|
| `on` | Counts reactions on the images and videos people post here, credited to whoever posted them |
| `off` | Stops counting them; those already counted are kept, and `source:Images and videos` still shows them |

Both need Manage Server and answer privately. It is **off** until somebody
turns it on: it counts every image in every channel, a bigger step than
counting the bot's own messages. An upload counts, and so does a link
straight to an image or video file, which Discord shows as one. A site's
preview does not, such as YouTube's player, and neither do Tenor or Giphy
GIFs. After turning it on, `/linkstats recompute` counts the images already
posted.

It looks for replacements posted by the bot's own account. A Debug build can be
pointed at another account's with `LATIBOT_DEBUG_RECOMPUTE_BOT_ID`, for testing
with a second bot; see
[Testing with a second bot account](../../README.md#testing-with-a-second-bot-account).

What it recognises, and how it finds whose link each one was, is under
[Reaction statistics](#reaction-statistics).

### `/llm`

The language model: whether it answers here, which model, its settings, the
documents that shape it, its advanced triggers and who it ignores. How it
decides to answer is under [Talking to the bot](#talking-to-the-bot).

| | |
|---|---|
| **Module** | [llm](../../src/modules/llm/README.md) |
| **Who** | everyone may run it; `status` and the personality are open to everyone by default, and everything else needs Manage Server, checked by the bot itself |
| **Where** | servers only |
| **Bot needs** | Send Messages, Read Message History |

| Subcommand | Options | Effect |
|---|---|---|
| `status` | none | Whether it is on, the model, and what was spent against the caps |
| `on` / `off` | none | Lets it answer here, or stops it |
| `model` | `name` (one of the models below) | This server's model from now on |
| `settings` | none | Opens the [settings panel](#llm-settings) |
| `personality` · `system` · `style` | a subcommand, below | The [documents](#the-documents) |
| `trigger` | `add` · `edit` · `remove` · `list` | [Advanced triggers](#advanced-triggers) |
| `blacklist` | `add` · `remove` (`user` or `role`) · `list` | People and roles it never answers here |

It is **off in every server** until someone runs `/llm on` there: every message
it answers costs money. It also needs an API key in the bot's environment,
`ANTHROPIC_API_KEY` or `OPENAI_API_KEY`; without the one its model needs, `on`
says so and it stays quiet. The models it can use are the ones whose price it
knows, since its spending limit is worked out from them:

| Model | Per million tokens in / out | |
|---|---|---|
| Claude Haiku 4.5 | $1 / $5 | the default |
| Claude Sonnet 5, Sonnet 5.5 | $2 / $10 | |
| Claude Opus 5.5 | $4 / $20 | |
| Claude Opus 5 | $5 / $25 | |
| Claude Fable 5.1 | $10 / $50 | |
| GPT-6 Luna | $0.10 / $0.50 | needs `OPENAI_API_KEY` |
| GPT-6 Sol | $2 / $10 | needs `OPENAI_API_KEY` |

| Situation | Reply |
|---|---|
| `on` | `ok, i'll answer here when addressed` |
| `on`, with no key for its model | `ok, i'll answer here when addressed, but there's no API key for its model on the bot, so it can't yet` |
| `off` | `ok, i'll stay quiet here` |
| `model` | `ok, this server now uses Claude Sonnet 5.5` |
| `model` with no key for it | `there's no openai API key on the bot, so GPT-6 Luna can't be used` |
| Anything but `status` or the personality, without Manage Server | `that needs Manage Server` |
| `status` | `The language model is **on** here, using Claude Haiku 4.5.` then what was spent today and this month, against the caps, and how much of the month's was this server's |

`status` adds a line when there is no API key for the model, and one when a
spending limit has been reached.

#### The documents

Three documents per server shape what the model says. **Every edit is a new
version**, nothing is overwritten, and any earlier version is one command away.

| Document | Group | Who may change it | What it is for |
|---|---|---|---|
| personality | `personality` | **everyone**, by default | How the bot comes across. Meant to be tuned by the people who talk to it |
| system instructions | `system` | Manage Server | Rules that are not up for negotiation; empty to begin with |
| trigger style | `style` | Manage Server | How advanced-trigger replies are written |

Each group has the same subcommands:

| Subcommand | Options | Effect |
|---|---|---|
| `view` | none | What it says now, in a code block, or as an attached file when it is long |
| `edit` | `file` (optional, a `.txt` or `.md` file) | Without `file`, a form holding the document in up to five parts of 4000 characters; with it, the file replaces the document |
| `history` | none | The newest fifteen versions: who, when, how long, and a note such as `reverted to version 2` |
| `diff` | `from`, `to` (optional versions) | What changed, line by line, from the version before `to` to `to` (the current one by default) |
| `revert` | `version` (required) | Saves that version's text as a new version. Version 0 is the default text |

`personality` also has `editors` (`role`): without a role it says who may
edit; with one it limits editing to that role, plus anyone with Manage Server.
Choosing @everyone opens it up again. Setting it needs Manage Server.

The personality can be read by anyone; the system instructions and the trigger
style only by people with Manage Server, since they may hold rules better kept
out of sight. **The personality cannot override the rest**: the model is told
it is style guidance written by people in the server, after the fixed rules
and the system instructions. Edits are recorded in the bot's own log, not
announced in the channel.

A document is at most 20,000 characters, and is sent with every message the
model answers, so its length costs money each time. Saving one over about 1500
tokens says so:

| Situation | Reply |
|---|---|
| Saved | `saved the personality as version 4 (about 180 tokens)` |
| Saved, and long | the same, then `that's long, and it's sent with every message the model answers, …` |
| The form, with nothing changed | `nothing changed, so nothing was saved` |
| Too long for the form | ``it's too long to edit in a form; attach it as a .txt or .md file with `file:` `` |
| `file` is not `.txt` or `.md` | `attach a .txt or .md file` |
| `file` over the limit | `that's over the 20000-character limit` |
| Not allowed to edit the personality | `you can't edit the personality here` |
| `revert` or `diff` to a version that does not exist | `there's no version 9 of the personality` |

#### `/llm settings`

A panel of the numbers that shape each reply. A menu opens a form for one
group; every value is checked against its range, and a form with any value out
of range changes nothing and says which, for example `"Token budget for them"
takes a whole number from 200 to 20000; nothing was changed`. Changes apply to
the next message. The panel also turns the model on or off.

| Setting | Default | Range |
|---|---|---|
| Recent messages it reads | 15 | 0–50 |
| Token budget for them | 3000 | 200–20000 |
| Messages an advanced trigger reads | 5 | 0–20 |
| Longest reply, in tokens | 1024 | 256–8192 |
| Replies per person per minute | 3 | 1–60 |
| Replies per channel per minute | 8 | 1–120 |
| Bot turns in a row (0: never) | 6 | 0–50 |
| Seconds between bot turns | 5 | 0–300 |
| Replies to bots per day | 50 | 0–1000 |
| Only after a person spoke | no | yes or no |

The token budget is a rough count, four characters to a token. The longest
reply includes the model's thinking, on the models that think, so it is not a
word count.

#### Advanced triggers

A [simple trigger](#trigger-responses) answers with fixed replies; an advanced
one asks the model to say something. Each has a pattern, matched the same way
(whole word or anywhere, ignoring case), and a **prompt**: one line about what
to say, such as `Someone mentioned pineapple pizza. Defend it with
unreasonable passion.` How to say it comes from the trigger style document, so
the prompt stays short.

| Subcommand | Options |
|---|---|
| `add` | `pattern` (required, up to 200 characters) · `prompt` (required, up to 500) · `chance` (percent, 100 by default) · `cooldown` (seconds, 300 by default) · `mode` |
| `edit` | `id` (required) and any of the above, plus `enabled`; what is left out stays |
| `remove` | `id` |
| `list` | none |

- **`chance`** is how often a match gets a reply, and a match that loses its
  roll does not start the cooldown.
- **The cooldown is per trigger, per channel**, as with simple triggers.
- **A simple trigger wins.** When a message sets off a simple trigger, no
  advanced trigger answers it, and no model call is made.
- **Other bots never set one off.** A bot has to address the bot to be
  answered at all.
- Its reply is posted silently, as a simple trigger's is, and reads the last
  five messages rather than fifteen (the setting above).

#### The blacklist

`/llm blacklist add` with a `user` or a `role` stops the model answering that
member, or anyone with that role, here. It still reads what they write, as part
of the conversation. `remove` undoes it and `list` shows it. Give one of `user`
and `role`, not both. Replies: `ok, i won't answer @name`, `ok, i'll answer
@name again`, or that they were already on (or not on) the list.

### `/memory`

What the model remembers in this server.

| | |
|---|---|
| **Module** | [llm](../../src/modules/llm/README.md) |
| **Subcommands** | `list` (`user`) · `forget` (`id`) · `clear` (`user`, `everything`) |
| **Who** | everyone, for what is about them; Manage Server for anyone else's |
| **Where** | servers only |
| **Bot needs** | nothing |

The model decides for itself what to remember, when people tell it things or
ask it to (see [Talking to the bot](#talking-to-the-bot)). Each memory is a
sentence, may be about one person, and has a number.

- **`list`** shows ten at a time, newest first, with ◀ / ▶ buttons. Without
  `user` it is what the model remembers about you; with Manage Server,
  without `user` it is everything here.
- **`forget`** removes one by its number: one about you, or with Manage
  Server, any.
- **`clear`** removes everything about you. With Manage Server, `user`
  clears someone else's and `everything:true` clears the whole server's.

| Situation | Reply |
|---|---|
| `forget` | `forgot #12` |
| `clear` | `forgot 3 things` |
| No such memory | `there's no memory #12 here` |
| Someone else's, without Manage Server | `you can remove memories about you; anyone else's needs Manage Server` |
| `list` of someone else's, without Manage Server | `you can see what i remember about you; anyone else needs Manage Server` |

All private.

---

## Passive behaviour

Things the bot does without being asked.

### The goodbye phrase

*Module: the core.*

An **administrator** saying the phrase — `say goodbye latibot` by default —
stops the bot. It replies `ok bye bye!`, waits about a second and a half so the
message actually goes out, and shuts down.

Three deliberate restrictions:

- **Administrator in that server**, checked against the member rather than the
  user, so being an admin somewhere else does not count.
- **The phrase must be essentially the whole message.** Quoting it mid-sentence
  does nothing. Comparison is on lowercased words, so `Say goodbye, LatiBot!`
  matches — punctuation and capitalisation make no difference.
- **A trailing emoji means no match.** Anything above plain ASCII counts as part
  of a word, so `say goodbye latibot 👋` is a different phrase. This stops the
  bot; near enough is not good enough.

Configure it with [`/goodbye`](#goodbye).

### Trigger responses

*Module: [triggers](../../src/modules/triggers/README.md).*

When a message matches one of this server's triggers, the bot replies with one
of that trigger's responses, chosen by weight.

A server starts with **no** triggers. The Java bot's `420`, `4:20` and `69`,
all answering "nice", are not added for you; add them with
[`/trigger add`](#trigger-add) if you want them.

- **Whole word** is the default: `420` fires on `it is 420 somewhere` and on
  `(420)`, but not on `4200`. Every occurrence is checked, so `4200 and also
  420` does match.
- **Anywhere** matches inside longer words, so `cat` fires on `catastrophe`.
- Matching ignores case on both sides.
- **The cooldown is per trigger, per channel.** Two channels do not share one.
  Cooldowns are forgotten when the bot restarts, which costs at most one extra
  reply.
- **Weights** are relative: with `3 | nice` and `very nice`, the first is three
  times as likely.
- **Replies are silent**, the way the Java bot's were: they notify nobody. A
  trigger can be set to notify, or to hide link previews in its replies, with
  `silent` and `previews` on [`/trigger add`](#trigger-add) and `edit`, or the
  panel's buttons.
- Triggers **do not consume the message**. One containing both `420` and a link
  gets the reply *and* the [link replacement](#url-replacement) — the Java
  version's early `return` meant it got only the first of those. A message a
  trigger answered does not also set off an
  [advanced trigger](#advanced-triggers); the simple, free reply wins.

**Other bots are ignored unless the server allows them.** Two bots answering
each other is a loop nobody asked for, so the default is silence. Allowing a bot
with [`/bots`](#bots) lets its messages reach the bot's features; each trigger
then decides separately whether it answers, with `bots:true`. Hearing and
answering are deliberately two decisions: a server-wide "I'll listen to DiceBot"
should not turn every existing trigger loose on it.

The bot never answers itself, and no setting changes that.

### URL replacement

*Module: [links](../../src/modules/links/README.md).*

When somebody posts a link to a site with a [rule](#links) — x.com,
tiktok.com, reddit.com, instagram.com — the bot posts it again on a mirror that
previews properly, and turns the preview on the original off.

**Only in servers that turned it on**, with [`/links enable`](#links) or
the panel's button. Every server starts with it off.

The replacement is a **plain message, never a reply**, with notifications
suppressed and one line per link:

```
🔗 [_](https://fxtwitter.com/somebody/status/1)
🔗 ||[_](https://tfxktok.com/@somebody/video/2)||
```

- **Every link** in a message is handled, up to five. The Java version found
  the first and silently ignored the rest.
- **A spoilered link stays spoilered.** The Java version's spoiler check had
  never worked: it was really testing whether the text around the link had an
  even number of characters.
- **A link to a site without a rule is skipped on its own.** In the Java
  version one such link stopped every other replacement in the message.
- **Query strings, fragments and trailing slashes survive**, and a mirror's
  `/en` goes on the path where it belongs.
- **Some links are left alone on purpose:** ones written as `<https://…>`,
  which is how you ask Discord for no preview; ones inside `code`; the same
  link twice; messages from bots; messages whose author already turned their
  previews off; and anybody who opted out with [`/urltoggle`](#urltoggle).

**The bot watches for the preview to actually appear.** Discord adds a link's
preview a moment after the message is posted, and the bot waits for that
rather than checking after a fixed delay. Each mirror gets **two tries of
about six seconds**, then the next mirror; each link in a message is followed
on its own, so one slow link does not hold up the rest. The Java version looked
five seconds later and compared a count, so a slow network looked exactly like
failure.

**When no mirror works**, the bot does not delete its message. The original's
own preview is turned back on, and the bot's message becomes a short note with
its own previews off and a **Retry** button:

```
🔗 couldn't get a preview for that link from fxtwitter.com or vxtwitter.com. The
original's own preview is back; press Retry to try the mirrors again.
```

**Anyone can press Retry.** It tries each mirror once, using the rule as it is
now — so fixing a rule and pressing Retry works. If a preview appears, the
replacement comes back and the original's preview goes off again; if not, the
note and the button stay. The button keeps working after a restart. If just one
of several links works, the message counts as working and the others stay on
their last mirror.

**A restart in the middle of watching** does not leave a replacement stuck.
When the bot comes back, it looks at each one it was still waiting on and
settles it on what it shows now: working if a preview appeared, otherwise
the note with its Retry button, and the original's preview back on. Nothing
is tried again by itself.

Turning the original's preview off needs **Manage Messages**; without it the
replacement still posts and both previews show. Our message's preview needs
**Embed Links**. Both are named at startup if missing.

#### Rules from the Java bot

If `UrlReplacements.txt` from the old bot is left next to the database — at
`data/UrlReplacements.txt` — its rules are copied into each server the first
time the bot sees it there. A rule the server already has is never
overwritten, and a server is only ever imported once, so deleting a rule later
is not undone by the next restart. Without the file, a server starts with no
rules and [`/links set`](#links) adds them. Either way the rules do nothing
until the server runs `/links enable`; importing does not turn it on.

### Reaction statistics

*Module: [linkstats](../../src/modules/linkstats/README.md).*

Reactions on the bot's replacement messages are counted three ways:

- **Received** — credited to whoever **posted the original link**. "Who gets
  the most 💀."
- **Given** — the same reactions, credited to **whoever reacted**. "Your top
  three reactions."
- **Self** — reacting to your own link. Recorded, **left out of both of the
  above**, and counted on its own.

Reactions are counted as they happen, including removals and a moderator
clearing them, and kept forever. `❤` and `❤️` are the same heart whichever
keyboard typed it. Custom emojis that should count as one can be merged with
[aliases](#linkstats-alias).

**Images.** In a server that has run [`/linkstats images on`](#linkstats-images),
people's own image and video posts are counted the same three ways, credited
to whoever posted them.

**Emotes as reactions.** Some emotes get sent as a message of their own just
after a post rather than as a reaction to it. Those count as reactions too:

- a message of **nothing but emojis**, custom or Unicode, that is somebody's
  **first message** after a replacement or an image post, within the next 25
  messages and before the next such post;
- a **reply** to the post that is nothing but emojis, however much later.

Each different emoji in the message counts once. Somebody who both reacted
with an emoji and sent it is counted once for it. A message that is a reply to
something else, or has any words, a mention, a file or a sticker in it, does
not count. Deleting the message takes its emotes back off the count.

**Emoji copies.** The bot keeps its own copy of every custom emoji used here,
as an emoji its application owns, so the statistics can still show an emote
after its server deletes it. Emojis with the same picture, or merged by an
alias, share one copy. `linkstats.emoji_copy_min_uses` in `config.json` sets how many
reactions an emote needs first (one, to start with).

**History.** [`/linkstats recompute`](#linkstats-recompute) recovers the
reactions on replacements the bot posted before it started counting — years of
them. It recognises all six shapes the bot's replacements have had:

| Shape | Whose link it was |
|---|---|
| A copy of the original message, as a reply | the message it replied to |
| Webhook mode, posted as the member | skipped: the original was deleted, so there is nobody to credit |
| A copy of the original, as a plain message | the nearest earlier link, below |
| `[.](link)` | likewise |
| `🔗 [.](link)` | likewise |
| `🔗 [_](link)`, today's format | likewise |

A message counts when the bot wrote it and either links to a mirror any rule
has ever used, or has a replacement's shape. Today's rules are not enough: over
the years mirrors broke and were swapped for others, and many were dropped
before this bot kept a list. So the `[.](link)` and `🔗 [_](link)` shapes count
whatever the mirror, since the bot writes them for nothing else. A copy of a
message counts when it answers somebody's link with the same path on another
host. A mirror recognised that way is **remembered**, under the site of the
link it replaced, and named in the report. Anything with a known mirror that
matches none of these shapes is **reported with a link to it, never guessed
at**.

**Finding who posted the link:** the nearest earlier message with a link,
skipping bots and chat in between — the bot answers in a second or two, so
that is nearly always the one. The link's path must match the replacement's.
If the nearest link is somebody else's, the bot keeps looking a little further
back; if nothing matches, the replacement is left **uncredited** and reported,
rather than credited to the wrong person. An uncredited replacement's
reactions still count as *given*.

One limit is Discord's rather than a choice: **Discord does not say when a
reaction was added.** Recovered reactions know who and which emoji, but not
when, so date filters use the message's own date for them.

### Nickname tracking

*Module: [nicknames](../../src/modules/nicknames/README.md).*

Every nickname change in the server is recorded, however it was made: through
[`/nickname`](#nickname), through Discord's own UI by a moderator, or by the
person themselves. [`/nicknames`](#nicknames) shows the result.

**Recording never waits on attribution.** The change is written down the moment
it is seen, with nobody against it; the audit log entry that follows fills in
who made it. A change with no name on it is much better than a change that was
missed. If no entry has turned up ten seconds later, the bot asks Discord for
the audit log directly — cover for a reconnect or a dropped event.

**The bot is never recorded as the one who did it.** Discord's audit log names
the bot for anything the bot did, which is exactly how the only certain
attribution — the person who ran `/nickname` — would be lost.

Anything still unattributed stays **unknown**. The Java version guessed "they
did it themselves", which was often wrong.

Changes made while the bot was **not running** are noticed the next time it
starts and recorded with nobody against them, since there is no way to know.

**This needs the Server Members intent**, which is privileged: it has to be
enabled for the application under *Bot → Privileged Gateway Intents* in the
Discord developer portal. Without it, nickname changes never arrive.
`"track_changes": false` in the `nicknames` section of `config.json` turns
the whole thing off,
including the request for that intent — `/nickname` still works and still
records its own changes, but changes made anywhere else go unseen. A bot that
asks for an intent it was not granted is refused the gateway outright, which
the log explains if it happens.

Naming who made a change also needs **View Audit Log** in the server. Without
it, changes are still recorded; they just say *unknown*.

#### History from the Java bot

If `nicknames.json` from the old bot is left next to the database — at
`data/nicknames.json` — its history is imported at startup. Importing the same
file twice adds nothing, so it can simply be left there.

Its timestamps are local wall-clock from a machine in US Central with no
timezone recorded, so each is converted using the full daylight-saving history
for `America/Chicago`, including the 2007 rule change. Two dates a year need a
decision rather than a conversion: the hour that happens twice each November
takes the earlier reading, and the hour that never happens each March is
shifted forward, so 02:30 becomes 03:30. **The original text is kept** against
every imported row, so the whole conversion can be redone if the timezone turns
out to be wrong.

Attribution comes across only where it is worth anything. The Java bot wrote
the member's own id whenever it could not tell who made a change, so that value
says nothing and is dropped; an id belonging to somebody else could only have
come from its own `/nickname`, so that one is kept.

Anything unreadable is named in the log and skipped — one bad entry does not
lose the rest.

### The midnight message

*Module: [midnight](../../src/modules/midnight/README.md).*

Each entry posts **once per local day**, shortly after midnight in its own
timezone, in the channel it was given. Configure them with
[`/midnight`](#midnight). Like trigger replies, it posts silently and with
link previews unless the entry says otherwise.

The date an entry last posted for is saved with the post, which is what makes a
restart at 00:00:30 not post a second time.

The bot checks the clock every thirty seconds rather than scheduling a delay.
That is deliberate, and it is the fix for a Java bug worth naming: the old
version computed a delay from the wall clock and then waited on a monotonic
timer, so whenever the machine slept the message arrived at whatever time it
happened to wake up — and then behaved normally again for a while.

**A midnight the bot was not running for is skipped.** If the bot comes back
more than five minutes into the new local day, that day is given up on and the
entry waits for the next midnight — yesterday's midnight message over
breakfast is worse than none. The log says so, once, naming the entry and how
late it already is.

A restart *inside* those five minutes still posts, which covers the ordinary
case of the bot being bounced a moment after midnight.

### Leaving empty voice channels

*Module: [voice](../../src/modules/voice/README.md).*

When everyone else has left the bot's voice channel, it waits, then leaves. The
wait is 30 seconds by default: long enough that someone dropping out and
rejoining does not lose a voice session. It applies however the bot got
there, by `/join`, `/speak` or `/voice start`, and `/voice grace` shows or
changes it per server (0 leaves at once). Other bots do not count as company,
as far as Discord has told the bot who is a bot.

Leaving by any route, a moderator disconnecting the bot included, ends the
server's voice session and drops whatever it was about to say.

### Talking to the bot

*Module: [llm](../../src/modules/llm/README.md).*

Once a server has turned it on with [`/llm on`](#llm), the language model
answers anyone who **addresses** the bot:

- **@mentions** it anywhere in a message,
- **replies** to one of its messages, or
- **starts the message with its name**: `latibot, what's the plan` or
  `LatiBot what's the plan`, but not `latibots` or `hey latibot`.

It shows *LatiBot is typing…* while it thinks, then replies to the message.
Whoever asked is notified of the reply, as with any reply; nothing the model
writes can ping anyone else, `@everyone` included. A reply too long for one
message is split, three messages at most.

**What it reads.** The last fifteen messages in the channel, cut to about 3000
tokens from the oldest end, then the message it is answering; both numbers are
[settings](#llm-settings). The messages are what it is shown, not instructions
it has to follow, and it is told so.

**Who it sees.** Never anyone's name or Discord ID. Each person is a random
alias to it, and the names people type are swapped for markers; the bot puts
the real names back into the reply before posting it. A name is caught when
it is the name of someone in the conversation, written as their name; one
spelt otherwise, or of someone else, is sent as typed. The server's documents
keep the names written in them.
[Language_Model.md §3.8](Language_Model.md#38-who-the-model-is-told-about)
has the details.

**What shapes it**, in this order, each outranking the next: rules built into
the bot, which nobody in Discord can change; the server's
[system instructions](#the-documents); its [personality](#the-documents),
which is only ever style; then the memories that look relevant, and the
conversation.

**Memory.** The model can remember things for later, look them up, and forget
them, by itself: when someone tells it something worth keeping, or asks it to
remember. The memories about whoever it is answering, and those matching what
they said, are put in front of it before it answers, so it rarely has to look.
It can forget only what is about the person it is answering, or what that
person had it remember, so nobody can talk it into forgetting someone else;
[`/memory`](#memory) is how people see and remove the rest. A server holds up
to 500 memories of up to 500 characters each.

**In a voice session**, a reply in the session's text channel is also spoken
there, in Paul's voice. The model is told it will be heard and keeps it short
and plain, and may use a few DECtalk inline commands such as `[:rate 250]`;
the posted text keeps the commands it was allowed, so the channel sees what
was said as it was said. Nothing the model writes is ever trusted with the
commands that touch the bot's machine, whoever asked, and the spoken part is
cut at this server's [`/speak` limit](#tts). Whoever asked can stop it with
`/tts stop`.

**When it stays quiet.** Checked in this order, before anything is spent:

- The server has not turned it on, or there is no API key for its model.
- Nobody addressed it and no [advanced trigger](#advanced-triggers) fired.
- The author, or one of their roles, is on the [blacklist](#the-blacklist).
- **The spending limit is reached**: $2 in a UTC day or $20 in a UTC month,
  across every server, from the `config.json` caps. It says
  `i've hit today's spending limit, so i'm staying quiet until tomorrow (UTC)`
  (or this month's) once per server, and warns in the bot's log. One reply can
  take spending a little past the cap; the next is refused.
- The author asked more than three times this minute, or the channel had eight
  replies this minute.
- For another bot: see below.

If the model itself fails, someone who addressed the bot is told
`sorry, i couldn't come up with anything just now`, or
`i'm a bit overloaded right now; try me again in a minute` when the model is
busy. An advanced trigger's failure is silent.

**Other bots.** The model answers a bot only if the server
[allows that bot](#bots), and only when the bot addresses it. Because two
models can talk to each other forever, it paces itself: at most six replies to
bots in a row in a channel before waiting for a person to say something, at
least five seconds between them, and fifty a day per server. Any person
speaking in the channel resets the count. It can also be told to answer bots
only once a person has spoken in the channel. All of these are
[settings](#llm-settings).

**Cost.** Every call to the model is written down with its tokens and price,
which is what the spending limit adds up. The instructions and documents are
marked for the provider's prompt cache, so a conversation in full swing pays
far less for them.

### Permission warnings

*Module: the core.*

When the bot joins a server — and for every server it is already in, each time
it starts — it checks what it can do there against what its features need, and
**logs a warning for anything missing**. It never exits and never disables
itself: a server missing one permission loses one feature there, not the whole
bot.

The warning names the missing permissions and what they were for, for example
`missing Send Messages for replying to messages` or
`missing Manage Messages for turning off the original preview when a link is
replaced`. Only what is actually missing is reported, and a server that granted
Administrator is never warned about anything, since Discord treats it as
everything.

Nothing is posted to Discord — this goes to the bot's own log.

---

## Behind the scenes

Not user-facing, but worth knowing when something looks wrong.

**Settings are per server.** The goodbye phrase, triggers, the bot allowlist,
URL rules, opt-outs, emoji aliases, speech limits, custom voices, the music
volume and track limit, and the language model's switch, model, settings, documents, memories, advanced
triggers and blacklist are all stored per server, in a SQLite database at `data/bot.db`. Two servers never
see each other's anything.

**Speech is DECtalk**, built from source alongside the bot, and it needs
`dtalk_us.dic` beside `dectalk.dll`, where the build puts it; without it the
bot starts, logs a warning, and every attempt to speak says why it failed.
Each utterance gets an engine of its own, so it always starts from DECtalk's
defaults, and one that takes more than ten seconds to make is abandoned. No
audio file is ever written to disk.

**The database upgrades itself** on startup, in a transaction. A failed upgrade
rolls back and keeps the previous version rather than leaving a half-migrated
database.

**Backups run on a schedule.** Every `backup_interval_minutes` (six hours by
default) the bot writes a consistent copy of the database to
`backup_directory` as `bot-YYYYMMDD-HHMMSS.db`, and deletes all but the
`backups_to_keep` newest. The copy is taken through SQLite's online backup
API, so it is a complete, valid database even though the bot is still running.
A backup that fails is logged and never stops the bot. Setting the interval or
the retention to zero turns backups off, which the startup log says.

**Commands are registered globally** when the bot starts, once per run. Discord
can take a little while to show changes to a command's options.

**How a command's messages are flagged is written beside the command**, in
the constructor that names it: ephemeral or public, silent or not, previews or
not, for its results, refusals and posts, with overrides per subcommand. The
bot refuses to start if one names a subcommand the command does not have, or
a flag that kind of message cannot carry, such as an ephemeral post. A panel's
buttons keep the flags its message was sent with.

**Everything is logged.** Every command records who ran it, what they passed
and what it did; so does every button and form in a panel, every message the
bot posts on its own, and each start, stop and schema change. Turning the level
up to `debug` adds the reasoning — which trigger matched, why one stayed quiet,
what a cooldown had left to run. It all goes to the bot's own log, and to a
Discord channel only if [`/logs`](#logs) has set one. See
[Logging](../../README.md#logging) for how to set the level.

**The bot needs two privileged intents**, both enabled for the application
under *Bot → Privileged Gateway Intents* in the Discord developer portal:

- **Message Content.** Without it every message arrives empty: slash commands
  keep working while the goodbye phrase and every trigger silently do nothing.
- **Server Members.** Without it no nickname change is ever seen. This one can
  be turned off with `"track_changes": false` in the `nicknames` section,
  which also stops the bot
  asking for it.

A bot that asks for an intent it was not granted is refused the gateway
entirely, and reconnects in a loop. The log says which toggle to go and find
when that happens.

---

## What is coming

Unscheduled, with the detail in [Planned.md](Planned.md): emote statistics
and appearance tracking.
