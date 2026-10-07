# The midnight module

Posts a message shortly after midnight: once per local day, per entry, in
the entry's own timezone and channel. How it behaves, and why, is
[docs/Midnight.md](docs/Midnight.md); the
commands' replies are in [the user guide](../../../docs/User_Guide.md#midnight).

Built unless `LATIBOT_WITH_MIDNIGHT` is off. Leaving it out leaves its table
and its rows where they are, for when it is built again.

## What it owns

`tests/midnight_module_test.cpp` checks the rows marked ✓ against what the
module registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/midnight` | ✓ |
| Panels | none | ✓ |
| Message stages | none | ✓ |
| Discord events | none | ✓ |
| Timers | `the midnight tick`, every 30 seconds | ✓ |
| Tables | `midnight_messages` | ✓ |
| Per-server settings | none | |
| Config section | none | ✓ |
| Environment | none | |
| Capabilities | offers none, uses none | |
| Requires | the core | |

## Files

| File | |
|---|---|
| `include/midnight/module.hpp` | `make_module`, which the bot's module list calls |
| `src/module.cpp` | the module: its schema, its command and its timer |
| `src/midnight.hpp`, `.cpp` | the entries, their store, and the scheduler that decides what to post |
| `src/midnight_command.hpp`, `.cpp` | `/midnight list`, `add`, `edit`, `remove`, `toggle` |
| `tests/` | `latibot_midnight_tests` |
