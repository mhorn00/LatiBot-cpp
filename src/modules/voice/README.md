# The voice module

Joins and leaves voice channels, keeps a server's voice session, leaves a
channel nobody else is in, and mixes what is played into one stream per
connection. How it behaves, and why, is
[docs/Voice_Channels.md](docs/Voice_Channels.md).

It has no switch of its own: it is built when dectalk or music is, which
require it (docs/modules/Module_Plan_Final.md D2).

## What it owns

`tests/voice_module_test.cpp` checks the rows marked ✓ against what the
module registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/join`, `/leave`, `/voice` (start, stop, grace) | ✓ |
| Panels | none | ✓ |
| Message stages | none | ✓ |
| Discord events | `voice: connections ready`, `voice: markers reached`, `voice: voice states` | ✓ |
| Timers | `feeding the mixer`, every second; `the voice auto-leave check`, every 5 seconds | ✓ |
| Tables | none | ✓ |
| Per-server settings | voice_grace_seconds, how long the bot stays alone before leaving | |
| Config section | none | ✓ |
| Capabilities | offers voice::services to the modules that require it; uses none | |
| Requires | the core | |

## What it offers dectalk and music

`include/voice/services.hpp`: the mixer and the sessions, and hooks run when
the bot leaves a channel, when a connection is ready and when a marker is
reached, in the order the modules are built. Leaving runs the session's end,
then the hooks (speech, then music), then the mixer (K9).

| Public header | |
|---|---|
| `voice/module.hpp` | `make_module` |
| `voice/services.hpp` | what it offers, and `required` to find it |
| `voice/voice_mixer.hpp`, `voice/voice_output.hpp`, `voice/pcm.hpp` | the mixer, the port it writes through, and PCM |
| `voice/voice_sessions.hpp` | sessions and the auto-leave |
| `voice/voice_state.hpp` | where the bot and people are in voice, from DPP's cache |

## Files

| File | |
|---|---|
| `src/module.cpp` | the module: its commands, listeners and timers, and leaving |
| `src/join_command.hpp`, `.cpp` | `/join` and `/leave` |
| `src/voice_command.hpp`, `.cpp` | `/voice start`, `stop`, `grace` |
| `src/dpp_voice_output.hpp`, `.cpp` | the voice output port, over DPP |
| `tests/` | `latibot_voice_tests` |
