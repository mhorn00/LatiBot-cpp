# Plan: language model personas

**Status:** built on 2026-10-20, with the commands as a `persona` group under
`/llm` rather than a command of their own (decision 8). What was built is in
[Language_Model.md §2.11 and §3.9](../../src/modules/llm/docs/Language_Model.md#211-personas);
this plan is the record of how it was designed.
**Date:** 2026-10-20
**Related:** [claude_webhook_llm_facade.md](claude_webhook_llm_facade.md)
(the idea), [Language_Model.md](../../src/modules/llm/docs/Language_Model.md)
(what this extends)

---

## The idea

Let LatiBot answer as someone else: a **persona**, with its own name, its
own avatar and its own instructions, posting through a webhook so that it
shows up in the channel as its own user rather than as LatiBot. The first one
in mind is a "Claude" who answers when somebody says "claude". Then make that
generic, so a server can set up any number of them, each answering to its name
or to simple triggers.

"Persona" is this plan's word for the idea's "facade user". It is shorter,
and it is what the command would be called.

---

## What a server would see

### Setting one up

`/persona` (Manage Server for everything but `list` and `view`):

| Subcommand | Does |
|---|---|
| `add name [avatar] [model]` | Makes a persona. `avatar` is an image URL. `model` is one of the known models (Language_Model.md §3.2), with the server's model as the default |
| `edit persona [name] [avatar] [model] [enabled]` | Changes any of those |
| `remove persona` | Removes it, its documents and its triggers. Its old messages stay where they are |
| `list` | Every persona here, on or off |
| `view persona` | Its name, avatar, model, triggers, and what it has spent this month |
| `system persona`, `personality persona` | Its two documents, with the same `view`, `edit`, `history`, `diff` and `revert` that LatiBot's have (§3.5 of the spec) |
| `trigger persona add \| edit \| remove \| list` | Patterns it answers without being named (below) |

The two documents are the idea's "system provided" and "user provided"
instructions. **System** is Manage Server to read and edit. **Personality**
is open to everyone by default, as LatiBot's is.

At most **10** personas per server (a proposal: Discord's own limits, below,
leave room for more).

### When it answers

Only in a server that has run `/llm on`, through every guard LatiBot's
answers already go through (spec §2.2): the blacklist, the spend caps, the
per-person and per-channel rate limits.

- A message that **starts with its name**: `claude, what's 2+2` and
  `Claude what's 2+2`, but not `claudes` or `hey claude`. These are the same
  rules as LatiBot's own name (spec §2.1).
- A **reply** to one of its messages.
- One of its **triggers** firing. A trigger is an advanced trigger (spec
  §2.6) that belongs to the persona: a pattern, a one-line prompt, a chance
  and a cooldown. Its reply is written the persona's way rather than in the
  trigger style.

A persona cannot be @mentioned, since a webhook is not a member. A message
addressed to LatiBot stays LatiBot's, even if it also names a persona. A
message that names two personas goes to the first one named.

### How it answers

- It posts **as itself**: its name and avatar, through a webhook in that
  channel. Discord marks it with the **APP** tag, so nobody takes it for a
  person.
- It reads the channel the way LatiBot does: the same 15 messages, the same
  token budget, everyone as an alias (spec §3.8).
- **It is not a Discord reply.** Webhooks cannot send replies (below). The
  answer is posted as an ordinary message right after the question. With no
  reply and no mention, it notifies nobody, not even whoever asked
  (decision 2).
- **No *typing…***. Discord would show "LatiBot is typing", which gives the
  persona away.
- It is never **spoken** in a voice session (not in the first version).
- It has **no memory** (not in the first version). It does not see the
  server's memories, and it cannot remember anything.
- It **never answers a bot**, another persona or LatiBot. Two models talking
  for ever is the loop the spec's pacing exists to stop, and with personas the
  loop is one webhook away.

### A Claude persona

```
/persona add name:Claude avatar:<an image URL> model:claude-sonnet-5-5
/persona system persona:Claude edit
```

Its system document says who it is. Nothing about Claude is built in: the
name, the avatar and the instructions are the server's. This repo does not
ship Anthropic's logo, since that is Anthropic's brand to hand out. The admin
points `avatar` at an image they have chosen.

---

## What Discord allows (to check before building)

| | What it means here | Confidence |
|---|---|---|
| A webhook sets the username and avatar URL for each message | One webhook per channel serves every persona | Checked: DPP's `execute_webhook` sends `wh.name` as `username` and `wh.avatar_url` as `avatar_url` |
| `wait=true` returns the created message | The message id of a persona's post can be recorded | Checked in DPP |
| No `message_reference` on webhook messages | A persona cannot reply, only post | Confident; verify in the first live test |
| No role colour on a webhook's name | The idea's "the Claude colour" cannot be done on the name. An embed's side colour could, but a reply in an embed reads worse | Confident |
| Usernames cannot contain "clyde" or "discord", and are 1–80 characters | `add` refuses those names | Confident |
| `avatar_url` must be a stable URL | An attachment's CDN link expires, so `avatar` takes a URL, not an upload | Confident |
| Creating webhooks needs **Manage Webhooks** | A new startup permission, warned per server like the others | Checked: `preflight.cpp` already names the bit |
| 15 webhooks per channel; a thread posts through its parent's webhook with `thread_id` | One per channel is plenty; threads work | Confident |
| A webhook message's author is the webhook (`author.bot` true, `webhook_id` set) | The pipeline already drops it as a bot not on the allowlist (Message_Pipeline.md §2.1), so a persona's post sets nothing off. That needs keeping on purpose, not by accident | Checked in `bot.cpp` and `message_pipeline.cpp` |

---

## Design

### Where it lives

All of it goes in the **llm module**. A persona is a language-model answer
with a different identity and a different way of posting. It needs the
responder, the providers, the prompt, the spend and the guards, and all of
those are the module's own. A module of its own would need half of llm's
private headers, which the build forbids (Module_Plan_Final.md §5.1).

It needs **no new stage**. `llm_stage` already does every guard in order and
claims the rate limits. It gains a persona step between "is LatiBot
addressed" and "an advanced trigger or conversation mode". That way, the
core's stage order (`core/events/stage_order.hpp`) and the module matrix stay
as they are.

### Data: the llm module's schema version 2

Version 1 is never edited (spec §3, Module_Plan_Final.md §7):

```sql
CREATE TABLE llm_personas (
    id         INTEGER PRIMARY KEY,
    guild_id   INTEGER NOT NULL,
    name       TEXT    NOT NULL,
    avatar_url TEXT    NOT NULL DEFAULT '',
    model      TEXT,                          -- NULL: the server's model
    enabled    INTEGER NOT NULL DEFAULT 1,
    created_by INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    UNIQUE (guild_id, name COLLATE NOCASE)
);

-- What each persona posted, so a reply to one addresses it, and so the
-- transcript shows it as itself. Kept like link replacements.
CREATE TABLE llm_persona_posts (
    message_id INTEGER PRIMARY KEY,
    persona_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,
    posted_at  INTEGER NOT NULL
) WITHOUT ROWID;

-- An advanced trigger that belongs to a persona; NULL is LatiBot's.
ALTER TABLE llm_triggers ADD COLUMN persona_id INTEGER;
```

**The documents need no new table.** `llm_documents.kind` is already text, so
a persona's documents are kinds of their own: `persona.7.system` and
`persona.7.personality`. Versions, history, diff, revert and the edit form all
come with them. `document_kind` becomes a key that can name a persona's
document, rather than an enum of three.

**Spend** is counted in `llm_usage` as now, under the server. `view` shows a
persona's share only if `llm_usage` gains a `persona_id` column in the same
step. That column is worth adding now, while the step is being written.

### Webhooks: a port, and a cache

`ports::discord_gateway` gains three calls, implemented in `dpp_gateway` and
in `tests/mocks/mock_discord.hpp`:

- `channel_webhooks(channel)`
- `create_webhook(channel, name)`
- `execute_webhook(webhook, message, username, avatar_url, thread)`, which
  waits for the message and gives it back.

A small `persona_webhooks` class in the llm module finds the bot's own
webhook in a channel, by the application id that owns it, or makes one called
"LatiBot personas". It keeps that webhook in memory, so its token is never
written to the database. Its token goes to `host::secret`, so the log
channel masks it. A 404, from an admin having deleted the webhook, empties
the cache and tries once more. A thread posts through its parent channel's
webhook.

### The stage

- `ask_llm` gains `persona_id` (0 for LatiBot).
- After the check for LatiBot being addressed, `llm_stage` checks the
  message:
  - Does it start with an enabled persona's name? This uses the existing
    `starts_with_name`.
  - Does it reply to a persona's post? This looks the reply up in
    `llm_persona_posts`, through a read-only view the stage is handed, as it
    is handed the link replacements.
  - Does one of a persona's triggers fire? The existing
    `advanced_trigger_matcher` already does this, and its cooldowns already
    belong to each trigger.
- Then it runs the same `admit` (blacklist, spend, rate limits). A bot's
  message never reaches a persona. A persona's own posts never reach the stage
  at all (the table above).

### The responder

- **Identity.** The fixed rules (`prompt.cpp`) say "You are LatiBot" and
  "LatiBot (you)". Both become the answering identity's name, and so do the
  transcript markers. In a persona's transcript, LatiBot is "LatiBot (a bot)"
  and other personas are themselves. In LatiBot's transcript, a persona's post
  is shown by the persona's name rather than an alias for a webhook id.
  `to_context` takes the posts view to tell them apart.
- **Instructions.** The fixed rules, then the persona's system document, then
  its personality. LatiBot's server documents are not included: a persona is
  someone else. A trigger's prompt is added when it fired.
- **Model.** The persona's model, or the server's when it has none.
- **No tools.** Memory is off in the first version, so the request carries
  none, and the tool loop ends after its first round.
- **Posting.** Through `persona_webhooks` instead of `send_message`:
  - no reference;
  - the same mention rules (nothing the model writes pings anyone);
  - silent when not addressed;
  - each message's id recorded in `llm_persona_posts`;
  - the same split into at most three messages.
- **Failure.** "sorry, i couldn't come up with anything just now" is posted as
  the persona when it was addressed, and nothing when a trigger fired, as
  now.

### The command

`/persona` is a command of its own, not a group under `/llm`. `/llm` is
already the largest command, and Discord allows 8,000 characters per command
(spec §5). It lives in `llm_command.*`, or `persona_command.*` beside it, and
reuses the document subcommands and their forms. The persona option
autocompletes from the server's personas.

---

## Order of work

Each step ends building, with the Debug tests passing:

1. **Webhook plumbing.** The three port calls, `dpp_gateway`, the mock, and
   `persona_webhooks` with tests: find, create, cache, and a 404 that
   recreates. Manage Webhooks added as a startup permission.
2. **Schema version 2 and the persona store.** The tables, `persona_store`
   (add, edit, remove, list, look up by name), documents keyed for personas,
   and `persona_id` on triggers. A schema test like the existing ones.
3. **The stage.** Name, reply and trigger addressing, with tests in
   `llm_answer_test` / a new `llm_persona_test`. These cover: LatiBot wins
   when both are named, a bot never reaches a persona, a persona's post
   triggers nothing, and the guards apply.
4. **The responder.** Identity in the prompt and transcript, the persona's
   documents and model, posting through the webhook, and recording posts.
   Tested with `mock_llm` and the mock gateway.
5. **`/persona`.** The commands and the document forms, with tests in
   `llm_command_test` and `llm_panel_test`.
6. **Docs.** New sections added after the existing ones so section numbers
   stay put (User_Guide.md says why):
   - the spec gets §2.11 *Personas* and §3.9 *How personas post*, plus rows
     in its decisions table;
   - the module README's "What it owns" table gets the command, the tables
     and the new panel names (its test checks them);
   - the user guide gets `/persona`;
   - the README's command table gets a row.
7. **A live test** in a test server: that webhooks cannot reply, that the
   APP tag shows, how threads and voice channels' text chats behave, and that
   a real model keeps to the persona.

---

## Decisions for the owner

Each has a recommendation. These are the questions whose answers change the
build.

| # | Question | Recommended |
|---|---|---|
| 1 | Does a persona get the server's **memory**? | Not at first: no memories shown, no tools. Sharing LatiBot's is one switch later, and keeping memories of its own is a bigger change |
| 2 | Should the persona's answer **@mention whoever asked**, since it cannot reply? | No. It posts right after the question, which reads clearly in a quiet channel. Revisit after the live test |
| 3 | Can personas take part in **conversation mode**? | Not at first. It adds a check model call per message per persona |
| 4 | Can a persona's **personality** be edited by everyone, like LatiBot's? | Yes, with the same `editors` role limit. The system document stays Manage Server |
| 5 | **Spoken** in a voice session, in a voice of its own? | Not at first. A `voice` option on `edit` is a small addition later, through the speech capability |
| 6 | Can personas **answer each other**, or LatiBot? | Never in the first version. The funny version needs the pacing rules (spec §2.7) applied to webhooks |
| 7 | A cap on personas per server? | 10 |
| 8 | `/persona`, or a group under `/llm`? | `/persona`, for the command size limit |

---

## Risks

- **Impersonation.** A persona can take any name and avatar, including a
  server member's. The APP tag tells them apart, and only Manage Server can
  make one. The plan relies on that, and adds no check of its own.
- **Cost.** Every persona answer is a full model call, and a persona on a
  bigger model than the server's costs more per answer. The spend caps cover
  it, since they are bot-wide.
- **Prompt-injected instructions.** A persona's system document is written by
  admins, but its personality is open to everyone. That is the same as
  LatiBot's personality today, labelled the same way in the prompt (spec
  §3.3).
- **The fixed rules assume LatiBot.** Making the name a parameter touches
  the prompt and the transcript, which have tests of their own.
  `starts_with_name` and the "LatiBot (you)" marker were changed once already
  (spec §4, 2026-10-07). Expect to update those tests rather than just add to
  them.
