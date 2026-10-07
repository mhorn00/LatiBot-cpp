# The nicknames module

Keeps every nickname a member has had in each server, who changed it when
that can be known, and lets moderators set one with the bot. How it behaves,
and why, is [docs/Nicknames.md](docs/Nicknames.md);
the commands' replies are in [the user guide](../../../docs/User_Guide.md#nicknames).

Built unless `LATIBOT_WITH_NICKNAMES` is off. Leaving it out leaves its table
and its rows where they are, for when it is built again. The language model's
aliases read names from DPP's member cache, which only fills with this
module's intent: without it the model has fewer names to hide, and nothing
breaks.

## What it owns

`tests/nicknames_module_test.cpp` checks the rows marked ✓ against what the
module registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/nickname`, `/nicknames` | ✓ |
| Panels | `nicks`, the pages of a history | ✓ |
| Message stages | none | ✓ |
| Discord events | `nicknames: member updates`, `nicknames: audit log entries`, `nicknames: changes made while offline` (servers connecting), `nicknames: the Server Members intent refused` (DPP's log); only while tracking | ✓ |
| Timers | `the audit log fallback`, once, 10 seconds after a change no audit entry has named | ✓ |
| Tables | `nickname_history` | ✓ |
| Per-server settings | none | |
| Config section | `nicknames.track_changes`, on by default; before sections it was track_nicknames, which still works | ✓ |
| Intents | Server Members, only while tracking | |
| Permissions | View Audit Log, for naming who made a change, only while tracking | |
| Environment | none | |
| Files | `nicknames.json` beside the database, the Java bot's history, imported at startup | |
| Capabilities | offers none, uses none | |
| Requires | the core | |

## Files

| File | |
|---|---|
| `include/nicknames/module.hpp` | `make_module` and `config_defaults`, which the bot's module list calls |
| `src/module.cpp` | the module: its schema, commands, panel, listeners and the audit fallback |
| `src/nicknames_config.hpp` | its config section |
| `src/nicknames.hpp`, `.cpp` | the history store, and deciding what is a change and who made it |
| `src/nickname_import.hpp`, `.cpp` | reading the Java bot's `nicknames.json` |
| `src/nickname_command.hpp`, `.cpp` | `/nickname` and `/nicknames`, and the history's pages |
| `tests/` | `latibot_nicknames_tests` |
