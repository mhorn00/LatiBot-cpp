# Triggers

A server's automatic replies. When a message matches a trigger's pattern,
the bot posts one of that trigger's responses, picked by weight, or, for a
trigger that **asks the model**, has the language model (or one of its
personas) answer following the trigger's prompt. It is the Java bot's
`420` → "nice", generalised: any pattern, any number of responses, a
cooldown, and a panel to manage them.

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](../../../../docs/User_Guide.md#trigger) says how to
use it, with every reply. Triggers that ask the model were the language
model's "advanced triggers" until 2026-10-29 (§2.6).

| | |
|---|---|
| **Module** | `triggers`: [its README](../README.md) lists what it owns |
| **Code** | `src/modules/triggers/src/`: `triggers.*`, `trigger_command.*` (the command and `trigger_panel`), `trigger_tools.*`, `module.cpp`; the model's side in `src/core/include/core/capabilities/model_replies.hpp` |
| **Tests** | `src/modules/triggers/tests/`, built as `latibot_triggers_tests`; a trigger beside a link in `tests/app` |
| **Tables** | `triggers`, `trigger_responses`, the module's schema version 1 (was migrations 2, 3 and 9); version 2 adds `reply`, `prompt`, `chance`, `persona_id` and `trigger_imports` |
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
write. A pattern is literal text, standing alone as a word or anywhere,
or, when asked for, a regular expression. Those run on PCRE2 with a match
limit rather than on `std::regex`, whose recursion and backtracking make a
user-written pattern a stack-overflow and performance risk on a path every
message takes.

## 2. Behaviour

### 2.1 Matching

- **Whole word** (the default): `420` fires on `it is 420 somewhere` and on
  `(420)`, not on `4200`. **Every** occurrence is checked, so
  `4200 and also 420` matches.
- **Anywhere**: `cat` fires on `catastrophe`.
- **Regex**: a PCRE2 regular expression, found anywhere in the message:
  `\b42[0-9]\b` fires on `421` but not on `4200`. `\b` and `\w` know
  letters beyond ASCII. A pattern that does not compile, or that matches an
  empty message and so would fire on every one, is refused when it is
  saved. A match that runs into the limit (100,000 steps) fires nothing.
- Case is ignored on both sides.
- **What sets one off** is its `when`: its pattern matching the text (the
  default, all of the above); **LatiBot addressed**, an @mention of it or
  its name first ("LatiBot" or its Discord username); or **a reply** to
  whoever answers the trigger, LatiBot's message (not a link replacement) or
  its persona's. The last two have no pattern, and need the language model,
  which tells who is addressed and whose message was replied to.
- Every trigger with responses that matches, is enabled and is off cooldown
  replies, so one message can set off several. At most one trigger that
  asks the model answers a message, and only when none with responses did
  (§2.6).

### 2.2 Replying

- The response is picked by **weight**. `3 | nice` and `very nice` make the
  first three times as likely. A trigger whose weights are all zero or
  negative says nothing.
- The reply is an ordinary message in the channel unless the trigger says
  **`reply`**, when it is a Discord reply to the message. Off by default, as
  every trigger posted before; on by default for a trigger that asks the
  model, whose answers were always replies. A persona's answer is never a
  reply: it posts through a webhook, which Discord does not let reply.
- It is **silent** by default, as the Java bot's were, and shows link
  previews. A trigger can be set to notify, or to hide previews
  (`silent`, `previews`). A silent reply notifies nobody, its author
  included.
- **The cooldown is per trigger, per channel**: 30 s by default, 0 to 86,400,
  and 0 turns it off. Two channels do not share one. Cooldowns are forgotten
  on restart, which costs at most one extra reply.
- A trigger **does not consume** the message: one with `420` and a link gets
  the reply and the [link replacement](../../links/docs/Url_Replacement.md). A reply marks the
  message **answered**, so neither a trigger that asks the model nor the
  model's conversation mode also answers it.
- **Bots** reach triggers only where a server allowed them
  ([Message_Pipeline.md §2.1](../../../core/docs/Message_Pipeline.md#21-who-is-heard)), and
  even then a trigger answers one only with `bots:true`, off by default.
- **Channels** a server set to `/channels ignore` (everything), and the bot's
  log channel for bots' messages, reach no trigger
  ([Message_Pipeline.md §2.4](../../../core/docs/Message_Pipeline.md#24-channels)).

### 2.3 No defaults

A server starts with **no** triggers with responses. The Java bot's `420`,
`4:20` and `69`, all answering "nice", are not added for anyone; a server
that wants them adds them with `/trigger add`. Servers that were given them
before 2026-09-30 keep them until someone removes them, and nothing brings
them back. With the language model, a server does get its **built-in**
triggers (§2.8), which can be turned off but never removed.

### 2.4 The command

| Subcommand | Does |
|---|---|
| `add [pattern] (responses, prompt or question) [chance] [persona] [when] [mode] [cooldown] [bots] [reply] [silent] [previews]` | Adds a trigger and answers with its line and every response |
| `edit id [any of those but when] [enabled]` | Changes only what is given, so fixing a cooldown does not mean retyping the responses. Responses, a prompt or a question also change which kind it is, except for a built-in one. Given only the id, it opens the form, holding the trigger as it is |
| `remove id` | Deletes it and its responses; a built-in one is refused, and turned off with `edit enabled:false` instead |
| `list` | Eight a page, with ◀ / ▶ |
| `panel` | The list with editing attached |

Manage Messages by default, for both kinds, and every answer is private. A
pattern is 1–200 characters, the responses up to 2,000, one
per line, and a prompt up to 500. `prompt`, `chance` and `persona` are there
only with the language model built in. A pattern of only whitespace is
refused, since it would match everything, and so are responses that parse
to nothing, since a trigger that matches and has nothing to say is worse
than none, and a trigger given both responses and a prompt.

A list line reads `` `4` **420** (whole word, 30s) -> 2 responses``, or for
one that asks the model `` `9` **pizza** (whole word, 300s, 50%, as Claude) -> asks the model: Defend pineapple on it.``,
with the whole prompt. It adds `disabled`, `answers bots`, `replies`,
`notifies` or `no previews` only where a trigger differs from the default,
and `a persona that is gone` for one whose persona was removed. A regex is
shown in backticks rather than bold, so its `*` and `_` are not read as
markdown. `add` and `edit` answer with that line and every response in
full, up to Discord's 2,000 characters.

### 2.5 The panel

Pick a trigger from the menu, then:

| Button | Does |
|---|---|
| **Edit** | A form with the pattern, responses (or prompt and chance), mode and cooldown |
| **Enable** / **Disable** | Toggles it; the label says what pressing it will do |
| **Answer bots** / **Ignore bots** | Toggles `bots` |
| **Reply silently** / **Reply with notifications** | Toggles `silent` |
| **Hide link previews** / **Show link previews** | Toggles `previews`; not for a trigger that asks the model |
| **Answer as a reply** / **Post plainly** | Toggles `reply`; not for a persona's trigger |
| **Delete** | Confirms in the panel itself, so nothing is left behind if ignored; not there for a built-in one |

**Add** opens the same form, empty. With the language model, **Add for the
model** opens the one with a prompt and a chance, and **Add an analysis**
the one with a question and a prompt. A trigger set off by being addressed
or replied to has no pattern or mode in its form. The form's mode takes `word`,
`anywhere` or `regex`, and `whole word`, `whole_word` and `substring` also
work. A pattern that will not work in its mode is refused. Any other field
it cannot read, such as "thirty" for the cooldown, keeps the value the
trigger had, and the panel says so under the list. After a save the panel
shows the trigger picked, on whichever page it landed. A page whose long
patterns would pass 2,000 characters has its lines shortened evenly.

### 2.6 Triggers that ask the model

With the [language model](../../llm/docs/Language_Model.md) built in, a
trigger can have a **prompt** instead of responses, one line about what to
say: "Someone mentioned pineapple pizza. Defend it with unreasonable
passion." How to say it comes from the server's **trigger style** document
(`/llm style`), so the prompt stays short. The model, or a **persona** it is
given, answers the message following it.

- `chance`: how often a match is answered, 100 % by default. A match that
  loses its roll does not start the cooldown.
- `cooldown`: 300 s by default, since each answer is a model call.
- **A trigger with responses wins**: a message one answered asks the model
  nothing, and costs nothing.
- **A message spoken to the model is the model's**: one that addresses
  LatiBot, replies to a persona, or is a persona speaking to LatiBot sets
  off no such trigger, since the model answers it anyway.
- At most **one** fires per message: the first that matches, is on, off its
  cooldown, and wins its roll.
- `bots`: off by default, when only people set it off. On, so do the bots
  the server [allows](../../../core/docs/Message_Pipeline.md#23-bots),
  LatiBot itself (but not a slash command's response, which quotes what the
  command was given), and the personas, so LatiBot and its personas can
  answer each other. **Never its own writer**: a trigger LatiBot answers is
  not set off by LatiBot, nor a persona's by that persona. A message
  `talk_to` handed to its target sets off nothing. Every answer to a bot is
  paced (Language_Model.md §2.7), which is what ends two of them answering
  each other. These are the only triggers the bot's own messages reach:
  for them the stage hears its own messages, with the model built in.
- Everything else about whether the model may speak is the model's: its
  switch, a channel it ignores, the blacklist, the spend caps, the rate
  limits, the pacing, a key for its provider, and the persona being on.
  The answer reads the last **5** messages, and arrives silently. Its
  failures are silent.
- **Without the language model** these triggers are kept and listed, and
  fire for nobody. A persona's trigger whose persona is removed stays too,
  shown as answered by `a persona that is gone`, until `edit persona:` gives
  it to someone else.

**An analysis** is the third kind: a pattern, and a short yes-or-no
**question**, such as "Is this message talking to LatiBot?". When the
pattern matches, the language model's cheap **check model** is asked the
question about the message, with the recent conversation, as conversation
mode asks whether to join in. A yes is answered by the model, or the
trigger's persona, as if spoken to; a no says nothing. An optional prompt
steers that answer. The check is a model call, so **an analysis never runs
on every message**: its pattern has to be words, with at least one letter
or digit, matched as whole words or anywhere, never a regular expression,
and never on being addressed or replied to. Its cooldown starts at 0, as
the check answers most matches with a no; it has no chance.

Being addressed, or replied to, a trigger that asks the model is answered
as spoken to (the author notified, a conversation opened), and needs no
prompt; on a pattern, it chimes in with its prompt. At most one trigger
asks the model per message: one answered as spoken to first, then an
analysis, then a comment.

The model's side is the `model_replies` capability, which the llm module
offers (docs/modules/Module_Plan_Final.md §5.2): the personas, who of
LatiBot's own wrote a message, whether a message addresses LatiBot and whose
message it replies to, and the answer itself, as a background task the
pipeline runs once every guard has let it through, the check first for an
analysis. Answered as spoken to past a spend cap, the answer is the notice
saying so, once per period.

### 2.7 The language model's tools

With the [language model](../../llm/docs/Language_Model.md#34-memory-and-tools)
built in, it can work with triggers too:

| Tool | Does | Needs |
|---|---|---|
| `list_triggers` | Every trigger here: id, pattern, mode, cooldown, and responses or prompt, chance and who answers | nothing |
| `add_trigger` | Adds one, with the pattern and either responses, a prompt (with a persona and a chance) or a question for an analysis, mode and cooldown; on, silent, never answering bots | Manage Messages of whoever asked |
| `remove_trigger` | Deletes one by id; never a built-in one | Manage Messages of whoever asked |

Like every tool that changes something, the last two refuse a bot's
message. The names the model writes as markers are put back into the
pattern and responses, and those it is shown are hidden, as everywhere
(Language_Model.md §3.8). Each change is logged. `/llm tools off` turns any
of them off in a server.

### 2.8 Built-in triggers: when LatiBot answers at all

With the language model built in, the triggers decide every time LatiBot or
a persona answers a message, except conversation mode, which stays the
model's own (Language_Model.md §2.10): at startup the triggers module calls
`model_replies::take_over_answering`, and the model stops answering what
addresses it on its own. What it used to decide for itself is a guild's
**built-in** triggers, made for every guild the first time it is seen with
the model, and for each persona as it appears:

| Built-in | When | Answers |
|---|---|---|
| `latibot-addressed` | LatiBot addressed: an @mention, or its name first | LatiBot, as spoken to, as a reply |
| `latibot-reply` | a reply to LatiBot's message | LatiBot, as spoken to, as a reply |
| `latibot-named` | `latibot` as a whole word, by analysis: "Is this message talking to LatiBot, or asking it something, rather than only talking about it?" | LatiBot, as spoken to, on a yes |
| `persona-reply:<id>` | a reply to that persona's message | the persona, as spoken to; people only |

LatiBot's answer to bots the server allows, and to personas that talk, is
paced as before; a persona never answers a bot's reply. A built-in can be
turned off, and edited (its pattern, question, prompt, cooldown, `bots` and
`reply`), but not removed, and keeps its kind, its `when` and who answers
it. A persona's goes when the persona does. Without the language model
there are none, and nothing changes; without the triggers module, the model
decides for itself as it always did.

### 2.9 Firing a trigger by hand

Right-click a message, then **Apps → Fire a trigger** (Manage Messages by
default). It answers privately with a menu of the server's triggers that
are on, 25 to a page with ◀ / ▶, and the one picked fires on that message
**whatever its pattern, chance, check or cooldown say**: a fixed reply is
posted, or the model answers, an analysis as if its check had said yes. The
model's own guards still apply, so the blacklist, the spend caps and the
rate limits can still keep it quiet, and the menu says when nothing came of
it. The message is remembered between the command and the pick for 15
minutes, as long as Discord keeps the menu answerable; after a restart the
menu says to try again. It is a message command, which the core routes
through the same registry as slash commands.

## 3. How it works

The decisions are pure functions in `events/triggers.hpp`:

- `matches(content, pattern, mode)`;
- `choose(responses, roll)`, which takes the random number from its caller,
  so tests can check the distribution;
- `off_cooldown(last_fired, now, cooldown)`.

`trigger_responder` is the pipeline stage. Those with responses come
first; then, only if none replied, the first trigger that asks the model and
fires, through `model_replies::answer`, which returns the model's answer as
a `background_task` or nothing. It reads the server's triggers
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

`trigger_store` holds `triggers` and `trigger_responses`. A trigger asks the
model when its `prompt` is not empty. At startup `import_llm_triggers`
copies the llm module's old `llm_triggers` rows in, once: they get new ids,
answer as replies, and `trigger_imports` remembers it was done, so a trigger
deleted later is not copied back. It runs at startup rather than in the
schema step because that table may not exist yet, or ever. Responses are
replaced wholesale on every edit, which is how both the command and the form
work. A server's responses are read in one query. `message_flags` keeps only
the flags a channel message can carry (silent, no previews), and defaults to
silent.

`trigger_panel` keeps no state on the bot's side. The page and the chosen
trigger travel in each component's `custom_id` (views `trigpanel`,
`trigpick`, `trigedit`, `trigdel`, `trigyes`, `trigadd`, `trigaddmodel`,
`trigform`, `trigonoff`, `trigbots`, `trigsilent`, `trigprev`, `trigreply`,
and `triggers` for the list's pages). `trigaddmodel` carries which kind to
add, a model trigger or an analysis, and the form's argument carries the
kind of a new one, so it knows which fields came back. `trigcmdform` is the
same form opened by `/trigger edit`, which answers privately, since there is
no panel to update. `trigfire` is the Apps menu's trigger menu (§2.9). So it survives a restart, has nothing to expire, and two
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
| 2026-10-28 | A third mode, `regex`, on PCRE2 with a match limit; supersedes literal-only | The owner's request. PCRE2 does not recurse on the stack and gives up at its limit, which answers why plan v4 refused them. A mode rather than every pattern a regex, so a saved `what?` keeps its meaning |
| 2026-10-29 | The language model's advanced triggers move here, as triggers with a prompt, in the one table, list and panel, through the `model_replies` capability | The owner's choice. One kind of thing, a pattern and what to do about it, in one place; the model keeps every decision about whether it may speak |
| 2026-10-29 | A trigger that asks the model takes Manage Messages, as any trigger does | The owner's choice. It took Manage Server under `/llm trigger`; the spend caps still bound what they cost |
| 2026-10-29 | A persona's removal leaves its triggers, answered by nobody | The owner's choice. The llm module cannot reach this module's table, and a hook for one tidy-up is more than it is worth |
| 2026-10-29 | `reply` per trigger: a Discord reply or a plain post; off for responses, on for the model | The owner's request. Each kind keeps what it did before |
| 2026-10-29 | `/trigger edit` with only the id opens the form | A command's options cannot start out filled in |
| 2026-10-29 | An **analysis** kind: words matched first, then the check model's yes or no on the trigger's question, then the model answers | The owner's design. The words keep the check from running on every message; the check is the same cheap call conversation mode makes |
| 2026-10-29 | Being addressed and replied to are conditions; LatiBot's own answering becomes built-in triggers, and the model hands its decisions over at startup | The owner's choice: every answer through a trigger. Conversation mode stays the model's, since it has no word to match. Without the triggers module the model keeps deciding, so neither needs the other |
| 2026-10-29 | Built-ins can be turned off and edited, never removed | The owner's choice. A server can always turn LatiBot back on |
| 2026-10-29 | **Fire a trigger**, a message command for Manage Messages, skipping the match, chance, check and cooldown | The owner's choice. The model's guards still bound what it costs |
| 2026-09-23 | The responses label is under 45 characters; the weight syntax went to the placeholder | Discord refuses the whole modal over a long label (plan §21.4) |
| 2026-09-25 | One lock around the cooldown check, the pick and the claim | Two messages at 4:20 could both find a trigger ready (cleanup CONC-002) |
| 2026-09-25 | Menu labels are truncated to 100 characters | A long pattern broke the panel for the whole server (cleanup DISC-002) |
| 2026-09-25 | Replies are silent by default; `silent` and `previews` per trigger | The Java bot's were silent; some triggers want to notify or hide previews (plan §21.15) |
| 2026-09-28 | A reply marks the message answered | The free, simple reply wins over a paid model answer |
| 2026-09-29 | The form keeps a field it cannot read, and says so | Resetting a cooldown because of a typo in it is worse than keeping it |
| 2026-09-30 | **No default triggers**; `seed_defaults` removed (the owner's decision) | The seeding checked for no triggers at all, so deleting every trigger brought the three back at the next connect. The bot need not add them; a server that wants them can |

## 5. Limits, and what is still to check

- Cooldowns are forgotten on restart.
- A pattern matches message text only: not embeds, attachments or edits.
- **Still to check in Discord:** that the panel's forms save, since the fix
  of 2026-09-29 (plan §21.21) was tested only through DPP's own interaction
  handling.
