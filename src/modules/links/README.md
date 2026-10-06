# The links module

Replaces links to sites whose previews Discord does not show with mirrors
that do, watches for the preview, and offers a Retry when none came. How it
behaves, and why, is [docs/features/Url_Replacement.md](../../../docs/features/Url_Replacement.md);
the commands' replies are in [the user guide](../../../docs/features/README.md#links).

Built unless `LATIBOT_WITH_LINKS` is off; linkstats requires it. Leaving it
out leaves its tables and their rows where they are, for when it is built
again.

## What it owns

`tests/links_module_test.cpp` checks the rows marked ✓ against what the
module registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/links` (was /urlrepl), `/urltoggle` | ✓ |
| Panels | `urllist`, `urlpanel`, `urlpick`, `urledit`, `urldel`, `urlyes`, `urladd`, `urlform`, `urlswitch`, and `urlretry`, the Retry on a replacement | ✓ |
| Message stages | `url replacement`, at the rewrite position: before the trigger replies | ✓ |
| Discord events | `links: the Java bot's rules, and replacements left unsettled` (servers connecting), `links: previews arriving` (message updates), `links: replacements deleted` | ✓ |
| Timers | `the preview tracker's tick`, every second | ✓ |
| Tables | `url_rules`, `url_opt_outs`, `known_mirrors`, `replacement_messages`, `replacement_links` | ✓ |
| Per-server settings | url_rules_imported, set once the Java bot's rules were imported | |
| Config section | none | ✓ |
| Permissions | Embed Links, for the replacements' previews; Manage Messages, to turn off the original's | |
| Environment | none | |
| Files | UrlReplacements.txt beside the database, the Java bot's rules, imported once per server | |
| Capabilities | offers none, uses none | |
| Requires | the core | |

## Public headers

Linkstats counts reactions on replacements and finds old ones by their
rules, so these are in `include/links/`:

| Header | |
|---|---|
| `links/module.hpp` | `make_module`, and `schema()`, which linkstats' tests need |
| `links/url_rules.hpp` | the rules, and their store |
| `links/replacements.hpp` | the replacements, and their store |

## Files

| File | |
|---|---|
| `src/module.cpp` | the module: its schema, commands, panels, stage, listeners and timer; Retry; the Java import; settling what the last run left |
| `src/url_replacer.hpp`, `.cpp` | the stage, and posting a replacement |
| `src/embed_watch.hpp`, `.cpp` | watching for previews, and trying the next mirror |
| `src/links_command.hpp`, `.cpp` | `/links` and `/urltoggle`, and the rules panel |
| `tests/` | `latibot_links_tests` |
