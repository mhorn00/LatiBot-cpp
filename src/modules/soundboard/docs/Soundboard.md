# Soundboard sounds as voice messages

`/soundboard` sends a soundboard sound into the chat as a voice message. A
sound sent as a file shows as an attachment to download. A voice message
shows a waveform and plays inline, on every client.

| | |
|---|---|
| **Code** | `src/modules/soundboard/` |
| **Tests** | `src/modules/soundboard/tests/` (`latibot_soundboard_tests`) |
| **Commands** | `/soundboard` ([user guide](../../../../docs/User_Guide.md#soundboard)) |

## 1. What it does

`/soundboard sound:<name>` suggests sounds as the name is typed. Picking one
answers the command with that sound as a voice message, posted silently, as
`/chat`'s is. The message reads "*someone* used /soundboard" above the voice
message.

The sound's name cannot be in the message itself. Discord refuses a voice
message that has any content, and the line above it is Discord's own,
naming only the command. So the message is the voice message alone.

Who sees the command is the same as who may use the soundboard in voice: it
defaults to members with Use Soundboard, and a server can change that under
Integrations.

## 2. The sounds it can see

Every soundboard sound the bot can see:

- Discord's own, which every server has (`GET /soundboard-default-sounds`);
- each server's the bot is in (`GET /guilds/{id}/soundboard-sounds`), which
  any member can list, so the bot needs no permission for it.

DPP wraps neither call, so both go through `raw_api`. A sound Discord marks
unavailable, which a server that lost its boosts can have, is left out.

**When they are read.** Discord's own are read when the bot connects. A
server's are read when it arrives (on connecting, or on joining it), and
again:

- every 30 minutes, for every server (`the soundboard refresh`);
- when someone looks through them, if they were last asked for more than a
  minute ago. Typing into the option reads that server's sounds again, so a
  sound added a moment ago turns up a keystroke or two later.

Discord tells a connected bot when sounds are added, changed or removed
(`GUILD_SOUNDBOARD_SOUND_*`), but DPP does not pass those events on, hence
the reading.

A failed read keeps what was there and is retried on the same schedule. The
minute is counted from when it was asked, not from when it worked, so a
server Discord will not list is not asked on every keystroke.

**Suggestions.** At most 25, Discord's limit: names starting with what was
typed before names only containing it, and within each, the server's own
sounds, then Discord's, then other servers'. A sound from elsewhere says
where it is from: "🦆 quack (Discord)", "quack (The Pond)". A suggestion
gives the command the sound's id, so two sounds with the same name never
mix. A name typed without picking a suggestion still works, ignoring case,
and prefers the server's own, then Discord's, then another server's.

Every server's sounds are offered in every server, which is what Discord
does for anyone with Nitro. The names of the servers the bot is in are
shown beside their sounds.

## 3. Making the voice message

1. The sound's file is fetched from Discord's CDN
   (`https://cdn.discordapp.com/soundboard-sounds/{id}`), as MP3 or Ogg,
   through the `http_client` port. A file over 1 MiB is refused: a
   soundboard sound is at most 512 KiB and 5.2 seconds.
2. It is written to a file of its own in the system's temporary folder,
   since `util::run` gives a program nothing on stdin, and removed once
   ffmpeg is done.
3. ffmpeg runs twice on it, on a worker thread of the module's own so DPP's
   threads never wait:
   - to **Ogg Opus**, one channel at 48 kHz, 64 kbit/s, which is what
     Discord's own apps record voice messages as;
   - to **16-bit samples**, the same way, which the duration and the
     waveform are read from (`discord::waveform`, as `/chat`'s).

   Both apply the sound's own volume, as the soundboard does when it plays
   it. Each run has 10 seconds and at most 30 seconds of audio.
4. The command is answered with the voice message through
   `discord::answer_with_voice_message`, which `/chat` uses too: response
   type 4 with flag 8192 (IS_VOICE_MESSAGE) and the attachment's
   `duration_secs` and `waveform`, and the file as the form part
   `files[0]`. The form is built by hand and posted through the
   `http_client` port, since DPP names a lone file's part `file`, which
   Discord refused as an invalid form body.

It answers directly rather than deferring, as `/chat` does, so that the
voice message is the reply itself. That has to happen within Discord's
three seconds. A sound is small, and the live test makes one second of
sound in well under that, fetching aside. What was made is kept, up to 128
sounds, by sound and volume, since a sound's file never changes, so a sound
played again answers at once.

## 4. When it cannot

| Case | Says |
|---|---|
| no sound by that name or id | i can't see a soundboard sound called "…" |
| ffmpeg not found | couldn't make that a voice message: ffmpeg isn't installed where i can find it |
| Discord's CDN refuses | … Discord wouldn't give me that sound (HTTP 404) |
| ffmpeg fails | … ffmpeg couldn't read that sound: *its last line* |
| Discord refuses the voice message | couldn't send the voice message; it's in the log |

Each is private to whoever asked.

## 5. Configuration

| Key | Default | |
|---|---|---|
| `soundboard.ffmpeg_path` | empty | where ffmpeg is; empty looks beside the bot, then on `PATH` |

It is a key of its own rather than music's `music.ffmpeg_path`, since either
module can be built without the other. ffmpeg is looked for once, at
startup. Without it the bot starts, warns, and `/soundboard` says what is
missing. `deploy/Install-Dependencies.ps1` puts ffmpeg beside the bot for
music already, which is where this looks first.

## 6. Decisions

| Date | Decision | Why |
|---|---|---|
| 2026-10-21 | A module of its own, needing no voice | A voice message needs no voice channel |
| 2026-10-21 | ffmpeg, through `util::process`, which moved back to the core | Sounds are MP3 or Ogg, and ffmpeg was already the bot's decoder; music and the soundboard both use it (Module_Plan_Final D30) |
| 2026-10-21 | The voice message helpers moved from dectalk to `core/discord/voice_message` | `/chat` and `/soundboard` send voice messages the same way |
| 2026-10-21 | Ogg Opus rather than WAV | It is what Discord's apps send, and a voice message has to play on every client |
| 2026-10-21 | Every server's sounds, everywhere | Asked for: "all of the soundboard sounds the bot can see" |
| 2026-10-21 | The sound's name is not in the message | Discord refuses content on a voice message |
| 2026-10-21 | The form built by hand, not by DPP | The first real run got `400: Invalid Form Body`: DPP names a lone file `file`, not `files[0]` |

## 7. Limits, and what is still to check

- **Not yet run in real Discord.** In particular: that Discord takes an
  interaction response as a voice message. `/chat` answers the same way,
  as the Java bot's did, and this sends the same payload with Ogg Opus.
- A sound fetched and converted for the first time has to beat the three
  seconds. If Discord's CDN is slow that once, the command fails and says
  so in the log. Played again, it is kept.
