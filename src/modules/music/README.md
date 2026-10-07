# The music module

Plays music in voice channels: a link, or a search, through yt-dlp and
ffmpeg, queued per server, with a page of what is playing. How it behaves,
and why, is [docs/Music.md](docs/Music.md); the
commands' replies are in [the user guide](../../../docs/User_Guide.md#music).

Built unless `LATIBOT_WITH_MUSIC` is off; it requires voice, whose mixer it
plays through.

## What it owns

`tests/music_module_test.cpp` checks the rows marked ✓ against what the
module registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/music`, and its short form `/m` | ✓ |
| Panels | `musicq`, the queue's pages | ✓ |
| Message stages | none | ✓ |
| Discord events | none of its own: voice tells it when the bot left, which takes the queue with it | ✓ |
| Timers | none of its own: voice's mixer reads from the player every second | ✓ |
| Tables | none | ✓ |
| Per-server settings | music_volume, music_track_limit_minutes (`src/music_command.hpp`) | |
| Config section | `music.ytdlp_path`, `music.ffmpeg_path`, `music.deno_path`, `music.pot_provider_path`, `music.pot_provider_port`; before sections each was at the top, which still works | ✓ |
| Environment | LATIBOT_YTDLP_FIREFOX_PROFILE or LATIBOT_YTDLP_COOKIES, the account yt-dlp signs in as when it must; never logged | |
| Programs | yt-dlp and ffmpeg, without which it says so and plays nothing; Deno and bgutil's PO token provider, which it runs, for YouTube | |
| Libraries | ws2_32, to refuse a link into a private network | |
| Capabilities | offers none, uses none | |
| Requires | voice | |

## Files

| File | |
|---|---|
| `include/music/module.hpp` | `make_module` and `config_defaults`, which the bot's module list calls |
| `src/module.cpp` | the module: finding the programs, the player, the PO token provider, the command and the queue's panel |
| `src/music_config.hpp` | its config section, and the sign-in it reads from the environment |
| `src/music_player.*`, `src/music_queue.*` | playing, per server, and the queue |
| `src/yt_dlp.*`, `src/media.hpp` | yt-dlp and ffmpeg, behind the media port |
| `src/cookies.*`, `src/pot_provider.*`, `src/links.*` | signing in, PO tokens, and which links are refused |
| `src/process.*` | running a program and reading what it says (was `util/process`) |
| `src/music_command.*` | `/music` and its queue page |
| `tests/` | `latibot_music_tests`; `mock_media.hpp` stands in for yt-dlp |
