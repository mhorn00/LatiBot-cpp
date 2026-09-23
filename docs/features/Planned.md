# What LatiBot will do

Features that are designed but not built, in the order they are planned, written
the same way as [what already exists](README.md): what each command does, its
options, who may run it, and how it should behave.

**This is the document to correct.** Nothing here is written yet, so changing a
reply, an option name or a rule costs nothing now and costs a rewrite later. The
technical design behind each one — schemas, algorithms, the bugs being fixed —
is in [docs/porting/Porting_Plan_Final.md](../porting/Porting_Plan_Final.md);
this is the user-facing side of the same thing.

Command names, option names and reply wording are all proposals.

---

## Phase 2 — nicknames and midnight

### `/nickname` — change someone's nickname

| | |
|---|---|
| **Options** | `user` (required) · `nickname` (optional — omit to clear it) |
| **Who** | Manage Nicknames, by default |
| **Bot needs** | Manage Nicknames |

Changes the nickname and records **who ran the command** in the history, which
is the part Discord's own audit log gets wrong: it attributes the change to the
bot, because the bot is what called the API.

The server owner's nickname cannot be changed by any bot, so the command says
so rather than failing silently. If the change fails, the history entry is
removed again — history should never claim something that did not happen.

### `/nicknames` — someone's nickname history

| | |
|---|---|
| **Options** | `user` (required) |
| **Who** | everyone |
| **Bot needs** | Send Messages |

Every nickname that user has had in this server, newest first, paginated with
◀ / ▶ the way `/trigger list` is. Each entry shows the nickname, when it
changed, and who changed it.

"Who" is one of: a named person, **unknown** when nothing could attribute it, or
nothing at all for entries imported from the Java bot's records. Cleared
nicknames show as *(cleared)* rather than blank. People who have left the server
still appear, by name where it is known and by id otherwise.

Very long histories get a `.txt` attachment instead of pages. The Java version
simply gave up past 2000 characters, which is most histories by now.

### Nickname tracking (passive)

Every nickname change in the server is recorded, however it was made: through
`/nickname`, through Discord's UI by a moderator, or by the person themselves.

Attribution is best-effort and layered. The change is recorded **immediately**
with the author unknown, then filled in when Discord's audit log entry arrives,
with one delayed lookup as a safety net if it does not. Recording never waits on
attribution — a change with no name against it is much better than a change that
was missed. Anything still unattributed stays **unknown**; the Java version
guessed "the user did it themselves", which was often wrong.

Changes made while the bot was **offline** are noticed at startup and recorded
with no author, since there is no way to know.

Years of existing history import from the Java bot's `nicknames.json`. Its
timestamps have no timezone and are US Central wall-clock, so they are converted
per date using the full daylight-saving history — including the 2007 rule
change, the ambiguous hour each November, and the hour that does not exist each
March. The original text is kept alongside each row, so the conversion can be
redone if it turns out to be wrong.

### `/midnight` — the midnight message

| | |
|---|---|
| **Who** | Manage Server, by default |
| **Bot needs** | Send Messages |

The bot posts a message at midnight. Any number of entries per server, each with
its own timezone, channel and text, so different timezones can say different
things.

| Subcommand | Options |
|---|---|
| `list` | — |
| `add` | `timezone` (autocompleted) · `channel` · `message` |
| `edit` | `id` plus any of the above |
| `remove` | `id` |
| `toggle` | `id` — turn one off without deleting it |

Each entry fires **once per local day**, shortly after midnight in its own
timezone, and the date it last fired is saved. That is what makes a restart at
00:00:30 not post a second time.

This replaces a Java bug worth naming: the old scheduler computed a delay from
the wall clock and then waited on a monotonic timer, so whenever the machine
slept, the message fired at whatever time it happened to wake up, then behaved
normally again for a while.

---

## Phase 3 — URL replacement

The most-used feature of the Java bot, and the buggiest. Rebuilt rather than
ported.

### Replacing links (passive)

When someone posts a link to a site whose embeds are poor — x.com, tiktok.com,
reddit.com, instagram.com — the bot posts a mirrored version that embeds
properly, and suppresses the preview on the original.

The replacement is a **plain message, not a reply**:

```
:link: [_](https://fxtwitter.com/…)
```

A spoilered link stays spoilered. **Every** link in a message is replaced, not
just the first — in the Java version a second link was silently ignored, and
spoilers never worked at all because of a regular-expression mistake.

Mirrors are tried in order, **twice each**. The bot watches for the embed to
actually appear rather than polling on a fixed delay, so a slow network no
longer looks like failure, and moves on after about six seconds without one.

**When every mirror fails**, the original's preview is restored and the bot's
message becomes a short failure note with a **Retry** button. **Anyone can press
it.** Retry makes one more pass; if it works the replacement comes back and the
original is suppressed again. The button keeps working after a restart.

Some rules can also add `/en` to the mirrored link, which makes those mirrors
translate the post. Where a rule has it, it is always applied.

**Per-user opt-out** means the bot leaves your links alone. Unlike the Java
version, that choice survives a restart.

### `/urlrepl` — manage the rules

| | |
|---|---|
| **Who** | Manage Server, by default |

`/urlrepl` on its own opens a panel: one row per domain with its mirrors in
priority order, Edit and Delete buttons, Add rule, and paging. Edit and Add open
a form with the domain and **the mirrors one per line**, so reordering is just
rewriting lines.

The commands stay for quick use:

| Subcommand | Does |
|---|---|
| `list` | The rules, paginated |
| `set` | Replace a domain's whole ordered mirror list in one go |
| `remove` | Delete a rule |
| `test <url>` | **Dry run** — shows exactly what the bot would post, without posting |

Domain names autocomplete, which removes the "what did I call it again"
problem. `test` is the one that makes misbehaviour debuggable.

### `/linkstats` — reaction statistics

| | |
|---|---|
| **Who** | everyone for the views; Manage Server for aliases and the backfill |

Reactions on replacement messages, counted three ways:

- **Received** — credited to the person who **posted the original link**. "Who
  gets the most 💀."
- **Given** — the same reactions counted by **who reacted**. "Your top three
  reactions."
- **Self-reactions** — reacting to your own link. Recorded, **excluded from both
  of the above**, and reported on their own, because "who likes their own posts
  the most" is funnier as its own leaderboard than as noise in everyone else's
  numbers.

Views: top posters by reactions received, top reactors, top emoji, a per-user
breakdown, and the self-reaction leaderboard — all filterable by domain and date
range, and paginated. Nothing is ever aged out.

**Emoji aliases** merge emotes that should count as one: the same emote from a
different server, or one that was deleted and re-added, which Discord treats as
a brand new emoji. `/linkstats alias add | remove | list`, plus
`/linkstats emojis` to list likely duplicates by name. Aliases apply **when
statistics are read**, so adding one immediately fixes all history rather than
only counting from now on.

### `/linkstats recompute` — the backfill

| | |
|---|---|
| **Options** | `since` (required) · `until` · `channel` |
| **Who** | Manage Server |
| **Bot needs** | Read Message History |

Walks back through history and recovers reactions from the thousands of
replacement messages that already exist, so the statistics start with years of
data rather than from zero.

It recognises all six message formats the bot has used over the years. The
webhook-era ones are skipped — the original message was deleted, so there is
nobody to credit, and few of them have reactions. Anything that looks like a
replacement but matches no known format is **counted and reported with its id**,
never guessed at.

The original poster is found by walking back to **the first earlier message
containing a link**, which handles someone chatting in between. The link's path
is checked against the replacement as confirmation, and a mismatch is reported
rather than accepted.

Progress is posted and updated as it goes; `/linkstats recompute cancel` stops
it; an interrupted run resumes where it left off. **Running it twice is safe.**
The final report gives messages scanned, replacements found, attributed,
unattributed, unparsed, and reactions recorded.

One limitation, which is Discord's rather than a choice: **the API does not say
when a reaction was added.** Recovered reactions know who and what, but not
when, so date filters fall back to the message's own timestamp for them.

---

## Phase 4 — speech

DECtalk is the 1980s text-to-speech engine the Java bot used. It is the reason
`/join` and `/leave` exist already.

### `/speak` — say something in voice

| | |
|---|---|
| **Options** | `text` (required) · `voice` · `rate` · `volume` |
| **Who** | everyone |
| **Bot needs** | Connect, Speak |

Speaks `text` in the voice channel the bot is in. `voice` autocompletes the
built-in voices plus this server's saved custom ones.

Speech **starts about a quarter of a second in** rather than after the whole
utterance is synthesized, and no `.wav` file is ever written — the Java version
wrote files and then had nothing to clean them up.

**Inline commands work.** DECtalk's own `[:rate 120]`, `[:pause]`, `[:dv …]` and
so on are part of the fun and pass through by default. A few are stripped
silently, because they read and write files on the machine the bot runs on:
`[:play]`, `[:log]`, `[:debug]`, `[:loadv]`, `[:setv]`. Those are available to
**trusted** users — listed by id in the bot's config, or an administrator of a
server listed as trusted — which keeps host access tied to servers the owner
controls rather than to whoever happens to be an admin somewhere the bot was
added. **Anything the LLM writes is never trusted**, whoever asked for it.

Input is capped at 1000 characters, and any single utterance stops after 60
seconds of audio. The cap is what contains `[:rate 75]` on a long message, or a
very long `[:pause]`, without having to anticipate each trick.

### `/tts stop` and `/tts skip`

`stop` clears everything queued and silences the bot immediately; `skip` drops
only what is speaking now. Available to trusted users, admins, and whoever
queued the utterance in question. Music resumes normally afterwards.

### `/voice` — voice sessions

| | |
|---|---|
| **Who** | everyone |

`/voice start` brings the bot into **your** voice channel and ties it to the
text channel you ran the command in. While a session is active, LLM replies in
that text channel are **spoken as well as posted**, and `/speak` from anywhere
in the server goes to that channel.

The posted copy keeps the inline `[:commands]` visible, so what the model was
trying to do with the voice can be read as well as heard.

The session ends on `/voice stop` or `/leave`, if the bot is disconnected, or
**30 seconds after the last human leaves** — long enough that a quick rejoin
does not kill it. The bot leaves at that point. The same auto-leave applies
after a plain `/join`, so it never sits alone in a channel indefinitely.

### `/voice lab` — build a custom voice

DECtalk exposes about 35 voice parameters. The lab is a panel for tuning them
without memorising any of it: the current parameters grouped by what they do,
per-group **Edit** forms, a **Raw** box for pasting a whole `[:dv …]` string,
and a **▶ Test** button that speaks a phrase in the bot's voice channel so the
loop is edit → listen → edit.

**Save as…** stores it for the server under a name that `/speak` then
autocompletes. Anyone can create a voice; the person who made it, or an admin,
can delete it. Your draft survives closing the panel by accident for about half
an hour.

### `/chat` — a voice message

Sends the spoken text as a Discord **voice message** — the kind with a waveform
and a play button — with no voice channel involved at all.

In the Java version the waveform was noise: it averaged raw bytes of the file,
header included, which for 16-bit audio averages to roughly zero everywhere.
This one is computed properly.

---

## Phase 5 — the LLM

The Java bot had an OpenAI integration that was written but never switched on.
This is designed fresh.

### Talking to the bot (passive)

The bot replies when **addressed**: @mentioned, replied to, or a message
starting with its name. It answers in text by default, and **speaks as well**
when a voice session is active in that channel.

Default model is Claude Haiku 4.5, chosen for cost. The model and provider are
per server; Sonnet 5 and Opus 5 are selectable, and OpenAI is supported.

### Advanced triggers

Separate from [the simple triggers](README.md#trigger-responses), and worth
understanding as a different thing: a simple trigger has fixed answers, while an
advanced trigger asks the model to say something.

Each has a pattern, a **short instruction about what to say** ("Someone
mentioned pineapple pizza. Defend it with unreasonable passion"), a probability,
and a per-channel cooldown. **How** to say it — length, tone, what to avoid —
comes from a shared style document, so each trigger's own instruction stays one
line.

**When a simple and an advanced trigger both match, the simple one wins** and no
model call is made. The cheap instant answer stays in charge.

### Memory

**Short-term** is a rolling window of recent messages in that channel, bounded
by both message count and an approximate token budget.

**Long-term** is the model's own: it decides what to remember, recall and
forget, through tools backed by a searchable store. Admins get
`/memory list | forget | clear`, and anyone can ask for their own memories to be
removed.

### Personality and instructions

Three living documents per server:

| Document | Who may edit | What it is |
|---|---|---|
| `personality` | **everyone**, by default | How the bot comes across. Meant to be tuned by whoever uses it |
| `system` | admins | Instructions that are not up for negotiation |
| `trigger_style` | admins | How advanced-trigger replies are written |

`/llm personality view | edit | history | diff | revert` — and the same for the
others. **Every edit is a new version**, so a bad one is one command away from
being undone and nothing is ever lost. Editing opens a form pre-filled with the
current text, or takes a `.txt` attachment for longer documents.

The personality document is explicitly **subordinate** to the fixed rules and
the system document, so editing it cannot be used to talk the bot out of its own
limits.

### Talking to other bots

The [allowlist already exists](README.md#bots). What phase 5 adds is pacing,
because an LLM conversation is expensive and open-ended in a way a trigger reply
is not: a limit of about six consecutive bot turns in a channel, a minimum delay
between them, a daily cap, and a human message resets the count. Optionally, only
when a human started the exchange.

### Guards

Per-server blacklists of users and roles; per-user and per-channel rate limits;
a cap on reply length; an on/off switch per server. **The bot turns the LLM off
by itself at $20 in a month or $2 in a day** and tells the admins, computed from
actual token usage.

### `/llm settings`

The numbers above — context size, token budget, reply cap, rate limits — are
editable at runtime by admins through a panel, and apply to the next message. No
restart, no config file.

---

## Later, unscheduled

**Music.** The Java bot played from YouTube and direct links through LavaPlayer,
which has no C++ equivalent. The rebuild will use yt-dlp and ffmpeg, with the
queue redesigned around Discord's own playback markers rather than the
hand-rolled bookkeeping the Java version had. Music pauses while the bot speaks
and resumes afterwards. The commands stay **unregistered** until it works, so
the slash menu is not full of things that reply "not implemented".

**Emote statistics.** Which custom emoji actually get used. The Java version
re-read every message in every channel on each run, which is why it was slow;
this one will scan incrementally and remember where it got to.

**Appearance tracking.** Nickname history, but for how someone *looked* — server
avatar, name colour including gradient roles, decorations. Probably shown as a
generated image, since an embed cannot represent a gradient.

**Self-hosted embeds.** Downloading the linked content and hosting the embed
directly, so it survives the original being deleted or the mirror services
disappearing. Written up in
[docs/ideas/Self_Hosted_Embeds.md](../ideas/Self_Hosted_Embeds.md).
