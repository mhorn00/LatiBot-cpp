# What LatiBot does

Everything the bot does today, as it actually behaves — commands, options, who
may run them, what it replies, and the edge cases each one handles. Features not
built yet are in [Planned.md](Planned.md), written the same way.

This is the reference for *intended* behaviour, so it is the right place to
disagree with something. A correction here is what the implementation follows;
the design and the order of work behind it are in
[docs/porting/Porting_Plan_Final.md](../porting/Porting_Plan_Final.md).

---

## At a glance

| | Feature | What it is for |
|---|---|---|
| 💬 | [`/ping`](#ping) | Is the bot alive, and how far away is it |
| 💬 | [`/say`](#say) | Post as the bot, optionally as a reply |
| 💬 | [`/status`](#status) | Set the bot's presence |
| 🔊 | [`/join`, `/leave`](#join--leave) | Move the bot in and out of a voice channel |
| 🛑 | [`/shutdown`](#shutdown) | Stop the bot |
| 🛑 | [`/goodbye`](#goodbye) | Configure the phrase that stops the bot |
| 🗣 | [`/trigger`](#trigger) | Manage automatic replies to phrases |
| 🤖 | [`/bots`](#bots) | Choose which other bots the bot may hear |
| 🏷 | [`/nickname`](#nickname) | Change somebody's nickname, on the record |
| 🏷 | [`/nicknames`](#nicknames) | Every nickname somebody has had here |
| 🌙 | [`/midnight`](#midnight) | Post a message at midnight |
| 🛑 | [The goodbye phrase](#the-goodbye-phrase) | Stop the bot by saying so, no slash command |
| 🗣 | [Trigger responses](#trigger-responses) | The "420 → nice" behaviour, generalised |
| 🏷 | [Nickname tracking](#nickname-tracking) | Records every nickname change, and who made it |
| 🌙 | [The midnight message](#the-midnight-message) | Posts once per local day, per timezone |
| 🔒 | [Permission warnings](#permission-warnings) | Says what it cannot do in a server, at startup |

Every command replies **ephemerally** — only the person who ran it sees the
answer. Where a command posts something publicly, such as `/say`, that is a
separate message and is called out below.

**Who may run what** is set as Discord's *default member permission*. Server
admins can override any of it per role or per channel in
**Server Settings → Integrations → LatiBot**, which is why these are defaults
rather than hard checks.

---

## Commands

### `/ping`

Round-trip and gateway latency.

| | |
|---|---|
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
| **Options** | `message` (required, 1–2000 characters) · `reply` (optional, a message id) |
| **Who** | Manage Messages, by default |
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
| **Options** | `status` (required, 1–128 characters) · `type` (optional: Playing, Watching, Listening to, Competing in, Custom) |
| **Who** | Manage Nicknames, by default |
| **Where** | servers and DMs |
| **Bot needs** | nothing |

Replies `status set to: <text>`. Presence is global — the bot has one, across
every server it is in.

`type` defaults to **Playing** when omitted or unrecognised, which is what the
Java version did. **Custom** is the odd one out: Discord takes a custom status's
text from a different field than the others and expects the activity itself to
be named "Custom Status", so a custom status shows just your text with no
"Playing" prefix.

### `/join` / `/leave`

Moves the bot in and out of a voice channel.

| | |
|---|---|
| **Options** | `/join`: `user` (optional — whose channel to join) |
| **Who** | Speak, by default |
| **Where** | servers only |
| **Bot needs** | Connect, and Speak for `/join` |

`/join` with no `user` joins the channel **you** are in; with one, it follows
that person. If the bot is already in a different channel in the same server it
moves rather than refusing.

| Situation | Reply |
|---|---|
| Joining | `ok joining` |
| Already connected elsewhere in this server | `ok moving` |
| Already in the right channel | `i'm already in your voice channel` / `i'm already in their voice channel` |
| The target is not in a voice channel | `you're not in a voice channel` / `they're not in a voice channel` |
| The gateway connection is unavailable | `i can't reach the gateway right now` |
| `/leave` when not connected | `i'm not in a voice channel` |
| `/leave` when connected | `ok bye` |

Nothing plays yet — these exist because text-to-speech in phase 4 needs them,
and because they are useful on their own.

### `/shutdown`

Stops the bot.

| | |
|---|---|
| **Options** | none |
| **Who** | Administrator, by default |
| **Where** | servers only |
| **Bot needs** | nothing |

Replies `ok bye bye!` and then shuts down. The reply is **awaited** rather than
queued, because the process is about to stop and an unanswered interaction shows
the caller an error instead of a goodbye. The shutdown is logged with the name
of whoever asked.

### `/goodbye`

Shows or changes [the goodbye phrase](#the-goodbye-phrase) for this server.

| | |
|---|---|
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

### `/trigger`

Manages this server's automatic replies. See
[Trigger responses](#trigger-responses) for how they behave.

| | |
|---|---|
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
`5` **69** (anywhere, no cooldown, disabled, answers bots) -> 1 response
```

#### `/trigger panel`

The same list with editing attached. Pick a trigger from the menu and four
buttons appear:

| Button | Does |
|---|---|
| **Edit** | Opens a form with the pattern, responses, mode and cooldown filled in |
| **Enable** / **Disable** | Toggles it. The label says what pressing it will do |
| **Answer bots** / **Ignore bots** | Toggles whether it replies to allowed bots |
| **Delete** | Asks to confirm in the panel itself, so nothing is left behind if ignored |

**Add** opens the same form, empty.

The form keeps fields it cannot read rather than resetting them: typing
"thirty" into the cooldown box leaves the cooldown you had. A blank pattern or
no responses is refused with an explanation, and nothing is saved.

The panel survives restarts and has nothing to expire, because which page and
which trigger is selected are carried in the buttons themselves rather than
remembered on the bot's side. It is ephemeral, so two people can each have their
own open.

### `/bots`

Chooses which other bots LatiBot may hear. See
[Trigger responses](#trigger-responses) for why this exists.

| | |
|---|---|
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
| **Options** | `user` (required) |
| **Who** | everyone |
| **Where** | servers only |
| **Bot needs** | Send Messages |

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
| **Who** | Manage Server, by default |
| **Where** | servers only |
| **Bot needs** | Send Messages |

| Subcommand | Options | Reply |
|---|---|---|
| `list` | none | Each entry with its id, channel, timezone, text and the date it last posted |
| `add` | `timezone` (required, autocompleted) · `channel` (required) · `message` (required) | `ok, that posts in #general at the next midnight in America/Chicago` |
| `edit` | `id` (required) plus any of `timezone`, `channel`, `message` | The entry as it now stands |
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

---

## Passive behaviour

Things the bot does without being asked.

### The goodbye phrase

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

When a message matches one of this server's triggers, the bot replies with one
of that trigger's responses, chosen by weight.

New servers start with the three the Java bot had — `420`, `4:20` and `69`, all
answering "nice" — seeded **only when the server has none**, so deleting them
does not bring them back on the next restart.

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
- Triggers **do not consume the message**. One containing both `420` and a link
  will get the reply *and* the link replacement once that exists — the Java
  version's early `return` meant it got only the first of those.

**Other bots are ignored unless the server allows them.** Two bots answering
each other is a loop nobody asked for, so the default is silence. Allowing a bot
with [`/bots`](#bots) lets its messages reach the bot's features; each trigger
then decides separately whether it answers, with `bots:true`. Hearing and
answering are deliberately two decisions: a server-wide "I'll listen to DiceBot"
should not turn every existing trigger loose on it.

The bot never answers itself, and no setting changes that.

### Nickname tracking

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
`"track_nicknames": false` in `config.json` turns the whole thing off,
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

Each entry posts **once per local day**, shortly after midnight in its own
timezone, in the channel it was given. Configure them with
[`/midnight`](#midnight).

The date an entry last posted for is saved with the post, which is what makes a
restart at 00:00:30 not post a second time.

The bot checks the clock every thirty seconds rather than scheduling a delay.
That is deliberate, and it is the fix for a Java bug worth naming: the old
version computed a delay from the wall clock and then waited on a monotonic
timer, so whenever the machine slept the message arrived at whatever time it
happened to wake up — and then behaved normally again for a while.

**An entry that missed its midnight posts when the bot comes back.** If the bot
was not running at midnight, the first check after it starts sees a new local
date and posts then. Late is treated as better than never.

### Permission warnings

When the bot joins a server — and for every server it is already in, each time
it starts — it checks what it can do there against what its features need, and
**logs a warning for anything missing**. It never exits and never disables
itself: a server missing one permission loses one feature there, not the whole
bot.

The warning names the missing permissions and what they were for, for example
`missing Send Messages for replying to messages`. Only what is actually missing
is reported, and a server that granted Administrator is never warned about
anything, since Discord treats it as everything.

Nothing is posted to Discord — this goes to the bot's own log.

---

## Behind the scenes

Not user-facing, but worth knowing when something looks wrong.

**Settings are per server.** The goodbye phrase, triggers and the bot allowlist
are all stored per server, in a SQLite database at `data/bot.db`. Two servers
never see each other's anything.

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

**Everything is logged.** Every command records who ran it, what they passed
and what it did; so does every button and form in a panel, every message the
bot posts on its own, and each start, stop and schema change. Turning the level
up to `debug` adds the reasoning — which trigger matched, why one stayed quiet,
what a cooldown had left to run. Nothing is posted to Discord; it all goes to
the bot's own log. See [Logging](../../README.md#logging) for how to set the
level.

**The bot needs two privileged intents**, both enabled for the application
under *Bot → Privileged Gateway Intents* in the Discord developer portal:

- **Message Content.** Without it every message arrives empty: slash commands
  keep working while the goodbye phrase and every trigger silently do nothing.
- **Server Members.** Without it no nickname change is ever seen. This one can
  be turned off with `"track_nicknames": false`, which also stops the bot
  asking for it.

A bot that asks for an intent it was not granted is refused the gateway
entirely, and reconnects in a loop. The log says which toggle to go and find
when that happens.

---

## What is coming

In order, with the detail in [Planned.md](Planned.md):

| Phase | Features |
|---|---|
| 3 | URL replacement · reaction statistics · the history backfill |
| 4 | DECtalk speech · `/speak` · custom voices · voice sessions |
| 5 | The LLM: replies, memory, personality, advanced triggers |
| Later | Music · emote statistics · appearance tracking |
