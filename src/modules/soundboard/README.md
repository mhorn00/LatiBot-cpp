# The soundboard module

Sends a soundboard sound as a voice message, which plays inline in the chat
rather than arriving as a file. Any sound the bot can see can be picked:
Discord's own, and every server's it is in. How it behaves, and why, is
[docs/Soundboard.md](docs/Soundboard.md); the command's replies are in
[the user guide](../../../docs/User_Guide.md#soundboard).

Built unless `LATIBOT_WITH_SOUNDBOARD` is off. It needs no voice channel, so
it does not require voice.

## What it owns

`tests/soundboard_module_test.cpp` checks the rows marked ✓ against what the
module registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/soundboard` | ✓ |
| Panels | none | ✓ |
| Message stages | none | ✓ |
| Discord events | `soundboard: Discord's own sounds` (connecting), `soundboard: a server's sounds` (servers arriving), `soundboard: servers left` | ✓ |
| Timers | `the soundboard refresh`, every 30 minutes | ✓ |
| Tables | none | ✓ |
| Model tools | `play_sound`, the language model's /soundboard: in voice when the bot is in a voice channel there, otherwise a voice message; whoever asked needs Use Soundboard; when the llm module is built | ✓ |
| Per-server settings | none | |
| Config section | `soundboard.ffmpeg_path` | ✓ |
| Environment | none | |
| Programs | ffmpeg, without which `/soundboard` says so | |
| Permissions | Use Soundboard, for playing a sound in voice; warned about per server when missing | |
| Capabilities | offers none, uses none | |
| Requires | the core | |

## Files

| File | |
|---|---|
| `include/soundboard/module.hpp` | `make_module` and `config_defaults`, which the bot's module list calls |
| `src/module.cpp` | the module: finding ffmpeg, the command, and reading each server's sounds |
| `src/soundboard_config.hpp` | its config section |
| `src/sounds.hpp`, `.cpp` | the sounds, reading them from Discord, and finding and suggesting them |
| `src/clips.hpp`, `.cpp` | fetching a sound and making it a voice message with ffmpeg |
| `src/soundboard_command.hpp`, `.cpp` | `/soundboard` |
| `tests/` | `latibot_soundboard_tests` |
