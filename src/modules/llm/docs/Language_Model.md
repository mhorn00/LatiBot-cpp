# The language model

Once a server turns it on, the bot answers people who address it, using a
language model: Anthropic's Claude by default, or OpenAI's. It reads the
recent conversation, remembers things for later, follows documents the
server writes (a personality, rules, a style for its triggers), and speaks
its replies in a voice session. [Triggers that ask the model](../../triggers/docs/Triggers.md#26-triggers-that-ask-the-model),
the triggers module's, let it chime in on a pattern without being
addressed. Every call is priced and counted against a
spending cap.

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](../../../../docs/User_Guide.md#llm) has `/llm`,
[`/memory`](../../../../docs/User_Guide.md#memory) and [Talking to the bot](../../../../docs/User_Guide.md#talking-to-the-bot),
with every reply.

| | |
|---|---|
| **Module** | `llm`: [its README](../README.md) lists what it owns |
| **Code** | `src/modules/llm/src/*`, `llm_command.*` being `/llm`, `/memory` and their panels; the core's `src/core/src/discord/dpp_http_client.*` |
| **Tests** | `src/modules/llm/tests/` (`latibot_llm_tests`, with `mock_llm.hpp`); `tests/mocks/{mock_http,mock_speech}.hpp` |
| **Tables** | `llm_usage`, `llm_documents`, `llm_memory`, `llm_memory_search` (FTS5), `llm_blacklist`, `llm_triggers` (unused since 2026-10-29: copied into the triggers module's table), `llm_aliases`, the module's schema version 1 (was migrations 11 and 15); `llm_personas` and `llm_persona_posts`, version 2; settings as `llm_*` rows in `guild_settings` |
| **Config** | the `llm` section of `config.json`: `provider`, `model`, `check_model`, `spend_cap_daily_usd`, `spend_cap_monthly_usd`, `tool_rounds`; `ANTHROPIC_API_KEY`, `OPENAI_API_KEY` in the environment |
| **Plan** | Replaces plan §14, §21.18–§21.20, and the LLM half of §21.21 |
| **Status** | Built in phase 5 (2026-09-28); personas added 2026-10-20. **Never called a real API or run in Discord**: see §5 |

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

**With the triggers module built in, triggers decide this** (../../triggers/docs/Triggers.md#28-built-in-triggers-when-latibot-answers-at-all):
everything below but conversation mode (§2.10) is a built-in trigger there,
which a server can turn off or edit, and this module answers what a trigger
asks it to, through the same guards. The rules here are what those built-ins
start as, and what the model does on its own when the triggers module is
left out.

In a server that has run `/llm on`, when somebody **addresses** the bot:

- an **@mention** anywhere in the message;
- a **reply** to one of its messages;
- a message **starting with its name** followed by anything that is not a
  letter: `latibot, what's the plan` and `LatiBot what's the plan`, but not
  `latibots` or `hey latibot`. Its name is LatiBot, or its Discord username
  where that differs, such as a test bot's `LatiBot 2`. The model is always
  told it is LatiBot, and its own messages are shown under that name, so its
  rules and the transcript agree.

A reply to a slash command's **result or refusal** does not address it: it
is about what the command said, like the list `/nicknames` shows. Nor does a
reply to a **link replacement**: people reply to one to comment on the post.
The links module says which messages those are, through the
`link_replacements` capability. A reply to any other **post**, a message the
bot put in the channel such as `/say`'s or its own answer, does. Discord marks a command's response with `interaction_metadata`,
which is how the two are told apart. And since a reply pings whoever it
replies to unless that is turned off, a reply's ping is not a mention: only
`<@LatiBot>` written in the message is.

Or when a [trigger asks it to](../../triggers/docs/Triggers.md#26-triggers-that-ask-the-model). An addressed
message is **consumed**, so nothing after it in the
[pipeline](../../../core/docs/Message_Pipeline.md) runs.

Never in a channel the server set to `/channels ignore what:llm` (or
`everything`): there it neither answers nor reads along, whoever writes. Nor
does any bot's message set it off in a log channel, its own and its personas'
included ([Message_Pipeline.md §2.4](../../../core/docs/Message_Pipeline.md#24-channels)).

### 2.2 When it stays quiet

Checked in this order, **before anything is spent**:

1. The server has not turned it on.
2. Nobody addressed it, and no trigger asked it to. A message a trigger
   already answered is not taken up by conversation mode either.
3. There is no API key for the model's provider.
4. The author, or one of their roles, is on the server's **blacklist**. It
   still reads what they write, as part of the conversation.
5. A **spending cap** is reached: $2 in a UTC day or $20 in a UTC month,
   across every server. It says so once per server per day or month where
   it was asked, and warns in the log. Caps are checked before a call, so
   one reply can go a little past; the next is refused.
6. **Rate limits**: 6 replies per person and 20 per channel, a minute.
7. For another bot: the pacing (§2.7).

### 2.3 What it reads, and how it replies

- It shows *typing…* when it starts. Discord keeps that up for ten seconds;
  a longer answer is left to stop showing, rather than kept alive with a
  timer.
- It reads the **last 15 messages** in the channel, cut from the oldest end
  to about **3,000 tokens** (four characters to a token), then the message
  it is answering. They are fetched from Discord for each answer, never kept
  in memory, so they are right after a restart, an edit or a deletion.
- When the message is a **reply**, the message it replies to is shown just
  before it, fetched if it is older than those fifteen, so a reply to
  something said an hour ago is not read on its own. A reply to a message
  deleted since is answered without it.
- The messages are one **transcript**, in one user turn: `u7kx3q: text`,
  each person an alias, never a name or a Discord id (§3.8). Continuation
  lines are indented, so a message cannot fake another speaker, and mentions
  of the bot are written as its name. The model is told that the transcript
  is what it is shown, not instructions to follow.
- The reply is a Discord reply to the message, split into messages of 2,000
  characters, at line breaks where it can. At most **four**, 8,000 characters
  in all: anything past that is cut, and the last message says so. The model
  is told to write as much as the message calls for and no more, not to aim
  for the limit. Whoever addressed the bot is
  notified, as with any reply. **Nothing the model writes can ping anyone**,
  `@everyone` included.
- The longest reply is **1,024 tokens**, including thinking on the models
  that think, so it is a budget, not a word count.

### 2.4 Memory

The model can **remember**, **recall** and **forget** things by itself, as
tools: when someone tells it something worth keeping, or asks it to. So can
each persona (§2.11).

- **Shared or personal.** A memory is shared by LatiBot and every persona,
  unless saved as `personal`: then only whoever saved it sees it, its own
  opinions or plans. What was saved before personas had memory is shared. A
  persona's personal memories go when it is removed.

- Before it answers, up to **8** memories are put in front of it: those
  about the author first, then those matching the message's words. So it
  rarely has to look.
- It writes people in a memory as markers, `<u7kx3q:name>`, and says whom a
  memory is about by alias (§3.8). `/memory` shows them as mentions.
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
[voice session](../../voice/docs/Voice_Channels.md#22-voice-sessions) is **spoken as well as
posted**, in Paul's voice, or a persona's own (§2.11).

- The prompt gains a speaking section: short, plain spoken text with no
  markdown, and a short list of inline commands and the built-in voices.
- The reply is [sanitized](../../dectalk/docs/Speech.md#22-inline-commands-and-who-may-use-which)
  at the `llm` trust level: **never** the host commands, whoever asked.
- The **posted** text is the sanitized text, inline commands and all, so the
  channel sees what was said as it was said.
- The spoken part is cut at the server's `/speak` character limit, and
  queued under whoever asked, so they can `/tts stop` it.

All of that is DECtalk's, reached through the `speech` capability
(`src/core/include/core/capabilities/speech.hpp`), which `audio::dectalk_speech`
implements: whether a channel is spoken in, preparing the text, saying it
in a voice, the voices there are, and the speaking section. The language model's code knows none of it, and
without the capability a reply is only posted
(docs/modules/Module_Plan_Final.md §5.3).

### 2.6 Triggers

The advanced triggers this section described moved to the triggers module
on 2026-10-29, as [triggers that ask the model](../../triggers/docs/Triggers.md#26-triggers-that-ask-the-model),
managed with `/trigger`. What decides they fire is the triggers module's:
the pattern, the chance, the cooldown, which bots set them off. What
decides the model may answer is still this module's, through the
`model_replies` capability it offers:

- **who wrote a message**, of LatiBot's own: LatiBot, a persona, or nobody
  for a command's response, someone else's webhook, or a `talk_to`
  message, which its target is answering already;
- **whether a message speaks to the model**: it addresses LatiBot, replies
  to a persona, or is a persona speaking to LatiBot (§2.11). Such a message
  is answered as such, and sets off no trigger;
- **the answer**, once the same guards as anything else let it through:
  the switch, a channel the model ignores, the persona being on, a key for
  its model, the blacklist, the spend caps (without a notice), the rate
  limits, and for a bot the pacing (§2.7). It reads the last **5** messages
  rather than 15, follows the **trigger style** document, and is posted
  silently, as a Discord reply unless the trigger says otherwise; a
  persona's never is one. Its failures are silent.
- **Answered as spoken to**, for a trigger set off by being addressed or
  replied to, the ask is as if addressed (§2.1): the author is notified, a
  conversation opens, a failure is apologised for, and a spend cap is said
  once. A persona writing to LatiBot this way has to be one that talks.
- **An analysis** asks the check model the trigger's own yes-or-no question
  about the message, in place of whether to reply (`check_instructions` with
  a question), and is answered as named (§2.10) only on a yes. The rate
  limits are taken once the check agrees.
- **`take_over_answering`**: the triggers module calls it at startup, and
  from then on the stage answers nothing on its own but conversation mode,
  and leaves a message that addresses it, replies to it or a persona, or
  names it, to the triggers.

### 2.7 Other bots

It answers a bot only if the server [allows that bot](../../../core/docs/Message_Pipeline.md#23-bots),
and only when the bot **addresses** it or sets off a trigger that asks the
model with `bots` on (§2.6). LatiBot and its personas count as bots here, and
set off nothing but such a trigger, or, for a persona that talks, LatiBot by
name. Then, per channel:

- at most **20** replies to bots in a row, before a person has to say
  something; any person speaking resets the count, whether or not the model
  answers them;
- at least **5 s** between them, **waited out** before answering rather than
  refused;
- at most **500** a day per server (UTC);
- optionally, only once a person has spoken in the channel since the bot
  started.

A turn is claimed when the stage decides, so two bot messages at once cannot
both take the last one.

### 2.8 When the model fails

Someone who addressed the bot is told `sorry, i couldn't come up with
anything just now`, or `i'm a bit overloaded right now; try me again in a
minute` for a 429 or 529. Neither is retried. A trigger's failure says
nothing.

### 2.9 `/llm`

Everyone may run it. `status` and the personality are open by default, and
everything else needs **Manage Server**, checked by the bot itself, since
default permissions are per command, not per subcommand.

| Subcommand | Does |
|---|---|
| `status` | On or off, the model, and what was spent today and this month against the caps, with this server's share |
| `on`, `off` | Lets it answer here, or stops it. `on` warns when the model has no API key |
| `conversation on`, `conversation off` | Conversation mode (§2.10). `on` warns when the model is off here, or the check model has no API key |
| `model name` | This server's model, from the known ones (§3.2) |
| `settings` | A panel of the numbers below, a form per group, and the switches for the model and for conversation mode |
| `personality`, `system`, `style` | The documents (§3.5) |
| `blacklist add \| remove \| list` | A `user` or a `role` it never answers here |
| `persona` | [Personas](#211-personas): `add`, `edit`, `remove`, `list`, `view`, `document` |

**Settings**, per server, each checked against its range. A form with any
value out of range changes nothing, and names the value. Stored values are
clamped when read.

| Setting | Default | Range |
|---|---|---|
| Recent messages it reads | 15 | 0–50 |
| Token budget for them | 3000 | 200–20000 |
| Messages a trigger's answer reads | 5 | 0–20 |
| Longest reply, in tokens | 4096 | 256–8192 |
| Replies per person per minute | 6 | 1–60 |
| Replies per channel per minute | 20 | 1–120 |
| Bot turns in a row (0: never) | 20 | 0–200 |
| Seconds between bot turns | 5 | 0–300 |
| Replies to bots per day | 500 | 0–10000 |
| Only after a person spoke | no | yes or no |
| Minutes it keeps listening after a reply | 10 | 1–120 |
| Replies per conversation, unaddressed | 30 | 1–200 |
| Longest conversation, in minutes | 60 | 1–720 |
| Seconds it waits for typing to stop | 6 | 0–30 |

### 2.10 Conversation mode

Answering only when addressed makes the bot a poor member of a
conversation: people stop saying its name once it is talking with them.
Conversation mode lets it keep up. It is **off in every server**, apart
from `/llm on`, until `/llm conversation on`, since it costs a check on
messages nobody addressed it in.

- **A conversation** opens in a channel when the bot answers someone who
  addressed it or named it, and stays open while the bot keeps replying:
  until **10 minutes** pass without a reply from it, it has made **30**
  replies nobody asked for, or **60 minutes** have passed since it opened.
  Being addressed again starts a fresh one. All three are settings.
- **Its name anywhere** in a message, `thanks latibot` as much as
  `latibot, ...`, is answered at once during a conversation. Outside one it
  could be people talking *about* the bot, so the **check** decides.
- **Each persona has conversations of its own** (§2.11), opened when it
  answers a reply to one of its messages or a `talk_to`, with the same
  limits, kept apart from LatiBot's. A persona's name is just a word: only
  LatiBot's is heard. A message nobody named goes to whoever of them,
  LatiBot or a persona, **replied last** among those with a conversation
  open in the channel.
- **During a conversation**, any other message from a person may be for
  it, and the check decides. Some are left without a check, since they are
  plainly for someone else: a reply to anybody but the bot (or to its
  command output or a link replacement, §2.1), a message that @mentions
  somebody else, one with no text, another bot's, or one a simple trigger
  answered.
- **The check** is one short call to `llm.check_model` (Claude Haiku 5.5
  by default, with thinking off, §3.2) with fixed instructions, the last 8 messages (about 1,500
  tokens at most), what the message replies to, and the message, asking
  whether the bot, or the persona whose conversation it is, should reply:
  yes or no, in at most 8 tokens. It leans towards yes: a short reaction
  right after the bot spoke, "lol" or "wait really?", is usually for it, and
  a close call is a yes, since joining in can still end in `[silent]`. People are
  aliases, as everywhere (§3.8). It costs about $0.0002 on Haiku 5.5, counts
  toward the spend caps, and a failure is a no. A message waiting on the
  check takes its place in the rate limits only once the check says yes.
- **Before the check** it waits while anyone in the channel is typing, up
  to **6 seconds**, and then checks only the author's latest message: one
  they have since followed up is answered through the newer one, which
  comes with it in the transcript.
- **Joining in**, the model is told nobody addressed it, and may answer
  `[silent]`, which posts nothing. It does not show *typing…* first, and
  its reply arrives silently, as a trigger's does. Named, it
  answers as if addressed, and whoever named it is notified.

The bot watches who is typing through Discord's typing events, which need
the `GUILD_MESSAGE_TYPING` intent. It is not privileged.

### 2.11 Personas

A **persona** is someone else the model answers as: a name, a picture, a
model, and instructions of its own. Its messages are posted through a
webhook, so they show under its name and picture, with Discord's APP tag
beside them. Nothing about any persona is built in: the name, picture and
instructions are the server's.

It answers, once the server has run `/llm on`, through the same guards as
LatiBot (§2.2):

- a **reply** to one of its messages;
- a **trigger** given its name as `persona` (§2.6);
- a message in a **conversation** it is in (§2.10);
- a **`talk_to`** from LatiBot or another persona, when it `talks` (below).

**A persona answers no name.** Only LatiBot answers to its name (§2.1): a
persona's name in a message is just a word, from anyone. A persona is never
@mentioned, since a webhook is not a member.

How it answers differs from LatiBot:

- **It is not a reply.** A webhook cannot send one, so the answer is posted
  after the question, and notifies nobody.
- **No *typing…***: Discord would show LatiBot typing.
- **Its own instructions only**: the fixed rules under its name, then its
  **system** document ("Who you are", Manage Server to read and edit), then
  its **personality** (open to everyone, and limited by the same `editors`
  role as LatiBot's). The server's own documents, the trigger style
  included, are LatiBot's and are not sent.
- **Its own model**, or the server's when it has none. A persona on a model
  with no API key does not answer.
- **Its memory**: the shared memories and its own personal ones (§2.4), and
  the same tools as LatiBot (§3.4), except `set_status`, since a webhook has
  no status of its own.
- **Talking to LatiBot and the other personas** is for a persona with
  `talks` on (`/llm persona add | edit talks:true`; off by default). Such a
  persona, and LatiBot always, has the **`talk_to`** tool: it posts a message
  in the channel as the sender, to LatiBot or a persona that also talks,
  never itself, and that one answers it, as if addressed, and so is in a
  conversation with the channel (§2.10). The model is told whom it can reach.
  Each `talk_to` takes a bot turn (§2.7), so the bot pacing ends an
  exchange; the message it posts sets off nothing else. And LatiBot answers
  its name from a persona that talks, as from a person: a message starting
  with it, or an @mention, at once; named in passing, through the check with
  conversation mode on. From a persona that does not talk, its name is
  ignored. No persona answers its name from anyone, and a reply between them
  counts for nothing, since LatiBot answers with one. A trigger with `bots`
  on (§2.6) is the other way in: it lets the bots, LatiBot and the personas
  set it off as a person would. The language model is the one stage that
  hears LatiBot's own webhook (Message_Pipeline.md §2.1), and it tells which
  persona wrote a message by the name it was posted under.
- **It is spoken in a voice of its own**, as LatiBot is (§2.5), when it
  answers in a voice session's text channel. `/llm persona add | edit
  voice:` picks one of the speech capability's voices: DECtalk's built-in
  ones, then the server's custom voices, completed as typed. None, or Paul,
  is the default voice. A custom voice deleted since is spoken as Paul, and
  logged. Without DECtalk built in, `voice` is not offered.
- **Conversations of its own** (§2.10) while the server has conversation
  mode on: it joins in where the check, asked about it by name, says yes.

At most **10** per server. A name is at most 80 characters, not LatiBot's,
and holds neither "clyde" nor "discord", which Discord refuses in a
webhook's name; the picture is an `https://` link, since an attachment's link
expires. The bot needs **Manage Webhooks** in a channel to post as a persona
there.

## 3. How it works

### 3.1 Deciding, then answering

`llm_stage` is the last pipeline stage. It **decides**, and never waits:
the guards of §2.2, the trigger match, the pacing claim. What it decides is
an `ask_llm` action, which says how the message came to the model
(`approach`: addressed, named, joined in or a trigger) and whether the
conversation check has to agree first (§2.10). The shell hands that to the
**responder**, after any pacing wait, since a model call cannot happen
inside a stage. For conversation mode the module waits for typing to stop,
asks the responder's `check`, claims the rate limits, and only then has it
answer; a posted answer opens or extends the channel's conversation.

The responder:

1. starts typing;
2. fetches the recent messages and trims them to the budget;
3. finds the memories to show;
4. builds the prompt;
5. runs the **tool loop**: at most `llm.tool_rounds` (8) rounds, after
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
because the caps are worked out from them. Startup refuses an `llm.model` it
does not know. Prices, per million tokens, checked in September 2026
(Claude Haiku 5.5 in October 2026):

| Model | ID | In | Out | Cache write / read | Effort |
|---|---|---|---|---|---|
| Claude Haiku 5.5 | `claude-haiku-5-5` | $0.10 | $0.50 | $0.125 / $0.01 | yes, **the default** |
| Claude Haiku 4.5 | `claude-haiku-4-5` | $1 | $5 | $1.25 / $0.10 | no |
| Claude Sonnet 5 | `claude-sonnet-5` | $2 | $10 | $2.50 / $0.20 | yes |
| Claude Sonnet 5.5 | `claude-sonnet-5-5` | $2 | $10 | $2.50 / $0.20 | yes |
| Claude Opus 5.5 | `claude-opus-5-5` | $4 | $20 | $5 / $0.20 | yes |
| Claude Opus 5 | `claude-opus-5` | $5 | $25 | $6.25 / $0.50 | yes |
| Claude Fable 5.1 | `claude-fable-5-1` | $10 | $50 | $12.50 / $0.25 | yes |
| GPT-6 Luna | `gpt-6-luna` | $0.10 | $0.50 | — / $0.01 | reasoning off |
| GPT-6 Sol | `gpt-6-sol` | $2 | $10 | — / $0.20 | reasoning off |

Claude Haiku 5.5 has a second rate: a call whose prompt (input, cache writes
and cache reads together) is over 100K tokens costs five times as much for
all of it, output included. `model_info::long_prompt_tokens` carries the
limit, and the cost of each call is worked out from it.

**The request is model-aware**, following the providers' documentation of
September 2026:

- **No `temperature`, ever.** Every current Claude model refuses a
  non-default one.
- **No `thinking` field, except on the check.** Each model keeps its own
  default. On Opus 5.5, thinking cannot be turned off, and `thinking:
  disabled` is a 400. Models that take an effort setting get
  `output_config.effort: low`, which keeps thinking short. Haiku 4.5 takes
  none, and sending one is a 400, so the table says which models take it.
- **The conversation check does not think** (§2.10), since its 8 tokens would
  go on thinking before the yes or no. `model_info::thinking_off` says how
  each model turns it off: `disabled` on Haiku 5.5, Sonnet 5 and Opus 5,
  `between_tools` on Sonnet 5.5. Opus 5.5 and Fable 5.1 cannot, so as
  `llm.check_model` they would think and run out; Haiku 4.5 and OpenAI's
  models do not think here anyway.
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
created_at, owner NULL)`, with `llm_memory_search`, an FTS5 index over `content`, kept
in step by three triggers. A search quotes each word, so nothing in a
message is read as FTS5 syntax. Common and short words are left out, and at
most 16 words are searched.

`owner` says whose a memory is: NULL shared, 0 LatiBot's own, a persona's id
its own. `recall`, and the memories put in front of the model, show the
shared ones and the answerer's own.

The tools are entries in a `tool_registry`: a name, a JSON schema, and a
handler, which may wait on Discord. A later feature adds a tool without
touching the loop. Each is handed the request's `people` in its
`tool_context`, so it reads the aliases the model passes, `remember`'s
`about`, and shows people as aliases (§3.8); the context also says who is
answering, whether the message is a bot's, and which tools are allowed.

Besides the memory tools, the model has these (`discord_tools.*`):

| Tool | Does | Refused when |
|---|---|---|
| `get_message` | Reads a message by id, here, or by a link into this server: who, when, text, attachments, reactions | a link to another server or a DM |
| `search_messages` | Searches a channel's last 500 messages at most, by text, author alias, or UTC dates | Discord gives bots no real search, so nothing older |
| `user_info` | A member: when they joined and made their account, roles, bot, owner, booster. By alias, never a name | they are not in the cache |
| `server_info` | The server: made when, members, boosts, channels, roles, custom emoji | |
| `channel_info` | A channel: kind, category, topic, age-restricted | another server's channel |
| `current_time` | The time now, in UTC or a named zone | an unknown zone |
| `set_nickname` | Changes anyone's nickname, or clears it, recorded by the nicknames module as asked for by whoever the model answers | the bot lacks Manage Nicknames, or the role order; the server owner |
| `set_status` | LatiBot's status, in every server | the server is not in `trusted_guilds`; never offered to a persona |
| `add_reaction` | Reacts to the message answered, or one named, with Unicode or one of the server's emoji | |
| `pin_message` | Pins or unpins | whoever asked lacks Manage Messages there |
| `create_poll` | Posts a Discord poll, as LatiBot or the persona | |
| `set_reminder` | Reminds someone here later, posted by whoever set it and mentioning them; at most 100 waiting per server, a year ahead | |
| `talk_to` | Posts a message to LatiBot or a persona, which answers it (§2.11) | a persona without `talks`, or a target without it; the bot pacing is used up |

The six that change Discord refuse any message from a bot, LatiBot's own
and its personas' included, so one model cannot talk another into acting.
Each is logged. `/llm tools off tool` turns one off in a server for LatiBot
and every persona; the model is then neither shown it nor able to run it.
All are on until then. `tool` completes as typed, since there are more tools
than Discord's 25 choices, and `/llm tools list` falls back to their names
alone when describing them all would pass 2,000 characters.

**The other modules' tools** come through the **`model_tools`** capability
(`core/capabilities/model_tools.hpp`), which this module offers in `offer`;
each module adds its own in `start` and implements them with its own code,
so this module knows none of them, and a tool is there only when its module
is. They join the same registry, so `/llm tools`, the per-persona list and
the tool loop treat them as the model's own. A module's tool is handed a
`model_tool_context`: who asked, where, their permissions in that channel,
the request's people (`user_for`, `shown_as`, `restore`, `sanitize`,
`hide`), `channel_for`, and `post`, as LatiBot or the persona. One marked
`changes` is refused for a bot's message here, before it runs, and logged
when it succeeds, so no module can forget either.

| Module | Tools | The module's spec |
|---|---|---|
| voice | `join_voice`, `leave_voice` | Voice_Channels.md §2.4 |
| dectalk | `speak`, `voice_message`, `stop_speech` | Speech.md §2.6 |
| soundboard | `play_sound` | Soundboard.md §5 |
| music | `music_queue`, `play_music`, `music_control` | Music.md §3.6 |
| nicknames | `nickname_history` | Nicknames.md §2.4 |
| linkstats | `link_stats` | Link_Stats.md §1.1 |
| links | `fix_link` | Url_Replacement.md §2.8 |
| triggers | `list_triggers`, `add_trigger`, `remove_trigger` | Triggers.md §2.6 |
| midnight | `list_midnight`, `add_midnight`, `edit_midnight` | Midnight.md §2.3 |

This module adds two of its own the same way (`bot_tools.*`); the triggers
that ask the model are the triggers module's `add_trigger`, with a prompt:

| Tool | Does | Refused when |
|---|---|---|
| `post_in_channel` | Posts in another channel, as LatiBot or the persona, mentioning nobody | whoever asked cannot post there; it is this channel |
| `bot_info` | The version, uptime, servers and gateway latency | |

Reminders are `llm_reminders`, looked through every 30 seconds by `the
reminder tick`. `set_nickname` goes through the nicknames module's
`nickname_changes` capability, and `set_status` through the core's
`bot_status`, which `/status` shares.

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

### 3.8 Who the model is told about

The model is never told who anyone is. Every person it hears of is an
**alias**, and names reach it only as **markers**, which the bot replaces in
what it posts. Added on 2026-10-02, at the owner's request, so the provider
never receives anything that ties a Discord id, or an alias, to a name.

**Aliases** (`alias_store`, `llm_aliases`, migration 15). Each person gets
one per server the first time the model hears of them: `u` and six
characters with no vowels, no `y` and none of `0`, `1` and `l`, so an alias
never spells a word, like `u7kx3q`. They are random, not derived from the
id, and kept, so a memory that names someone by alias still means them
later; the provider can tell it is the same person from one request to the
next, as it could with names. A person has a different alias in each
server. The table also keeps the name and username each was last seen with,
to put back into a reply when the bot's cache does not know them; those are
never sent.

**What the model is sent** (`people::sanitize`). Each answer has a `people`
of its own. Everyone is met first: **everyone this server has an alias
for**, by the names last noted for them, then whoever is answered, the
authors of the recent messages and whom they mention, and whom the memories
shown are about. The first matter because the bot's and its personas' posts
carry the names put back into them, of people who need not be among the
messages read now. Then:

| In the text | Becomes |
|---|---|
| A transcript line's author | Their alias: `u7kx3q: text`, `u7kx3q (a bot): text`, and the bot `LatiBot (you): text` |
| `<@id>`, `<@!id>` | `@u7kx3q`; the bot's own, `@LatiBot` |
| A name of anyone met, typed in a message or a memory | `<u7kx3q:name>`, or `<u7kx3q:username>` for a username: whole words, any case, longest first, at least three characters |
| A name set with `/llm names` | `<u7kx3q:name>`, as their own names are |
| `<@&id>`, `<#id>` | `@role name`, `#channel name` from the cache, or `@a role`, `#a channel` |
| `<:name:id>`, `<a:name:id>` | `:name:` |
| `<t:seconds>`, `<t:seconds:R>` | `2026-03-15 12:00 UTC` |
| `</name:id>` | `/name` |
| A memory's subject | `(about u7kx3q)` |
| A marker already written, in a memory | itself |

A person's names are their server nickname, from the bot's cache, since a
fetched message carries none; their display name; their username; what was
noted before; and any set by hand with **`/llm names add user name`**
(Manage Server), for the names people go by that Discord does not know.
Those are kept in `llm_alias_names`, where a name stands for one person per
server; adding one gives the person an alias if they had none, so the name
is hidden even before the model has heard of them. `remove` takes one back,
and `list` shows them. The server's documents and a trigger's prompt
have mentions made aliases, but keep their names: the same text then goes
each time, and stays cached (§3.3).

**What the model writes** (`people::restore`). The fixed rules tell it to
write `<u7kx3q:name>` wherever it would write a name, `<u7kx3q:username>`
for a username, and `<u7kx3q:mention>` to mention someone; never to guess a
name. Before the reply is posted:

- `name` (or `nickname`) becomes the name people see in the server: the
  nickname, else the display name, else the username;
- `username` the username;
- `mention` a mention, `<@id>`, which notifies nobody (§2.3); in a spoken
  reply, the name;
- a bare alias, or `@u7kx3q`, the name; and a marker written without its
  brackets, `u7kx3q:name`, as the marker, so nothing of it is left beside
  the name;
- a marker for an alias that is nobody's, "someone".

**Memories** are written by the model with markers, and saved as written;
`remember` takes `about` as an alias. `/memory` shows the people a memory
names as mentions, which Discord shows as their names. A memory saved before
aliases is shown to the model sanitized like a message.

**What it cannot catch.** A name is found only when it is the name of
someone in the request, written as that name: a nickname misspelt, a first
name of a longer display name, or someone who is not in the conversation is
sent as typed, though with no alias beside it. A name that is also a common
word, "Will", is taken for the person wherever it is a whole word, and comes
back as their name in the reply. The documents keep their names. A memory
saved before 2026-10-02 may hold a name the model wrote then.


**Hidden text** (`people::hide`). A tool that must hand the model a name it
may use but not read, such as a past nickname, gets a marker for it,
`<hidden1>`, `<hidden2>`, numbered within the request. `restore` puts the
text back where the model wrote the marker. The markers mean nothing in
another request: one saved in a memory is posted as "something".
### 3.9 How personas post

`persona_store` keeps `llm_personas(id, guild_id, name, avatar_url, model,
enabled, created_by, created_at)`, the name unique per server without regard
to case, and `llm_persona_posts(message_id, persona_id, guild_id, channel_id,
posted_at)`, every message a persona posted. A reply to one of those is a
reply to the persona, and a transcript shows it by the persona's name.
`llm_usage.persona_id` says whose a call is, NULL for LatiBot's; a
trigger's is the triggers module's `persona_id`. A persona's documents are `llm_documents`
rows of kind `persona.<id>.system` and `persona.<id>.personality`, so they
have versions, history, diff and revert like the server's. Removing a persona
removes its documents and posts; its messages stay in Discord, and its
triggers stay in the triggers module, answered by nobody until given to
someone else.

`llm_stage` takes the guild's enabled personas after checking whether
LatiBot is addressed, and its `ask_llm` carries `persona_id`. The responder
looks the persona up again, since it may have gone since, and then:

- the **transcript** shows a persona's post as `Claude (a bot)`, or
  `Claude (you)` to Claude itself, and LatiBot's as `LatiBot (a bot)` to a
  persona. A persona is shown by name and never met as a person, so its name
  typed in a message is left as typed rather than made a marker (§3.8);
- **posting** goes through `persona_webhooks`. It finds the bot's own webhook
  in the channel, named "LatiBot personas" and made by the bot, or makes
  one, and keeps it in memory: the token is never stored, and is masked in
  the log channel. A thread posts through its parent channel's webhook, with
  the thread's id. A 404 or 401, a webhook someone deleted, empties the cache
  and tries once more.

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
| 2026-10-02 | Every person an alias, names as markers put back only in what is posted; no Discord id or name sent | The owner's request: the provider never gets anything that ties an id, or an alias, to a name (§3.8) |
| 2026-10-02 | Aliases random, kept per person per server | Memories name people by alias, and must still mean them later; a different alias in each server keeps the servers apart |
| 2026-10-02 | Names typed in messages found and replaced too, whole words of three characters or more | A name beside an alias would undo it. Shorter names are mostly words |
| 2026-10-27 | Everyone with an alias in the server is met, so their names are hidden even outside the messages read | A persona read a name from an earlier post of the bot's, whose subject was not among the last five messages, and wrote it as a marker |
| 2026-10-27 | A slash command's response is not LatiBot talking, and sets off no advanced trigger | `/llm trigger edit`'s answer quoted the pattern and set the trigger off |
| 2026-10-02 | Documents keep their names; only their mentions become aliases | They are the cached prefix, and must be the same text each time; admins write them |
| 2026-10-02 | Roles, channels, emoji, timestamps and commands written as names | They carried ids, and their names read better to the model |
| 2026-10-06 | A reply to a command's result or refusal does not address the bot; a reply's ping is not a mention | The owner's request: replying to `/nicknames` or `/linkstats` output started a conversation nobody asked for |
| 2026-10-06 | The message a reply replies to shown beside it, fetched when outside the window | Replies to older messages were answered as if on their own |
| 2026-10-06 | A reply to a link replacement does not address the bot | The owner's request: people reply to a replacement to comment on the post |
| 2026-10-07 | Conversation mode: a window per channel after an answer, a cheap yes-or-no check on `llm.check_model` (Haiku 4.5) for what nobody addressed, the bot's name anywhere checked outside a window and answered within one; off until `/llm conversation on` | The owner's request: the bot only answered when addressed. A separate check came out cheaper than letting the answering model decide, unless nearly every message deserves a reply; free rules skip what is plainly for someone else |
| 2026-10-07 | It answers to LatiBot and to its Discord username; the model always sees itself as LatiBot | A test bot named "LatiBot 2" ignored "latibot ..." and read its own messages as "LatiBot 2 (you)" while its rules said "LatiBot (you)" |
| 2026-10-07 | Joining in is told the check judged the message to be for it, and to stay silent only when it plainly is not | In the first live run it chose `[silent]` for a message clearly aimed at it |
| 2026-10-07 | No two-person shortcut: every unaddressed message in a window goes to the check | The owner judged it would answer wrongly |
| 2026-10-07 | Joining in may answer `[silent]`; it waits up to 6 s for typing to stop, and checks only an author's latest message | A free second opinion after the check; not talking over people; not answering twice |
| 2026-10-20 | Personas: another name and picture through a webhook, answering to its name, replies, and triggers of its own; managed with `/llm persona` | The owner's idea (docs/ideas/claude_webhook_llm_facade.md); a group under `/llm` was the owner's choice over a command of its own |
| 2026-10-20 | A persona has no memory, joins no conversation, is never spoken, and never answers a bot (since reversed, each but the last) | The owner's choices in the plan (docs/ideas/Llm_Personas.md); a webhook is one step from two models talking for ever |
| 2026-10-27 | Replies up to four messages, 8,000 characters; 4096 output tokens by default; the rate, pacing and conversation limits raised several times over | The owner's request, with a better idea of what it costs: those limits are there to stop a loop running unchecked, and the spend caps keep the bill in check |
| 2026-10-27 | `/llm names` hides more names behind a person's alias; a name stands for one person per server | The owner's request: people go by names Discord does not know |
| 2026-10-27 | Discord tools: reading messages, people, the server and channels, the time; nicknames, status, reactions, pins, polls, reminders. All on until `/llm tools off`; the ones that change something refuse bots | The owner's list. Renaming needs no permission from the asker, at the owner's say; pinning does. The status is shared by every server, so only `trusted_guilds` may set it |
| 2026-10-27 | Personas get memory and tools; memories are shared or personal | The owner's request: shared notes for the whole cast, and each one's own |
| 2026-10-27 | `llm.tool_rounds` 8 by default | More tools, and more of them needed in one answer |
| 2026-10-27 | LatiBot and the personas answer each other by name, as people address them, but never by a reply, never unnamed, and paced | The owner's request: a persona saying "hey LatiBot" was not answered. LatiBot answers with a reply, so replies would loop. Replaced the same day, below |
| 2026-10-27 | Personas answer no name; LatiBot and the personas that `talks` message each other with the `talk_to` tool; LatiBot answers its name only from a persona that talks | The owner's design: an explicit message, not text read for names, which is where accidental loops come from. `talks` is off by default |
| 2026-10-27 | Personas join conversation mode, each with windows of its own; an unnamed message goes to whoever replied last | The owner's request. A window per speaker keeps each one's limits its own; the last to reply is who people are most likely answering |
| 2026-10-27 | The check leans towards yes, short reactions included | The owner's request: it let short replies to the bot go unanswered. Joining in can still answer `[silent]` |
| 2026-10-27 | A marker written without its brackets, `u7kx3q:name`, is restored whole | Taken as a bare alias, `u7kx3q:name` was posted as `Alice:name`; a persona was seen writing about a user identifier with a name in it |
| 2026-10-20 | A persona's instructions are its own system document and personality, not the server's | A persona is someone else; the server's documents describe LatiBot |
| 2026-10-20 | One webhook per channel serves every persona, kept in memory only | Discord takes the name and picture per message; a token written down is one more secret to keep |
| 2026-10-27 | An advanced trigger with `bots` on is set off by allowed bots, LatiBot and the personas, never by whoever answers it; only the model's stage hears LatiBot's own messages and webhooks' | The owner's request: personas and LatiBot answering each other. Bot pacing already bounds the exchange, and only a trigger, not a name or a reply, starts one |
| 2026-10-27 | Claude Haiku 5.5 is the default for answers and the check, which runs with thinking off | A tenth of Haiku 4.5's price, and 4.5 is due for retirement; the check's 8 tokens leave no room for thinking |
| 2026-10-27 | A persona is spoken in a voice session, in a voice of its own; the speech capability says the voices and `say` takes one | The owner's request, reversing 2026-10-20. Its own voice keeps it from being heard as LatiBot; the list stays DECtalk's, so the language model's code knows none of its voices |
| 2026-10-28 | The other modules give the model tools through one capability, `model_tools`, which llm offers, rather than a bridge library or a capability each | The owner's choice. Eighteen tools across seven modules: each module keeps its tool beside its command, llm knows none of them, and the bot rule is applied once |
| 2026-10-28 | A name a tool must give the model, such as a past nickname, is hidden text: a marker put back as the reply is posted | §3.8: the provider never receives a name, and the tool would be useless with none |
| 2026-10-28 | `/llm trigger edit` with only the id opens a form holding the trigger; replies show the whole prompt | A command's options cannot start out filled in, and a prompt cut at 120 characters could not be checked before editing it. Superseded the next day, below |
| 2026-10-29 | Advanced triggers move to the triggers module, which reaches the model through a `model_replies` capability; `/llm trigger` and its tools are gone, and `llm_triggers` stays, unused, until the owner says | The owner's choice (Triggers.md §4). This module keeps every decision about whether the model may speak |
| 2026-10-29 | With the triggers module, being addressed, replied to and named are its built-in triggers, and this stage keeps only conversation mode; the check takes an analysis trigger's question | The owner's choice: every answer through a trigger. Conversation mode has no word to match, which an analysis needs. Without the triggers module, nothing changes here |

## 5. Limits, and what is still to check

- **Claude Haiku 4.5 may be retired from 15 October 2026.** When it goes,
  every server still on it gets a 404. The default moved to Haiku 5.5 on
  27 October 2026, but a server that picked 4.5 with `/llm model`, or a
  `config.json` that names it, keeps it until it is changed.
- **Haiku 5.5 counts the same text as about 30% more tokens** than Haiku
  4.5, so the token budgets of §2.3 hold a little less text.
- It has never called a real API. The request shapes follow the providers'
  documentation of September 2026, and the first real call is the test that
  settles them:
  - that the Messages API accepts `output_config.effort`, the
    `cache_control` on the system block, and `tool_choice: none`;
  - that OpenAI's GPT-6 models accept `reasoning_effort: none` under Chat
    Completions.
- **Still to check in Discord:**
  - that `describe` sees mentions and replies, and that Discord's
    `referenced_message` carries `interaction_metadata` for a command's
    response, so replies to `/nicknames` are left alone;
  - that real models write markers where they would write names, and
    `about` as an alias, and how often a name gets through in what people
    type (§3.8);
  - that the cache has nicknames for the authors of fetched messages, which
    needs the Server Members intent (`nicknames.track_changes`);
  - that `/llm` registers, since it is the largest command and Discord allows
    8,000 characters per command;
  - that the `/llm settings` panel routes, and whether its group menu sticks
    on the picked option after a cancelled form;
  - whether any document was saved blank before the fix of 2026-09-29.
    `history` shows one, and `revert` restores it.
- Pacing and rate limits live in memory, so a restart resets them, and so
  do conversations and who is typing.
- **Personas have never run in Discord.** Still to see: that a webhook's
  message really cannot be a reply, that the APP tag shows, how threads and
  voice channels' text chats take a webhook, and that a real model keeps to
  the persona and writes markers under its rules.
- **Conversation mode has never run.** Still to see in Discord: how often
  the check says yes when it should not, and the other way round; whether
  the windows' defaults suit the server; and whether the model uses
  `[silent]` when it should.
