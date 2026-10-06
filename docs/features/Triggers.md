# Triggers

A server's automatic replies. When a message matches a trigger's pattern,
the bot posts one of that trigger's responses, picked by weight. It is the
Java bot's `420` → "nice", generalised: any pattern, any number of
responses, a cooldown, and a panel to manage them.

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](README.md#trigger) says how to
use it, with every reply. [Classes.md §12](../architecture/Classes.md#12-triggers-nicknames-and-midnight-messages)
draws its classes. The triggers that ask the language model to answer
instead are [advanced triggers](Language_Model.md#26-advanced-triggers).

| | |
|---|---|
| **Module** | `triggers`: [its README](../../src/modules/triggers/README.md) lists what it owns |
| **Code** | `src/modules/triggers/src/`: `triggers.*`, `trigger_command.*` (the command and `trigger_panel`), `module.cpp` |
| **Tests** | `src/modules/triggers/tests/`, built as `latibot_triggers_tests`; a trigger beside a link in `tests/app` |
| **Tables** | `triggers`, `trigger_responses`, the module's schema version 1 (was migrations 2, 3 and 9) |
| **Plan** | Replaces plan §11 |
| **Status** | Built in phase 1 (2026-09-21); the panel's forms were fixed on 2026-09-29 and have not been seen working in Discord since |

## Contents

1. [Intent](#1-intent)
2. [Behaviour](#2-behaviour)
3. [How it works](#3-how-it-works)
4. [Decisions](#4-decisions)
5. [Limits, and what is still to check](#5-limits-and-what-is-still-to-check)

## 1. Intent

The Java bot answered `420`, `4:20` and `69` with "nice", hard-coded. People
liked it and wanted more of them, so triggers became data: per server,
editable without a restart, and safe to let anyone with Manage Messages
write. The one thing not offered is regular expressions. A user-written
regex is a performance risk and, with MSVC's recursive `std::regex`, a
stack-overflow risk on a path every message takes. A pattern is literal
text, and the only choice is whether it must stand alone as a word.

## 2. Behaviour

### 2.1 Matching

- **Whole word** (the default): `420` fires on `it is 420 somewhere` and on
  `(420)`, not on `4200`. **Every** occurrence is checked, so
  `4200 and also 420` matches.
- **Anywhere**: `cat` fires on `catastrophe`.
- Case is ignored on both sides.
- Every trigger that matches, is enabled and is off cooldown replies, so one
  message can set off several.

### 2.2 Replying

- The response is picked by **weight**. `3 | nice` and `very nice` make the
  first three times as likely. A trigger whose weights are all zero or
  negative says nothing.
- The reply is an ordinary message in the channel, not a Discord reply.
- It is **silent** by default, as the Java bot's were, and shows link
  previews. A trigger can be set to notify, or to hide previews
  (`silent`, `previews`).
- **The cooldown is per trigger, per channel**: 30 s by default, 0 to 86,400,
  and 0 turns it off. Two channels do not share one. Cooldowns are forgotten
  on restart, which costs at most one extra reply.
- A trigger **does not consume** the message: one with `420` and a link gets
  the reply and the [link replacement](Url_Replacement.md). A reply marks the
  message **answered**, so no [advanced trigger](Language_Model.md#26-advanced-triggers)
  also answers it.
- **Bots** reach triggers only where a server allowed them
  ([Message_Pipeline.md §2.1](Message_Pipeline.md#21-who-is-heard)), and
  even then a trigger answers one only with `bots:true`, off by default.

### 2.3 No defaults

A server starts with **no** triggers. The Java bot's `420`, `4:20` and `69`,
all answering "nice", are not added for anyone; a server that wants them adds
them with `/trigger add`. Servers that were given them before 2026-09-30
keep them until someone removes them, and nothing brings them back.

### 2.4 The command

| Subcommand | Does |
|---|---|
| `add pattern responses [mode] [cooldown] [bots] [silent] [previews]` | Adds a trigger and replies with its id |
| `edit id [any of those] [enabled]` | Changes only what is given, so fixing a cooldown does not mean retyping the responses |
| `remove id` | Deletes it and its responses |
| `list` | Eight a page, with ◀ / ▶ |
| `panel` | The list with editing attached |

Manage Messages by default, and every answer is private. A pattern is 1–200
characters, and the responses up to 2,000, one per line. A pattern of only
whitespace is refused, since it would match everything, and so are
responses that parse to nothing, since a trigger that matches and has
nothing to say is worse than none.

A list line reads `` `4` **420** (whole word, 30s) -> 2 responses``. It adds
`disabled`, `answers bots`, `notifies` or `no previews` only where a trigger
differs from the default.

### 2.5 The panel

Pick a trigger from the menu, then:

| Button | Does |
|---|---|
| **Edit** | A form with the pattern, responses, mode and cooldown |
| **Enable** / **Disable** | Toggles it; the label says what pressing it will do |
| **Answer bots** / **Ignore bots** | Toggles `bots` |
| **Reply silently** / **Reply with notifications** | Toggles `silent` |
| **Hide link previews** / **Show link previews** | Toggles `previews` |
| **Delete** | Confirms in the panel itself, so nothing is left behind if ignored |

**Add** opens the same form, empty. The form's mode takes `word` or
`anywhere`, and `whole word`, `whole_word` and `substring` also work. A field
it cannot read, such as "thirty" for the cooldown, keeps the value the
trigger had, and the panel says so under the list. After a save the panel
shows the trigger picked, on whichever page it landed. A page whose long
patterns would pass 2,000 characters has its lines shortened evenly.

## 3. How it works

The decisions are pure functions in `events/triggers.hpp`:

- `matches(content, pattern, mode)`;
- `choose(responses, roll)`, which takes the random number from its caller,
  so tests can check the distribution;
- `off_cooldown(last_fired, now, cooldown)`.

`trigger_responder` is the pipeline stage. It reads the server's triggers
**before** taking its lock, so no message waits on SQLite while holding up
another's cooldown check. Then, for each trigger that matched, it checks the
cooldown, picks a response and claims the cooldown under **one lock**. 4:20
is exactly when several people post at once, and two messages must not both
find the trigger ready. Cooldowns live in memory, keyed by
`(trigger id, channel)`, on the steady clock.

Every way a matched trigger stays quiet is logged at `debug` with the
reason: disabled, a bot it does not answer, on cooldown (with how long is
left), or nothing to pick. That is what gets asked when somebody says a
trigger "stopped working".

`trigger_store` holds `triggers` and `trigger_responses`. Responses are
replaced wholesale on every edit, which is how both the command and the form
work. A server's responses are read in one query. `message_flags` keeps only
the flags a channel message can carry (silent, no previews), and defaults to
silent.

`trigger_panel` keeps no state on the bot's side. The page and the chosen
trigger travel in each component's `custom_id` (views `trigpanel`,
`trigpick`, `trigedit`, `trigdel`, `trigyes`, `trigadd`, `trigform`,
`trigonoff`, `trigbots`, `trigsilent`, `trigprev`, and `triggers` for the
list's pages). So it survives a restart, has nothing to expire, and two
people can each have their own open. Menu labels are cut to Discord's 100
characters, so a long pattern cannot break the panel for the whole server.

## 4. Decisions

| Date | Decision | Why |
|---|---|---|
| plan v4 | Literal patterns, whole word or anywhere; no regular expressions | A user's regex is a performance and stack-overflow risk on every message |
| plan v4 | A cooldown per trigger per channel, 30 s by default, 0 allowed | A joke repeated in one channel is noise; another channel has not seen it |
| plan v4 | The randomness comes from the caller | The weighted distribution can be tested |
| plan v4 | Seed the Java defaults only into a server with no triggers | Deleting them must not bring them back on the next restart. Superseded on 2026-09-30, below |
| plan v4 | Triggers do not consume the message | The Java bot's early return cost a message its link replacement |
| 2026-09-22 | A panel with a menu, buttons and forms, all state in `custom_id` | It survives restarts and nothing on the bot's side expires |
| 2026-09-23 | Answering bots is per trigger, off by default | Hearing a bot and answering it are separate decisions |
| 2026-09-23 | The responses label is under 45 characters; the weight syntax went to the placeholder | Discord refuses the whole modal over a long label (plan §21.4) |
| 2026-09-25 | One lock around the cooldown check, the pick and the claim | Two messages at 4:20 could both find a trigger ready (cleanup CONC-002) |
| 2026-09-25 | Menu labels are truncated to 100 characters | A long pattern broke the panel for the whole server (cleanup DISC-002) |
| 2026-09-25 | Replies are silent by default; `silent` and `previews` per trigger | The Java bot's were silent; some triggers want to notify or hide previews (plan §21.15) |
| 2026-09-28 | A reply marks the message answered | The free, simple reply wins over an advanced trigger's paid one |
| 2026-09-29 | The form keeps a field it cannot read, and says so | Resetting a cooldown because of a typo in it is worse than keeping it |
| 2026-09-30 | **No default triggers**; `seed_defaults` removed (the owner's decision) | The seeding checked for no triggers at all, so deleting every trigger brought the three back at the next connect. The bot need not add them; a server that wants them can |

## 5. Limits, and what is still to check

- Cooldowns are forgotten on restart.
- A pattern matches message text only: not embeds, attachments or edits.
- **Still to check in Discord:** that the panel's forms save, since the fix
  of 2026-09-29 (plan §21.21) was tested only through DPP's own interaction
  handling.
