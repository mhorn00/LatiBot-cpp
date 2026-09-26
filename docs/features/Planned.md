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

## Phase 5 — the LLM

The Java bot had an OpenAI integration that was written but never switched on.
This is designed fresh.

### Talking to the bot (passive)

The bot replies when **addressed**: @mentioned, replied to, or a message
starting with its name. It answers in text by default, and **speaks as well**
when a [voice session](README.md#voice) is active in that channel.

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
