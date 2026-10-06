# The linkstats module

Counts the reactions on the bot's link replacements, and on people's image
and video posts where a server asks, with emotes sent as messages counted as
reactions; ranks them; and keeps the bot's own copies of the emojis it has
seen. How it behaves, and why, is [docs/features/Link_Stats.md](../../../docs/features/Link_Stats.md);
the commands' replies are in [the user guide](../../../docs/features/README.md#linkstats).

Built unless `LATIBOT_WITH_LINKSTATS` is off. It requires links: its tables
refer to links' `replacement_messages`, and it finds the bot's old
replacements by links' rules, through links' public headers. Leaving it out
leaves its tables and their rows where they are.

## What it owns

`tests/linkstats_module_test.cpp` checks the rows marked ✓ against what the
module registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/linkstats` | ✓ |
| Panels | `linkboard` (a board's pages), `linkdupes`, `linkkeep`, `linkmerge` | ✓ |
| Message stages | none | ✓ |
| Discord events | `linkstats: reactions added`, `linkstats: reactions taken back`, `linkstats: an emoji cleared`, `linkstats: every reaction cleared`, `linkstats: image posts and emotes sent as reactions` (new messages), `linkstats: a link turning out to be an image` (message updates), `linkstats: emotes deleted` | ✓ |
| Timers | `copying emojis`, every minute, unless copying is off | ✓ |
| Tables | `reactions`, `reaction_log`, `emojis`, `emoji_aliases`, `backfill_progress`, `emoji_images`, `emoji_copies`, `emote_reactions`, and the view `counted_reactions` | ✓ |
| Per-server settings | linkstats_images, whether image and video posts are counted | |
| Config section | `linkstats.emoji_copy_min_uses`, 1 by default; 0 turns copying off | ✓ |
| Environment | LATIBOT_DEBUG_RECOMPUTE_BOT_ID, Debug builds only: whose replacements a recompute reads | |
| Libraries | OpenSSL's crypto, for the hash that tells two emoji images apart | |
| Capabilities | offers none, uses none | |
| Requires | links | |

## Files

| File | |
|---|---|
| `include/linkstats/module.hpp` | `make_module` and `config_defaults`, which the bot's module list calls |
| `src/module.cpp` | the module: its schema, command, panels, listeners and the copying timer |
| `src/linkstats_config.hpp` | its config section |
| `src/reactions.hpp`, `.cpp` | the reaction store and the boards' queries |
| `src/emote_reactions.hpp`, `.cpp` | emotes sent as messages, counted as reactions |
| `src/media_posts.hpp`, `.cpp` | people's image and video posts |
| `src/backfill.hpp`, `.cpp` | `/linkstats recompute`, reading old messages back |
| `src/legacy_replacements.hpp`, `.cpp` | recognising the Java bot's old replacements |
| `src/emoji_copies.hpp`, `.cpp` | the bot's own copies of emojis |
| `src/linkstats_command.hpp`, `.cpp` | `/linkstats`, its boards and panels |
| `tests/` | `latibot_linkstats_tests` |
