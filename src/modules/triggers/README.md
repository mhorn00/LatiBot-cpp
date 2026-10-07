# The triggers module

Replies to messages that match a pattern, with one of a trigger's responses,
at most once per cooldown in each channel. How it behaves, and why, is
[docs/Triggers.md](docs/Triggers.md); the
commands' replies are in [the user guide](../../../docs/User_Guide.md#triggers).

Built unless `LATIBOT_WITH_TRIGGERS` is off. Leaving it out leaves its tables
and their rows where they are, for when it is built again. The language
model's advanced triggers match the same way, through `util/match` in the
core, and do not need this module.

## What it owns

`tests/triggers_module_test.cpp` checks the rows marked ✓ against what the
module registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/trigger` | ✓ |
| Panels | `triggers` (the list's pages), `trigpanel`, `trigpick`, `trigedit`, `trigdel`, `trigyes`, `trigadd`, `trigform`, `trigonoff`, `trigbots`, `trigsilent`, `trigprev` | ✓ |
| Message stages | `triggers`, at the reply position: after link replacement, before the language model, which a reply here keeps quiet | ✓ |
| Discord events | none | ✓ |
| Timers | none | ✓ |
| Tables | `triggers`, `trigger_responses` | ✓ |
| Per-server settings | none | |
| Config section | none | ✓ |
| Environment | none | |
| Capabilities | offers none, uses none | |
| Requires | the core | |

## Files

| File | |
|---|---|
| `include/triggers/module.hpp` | `make_module`, which the bot's module list calls |
| `src/module.cpp` | the module: its schema, command, panel and stage |
| `src/triggers.hpp`, `.cpp` | the store, and the stage that picks a reply |
| `src/trigger_command.hpp`, `.cpp` | `/trigger`, and its panel |
| `tests/` | `latibot_triggers_tests` |
