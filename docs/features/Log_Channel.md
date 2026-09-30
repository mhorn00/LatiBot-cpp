# The log channel

The bot can post its own log in one Discord channel, as well as to its
console, from a level of its own. `/logs` chooses the channel and the
level. There is one for the whole bot, and only the people in
`trusted_users` can set it.

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](README.md#logs) has the
subcommands and replies, and the root README's
[Logging](../../README.md#logging) covers the console log and its levels.
[Classes.md §13](../architecture/Classes.md#13-configuration-and-logging)
draws its classes.

| | |
|---|---|
| **Code** | `src/core/events/log_channel.*`, `src/core/commands/logs.*`, the tap in `src/core/util/log.*` |
| **Tests** | `tests/unit/log_channel_test.cpp`, `tests/unit/logs_command_test.cpp`, `tests/unit/log_test.cpp` |
| **Tables** | `guild_settings`, under the bot-wide server id 0 |
| **Config** | `trusted_users` in `config.json` |
| **Plan** | Replaces the log channel paragraph of plan §5.1 |
| **Status** | Built on 2026-09-27 |

## Contents

1. [Intent](#1-intent)
2. [Behaviour](#2-behaviour)
3. [How it works](#3-how-it-works)
4. [Decisions](#4-decisions)
5. [Limits](#5-limits)

## 1. Intent

Without it, reading the log means being at the machine the bot runs on.
Posting it in a channel puts it in front of the people who can act on it:
the warning that the language model hit its spending cap, for one, or the
reasoning at `debug` behind a trigger that stayed quiet.

The log covers **every** server the bot is in. So there is one channel,
bot-wide, and it is chosen only by people the bot's own `config.json`
trusts, not by any server's administrators.

## 2. Behaviour

| Subcommand | Does |
|---|---|
| `set channel [level]` | Posts a first message there, and only if that works, points the log at it. `level` defaults to the current one, or `info` the first time |
| `level level` | Changes only the level |
| `off` | Stops posting, and drops what was waiting |
| `show` | Where it goes, from which level, how many lines wait, and whether posting is failing, why, and when it tries next |

- **Who.** Only users in `trusted_users`. The command is shown to
  Administrators by default, and anyone else is told why not.
- **Levels** are `error`, `warn`, `info`, `debug` and `trace`, each taking
  everything above it. The channel's level is **independent** of the
  console's: a console at `info` and a channel at `debug` each get their own
  share. `trace` is everything, including the text of every message the bot
  sees in every server, which puts every server's conversations in the
  channel.
- **Posting.** Lines are gathered and posted every **2 s**, as few messages
  as fit, at most **2** a time: Discord allows five messages every five
  seconds in one channel. Each message is a code block of
  `12:34:56 [info] …` lines, in UTC, silent, with no previews or mentions.
- **Floods.** Up to **1,000** lines wait. Past that the **oldest are kept**
  and the rest counted, since the start of a flood is usually the part worth
  reading, and the next message says how many were dropped. A line is cut at
  1,900 characters, so one always fits.
- **Failures.** If posting fails (the channel was deleted, or the bot lost
  access), what was being posted is lost, the console gets one warning, and
  the bot waits **30 s** before trying again, doubling each time up to
  **15 minutes**. Lines keep waiting meanwhile. Moving to another channel
  forgets the failure, since it was likely the old channel's.
- **Secrets.** The bot's token and API keys are replaced by asterisks in
  anything posted, in case anything ever logs one.
- **Backticks.** Runs of backticks in a line are broken up, so no line can
  close its code block.
- **Restarts.** The setting is kept. What was logged while starting up is
  posted once the bot has connected. Lines from the last moments before a
  shutdown may not make it.

## 3. How it works

The logger (`util::log`) has a second output, a **tap** with its own level.
`log_channel::start` hooks a `log_buffer` into it:

- `log_buffer::push` runs on whichever thread logged, under the logger's own
  lock. So it does nothing but copy the line in, masking secrets.
- A 2-second cluster timer calls `log_channel::flush`, which takes up to two
  messages' worth (`take`) and posts them through the `discord_gateway`
  port. A flush still running, or a failure being waited out, skips the
  tick.
- `log_channel`'s own mutex is never held while logging, since the logger
  calls into the buffer with its lock held, and the other order would
  deadlock.

`log_destination_store` keeps `{guild, channel, level}` in `guild_settings`
under `config::bot_wide` (server id 0), so there is only ever one, and it
survives a restart. `/logs set` posts `log_channel_greeting(level)` first,
which is also how it learns the bot can post there. A refusal is reported
with Discord's reason, and the old setting is kept.

## 4. Decisions

| Date | Decision | Why |
|---|---|---|
| 2026-09-27 | One channel for the whole bot | The log is the bot's, not a server's |
| 2026-09-27 | Only `trusted_users` can set it, not the admins of `trusted_guilds` | The channel's readers see every server's log |
| 2026-09-27 | A level of its own, through a second output on the logger | The channel can take lines the console does not, and the reverse |
| 2026-09-27 | Every 2 s, at most 2 messages; back off from 30 s to 15 min on failure | Inside Discord's per-channel limit; a dead channel costs an occasional request, not one a tick |
| 2026-09-27 | Keep the oldest 1,000 lines in a flood, and count the rest | The start of a flood is usually the part worth reading |
| 2026-09-27 | Mask the token and API keys | A channel is far more public than a console |
| 2026-09-27 | `set` posts before it changes anything | A channel the bot cannot post in is refused, and the old one kept |

## 5. Limits

- Lines logged just before a shutdown may never be posted.
- A `trace` channel carries every server's messages. Nothing stops a
  trusted user choosing it; the user guide warns.
- The 2-second timer's start lives in the untested shell.
