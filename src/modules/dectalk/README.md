# The dectalk module

Speaks in voice channels with DECtalk: `/speak` once, `/tts` for everything
a person writes, `/chat` for a channel's messages, and custom voices made in
the voice lab. It also offers the speech the language model speaks through.
How it behaves, and why, is [docs/Speech.md](docs/Speech.md);
the commands' replies are in [the user guide](../../../docs/User_Guide.md#tts).

Built unless `LATIBOT_WITH_DECTALK` is off; it requires voice. DECtalk itself
(`third_party/dectalk`, `cmake/dectalk.cmake`) is linked by this module only.
Leaving it out leaves `tts_voices` and its rows where they are.

## What it owns

`tests/dectalk_module_test.cpp` checks the rows marked ✓ against what the
module registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/speak`, `/tts` (with voices lab, list, delete), `/chat` | ✓ |
| Panels | `vlabedit`, `vlabbase`, `vlabtest`, `vlabsave`, `vlabreset`, `vlabform`, `vlabname`, `vlabopen`, the voice lab | ✓ |
| Message stages | none | ✓ |
| Discord events | none of its own: voice tells it when a connection is ready, a marker is reached, or the bot left | ✓ |
| Timers | none | ✓ |
| Tables | `tts_voices` | ✓ |
| Per-server settings | tts_max_characters and tts_max_seconds, the speech limits (`src/speak_command.hpp`) | |
| Config section | none; the trusted servers and users of config.json decide who may use DECtalk's host commands | ✓ |
| Capabilities | offers speech (core/capabilities/speech.hpp), which llm uses | |
| Requires | voice | |

## Files

| File | |
|---|---|
| `include/dectalk/module.hpp` | `make_module` |
| `src/module.cpp` | the module: its schema, the queue and the speech it offers, commands and the lab's panel |
| `src/dectalk_engine.*`, `src/tts_engine.hpp` | DECtalk, behind the text-to-speech port |
| `src/dectalk_sanitizer.*` | what DECtalk may be told, and by whom |
| `src/speech_queue.*` | what is waiting to be said, per server |
| `src/dectalk_speech.*` | the speech capability |
| `src/voice_params.*`, `src/voice_store.*`, `src/voice_lab.*` | custom voices, and the lab that makes them |
| `src/wav.*` | the WAV files `[:play]` reads |
| `src/speak_command.*`, `src/chat_command.*` | `/speak`, `/tts` and `/chat` |
| `tests/` | `latibot_dectalk_tests`; `mock_tts.hpp` stands in for the engine |
