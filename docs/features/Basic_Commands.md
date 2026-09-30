# Basic commands, and the goodbye phrase

The small commands the Java bot had: `/ping`, `/say`, `/status` and
`/shutdown`. Also the **goodbye phrase**: an administrator saying
"say goodbye latibot" stops the bot, and `/goodbye` changes the phrase.
`/join` and `/leave` live in the same file, but are part of
[voice channels](Voice_Channels.md).

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](README.md#ping) has every
option and reply. [Classes.md §4](../architecture/Classes.md#4-slash-commands)
draws the command classes.

| | |
|---|---|
| **Code** | `src/core/commands/basic.*`, `src/core/events/goodbye.*`; the status is put back in `bot`'s `on_ready` |
| **Tests** | `tests/unit/basic_commands_test.cpp`, `tests/unit/goodbye_test.cpp`, `tests/unit/command_responses_test.cpp` |
| **Tables** | `guild_settings`: `goodbye_phrase` per server; the saved status under server 0, the bot's own |
| **Plan** | Replaces plan §6 |
| **Status** | Built in phase 1 (2026-09-21). Not yet seen working in Discord: the goodbye's shutdown since it was made a joined thread, `/say reply:` since it was deferred, and the restored status (all 2026-09-25) |

## Contents

1. [Intent](#1-intent)
2. [The commands](#2-the-commands)
3. [The goodbye phrase](#3-the-goodbye-phrase)
4. [How it works](#4-how-it-works)
5. [Decisions](#5-decisions)
6. [What is still to check](#6-what-is-still-to-check)

## 1. Intent

Keep everything the Java bot's basic commands did, and fix where they went
wrong. `/say` answered only after posting, so a bad reply target failed at
the API with nothing shown to the caller. `/status` was forgotten on every
restart. And there was no way to stop the bot from Discord without a slash
command. Where the port first drifted from the Java bot without a record,
the cleanup analysis (2026-09-25) asked the owner, and §5 records the answers.

## 2. The commands

| Command | Who, by default | Answer | Does |
|---|---|---|---|
| `/ping` | everyone | private | `Pong!`, then edits in the round trip and the gateway latency |
| `/say message [reply]` | Manage Messages | private `ok`, then a public post | Posts as the bot in this channel, optionally as a reply to a message id |
| `/status status [type]` | Manage Nicknames | private | Sets the bot's presence, everywhere, and keeps it |
| `/shutdown` | Administrator | public | `ok bye bye!`, then stops |
| `/goodbye [phrase] [off]` | Administrator | private | Shows, sets or turns off the goodbye phrase |

- **`/ping`** gives two numbers because they answer different questions.
  The round trip is a REST call to Discord and back, measured around the
  reply itself. The gateway latency is DPP's websocket heartbeat. A healthy
  gateway with a slow round trip means Discord's REST API is congested, not
  the bot.
- **`/say`** refuses a message of only whitespace, which Discord's own length
  check lets through and then fails at the API. With `reply`, it fetches the
  target **before** posting: a message that is not in this channel, or was
  deleted, is refused by name. Fetching is waiting on Discord, so the answer
  is deferred first. The post itself notifies, as any message would.
- **`/status`** takes a `type` of Playing (the default, also for anything
  unrecognised, as in Java), Watching, Listening to, Competing in or Custom.
  A custom status shows just the text: Discord reads it from `state`, and
  wants the activity named "Custom Status". The last status is **saved**, and
  set again whenever the bot connects, so a restart or a dropped connection
  does not clear it.
- **`/shutdown`** awaits its reply before stopping, because an unanswered
  interaction shows its caller an error instead of the goodbye. It logs who
  asked.

## 3. The goodbye phrase

An **administrator** saying the phrase stops the bot. The bot replies
`ok bye bye!`, waits 1.5 seconds so the reply goes out, and shuts down.

- **Administrator in that server**, checked on the member (roles and
  overwrites), not on the user: an admin somewhere else does not count.
- **The phrase must be essentially the whole message**, so quoting it
  mid-sentence does nothing. Comparison is on lowercased word sequences, so
  `Say goodbye, LatiBot!` matches: punctuation and case make no difference.
- **A trailing emoji means no match.** Bytes above 127 count as word
  characters, so `say goodbye latibot 👋` is a different phrase. It stops the
  bot, and near enough is not good enough.
- The stage **consumes** the message, so nothing else answers it.

The phrase is per server, `say goodbye latibot` by default.
`/goodbye phrase:` sets it, `/goodbye off:true` turns it off, and `/goodbye`
alone shows it. Off is stored as an **empty phrase**, which never matches,
rather than by deleting the setting. Deleting it would fall back to the
default on the next read, which is the opposite of off.

## 4. How it works

The decisions are pure functions, so the rules above are tested directly:

- `plan_say(message, reply_to)`: send, reply, a bad id, or a blank message;
- `parse_activity_type` and `make_activity`;
- `is_goodbye(content, phrase)`;
- `save_status` and `load_status`, which keep the status under
  `config::bot_wide`, the server id 0 that stands for the bot itself.

`goodbye_stage` is the first stage of the
[message pipeline](Message_Pipeline.md). It returns a `send_message` and a
`stop_bot` with a 1.5 s delay. The shell runs the stop on a `std::jthread`
member that the bot joins when it is destroyed, so the goodbye's thread
cannot outlive the bot.

How each command's replies are flagged (private, public, silent) is
declared beside the command, per kind of message: see
[Commands_and_Panels.md](Commands_and_Panels.md#3-message-flags).
`/shutdown`'s public reply, like `/join`'s and `/leave`'s, is sent with
notifications suppressed. `/say`'s post is not: it is a message like any
other.

## 5. Decisions

| Date | Decision | Why |
|---|---|---|
| plan v4 | Keep all the Java basic commands, and add the goodbye phrase | They were used; the phrase stops the bot without a slash command |
| plan v4 | The goodbye needs Administrator in that server, and the phrase as the whole message | It stops the bot: quoting it, or being an admin elsewhere, must not |
| plan v4 | A trailing emoji means no match | Near enough is not good enough for something that stops the bot |
| plan v4 | Reply, wait, then shut down | The bot should not vanish mid-sentence |
| plan v4 | Off is an empty phrase, not a deleted row | A deleted row reads back as the default |
| 2026-09-21 | `/say` fetches the target first, and refuses whitespace | Otherwise both fail at the API with nothing shown to the caller |
| 2026-09-23 | `/say` needs Manage Messages, not Manage Roles as in Java (kept by the owner as cleanup decision 2) | Speaking as the bot is about messages; admins can change it per role either way |
| 2026-09-25 | `/join`, `/leave` and `/shutdown` answer publicly, as in Java (cleanup decision 1) | The room sees the bot come, go or stop, and should see why |
| 2026-09-25 | The last `/status` is kept and put back on connect (cleanup decision 3) | Java set a fixed status at every start; a restart should not clear what the owner chose |
| 2026-09-25 | Commands that wait on Discord before answering are deferred | Discord allows three seconds for the first response (cleanup DISC-004) |
| 2026-09-25 | The goodbye's delayed stop is a joined `std::jthread` | A detached thread could outlive the bot during shutdown (cleanup LIFE-001) |
| 2026-09-26 | Public replies suppress notifications | They are acknowledgements, not things to be pinged for |

## 6. What is still to check

In Discord, since each changed after the last live run:

- the goodbye phrase shutting the bot down cleanly;
- `/say reply:` and `/nickname`, now deferred;
- the status coming back after a restart.
