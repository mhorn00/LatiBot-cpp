# URL replacement

When somebody posts a link to a site whose previews are poor or missing,
such as x.com or tiktok.com, the bot posts it again on a mirror that previews
properly. It then turns the preview on the original off, and watches until
the new preview has actually appeared. A server chooses the sites and
mirrors, and each member can opt out.

This is the feature's spec: what it is for, how it behaves, how it is built,
and what was decided and why. [The user guide](README.md#urlrepl) says how to
use it, with every reply. [Classes.md §7](../architecture/Classes.md#7-link-replacement)
draws its classes, and
[Execution_Flow.md §6](../architecture/Execution_Flow.md#6-link-replacement-posting-and-watching)
the posting and watching. The reactions on replacements are counted by
[link stats](Link_Stats.md).

| | |
|---|---|
| **Code** | `src/core/util/url_scan.*`, `src/core/events/{url_rules,url_replacer,embed_watch,replacements}.*`, `src/core/commands/urlrepl.*` |
| **Tests** | `tests/unit/{url_scan,url_rules,embed_watch,urlrepl_command}_test.cpp`, `tests/db/{url_rule_store,replacement_store}_test.cpp`, the URL panel in `tests/unit/panels_test.cpp`, `tests/fuzz/fuzz_url_scan.cpp` |
| **Tables** | `url_rules`, `url_opt_outs`, `known_mirrors`, `replacement_messages`, `replacement_links` (migration 6) |
| **Settings** | `url_replacement_enabled` and `url_rules_imported` per server in `guild_settings` |
| **Plan** | Replaces plan §9.1–§9.5, §21.11 and §21.13 |
| **Status** | Built in phase 3 (2026-09-24). The restart sweep (§3.5) has not been seen working in Discord yet |

## Contents

1. [Intent](#1-intent)
2. [Behaviour](#2-behaviour)
3. [How it works](#3-how-it-works)
4. [Decisions](#4-decisions)
5. [Limits, and what is still to check](#5-limits-and-what-is-still-to-check)

## 1. Intent

Some sites' links preview badly in Discord, or not at all, and "embed fixer"
mirrors exist to fix that: `fxtwitter.com/…` previews what `x.com/…` does
not. This was the Java bot's most-used feature, and its buggiest. The port
keeps what it did and fixes the ways it went wrong:

| The Java bot | Why | Now |
|---|---|---|
| Replaced only the first link in a message | A greedy `(?<before>.*)` group swallowed the whole message on the first match | Every link, up to five |
| Lost spoilers | `split("||")` takes a regex, so it tested whether the text had an even number of characters | Spoilers are counted properly, and a spoilered link stays spoilered |
| Gave up on the whole message at one link without a rule | One lookup failure ended the loop | That link is skipped on its own |
| Called a slow preview a failure | It looked five seconds later and compared a count | It watches for the preview to arrive |
| Deleted its message when no mirror worked | | Leaves a note with a Retry button, and gives the original its preview back |
| Forgot every `/toggle` opt-out on restart | They were kept in memory | Opt-outs are stored |
| Replaced links in every server it was in | | Off in each server until someone turns it on |

**Webhook mode**, in which the bot deleted the original and reposted it as
the member through a webhook, is gone.

## 2. Behaviour

### 2.1 Which links are replaced

A link is replaced when all of these hold:

- the server has turned replacement on (`/urlrepl enable`, or the panel's
  button);
- the message is from a person in a server, not a bot or a DM;
- the author has not opted out with `/urltoggle`, and did not turn the
  message's previews off themselves;
- the link's site has a rule. Sites are matched by host, lowercased, without
  `www.`, credentials or a port, so `https://WWW.X.com:443/a` is `x.com`;
- the link is not written as `<https://…>`, which is how somebody asks
  Discord for no preview, and is not inside `code` or a code block;
- it is not a link already seen earlier in the same message.

At most **five** links go in one replacement. Links are then dropped from the
end until the post fits in Discord's 2,000 characters.

### 2.2 What is posted

A **plain message, never a reply**, with one line per link, notifications
suppressed and no mentions parsed:

```
🔗 [_](https://fxtwitter.com/somebody/status/1)
🔗 ||[_](https://tfxktok.com/@somebody/video/2)||
```

The underscore is the whole visible text, so what people see is the preview.
A link that was inside a `||spoiler||` has its link spoilered too. A mirror
can carry a path to add to every link, such as `fxtwitter.com/en` for
mirrors that translate when asked. The path goes before any query or
fragment, a trailing slash does not make it `//en`, and a path that already
ends in it does not get it twice. Mirrors are always reached over `https`.

The replacement is posted **first**. Only then are the original's previews
turned off, so a post that fails leaves the original as it was.

### 2.3 Watching for the preview

Each mirror gets **two tries of six seconds** (`alt1, alt1, alt2, alt2, …`).
Every link in a message is followed on its own. A link counts as working as
soon as a preview for it appears on our message. When a try runs out of time,
the message is edited to the next try's URL. If one link of several works,
the replacement counts as working and the others stay on their last mirror.

### 2.4 When no mirror works

1. The original's previews are turned **back on**.
2. Our message is **kept**, edited to a note naming the mirrors tried, with
   its own previews off and a **Retry** button.
3. **Anyone** can press Retry. It makes one pass, one try per mirror, using
   the rule **as it is now**, so fixing a rule and pressing Retry works. On
   success the replacement comes back and the original's previews go off
   again. On failure the note and the button stay.

A second press while a retry is running is told so. Retry does nothing in a
server that has since turned replacement off, and skips links whose rule has
since been removed. The button works after a restart.

### 2.5 A restart during a watch

A watch lives only in memory. A replacement still being watched when the bot
stopped would otherwise stay `pending` for ever, with no Retry button and the
original's preview off. At startup these are read from the database. When
each server connects, each one is fetched and **settled on what it shows
now**: working if it has a preview, otherwise the note and Retry, with the
original's preview back on. Nothing is tried again by itself. A message that
cannot be reached any more (403, 404) is marked failed; any other error
leaves it for the next start.

### 2.6 The commands

| Command | Who | What it does |
|---|---|---|
| `/urlrepl enable`, `disable` | Manage Server | Turns replacement on or off here. Off stops new replacements and Retry; replacements already posted stay, and their reactions still count |
| `/urlrepl list` | Manage Server | Whether it is on, then the rules, five a page |
| `/urlrepl set domain mirrors` | Manage Server | Adds a rule, or replaces a site's whole mirror list, which is also how reordering works |
| `/urlrepl remove domain` | Manage Server | Deletes a rule |
| `/urlrepl test text` | Manage Server | A dry run over a whole message: what would be posted, and a verdict for every link |
| `/urlrepl panel` | Manage Server | The rules with Edit, Delete, Add rule and an on/off button |
| `/urltoggle [user]` | everyone for themselves; Manage Server for anyone else | Flips an opt-out, per server |

All of them answer privately. `domain` autocompletes the sites that have a
rule. A rule that could not work is refused, with the reason:

| Refused | Because |
|---|---|
| No mirrors | nothing to send links to |
| A mirror that is the site itself | it would "replace" a link with the same link |
| A site or mirror with no dot in it | it cannot be a host |
| More than **8** mirrors | at two tries of six seconds each, nobody waits for more |

A mirror listed twice is kept once, in its first place. In the panel, Edit
and Add open a form with the site and **one mirror per line** in the order
they are tried. Changing the site renames the rule. Neither may save over
another site's rule, whether by adding a site that has one or renaming onto
one.

`/urlrepl test` runs the same code a real message does, so the two cannot
disagree. It works while replacement is off, and mentions an opt-out, since
that would explain a link being left alone.

### 2.7 Rules from the Java bot

When `UrlReplacements.txt` (`domain|mirror^mirror^…`, one rule per line) is
beside the database, its rules are copied into each server the first time the
bot sees that server with the file there. A server is imported once
(`url_rules_imported`); a rule the server already has is never overwritten.
A bad line is named in the log, and a domain listed twice keeps its last line,
as the Java bot's map did. Importing does not turn replacement on.

## 3. How it works

### 3.1 The scanner

`util::find_links` finds every `http(s)` link, each separately, using CTRE:
the pattern is compiled into ordinary C++, so a typo is a compile error, and
matching neither recurses nor allocates. `std::regex` was ruled out because
MSVC's implementation recurses and can overflow the stack on long input, on
a path that sees every message. For each link it records:

- the byte offsets;
- whether the link is inside an open `||` spoiler (markers inside code do
  not count);
- whether it is written as `<…>`;
- whether it is inside code (`util::code_spans`: a run of backticks is closed
  by the next run of the same length).

Trailing punctuation and a closing bracket the link did not open are left
off, as Discord does.

`explain_links` gives every link a `link_decision` (`replaced`, `no_rule`,
`preview_off`, `in_code`, `duplicate`, `over_limit`). `plan_replacements` is
that list filtered to `replaced`, which is why `/urlrepl test` cannot
disagree with a real message. A `planned_link` copies the rule's mirrors
when the message arrives, so editing a rule does not change a replacement
already being tried.

### 3.2 The stage and the post

`url_replacer` is a stage of the [message pipeline](Message_Pipeline.md). It
returns a `replace_links` action and **does not consume** the message, so a
message with "420" and a link gets the trigger's reply and the replacement.
It keeps almost every message away from the database: messages from bots
and DMs, and messages without `://`, are turned away before anything is read.

`post_replacement` posts, records the replacement in `replacement_messages`
and `replacement_links` as `pending`, turns the original's previews off
(`discord_gateway::set_embeds_suppressed`, a PATCH carrying only `flags`, the
one edit Discord allows on somebody else's message), and starts the watch.

### 3.3 The tracker

`embed_tracker` is a state machine, not a coroutine per message. Every
method returns the edits to make as plain data (`embed_action`: edit our
message, or set the original's suppression), which the shell carries out.
It is tested with a mock clock and no waiting.

| Input | From | Does |
|---|---|---|
| `watch` | the post, or Retry | Starts a watch, taking any previews already on the message and any early update (below) |
| `on_embeds` | `on_message_update` | Marks the links the previews cover; a watch with nothing left waiting ends as working |
| `tick` | a one-second cluster timer | Moves each link whose time is up to its next try, or ends the watch as failed |
| `settle` | the restart sweep | Ends a watch at once on the previews there now |
| `forget` | `on_message_delete` | Our message is gone; nothing left to edit |

**Matching previews to links.** Mirrors usually report the original site's
URL, so a preview is matched to a link by path, on any host. Previews left
over go to the remaining links in order, which is the order Discord builds
them in. Several previews with one URL, such as a post with four images, all
belong to one link.

**Early updates.** Discord sends the preview in a gateway `MESSAGE_UPDATE`,
and our message's id arrives in the REST reply to the post. The two are not
ordered, and a quick preview can arrive first. So an update for a message
nobody is watching, if it carries previews, is held for **30 seconds**, up
to **256** of them, and a new watch absorbs any it finds.

### 3.4 Storage

`replacement_messages` holds one row per replacement, with `state` one of
`pending`, `ok`, `failed` or `retrying`. `replacement_links` holds its links
in order. Mirrors are not stored, since Retry uses the rule as it is when
pressed. Rows are kept after the message is gone, because
[link stats](Link_Stats.md) counts reactions against them, and a
`kind` column (migration 12) lets that table hold image posts too.

`url_rules` holds one row per mirror, with `position` 0 tried first. A rule
is written wholesale, so the order is simply the list. `url_rule_store::rename`
swaps a rule for another site's in one transaction. `known_mirrors` is every
mirror host any rule has named, and is never pruned: the recompute recognises
old replacements by it. The on/off switch and the import marker are
`guild_settings` rows, so neither needed a migration.

### 3.5 The panel

`url_panel` keeps nothing on the bot's side. The page and the chosen rule
travel in each component's `custom_id` (views `urlpanel`, `urlpick`,
`urledit`, `urldel`, `urlyes`, `urladd`, `urlform`, `urlswitch`, and
`urllist` for `/urlrepl list`'s pages), so it survives a restart and two
people can each have their own open. Lines are shortened evenly when a page
of long mirror lists would pass 2,000 characters. The shared shape of
panels is in [Commands_and_Panels.md](Commands_and_Panels.md).

## 4. Decisions

| Date | Decision | Why |
|---|---|---|
| plan v4 | A plain message, never a reply | Replies were tried in the Java bot and rejected on looks; the bot answers fast enough to be the next message anyway |
| plan v4 | Two tries per mirror, about six seconds each | A slow first fetch is common enough to deserve a second chance |
| plan v4 | Watch for the preview rather than poll | Polling made a slow network look exactly like a failure |
| plan v4 | Keep the message on failure, with a Retry anyone can press | Deleting it lost the link, and a failure is often a mirror that will work later |
| plan v4 | CTRE, never `std::regex` | MSVC's is recursive and can overflow the stack, on a path that sees every message |
| plan v4 | Opt-outs stored, per server | The Java `/toggle` forgot them on every restart |
| plan v4 | Webhook mode dropped | |
| 2026-09-24 | A tracker returning actions, not a coroutine per message | Tests run with a mock clock and no waiting, and one timer serves every message |
| 2026-09-24 | Post first, then suppress the original | A failed post then leaves the original's preview |
| 2026-09-24 | At most five links, dropped from the end past 2,000 characters | A message that is mostly links is somebody pasting a list |
| 2026-09-24 | Retry uses the rule as it is when pressed; mirrors are not stored | A rule fixed since the failure is usually why somebody presses Retry |
| 2026-09-24 | One timeout constant, not a per-server setting | Nobody has needed another value |
| 2026-09-24 | Hold unclaimed preview updates for 30 s, at most 256 | A preview can arrive before the post's reply; the cap is because every update in every server passes through |
| 2026-09-24 | The panel is `/urlrepl panel`, and opting out is its own command `/urltoggle` | A command with subcommands cannot run bare, and default permissions are per command, not per subcommand |
| 2026-09-24 | A mirror's translation path is written on the mirror (`fxtwitter.com/en`) | It needs no option of its own |
| 2026-09-24 | Import `UrlReplacements.txt` once per server, never over an existing rule | Deleting a rule must not be undone by the next restart |
| 2026-09-24 | Off in each server until turned on | The bot joins servers for other features too, and the import creates rules everywhere |
| 2026-09-25 | Settle a replacement stranded by a restart at once, from a fetched copy (cleanup decision 7) | Restarting its watch would edit a message that may have sat there for hours |
| 2026-09-25 | Suppressed links and spoiler markers in code are read as Discord reads them | Found by the cleanup analysis (BUG-001) |
| 2026-09-25 | `/urlrepl set` and `remove`, and `/urltoggle`, answer privately (cleanup decision 1) | Public in the Java bot, but a rule change or an opt-out is the business of whoever made it |

## 5. Limits, and what is still to check

- A link's preview is Discord's to build. A mirror that previews in a browser
  may still not embed, and a site's own preview may improve, leaving the rule
  unnecessary.
- The tracker is memory only; §2.5 covers a restart, and nothing covers a
  crash that loses the database write.
- Turning the original's preview off needs **Manage Messages**, and our
  preview needs **Embed Links**. Without them the replacement still posts,
  and the startup log names what is missing ([Operations.md](Operations.md#6-permission-warnings)).
- Wiring in the shell is untested by design: `on_message_update` feeding the
  tracker, `on_message_delete` forgetting a message, and the one-second
  timer.
- **Still to check in Discord:** that a restart during a watch settles the
  replacement when the server connects.
