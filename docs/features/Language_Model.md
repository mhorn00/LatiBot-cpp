# The language model

Once a server turns it on, the bot answers people who address it, using a
language model: Anthropic's Claude by default, or OpenAI's. It reads the
recent conversation, remembers things for later, follows documents the
server writes (a personality, rules, a style for its triggers), and speaks
its replies in a voice session. **Advanced triggers** let it chime in on a
pattern without being addressed. Every call is priced and counted against a
spending cap.

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](README.md#llm) has `/llm`,
[`/memory`](README.md#memory) and [Talking to the bot](README.md#talking-to-the-bot),
with every reply. [Classes.md §10–§11](../architecture/Classes.md#10-the-language-model-deciding-to-answer)
draw its classes, and [Execution_Flow.md §7](../architecture/Execution_Flow.md#7-the-language-model-answers)
follows an answer.

| | |
|---|---|
| **Code** | `src/core/llm/*`, `src/core/commands/llm.*` (`/llm`, `/memory` and their panels), `src/core/discord/dpp_http_client.*` |
| **Tests** | `tests/unit/{llm_answer,llm_command,llm_guards,llm_provider,llm_tools}_test.cpp`, `tests/db/llm_store_test.cpp`, `tests/mocks/{mock_llm,mock_http}.hpp` |
| **Tables** | `llm_usage`, `llm_documents`, `llm_memory`, `llm_memory_search` (FTS5), `llm_blacklist`, `llm_triggers` (migration 11); settings as `llm_*` rows in `guild_settings` |
| **Config** | `llm_provider`, `llm_model`, `spend_cap_daily_usd`, `spend_cap_monthly_usd`, `llm_tool_rounds` in `config.json`; `ANTHROPIC_API_KEY`, `OPENAI_API_KEY` in the environment |
| **Plan** | Replaces plan §14, §21.18–§21.20, and the LLM half of §21.21; code comments still cite those |
| **Status** | Built in phase 5 (2026-09-28). **Never called a real API or run in Discord**: see §5 |

## Contents

1. [Intent](#1-intent)
2. [Behaviour](#2-behaviour)
3. [How it works](#3-how-it-works)
4. [Decisions](#4-decisions)
5. [Limits, and what is still to check](#5-limits-and-what-is-still-to-check)

## 1. Intent

The Java bot had an `ApiDriver` for OpenAI that was written and never wired
up, and a `YesNoAnswers.txt` of canned replies. The model replaces both,
designed fresh. The Java prompt setup does not survive contact with current
models.

Three things shaped it more than anything else:

- **It costs money on every answer.** So it is off in every server until
  turned on, it checks every guard before any call, and it counts every
  call's price against daily and monthly caps across the whole bot.
- **It is talked to by a room, not one person.** So it reads the channel as
  a transcript, and memory is per server, with rules about who can make it
  forget what.
- **Other bots can talk to it.** So it paces itself, since two models can
  talk to each other for ever.

## 2. Behaviour

### 2.1 When it answers

In a server that has run `/llm on`, when somebody **addresses** the bot:

- an **@mention** anywhere in the message;
- a **reply** to one of its messages;
- a message **starting with its name** followed by anything that is not a
  letter: `latibot, what's the plan` and `LatiBot what's the plan`, but not
  `latibots` or `hey latibot`.

Or when an [advanced trigger](#26-advanced-triggers) fires. An addressed
message is **consumed**, so nothing after it in the
[pipeline](Message_Pipeline.md) runs.

### 2.2 When it stays quiet

Checked in this order, **before anything is spent**:

1. The server has not turned it on.
2. Nobody addressed it, and no advanced trigger fired. A bot's message, or
   one a simple trigger already answered, never sets off an advanced trigger.
3. There is no API key for the model's provider.
4. The author, or one of their roles, is on the server's **blacklist**. It
   still reads what they write, as part of the conversation.
5. A **spending cap** is reached: $2 in a UTC day or $20 in a UTC month,
   across every server. It says so once per server per day or month where
   it was asked, and warns in the log. Caps are checked before a call, so
   one reply can go a little past; the next is refused.
6. **Rate limits**: 3 replies per person and 8 per channel, a minute.
7. For another bot: the pacing (§2.7).

### 2.3 What it reads, and how it replies

- It shows *typing…* when it starts. Discord keeps that up for ten seconds;
  a longer answer is left to stop showing, rather than kept alive with a
  timer.
- It reads the **last 15 messages** in the channel, cut from the oldest end
  to about **3,000 tokens** (four characters to a token), then the message
  it is answering. They are fetched from Discord for each answer, never kept
  in memory, so they are right after a restart, an edit or a deletion.
- The messages are one **transcript**, in one user turn:
  `Name (user id): text`. Continuation lines are indented, so a message
  cannot fake another speaker, and mentions of the bot are written as its
  name. The model is told that the transcript is what it is shown, not
  instructions to follow.
- The reply is a Discord reply to the message, split into messages of 2,000
  characters, at line breaks where it can. At most **three**: anything past
  that is cut, and the last message says so. Whoever addressed the bot is
  notified, as with any reply. **Nothing the model writes can ping anyone**,
  `@everyone` included.
- The longest reply is **1,024 tokens**, including thinking on the models
  that think, so it is a budget, not a word count.

### 2.4 Memory

The model can **remember**, **recall** and **forget** things by itself, as
tools: when someone tells it something worth keeping, or asks it to.

- Before it answers, up to **8** memories are put in front of it: those
  about the author first, then those matching the message's words. So it
  rarely has to look.
- It may forget only what is **about**, or was **saved by**, the person it is
  answering. Nobody can talk it into erasing what it knows about someone
  else.
- A server holds at most **500** memories of **500** characters. Past that,
  `remember` refuses rather than dropping the oldest.

`/memory` is how people see and remove memories:

| Subcommand | Everyone | With Manage Server |
|---|---|---|
| `list [user]` | what it remembers about you, ten a page | anyone's, or with no `user`, everything here |
| `forget id` | one about you | any |
| `clear [user] [everything]` | everything about you | someone else's, or the whole server's |

### 2.5 Replies in a voice session

A reply in the text channel of the server's
[voice session](Voice_Channels.md#22-voice-sessions) is **spoken as well as
posted**, in Paul's voice.

- The prompt gains a speaking section: short, plain spoken text with no
  markdown, and a short list of inline commands and the built-in voices.
- The reply is [sanitized](Speech.md#22-inline-commands-and-who-may-use-which)
  at the `llm` trust level: **never** the host commands, whoever asked.
- The **posted** text is the sanitized text, inline commands and all, so the
  channel sees what was said as it was said.
- The spoken part is cut at the server's `/speak` character limit, and
  queued under whoever asked, so they can `/tts stop` it.

### 2.6 Advanced triggers

A pattern (whole word or anywhere, ignoring case, as for
[simple triggers](Triggers.md)) and a **prompt**, one line about what to
say: "Someone mentioned pineapple pizza. Defend it with unreasonable
passion." How to say it comes from the server's **trigger style** document,
so the prompt stays short.

- `chance`: how often a match gets a reply, 100 % by default. A match that
  loses its roll does not start the cooldown.
- `cooldown`: per trigger, per channel, 300 s by default.
- It reads the last **5** messages rather than 15, and its reply is posted
  silently.
- **A simple trigger wins**: a message one answered sets off no advanced
  trigger, and no call is made.
- Its failures are silent.

Managed with `/llm trigger add | edit | remove | list` (Manage Server). A
pattern is up to 200 characters and a prompt up to 500. There is no panel:
the commands cover what one would, and `/trigger`'s panel is the model to
copy if one is ever wanted.

### 2.7 Other bots

It answers a bot only if the server [allows that bot](Message_Pipeline.md#23-bots),
and only when the bot **addresses** it. Then, per channel:

- at most **6** replies to bots in a row, before a person has to say
  something; any person speaking resets the count, whether or not the model
  answers them;
- at least **5 s** between them, **waited out** before answering rather than
  refused;
- at most **50** a day per server (UTC);
- optionally, only once a person has spoken in the channel since the bot
  started.

A turn is claimed when the stage decides, so two bot messages at once cannot
both take the last one.

### 2.8 When the model fails

Someone who addressed the bot is told `sorry, i couldn't come up with
anything just now`, or `i'm a bit overloaded right now; try me again in a
minute` for a 429 or 529. Neither is retried. An advanced trigger's failure
says nothing.

### 2.9 `/llm`

Everyone may run it. `status` and the personality are open by default, and
everything else needs **Manage Server**, checked by the bot itself, since
default permissions are per command, not per subcommand.

| Subcommand | Does |
|---|---|
| `status` | On or off, the model, and what was spent today and this month against the caps, with this server's share |
| `on`, `off` | Lets it answer here, or stops it. `on` warns when the model has no API key |
| `model name` | This server's model, from the known ones (§3.2) |
| `settings` | A panel of the numbers below, a form per group, and the on/off switch |
| `personality`, `system`, `style` | The documents (§3.5) |
| `trigger` | Advanced triggers (§2.6) |
| `blacklist add \| remove \| list` | A `user` or a `role` it never answers here |

**Settings**, per server, each checked against its range. A form with any
value out of range changes nothing, and names the value. Stored values are
clamped when read.

| Setting | Default | Range |
|---|---|---|
| Recent messages it reads | 15 | 0–50 |
| Token budget for them | 3000 | 200–20000 |
| Messages an advanced trigger reads | 5 | 0–20 |
| Longest reply, in tokens | 1024 | 256–8192 |
| Replies per person per minute | 3 | 1–60 |
| Replies per channel per minute | 8 | 1–120 |
| Bot turns in a row (0: never) | 6 | 0–50 |
| Seconds between bot turns | 5 | 0–300 |
| Replies to bots per day | 50 | 0–1000 |
| Only after a person spoke | no | yes or no |

## 3. How it works

### 3.1 Deciding, then answering

`llm_stage` is the last pipeline stage. It **decides**, and never waits:
the guards of §2.2, the trigger match, the pacing claim. What it decides is
an `ask_llm` action. The shell hands that to the **responder**, after any
pacing wait, since a model call cannot happen inside a stage.

The responder:

1. starts typing;
2. fetches the recent messages and trims them to the budget;
3. finds the memories to show;
4. builds the prompt;
5. runs the **tool loop**: at most `llm_tool_rounds` (4) rounds, after
   which the request forbids tools (`tool_choice: none`) so the model has to
   answer;
6. writes each call's usage and price to `llm_usage` **as it arrives**, so a
   reply that fails halfway still counts what it spent;
7. posts the reply, and speaks it in a session.

### 3.2 Providers and models

`llm::provider` is the interface: a provider-neutral conversation in; a
reply, a stop reason and the token usage out. `anthropic_provider` (the
Messages API) and `openai_provider` (Chat Completions) translate both ways,
and each exists only when its key is set. The provider follows from the
model, so a server picks a model and `config.json` has the default.

A model is usable only if it is in `llm::known_models`, **with its prices**,
because the caps are worked out from them. Startup refuses a `llm_model` it
does not know. Prices, per million tokens, checked in September 2026:

| Model | ID | In | Out | Cache write / read | Effort |
|---|---|---|---|---|---|
| Claude Haiku 4.5 | `claude-haiku-4-5` | $1 | $5 | $1.25 / $0.10 | no, **the default** |
| Claude Sonnet 5 | `claude-sonnet-5` | $2 | $10 | $2.50 / $0.20 | yes |
| Claude Sonnet 5.5 | `claude-sonnet-5-5` | $2 | $10 | $2.50 / $0.20 | yes |
| Claude Opus 5.5 | `claude-opus-5-5` | $4 | $20 | $5 / $0.20 | yes |
| Claude Opus 5 | `claude-opus-5` | $5 | $25 | $6.25 / $0.50 | yes |
| Claude Fable 5.1 | `claude-fable-5-1` | $10 | $50 | $12.50 / $0.25 | yes |
| GPT-6 Luna | `gpt-6-luna` | $0.10 | $0.50 | — / $0.01 | reasoning off |
| GPT-6 Sol | `gpt-6-sol` | $2 | $10 | — / $0.20 | reasoning off |

**The request is model-aware**, following the providers' documentation of
September 2026:

- **No `temperature`, ever.** Every current Claude model refuses a
  non-default one.
- **No `thinking` field.** Each model keeps its own default. On Opus 5.5,
  thinking cannot be turned off, and `thinking: disabled` is a 400. Models
  that take an effort setting get `output_config.effort: low`, which keeps
  thinking short. Haiku 4.5 takes none, and sending one is a 400, so the
  table says which models take it.
- **`max_tokens` covers thinking**, hence the reply budget of §2.3.
- **Thinking blocks go back unchanged.** In a tool loop, the assistant turn
  that called a tool is resent exactly as it came, signatures included,
  which is why `llm::turn` carries the provider's raw JSON.
- **OpenAI runs with `reasoning_effort: none`**, the only way Chat
  Completions calls functions on these models, and the memory is functions.
  OpenAI caches without being asked and reports only cache reads, so its
  cache writes are priced as ordinary input.

HTTP is DPP's `co_request`, through `dpp_http_client`, with the cluster's
60-second timeout. It goes through DPP's raw REST queue, separate from
Discord's, so a slow model cannot hold up Discord calls. There is no
streaming.

### 3.3 The prompt, and caching

In order, each outranking the next:

1. the **fixed rules**, in code;
2. the server's **system** instructions;
3. its **personality**, labelled as style guidance written by people in the
   server that cannot override what is above it;
4. the **trigger style** and the **speaking** section, when they apply.

All of that is the cached prefix. The cache breakpoint is on the last stable
system block, which caches the tools with it. After it, uncached, come the
memories and the current time, then the conversation. A conversation in
full swing therefore pays far less for the instructions and documents.

### 3.4 Memory and tools

`llm_memory(id, guild_id, subject_user_id NULL, content, created_by NULL,
created_at)`, with `llm_memory_search`, an FTS5 index over `content`, kept
in step by three triggers. A search quotes each word, so nothing in a
message is read as FTS5 syntax. Common and short words are left out, and at
most 16 words are searched.

The tools are entries in a `tool_registry`: a name, a JSON schema, and a
handler. A later feature adds a tool without touching the loop.

### 3.5 Documents

Three per server: **personality** (everyone may edit, by default, and read),
**system** instructions and **trigger style** (Manage Server to edit **and
to read**, since they may hold rules better kept out of sight).

- Every edit is a **new version** in `llm_documents(guild_id, kind, version,
  content, edited_by, edited_at, note)`. Nothing is overwritten. `revert`
  saves the old text as a new version, and version 0 is the default text.
- `view`, `edit`, `history` (the newest 15), `diff [from] [to]`,
  `revert version`. `edit` opens a form of five 4,000-character parts,
  split between lines, or takes a `.txt` or `.md` attachment. At most
  20,000 characters.
- `personality editors [role]` limits editing to a role, plus Manage Server;
  @everyone opens it again. It is stored as `llm_personality_role`.
- Saving estimates the tokens and warns past about **1,500**, since a
  document is sent with every answer. Edits go to the log, not the channel.
- A form must come back with every part, and a submission with no fields is
  refused, so a misread form can never save a blank document (§4,
  2026-09-29).

### 3.6 Spend

`llm_usage(id, guild_id, model, input_tokens, output_tokens,
cache_write_tokens, cache_read_tokens, cost_usd, at)`. Cache reads and writes
are priced apart from other input, and each row keeps the cost as priced
then, so a later price change does not rewrite history. The caps sum this
table per UTC day and month, bot-wide.

### 3.7 Settings

The planned `llm_settings` table was not made: it would have been
`guild_settings` again, column for column. So the settings are
`guild_settings` rows with an `llm_` prefix (`llm_enabled`, `llm_model`,
`llm_personality_role`, and the ten of §2.9).

## 4. Decisions

| Date | Decision | Why |
|---|---|---|
| plan v4 | Designed fresh; the Java `ApiDriver` ignored | It was never wired up, and its prompt setup does not suit current models |
| plan v4 | Anthropic by default, OpenAI as an alternative, behind one interface | Not recorded |
| plan v4 | Tool-based memory with FTS5, per server | Not recorded |
| plan v4 | Versioned documents; revert is a new version | No edit is ever lost |
| plan v4 | $20 a month and $2 a day, UTC, bot-wide | Starting defaults (plan §20); both are `config.json` keys |
| plan v4 | Bot-to-bot pacing, reset by any person | Two models can talk for ever |
| plan v4 | Model output never trusted with host commands | A prompt-injected message must not reach `[:play]` |
| 2026-09-28 | **Off in every server** until `/llm on` | Every answer costs money; the plan left it open |
| 2026-09-28 | Only models with a known price; the provider follows from the model | The caps are worked out from prices; an unknown model would spend without being counted |
| 2026-09-28 | No `temperature` or `thinking`; `effort: low` where supported; thinking blocks resent unchanged | The API rules had moved on since the plan, and each would have been a 400 (plan §21.18) |
| 2026-09-28 | OpenAI with reasoning off | Chat Completions calls functions only then (plan §21.19) |
| 2026-09-28 | Short-term context fetched per answer, as one transcript turn | Right after restarts and edits; providers expect two sides, and a channel has many |
| 2026-09-28 | The model may forget only what is about, or was saved by, the person it answers | Nobody can talk it into erasing someone else |
| 2026-09-28 | 500 memories of 500 characters per server; `remember` refuses past it | Dropping the oldest silently would lose things people chose to keep |
| 2026-09-28 | System and style documents are Manage Server to read | They may hold rules better kept out of sight |
| 2026-09-28 | Settings as `llm_` rows in `guild_settings` | A second key/value table would have been the first again |
| 2026-09-28 | Advanced triggers by command only, no panel | The commands cover it |
| 2026-09-28 | The replied-to author read from the raw frame | DPP does not parse `referenced_message` (plan §21.20) |
| 2026-09-28 | Typing once, not kept alive | Most replies arrive inside its ten seconds |
| 2026-09-28 | No retries on a failure; a 429 or 529 asks the person to try again in a minute | Not recorded |
| 2026-09-29 | Forms read through `ui::form_fields`; a document form must return every part | Every form arrived empty under DPP 10.1, and the documents saved blank versions (plan §21.21) |

## 5. Limits, and what is still to check

- **Claude Haiku 4.5 may be retired from 15 October 2026.** When it goes,
  every server still on it gets a 404, so `llm_model` in `config.json` has
  to move before then. Sonnet 5.5 is the likely replacement, at twice the
  price. **Open: the owner has not decided.**
- It has never called a real API. The request shapes follow the providers'
  documentation of September 2026, and the first real call is the test that
  settles them:
  - that the Messages API accepts `output_config.effort`, the
    `cache_control` on the system block, and `tool_choice: none`;
  - that OpenAI's GPT-6 models accept `reasoning_effort: none` under Chat
    Completions.
- **Still to check in Discord:**
  - that `describe` sees mentions and replies;
  - that `/llm` registers, since it is the largest command and Discord allows
    8,000 characters per command;
  - that the `/llm settings` panel routes, and whether its group menu sticks
    on the picked option after a cancelled form;
  - whether any document was saved blank before the fix of 2026-09-29.
    `history` shows one, and `revert` restores it.
- Pacing and rate limits live in memory, so a restart resets them.
