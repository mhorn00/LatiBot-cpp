# The message pipeline

Every message the bot sees passes through one ordered list of **stages**:
the goodbye phrase, URL replacement, simple triggers and the language model.
Each stage looks at the message as plain data and says what it wants done.
The pipeline also decides whose messages are heard at all, which is where
the per-server **bot allowlist** (`/bots`) comes in.

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](../../../docs/User_Guide.md#bots) covers `/bots`.

| | |
|---|---|
| **Code** | `src/core/{include/core,src}/events/message_pipeline.*`, `src/core/src/events/bot_allowlist.*`, `src/core/src/commands/bots.*`; `bot::describe` and `bot::carry_out` in `src/core/src/bot.cpp` |
| **Tests** | `src/core/tests/message_pipeline_test.cpp`, `src/core/tests/bots_command_test.cpp`, `src/core/tests/bot_allowlist_test.cpp` |
| **Tables** | `allowed_bots` (migration 3) |
| **Plan** | Replaces plan §5.4 and §11.1 |
| **Status** | Built in phase 1 (2026-09-21, the allowlist 2026-09-23); the `answered` flag in phase 5 |

## Contents

1. [Intent](#1-intent)
2. [Behaviour](#2-behaviour)
3. [How it works](#3-how-it-works)
4. [Decisions](#4-decisions)
5. [Limits](#5-limits)

## 1. Intent

The Java bot handled each feature inside one `onMessageReceived`, with early
`return`s. So **a message with both "420" and a link got "nice" and no link
replacement**: the first feature to answer ended the handler. The pipeline
fixes that class of bug rather than the one instance, by making "does this
stage stop the ones after it" an explicit property of each stage.

It also gives every message-reading feature one answer to "whose messages do
we read". Our own messages are never read, since answering ourselves is a
loop with no exit. Other bots' messages are read only where a server allowed
that bot, since two bots answering each other is a loop nobody asked for.

## 2. Behaviour

### 2.1 Who is heard

| Author | Reaches the stages |
|---|---|
| LatiBot itself | never, and no setting changes that |
| Another bot | only if this server allowed that bot with `/bots allow` |
| A person | always |

Reaching the stages is only permission to be considered. Each stage decides
for itself whether it answers a bot:

- **URL replacement** never replaces a bot's links.
- **A simple trigger** answers a bot only if that trigger has `bots:true`
  ([Triggers.md](../../modules/triggers/docs/Triggers.md)).
- **The language model** answers a bot only when the bot addresses it, and
  paces itself ([Language_Model.md](../../modules/llm/docs/Language_Model.md)). No bot sets off an
  advanced trigger.
- **The goodbye phrase** needs Administrator, which a bot could in principle
  have.

### 2.2 The stages, in order

| Position | Stage | Consumes the message | Marks it answered |
|---|---|---|---|
| 100, `stop` | [The goodbye phrase](Basic_Commands.md#3-the-goodbye-phrase) | yes, when it matches | — |
| 200, `rewrite` | [URL replacement](../../modules/links/docs/Url_Replacement.md) | no | no |
| 300, `reply` | [Simple triggers](../../modules/triggers/docs/Triggers.md) | no | yes, when one replies |
| 400, `model` | [The language model](../../modules/llm/docs/Language_Model.md): addressed, or an advanced trigger | yes, when it answers | — |

- **Positions** are named in `events/stage_order.hpp`, and each stage is
  added at one. They run lowest first, whatever order they were added in, so
  a module's stage lands in the same place whichever modules are built. Two
  stages at one position stop startup.

- **Consumes** means the stages after it are skipped. Nothing should follow
  a goodbye.
- **Answered** is passed on to the stages after it, as
  `incoming_message::answered`. That is how a simple trigger's free reply
  keeps an advanced trigger quiet, without the two stages knowing about each
  other.
- A stage that **throws** is logged, and the rest still run. An exception
  must never reach DPP's event thread, where it would end the process.

### 2.3 `/bots`

| Subcommand | Does |
|---|---|
| `allow bot` | Lets this server's messages from that bot reach the stages |
| `deny bot` | Stops it |
| `list` | The allowed bots, by name; one no longer in the server shows its id and `(not in this server any more)`, so it can still be removed |

Manage Server, by default. `allow` refuses a person ("X is not a bot, and i
already hear everyone else"), since storing one would look like it worked
while doing nothing, and refuses LatiBot itself. `allow` and `deny` say when
nothing changed. The empty list says so explicitly, since "no bots allowed"
and "the feature is off" would otherwise look the same.

## 3. How it works

**Plain data in.** `bot::describe` reduces a `dpp::message` to an
`incoming_message`. The fields a stage cannot work out for itself are
resolved there:

- whether the author is an allowed bot;
- whether the author has Administrator in this server, from roles and
  overwrites;
- the author's roles, for the language model's blacklist;
- whether the message mentions the bot, or replies to one of its messages.
  DPP does not parse the message a reply points to, so its author is read
  from the gateway frame's `referenced_message`, and only for replies.

A stage takes that and nothing else, so it is tested without a gateway.

**Actions out.** A stage returns a `stage_result`: a list of actions, plus
`consumed` and `answered`. The actions are:

| Action | From | Carried out by |
|---|---|---|
| `send_message` | the goodbye, a trigger | posting it with the flags it carries, then logging whether it was posted |
| `stop_bot` | the goodbye | stopping the bot after a delay long enough for the goodbye to arrive |
| `background_task` | URL replacement, the language model | running it without holding the pipeline up: `post_replacement` ([Url_Replacement.md §3.2](../../modules/links/docs/Url_Replacement.md#32-the-stage-and-the-post)) for a `replace_links`, the responder, after any pacing wait, for an `ask_llm` |

The core knows only those three. A stage with work of its own decides it as
its own type, an `own_stage_result<replace_links>` or
`own_stage_result<ask_llm>`, which its tests read. When the stage is added to
the pipeline, `carried_out_by` turns each of those into a `background_task`
and passes the rest through, so the core never names a feature's action.

`bot::carry_out` is the only code that touches Discord. The order of the
stages is a list built in the `bot` constructor
(`pipeline_.add(name, handler)`), so reordering is moving one line, and a
test reads the order back with `stage_names()`.

**Messages are handled concurrently.** DPP delivers events on a thread pool,
so two messages can be in the pipeline at once. Every stage is safe to call
from several threads. The trigger responder, for one, takes one lock around
checking a cooldown, picking a reply and claiming the cooldown.

**Other readers.** `on_message_create` also feeds the
[link stats](../../modules/linkstats/docs/Link_Stats.md#9-reactions-on-images) media tracker, which is
not a stage: it never answers, and it looks at uploads rather than text.

**The allowlist** is `allowed_bots(guild_id, bot_id)`, read per message by
`describe`. It starts empty in every server.

## 4. Decisions

| Date | Decision | Why |
|---|---|---|
| plan v4 | Ordered stages, each saying whether it consumes the message | The Java bot's early returns cost a message its link replacement whenever a trigger answered it |
| plan v4 | Stages return actions; only the shell touches Discord | A stage is then a pure function that tests read directly |
| plan v4 | A stage that throws is logged and the rest run | One broken feature should not silence the others, or reach DPP's event thread |
| plan v4 | The bot never hears itself | Answering itself is a loop with no exit |
| 2026-09-23 | Other bots are ignored unless a server allows each one; the list starts empty | Two bots answering each other is a loop nobody asked for |
| 2026-09-23 | Hearing and answering are separate decisions | A server-wide "listen to DiceBot" should not turn every existing trigger loose on it |
| 2026-09-23 | The allowlist moved from the language model phase to phase 1 | Every message-reading feature needs it, not only the model; the pacing stayed in phase 5 |
| 2026-09-23 | `/bots allow` refuses people and LatiBot itself | Storing a person looks like it worked while doing nothing |
| 2026-09-28 | Stages can mark a message answered, and later stages see it | The simple trigger wins over an advanced one, without either knowing about the other |
| 2026-09-28 | The replied-to author is read from the raw frame, and only for replies | DPP reads only the reply's ids; reading the frame for every message would cost for nothing |

## 5. Limits

- Only new messages go through the pipeline. An edited message is not read
  again, so editing a link into a message does not replace it.
- Which roles and overwrites give Administrator is resolved from DPP's
  cache, and so is who counts as a bot.
- The message content intent is required. Without it every message arrives
  with empty text: slash commands keep working, and every stage goes quiet
  ([Operations.md](Operations.md#3-discord-intents)).
