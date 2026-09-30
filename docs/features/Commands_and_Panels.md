# Commands and panels

What every slash command and every panel shares: how a command is declared,
registered with Discord and dispatched; how its messages are flagged; how a
panel's buttons, menus and forms find their way back to it with no state
kept on the bot's side; and how the bot's commands are unregistered. The
features themselves are in their own specs.

This is the spec for that shared machinery: what it is for, how it behaves,
how it is built, and what was decided and why. [Classes.md §4–§5](../architecture/Classes.md#4-slash-commands)
draw the classes, and [Execution_Flow.md §8](../architecture/Execution_Flow.md#8-a-slash-command)
and [§10](../architecture/Execution_Flow.md#10-panels-buttons-menus-and-forms)
the flows.

| | |
|---|---|
| **Code** | `src/core/commands/{registry,options,message_options,unregister}.*`, `src/core/discord/{message_flags,unregister_commands}.*`, `src/core/ui/{paginator,interaction}.*`, `src/core/util/text.*` (`fit_lines`, `truncate`); registration and routing in `src/core/bot.cpp` |
| **Tests** | `tests/unit/{registry,command_options,command_responses,command_log,message_flags,paginator,panels,unregister}_test.cpp`, `tests/support/{panel_harness,slash_event,discord_limits}.hpp` |
| **Plan** | Replaces plan §5.3, §21.4, §21.5, §21.13, §21.15 and §21.21; code comments still cite those |
| **Status** | Built in phase 0 and 1; message flags per kind on 2026-09-25; forms read through `ui::form_fields` since 2026-09-29; `--unregister-commands` on 2026-09-30, not yet run against Discord |

## Contents

1. [Intent](#1-intent)
2. [Commands](#2-commands)
3. [Message flags](#3-message-flags)
4. [Panels](#4-panels)
5. [Discord's limits](#5-discords-limits)
6. [Decisions](#6-decisions)
7. [Limits, and what is still to check](#7-limits-and-what-is-still-to-check)

## 1. Intent

The Java bot had a hand-rolled `BaseCommand` / `CommandRegistry` and a static
`Commands` holder. The port keeps the idea and makes the parts that went
wrong impossible:

- A command that throws answers its caller, rather than leaving Discord to
  say *The application did not respond*.
- Registering an empty list, which **deletes** every command, is refused.
- Whether a reply is private, silent or preview-free is decided in one
  place per command, not by each renderer.
- Panels keep nothing in memory, so a restart never breaks a button.

## 2. Commands

### 2.1 Declaring one

A `commands::command` returns a `command_info`:

| Field | What |
|---|---|
| `name`, `description`, `aliases` | Each alias is registered as its own slash command pointing at the same handler |
| `required_bot_permissions` | Fed to the startup [permission warnings](Operations.md#6-permission-warnings) |
| `default_member_permissions` | Who Discord offers it to by default; admins can change it per role in *Server Settings → Integrations* |
| `guild_only` | Not offered in DMs |
| `responses`, `subcommand_responses` | The flags for each kind of message (§3) |

Handlers return `dpp::task<void>` and stay thin: turn the event into plain
data, call a core function, act on what it returns. Options are read through
one set of helpers (`commands/options`). Autocomplete is **not** a coroutine:
Discord allows three seconds, and the answer comes from what the bot already
knows.

**Default member permissions are per command, not per subcommand.** A
command open to everyone whose subcommands need more checks the permission
itself: `/linkstats`'s aliases and recompute, `/llm`'s everything but
`status` and the personality. `/urltoggle` is its own command rather than a
subcommand of the Manage Server `/urlrepl` for the same reason.

**A command with subcommands cannot also run bare**, and a subcommand cannot
also be a group. So `/urlrepl panel`, not `/urlrepl`, and
`/linkstats recompute start` and `cancel`.

### 2.2 Registering

On the **first** `on_ready` of a run (`dpp::run_once`), the registry's
payloads, one per command and per alias, go to Discord in one
`global_bulk_command_create`. A reconnect's `on_ready` skips it. An empty
registry is **refused** with a warning, since a bulk create with an empty
list deletes every registered command. Global commands can take a little
while to show changes in Discord.

### 2.3 Dispatching

`registry::dispatch` finds the command by name or alias and runs it.

- **An unknown name** is logged and answered `i don't have that command any
  more`. Discord can still deliver a command removed from the code but not
  yet from its cache.
- **A command that throws** is logged and answered
  `something went wrong on my end running that; it's in the log`, with the
  command's refusal flags. The exception never reaches DPP, where it would
  end the process. A deferred command's failure edits the deferred answer.
- **Every invocation is logged** at `info`: who (`name (id)`), the
  subcommand path and each option as `name=value`, on one bounded line.
  Newlines are escaped, and long values are cut with their length noted.
- **Deferring.** The first response is due within three seconds. A command
  that must wait on Discord first, such as `/say reply:` or `/nickname`,
  calls `defer`, then `answer_deferred`. The deferral is private when the
  subcommand's result is, since that cannot change afterwards.

### 2.4 Unregistering: `--unregister-commands`

A test bot sharing a server with the real one leaves its commands beside the
real ones, so every command shows twice. Started with
`--unregister-commands`, the bot deletes its commands and exits:

- It signs in over **REST only**: a DPP cluster with no shards, so it never
  comes online and nothing else of the bot runs. DPP asks who the token
  belongs to, and fires `on_ready` itself.
- It logs **"signed in as <name> (<id>)"** before deleting anything, since
  the token in `DISCORD_BOT_TOKEN` decides whose commands go.
- It deletes the global commands, then each server's own, in every server
  the bot is in (up to 200). An empty set is not sent. A failure is recorded
  and the rest carry on.
- It exits **0** when everything went, **1** otherwise, and **1** if sign-in
  has not finished in **30 s** (a bad token surfaces only as a DPP log line).

`commands::unregister_all` does the deleting through the
`ports::command_host` port and is tested against `mock_command_host`.
`discord::unregister_commands` runs it over DPP. The VS Code task
**Unregister the bot's commands (Debug)** builds the Debug bot and runs it.
The next ordinary start registers the commands again. The arguments are
parsed by `config::command_line` ([Operations.md §2](Operations.md#2-starting-up)).

## 3. Message flags

A command's messages come in three **kinds**, each with its own flags:

| Kind | What | Default |
|---|---|---|
| `result` | the answer the command exists to give | private |
| `refusal` | a typo, a missing permission, a failure | private |
| `post` | an ordinary channel message sent on its behalf, such as what `/say` says or a recompute's progress | public; never ephemeral, since only a reply can be |

A subcommand overrides only what differs (`subcommand_responses`, keyed by
its path, `"alias add"`). The helpers `result`, `refusal` and `post` are the
**only** way a command builds a message, and they replace any flag a
renderer set, so a command's flags are decided in its constructor and
nowhere else. `registry::add` refuses, at startup, an override naming a
subcommand the command does not have, and a flag that kind of message
cannot carry, such as an ephemeral post.

- A **panel button** edits a message rather than answering a command, so it
  keeps the flags the message was sent with (`ui::update_panel`). An edit
  that left out "no previews" would bring the previews back.
- **Messages that are not command replies** carry their own flags on
  `events::send_message`. Trigger replies and midnight messages store theirs
  (`silent`, `previews`), and default to silent. URL replacements keep fixed
  flags, since a preview is their whole job.

## 4. Panels

A panel is a message with menus and buttons, and sometimes a form, such as
`/trigger panel`, `/urlrepl panel`, `/voice lab` or `/llm settings`.

**State lives in the `custom_id`.** `ui::page_state` is `view:page:argument`:

- the **view** names what the component does;
- the **page** is zero-based;
- the **argument** is whatever the view needs to rebuild itself, such as a
  trigger id, a domain, or a board's filters.

`ui::encode` returns **nothing** rather than a truncated id when the state
would pass Discord's 100 characters, since a truncated one decodes to the
wrong page. A stale button from a longer list is clamped to the last page,
not refused. So paging survives a restart, nothing on the bot's side
expires, and two people can each have their own panel open. The one
exception is the voice lab's per-person draft, kept for 30 minutes.

New views take a short prefix of their panel (`urlpanel`, `trigedit`,
`vlabsave`). Older names that do not follow that (`triggers`, `nicks`,
`linkboard`) are kept, since renaming one breaks buttons already sent.

**Routing.** `on_button_click` and `on_select_click` go to
`bot::on_component`, and `on_form_submit` to `bot::on_form`. The nickname
history's pages and URL replacement's Retry are handled there; otherwise
each panel is asked in turn (`trigger_panel`, `url_panel`, `voice_lab`, the
`/llm` panels, then the link stats boards) whether the view is its own. Each panel is a class
with `on_component` and `on_form`, not a shared base: that shape is what the
"third example" turned out to share. A view nobody claims, or a panel that
throws, is still answered privately.

**Forms.** `ui::form_fields` reads a submitted modal in **both** of the
shapes DPP has used:

- DPP 10.1 wraps each field in a Label, and hands back the fields
  themselves;
- older modals had action rows with the fields inside.

The shell refuses a submission with **no** fields, since Discord always
sends every field back. A document form must return every part. So a
misread form can never again save blanks.

**Testing through DPP.** `tests/support/panel_harness.hpp` writes what
Discord would send, has DPP's own `events::internal_handle_interaction`
read it, and takes the answer from DPP's own `reply` and `dialog`. A form
is submitted by taking the modal DPP wrote and sending it back as Discord
would, so a change on either side shows.

**Long pages.** `util::fit_lines` shortens a page's lines evenly when it
would pass 2,000 characters, and `util::truncate` keeps menu labels within
100.

## 5. Discord's limits

Discord enforces these only at the API, often by refusing the whole message
with `50035 Invalid Form Body`. So every renderer's test checks its output
against `tests/support/discord_limits.hpp`:

| Thing | Limit |
|---|---|
| Message text | 2,000 characters |
| Message components | 5 action rows of 5 |
| `custom_id` | 1–100 characters |
| Button label | 80 characters |
| Select menu | 25 options; option text 100 characters; placeholder 150 |
| Modal title, text input label | 45 characters |
| Text input | placeholder 100 characters; value 4,000 |
| Modal | 5 components; cannot be updated in place, or answered with another modal |
| Autocomplete | 25 choices |

DPP 10.1.6 lacks Discord's Radio Group, Checkbox Group and Checkbox
components, so an editable list cannot live in a modal alone. It needs a
panel message plus modals, which is the shape every panel here has.

## 6. Decisions

| Date | Decision | Why |
|---|---|---|
| plan v4 | One registry; aliases are extra slash commands to the same handler | Replaces the Java `CommandRegistry` and static `Commands` holder |
| plan v4 | Handlers thin, logic in plain-data functions | Tests reach the logic without a gateway |
| plan v4 | Refuse to register an empty list | A bulk create with none deletes every command |
| plan v4 | Panel state in `custom_id`; `encode` refuses rather than truncates | Survives restarts; a truncated id decodes wrong |
| 2026-09-22 | Panels stay concrete until a third example shows what they share | A shared base guessed from one example is usually wrong (plan §21.5) |
| 2026-09-23 | Assert Discord's limits in tests that build the payload | A 50-character label silently broke the trigger modal (plan §21.4) |
| 2026-09-24 | `panel` and `start`/`cancel` subcommands; `/urltoggle` separate; permission checks inside commands | Discord's command shapes and per-command permissions (plan §21.13) |
| 2026-09-25 | Flags per kind of message, with per-subcommand overrides, checked at registration | One set per command could not describe `/say` or `/linkstats` (plan §21.15) |
| 2026-09-25 | A failing or unclaimed component or form is answered | It got no answer before (cleanup ERR-002) |
| 2026-09-25 | `defer` and `answer_deferred` for commands that wait on Discord | The three-second first response (cleanup DISC-004) |
| 2026-09-25 | View names get a panel prefix; old ones stay | Renaming breaks buttons already sent |
| 2026-09-29 | `ui::form_fields` reads both shapes; empty submissions refused; panels tested through DPP | Every form arrived empty under DPP 10.1, and hand-built test events hid it (plan §21.21) |
| 2026-09-29 | Panels route their own components; the shared shape is a class with `on_component` and `on_form` | That is what the third example turned out to share |
| 2026-09-30 | `--unregister-commands`: REST only, names the bot first, exits 0 or 1 | A test bot's commands doubled every command in a shared server |

## 7. Limits, and what is still to check

- A select menu that opens a form (the voice lab's groups, `/llm settings`)
  cannot also be reset by it, since a response does one thing. If Discord
  leaves the menu on the option picked after the form is cancelled, picking
  it again does nothing until something else updates the panel. **Still to
  check in Discord.**
- The one-line hand-off from `on_component` to each panel is not tested,
  nor the nickname history's and link board's paging.
- `--unregister-commands` has not been run against Discord.
- The guild listing for unregistering covers at most 200 servers.
