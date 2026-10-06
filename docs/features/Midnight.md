# Midnight messages

The bot posts a message shortly after midnight: once per local day, per
entry, in the entry's own timezone and channel. A server can have any number
of entries, managed with `/midnight`.

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](README.md#midnight) has the
commands and replies. [Classes.md §12](../architecture/Classes.md#12-triggers-nicknames-and-midnight-messages)
draws its classes, and [Execution_Flow.md §13](../architecture/Execution_Flow.md#13-the-timers)
its timer.

| | |
|---|---|
| **Module** | `midnight`, the first one (docs/modules/Module_Plan_Final.md): `src/core/events/midnight_module.*` registers the command and the timer |
| **Code** | `src/core/events/midnight.*`, `src/core/commands/midnight.*`, `src/core/events/midnight_module.*` |
| **Tests** | `tests/unit/midnight_test.cpp`, `tests/unit/midnight_command_test.cpp`, `tests/unit/midnight_module_test.cpp`, `tests/db/midnight_store_test.cpp` |
| **Tables** | `midnight_messages`, the module's schema version 1 (was migrations 5 and 9) |
| **Plan** | Replaces plan §10 and §21.10 |
| **Status** | Built in phase 2 (2026-09-23); the missed-midnight rule since 2026-09-24 |

## Contents

1. [Intent](#1-intent)
2. [Behaviour](#2-behaviour)
3. [How it works](#3-how-it-works)
4. [Decisions](#4-decisions)
5. [Limits](#5-limits)

## 1. Intent

The Java bot posted one midnight message, and sometimes posted it at random
times of day. The cause:

- It used `ScheduledExecutorService.schedule`, whose delay is measured on a
  **monotonic** clock, but it computed that delay from the **wall** clock.
- The two diverge when the machine sleeps or the system clock jumps. On
  resume, the task fired at whatever time it happened to be.
- The handler then computed the next run correctly, so things went back to
  normal for a while, which is exactly what was seen.
- Its `if (now.isAfter(nextMidnight))` branch, which logged `"huh???"`, could
  never run, since `nextMidnight` was always built as tomorrow.

The port fixes the cause, and makes the feature per server, per timezone,
with any number of entries.

## 2. Behaviour

### 2.1 When an entry posts

- **Once per local day**, from 00:00:05 in its timezone.
- **Not late.** The window closes five minutes into the local day. A
  midnight the bot was not running for is **skipped**, not posted over
  breakfast. The log says so once, naming the entry and how late it already
  is.
- **Never twice.** The date posted for is saved with the post, so a restart
  at 00:00:30 does not post again.
- **From the next midnight.** A new entry counts as having already posted
  today, so adding one at three in the afternoon does not post it half a
  minute later.
- Silent, with link previews, unless the entry says otherwise (`silent`,
  `previews`), as trigger replies are.

### 2.2 `/midnight`

Manage Server, by default, and every answer is private.

| Subcommand | Does |
|---|---|
| `list` | Each entry: id, channel, timezone, text, and the date it last posted |
| `add timezone channel message [silent] [previews]` | Adds one, starting from the next midnight there |
| `edit id [any of those]` | Changes only what is given, and **never** the date it last posted, so fixing a typo does not post it again the same day |
| `remove id` | Deletes it |
| `toggle id` | Turns it on or off |

`timezone` autocompletes from the machine's own timezone database, matching
anywhere in the name (`chicago` finds `America/Chicago`), 25 suggestions at a
time. A timezone the machine does not know is refused rather than stored.

## 3. How it works

**Poll the wall clock.** A 30-second cluster timer calls
`midnight_scheduler::tick`. For each enabled entry in every server, it reads
the clock in the entry's timezone and asks `verdict_for(entry, now)`, a pure
function:

| Verdict | When |
|---|---|
| `wait` | still the day it last posted for, before 00:00:05, or off |
| `post` | a new local day, between 00:00:05 and 00:05:00 |
| `missed` | a new local day, but past 00:05:00 |

Polling is immune to suspend, clock jumps and daylight saving, and because
the verdict is pure, both DST nights and an overnight gap are unit-tested.
The five-minute window is wider than the tick, so scheduling jitter and a
quick restart still post.

**Claim, then post.** `tick` marks each entry as fired
(`midnight_store::mark_fired`, which refuses a date already recorded) and
only then returns the `send_message` actions. A crash between deciding and
posting costs one message, rather than repeating it every thirty seconds.

**A missed day is not written down.** `last_fired_date` means "posted", and
writing a skipped day there would be a lie. It needs no marker either: the
time since midnight only grows, so the rest of that day answers `missed` on
its own, and the next midnight starts clean. The scheduler remembers in
memory which day it already logged as missed, so the log says it once.

**Time zones** come from `std::chrono::locate_zone`. MSVC's `<chrono>` reads
Windows' own ICU data (Windows 10 1903 and later), so no time-zone library is
needed. `already_posted_today(timezone, now)` gives a new entry its starting
date.

**Storage.** `midnight_messages(id, guild_id, channel_id, timezone, message,
enabled, last_fired_date NULL, message_flags)`, with the date as local
`YYYY-MM-DD`.

**Errors.** Each entry's claim is under its own catch, so one database error
costs only that entry until the next tick, and the timer itself is guarded,
so a throw cannot stop the ticks. The log says whether each post was
delivered, after Discord answers.

## 4. Decisions

| Date | Decision | Why |
|---|---|---|
| plan v4 | Per server, any number of entries, each with its own timezone and channel | One hard-coded message in one zone was the Java bot's limit, not a design |
| plan v4 | Poll the wall clock every 30 s rather than schedule a delay | A delay computed from the wall clock and waited on a monotonic one is the Java bug |
| plan v4 | Save the date posted for in the same statement that claims it | Makes a double post impossible, even across a restart |
| plan v4 | A pure `verdict_for` | DST nights and gaps are testable without waiting for one |
| 2026-09-23 | A new entry counts as posted today | Otherwise one added at 3 pm posts within 30 s; found by a failing test (plan §21.10) |
| 2026-09-23 | `edit` never touches the last-posted date | Fixing a typo must not post it again the same day |
| 2026-09-24 | Skip a midnight more than five minutes gone | Posting at whatever time the bot comes back is the Java bug's shape, from the other side (plan §21.10) |
| 2026-09-24 | Don't record missed days; log them once | `last_fired_date` means posted |
| 2026-09-25 | One failing entry or tick cannot stop the timer | A database error used to stop midnight posts until a restart (cleanup ERR-001) |
| 2026-09-25 | Posts silent by default; `silent` and `previews` per entry | As they always were, now choosable (plan §21.15) |

## 5. Limits

- The window is a constant, `midnight_window` (five minutes), not a
  setting.
- A timezone Windows' data does not know cannot be added. An entry whose
  zone stopped being known, say after a Windows update renamed it, would
  wait for ever **without a log line**: `verdict_for` treats an unknown zone
  as `wait`, since saying nothing beats posting at the wrong time.
- That the 30-second timer is started at all lives in the untested shell.
