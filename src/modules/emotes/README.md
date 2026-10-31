# The emotes module

Adds the custom emotes and stickers in somebody's message to a server in
`trusted_guilds`, from a message's Apps menu. How it behaves, and why, is
[docs/Steal_Emote.md](docs/Steal_Emote.md); the replies are in
[the user guide](../../../docs/User_Guide.md#steal-this-emote-apps-menu).

Built unless `LATIBOT_WITH_EMOTES` is off. It stores nothing, so leaving it out
leaves nothing behind.

## What it owns

`tests/emotes_module_test.cpp` checks the rows marked ✓ against what the
module registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/Steal this emote`, a message's Apps menu command | ✓ |
| Panels | `stealpage`, `stealpick`, `stealname`, `stealform`, `stealadd` | ✓ |
| Message stages | none | ✓ |
| Discord events | none | ✓ |
| Timers | none | ✓ |
| Tables | none | ✓ |
| Model tools | none | |
| Per-server settings | none | |
| Config section | none | ✓ |
| Permissions | Create or Manage Expressions, in each server it adds to; asked of nobody at startup, since a server without it simply isn't offered | |
| Core config | reads `trusted_guilds`: the only servers it adds to | |
| Environment | none | |
| Capabilities | offers none, uses none | |
| Requires | the core | |

## Files

| File | |
|---|---|
| `include/emotes/module.hpp` | `make_module`, which the bot's module list calls |
| `src/module.cpp` | the module: its command and its panel |
| `src/steal_command.hpp`, `.cpp` | reading emotes and stickers out of a message, the panel, adding them, and the command |
| `tests/` | `latibot_emotes_tests` |
