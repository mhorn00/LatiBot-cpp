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
| 🔗 | [`/urlrepl`](#urlrepl) | Turn link replacement on, and choose which links get posted again with a working preview |
| 🔗 | [`/urltoggle`](#urltoggle) | Have your own links left alone |
| 📊 | [`/linkstats`](#linkstats) | Who gets the most reactions on replaced links |
| 🛑 | [The goodbye phrase](#the-goodbye-phrase) | Stop the bot by saying so, no slash command |
| 🗣 | [Trigger responses](#trigger-responses) | The "420 → nice" behaviour, generalised |
| 🔗 | [URL replacement](#url-replacement) | Posts poor-preview links again on a mirror that previews properly, once a server turns it on |
| 📊 | [Reaction statistics](#reaction-statistics) | Counts reactions on those, three ways |
| 🏷 | [Nickname tracking](#nickname-tracking) | Records every nickname change, and who made it |
| 🌙 | [The midnight message](#the-midnight-message) | Posts once per local day, per timezone |
| 🔒 | [Permission warnings](#permission-warnings) | Says what it cannot do in a server, at startup |

Commands reply **ephemerally** by default — only the person who ran it sees the
answer. The exceptions are called out below: `/say` posts a separate public
message; [`/nicknames`](#nicknames) and the [`/linkstats`](#linkstats) views
answer publicly, because those are things a room reads together; and
[`/join`, `/leave`](#join--leave) and [`/shutdown`](#shutdown) answer publicly,
because the room sees the bot come and go and should see why.

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

Replies `ok bye bye!`, publicly, and then shuts down. The reply is **awaited** rather than
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

### `/urlrepl`

Manages which sites' links are posted again on a mirror, and whether that
happens in this server at all. See [URL replacement](#url-replacement) for
what happens to a link.

| | |
|---|---|
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
| `enable` | `Link replacement is on in this server. Its 3 rules apply from now on; /urlrepl list shows them.` With no rules yet it says so and points at `set`. |
| `disable` | `Link replacement is off in this server. The rules are kept, so /urlrepl enable picks up where it left off.` |
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
rather than adding a second one. Delete asks to confirm in the panel itself.
Like the trigger panel it keeps nothing on the bot's side, survives restarts,
and is ephemeral.

`/urlrepl` needs a subcommand; Discord does not let a command with
subcommands run without one, so the panel is `/urlrepl panel` rather than
`/urlrepl` on its own.

### `/urltoggle`

Stops the bot replacing your links in this server, or starts it again.

| | |
|---|---|
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

Reactions on the bot's replacement messages. See
[Reaction statistics](#reaction-statistics) for what is counted.

| | |
|---|---|
| **Who** | everyone for the views; Manage Server for aliases and `recompute` |
| **Where** | servers only |
| **Bot needs** | Send Messages, and Read Message History for `recompute` |

The views answer **publicly**, and nobody is pinged by appearing in one.

#### `/linkstats top`

A leaderboard, ten a page with ◀ / ▶ that anybody can use.

| Option | Meaning |
|---|---|
| `by` | **Reactions received** (default), **Reactions given**, **Reactions to your own links**, or **Most used emojis** |
| `emoji` | Only this emoji. Autocompleted from the ones used here; typing a name like `skull` also works |
| `since` / `until` | `YYYY-MM-DD`, in UTC. `until` includes the day typed |
| `domain` | Only links to this site. Autocompleted |

```
Most 💀 received on replaced x.com links since 2025-01-01
1. @worm 42
2. @latios 17
```

#### `/linkstats user`

One person — you, unless `user` names somebody — with the same date and site
filters:

```
Link stats for @worm
Reactions received: 120 (💀 40, 😂 30, 🔥 12)
Reactions given: 80 (💀 25, 😭 20, 👀 9)
Reacted to their own links: 5 times
```

#### `/linkstats emojis`

Custom emojis that share a name — usually one emote uploaded twice, or deleted
and uploaded again, which Discord treats as a brand new emoji. Each group is a
candidate for an alias.

#### `/linkstats alias`

| Subcommand | Options | Reply |
|---|---|---|
| `add` | `emoji` · `as` (both required, autocompleted) | `💀 counts as ☠️ now, in every statistic back to the start.` |
| `remove` | `emoji` | `💀 counts as itself again.` |
| `list` | none | Every alias here |

Aliases apply **when statistics are read**, so adding one changes all of
history at once and removing it puts history back. An alias of an alias is
pointed straight at the end of the chain, and one that would make two emojis
count as each other is refused.

#### `/linkstats recompute`

Reads channel history back into the statistics, so they start with years of
reactions rather than from the day the bot began counting.

| Subcommand | Options |
|---|---|
| `start` | `since` (required, `YYYY-MM-DD`) · `until` · `channel` (every text channel if left out) · `fresh` |
| `cancel` | none |

It replies `Started. Progress goes in this channel.` and posts a progress
message there, updated every five hundred messages; an interaction's reply
stops being editable after fifteen minutes, and a recompute can take hours.
The finished message reports channels, messages scanned, replacements found,
how many were credited to whoever posted the link and how many were not, any
webhook replacements skipped, reactions recorded, the ids of anything it did
not understand, and any channel it could not read.

**It is safe to run again.** Each replacement's reactions are rebuilt to match
what Discord shows now rather than added to, and a reaction the bot saw being
added keeps the time it saw. How far it got is saved per channel, so running it
again with the same dates carries on from where it stopped — after
`cancel`, or a restart — and `fresh:true` starts every channel over. One runs
per server at a time. Threads are not scanned.

It looks for replacements posted by the bot's own account. A Debug build can be
pointed at another account's with `LATIBOT_DEBUG_RECOMPUTE_BOT_ID`, for testing
with a second bot; see
[Testing with a second bot account](../../README.md#testing-with-a-second-bot-account).

What it recognises, and how it finds whose link each one was, is under
[Reaction statistics](#reaction-statistics).

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
- **Replies are silent**, the way the Java bot's were: they notify nobody. A
  trigger can be set to notify, or to hide link previews in its replies, with
  `silent` and `previews` on [`/trigger add`](#trigger-add) and `edit`, or the
  panel's buttons.
- Triggers **do not consume the message**. One containing both `420` and a link
  gets the reply *and* the [link replacement](#url-replacement) — the Java
  version's early `return` meant it got only the first of those.

**Other bots are ignored unless the server allows them.** Two bots answering
each other is a loop nobody asked for, so the default is silence. Allowing a bot
with [`/bots`](#bots) lets its messages reach the bot's features; each trigger
then decides separately whether it answers, with `bots:true`. Hearing and
answering are deliberately two decisions: a server-wide "I'll listen to DiceBot"
should not turn every existing trigger loose on it.

The bot never answers itself, and no setting changes that.

### URL replacement

When somebody posts a link to a site with a [rule](#urlrepl) — x.com,
tiktok.com, reddit.com, instagram.com — the bot posts it again on a mirror that
previews properly, and turns the preview on the original off.

**Only in servers that turned it on**, with [`/urlrepl enable`](#urlrepl) or
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
rules and [`/urlrepl set`](#urlrepl) adds them. Either way the rules do nothing
until the server runs `/urlrepl enable`; importing does not turn it on.

### Reaction statistics

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

A message counts when the bot wrote it and it links to a mirror any rule has
ever used — mirrors are remembered after a rule changes, so old messages are
still recognised. Anything that looks like a replacement but matches none of
these is **reported with its id, never guessed at**.

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

### Permission warnings

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
URL rules, opt-outs and emoji aliases are all stored per server, in a SQLite
database at `data/bot.db`. Two servers never see each other's anything.

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
| 4 | DECtalk speech · `/speak` · custom voices · voice sessions |
| 5 | The LLM: replies, memory, personality, advanced triggers |
| Later | Music · emote statistics · appearance tracking |
