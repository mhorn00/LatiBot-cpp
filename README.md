# LatiBot

C++ port of a Discord bot (originally written in Java), built with CMake + MSVC
as C++23. It uses [DPP (D++)](https://dpp.dev/) for the Discord API, built from
source as a git submodule, and Conan 2 for the remaining dependencies.

The `java-reference/` folder holds the original Java source purely for
reference during the port; it is not part of the C++ build.

## Setup

On a Windows machine with nothing installed, Git first, then the clone, then
one script that does the rest:

```powershell
winget install Git.Git        # skip if you have Git; open a new terminal after
git clone --recursive https://github.com/mhorn00/LatiBot-cpp.git
cd LatiBot-cpp
powershell -ExecutionPolicy Bypass -File tools\DevEnvSetup.ps1
```

[tools/DevEnvSetup.ps1](tools/DevEnvSetup.ps1) does steps 1 to 5 below, up
to configuring (building too with `-Build`), and checks each thing before
doing it, so anything already there is left alone.
It installs CMake, PowerShell 7 and Conan with winget, and installs the
Visual Studio 2026 Build Tools, or adds whatever components an existing
install lacks. It checks out the submodules and creates a Conan profile if
there is none. A profile naming a compiler that is not installed is pointed
at VS 2026. Finally it installs the dependencies for Debug, Release and
clang-tidy, configures `build\`, and copies `.env.example` to `.env`.

- **Timing and prompts.** On a fresh machine the Build Tools download is
  several GB, and the dependencies build from source for about twenty
  minutes. Each installer asks for administrator rights. Run again, the
  script takes seconds.
- **`-ExecutionPolicy Bypass`.** Windows' built-in PowerShell blocks scripts
  by default, which is why the script is written to run there as well as in
  PowerShell 7: installing 7 is one of its jobs.

| Option | |
|---|---|
| `-CheckOnly` | report what is missing, change nothing; exits 1 if anything is |
| `-Build` | also build Debug and run the tests, the proof it all works (about ten more minutes the first time) |
| `-ResetBuild` | delete `build\` if it was configured for another compiler, rather than stopping to ask |
| `-SkipTidy` | leave out the dependency install clang-tidy needs |

The steps below are what it does, for reference or to do by hand.

## Prerequisites

| Tool | Version used | Notes |
|---|---|---|
| Windows | 10 (1903+) / 11 | x64. 1903+ is needed for C++20 `<chrono>` time zones |
| [winget](https://learn.microsoft.com/windows/package-manager/winget/) | any recent | used to install MSVC Build Tools |
| MSVC Build Tools | VS 2026 (v145 toolset, MSVC 14.51) | C++ compiler, installed via winget below. Matches what GitHub's `windows-latest` runner ships, so CI and local builds use the same compiler. VS 2022 also works: no preset pins a Visual Studio version |
| [Git](https://git-scm.com/) | any recent | needed for the submodules (and by DPP's CMake) |
| [CMake](https://cmake.org/) | >= 3.21 | required for the `TARGET_RUNTIME_DLLS` generator expression |
| [Conan](https://conan.io/) | 2.x | dependency manager |
| [PowerShell](https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-windows) | 7.x (`pwsh`) | runs the scripts in `tools/` and VS Code's tasks. Windows' built-in PowerShell 5.1 is not enough: the scripts use .NET APIs it does not have |

If any are missing:

```powershell
winget install Git.Git
winget install Kitware.CMake
winget install Microsoft.PowerShell
winget install --id Conan.Conan   # or, with Python installed: pip install conan
```

Open a new terminal afterwards, so the tools are on `PATH`.

The Build Tools install in step 2 already includes AddressSanitizer, Ninja and
clang-tidy/clang-format. One more is optional:

| Tool | Install | Used by |
|---|---|---|
| OpenCppCoverage | `winget install OpenCppCoverage.OpenCppCoverage` | local coverage reports |

## 1. Clone with submodules

DPP and DECtalk live in `third_party/` as git submodules:

```powershell
git clone --recursive <repo-url>
# or, in an existing clone:
git submodule update --init --recursive
```

## 2. Install MSVC Build Tools

```powershell
winget install --id Microsoft.VisualStudio.BuildTools --source winget `
  --accept-package-agreements --accept-source-agreements `
  --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --add Microsoft.VisualStudio.Component.VC.ASAN --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset"
```

This installs the C++ build tools (compiler, linker, Windows SDK, CMake,
Ninja) without the Visual Studio IDE, plus AddressSanitizer for the `asan` and
`fuzz` presets and clang-tidy/clang-format for linting.

**`Microsoft.VisualStudio.Component.VC.Tools.x86.x64` has to be listed
explicitly.** In VS 2026 the VCTools *workload* alone no longer pulls in the
x64 compiler component. Without it, `cl.exe` is on disk but the instance is
not registered as having a C++ compiler, and CMake fails with
`could not find any instance of Visual Studio`.

Verify afterwards:

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -all -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
```

This should print the BuildTools install path. If you add components later,
run the installer **elevated**: `--passive` and `--quiet` refuse to prompt for
elevation and exit with code 5007 instead.

## 3. Set up the Conan profile

Once per machine, if you have never used Conan on it:

```powershell
conan profile detect
```

This writes `~/.conan2/profiles/default` from the compiler it finds. If one is
already there it stops with `ERROR: Profile ... already exists`, which is
fine: keep the one you have. It should say `compiler=msvc` and
`compiler.version=195`, the v145 toolset that ships with VS 2026 (`194` is
VS 2022 17.13 or newer, which works too). It will also say
`compiler.cppstd=14`: leave that, since the install commands below ask for
C++20 themselves, which is what the dependencies are built as and what CI
does. The bot itself is C++23, which `CMakeLists.txt` sets.

**The profile, not the newest Visual Studio installed, decides which compiler
builds everything**, dependencies and bot alike: Conan's toolchain sets the
toolset. A profile made while only VS 2022 was installed keeps building with
VS 2022 after VS 2026 is added. To move to the compiler CI uses, run
`conan profile detect --force`, which replaces the profile (or change only
its `compiler.version` line), delete `build\`, then steps 4 and 5 again. A
build folder remembers its toolset, and configuring it for another fails with
`generator toolset ... does not match the toolset used previously`.
The first install after that builds the dependencies from source once more.

The default `conancenter` remote is all that's needed.

## 4. Install dependencies

DPP is a package built from the `third_party/DPP` submodule
([conan/dpp](conan/dpp/conanfile.py)), so tell Conan about it first. Then
install both configurations; they go side by side into `build\conan`, which
every build folder shares:

```powershell
conan export conan/dpp
conan install . --build=missing -s build_type=Release -s compiler.cppstd=20 --lockfile-partial
conan install . --build=missing -s build_type=Debug -s compiler.cppstd=20 --lockfile-partial
```

This provides DPP, built once per configuration and kept in Conan's cache,
with its OpenSSL, zlib and opus; SQLite with FTS5; CTRE; and Catch2 for the
tests. `conan.lock` pins every one of them, and their recipes' revisions,
except DPP, which the submodule pins; `--lockfile-partial` is what lets DPP
through. A recipe's revision depends on its files' line endings, so locking
DPP would fail on any checkout whose line endings differ. `-s compiler.cppstd=20` is required: CTRE
refuses anything below C++17, and without it the profile's `14` applies.

ConanCenter has no prebuilt binaries for the VS 2026 compiler yet, so the
first time, every dependency is built from source: about ten minutes for each
command. After that they come from the cache in `~/.conan2/p/`, and running
the commands again takes seconds.

## 5. Configure and build

The presets build with Ninja, which needs the Visual Studio developer
environment: run these in a **Developer PowerShell for VS**, or after
`build\conan\conanbuild.bat` in a `cmd` prompt. VS Code's CMake Tools sets the
environment up by itself (step 6).

```powershell
cmake --workflow --preset debug     # configure, build, then test Debug
cmake --workflow --preset release   # the same for Release
```

Or one step at a time: `cmake --preset default`, `cmake --build --preset
debug`, `ctest --preset debug`. DPP was built by step 4, so this compiles
only LatiBot and DECtalk, and later builds only recompile what changed.

Every build folder is under `build\`. The default preset builds in
`build\build`, and binaries land in `build\build\bin\Debug\` and
`build\build\bin\Release\`, with `dectalk.dll` and DECtalk's dictionary
beside `LatiBot.exe`, so it runs without extra `PATH` setup.

Every test should pass.

### Leaving modules out

Each feature module can be left out of the build, with
`-DLATIBOT_WITH_<NAME>=OFF` when configuring: `DECTALK`, `MUSIC`, `LLM`,
`TRIGGERS`, `NICKNAMES`, `MIDNIGHT`, `LINKS` and `LINKSTATS`. A module left
out takes its commands, tables and config section with it, and the modules
that require it: no links, no linkstats. Voice has no switch: it is built
when dectalk or music is. A database keeps a left-out module's tables, so
switching it back on finds its data where it was.

```powershell
cmake --preset default -DLATIBOT_WITH_MUSIC=OFF -DLATIBOT_WITH_LLM=OFF
```

What each module owns is in its README, under `src/modules/<name>/`, and the
design in [docs/modules/Module_Plan_Final.md](docs/modules/Module_Plan_Final.md).

## 6. In VS Code

Install the recommended extensions when prompted (`.vscode/extensions.json`),
then **select a configure preset once**: the CMake status bar, or
`CMake: Select Configure Preset` → **default**.

That selection is what makes IntelliSense work. CMake Tools supplies cpptools
the include paths, defines and compiler for every file, and it only does so
after it has configured, so before the first selection every `#include` is
underlined in red while the command line builds perfectly.
`.vscode/c_cpp_properties.json` has a fallback for that gap, but it cannot
tell which build of a Conan package this project links, so it is a stopgap
rather than the answer.

## Running

### The Discord application

The bot needs an application of its own in the
[Discord Developer Portal](https://discord.com/developers/applications):

1. **New Application**, then under **Bot**, **Reset Token** and keep the
   token for the next step. It is shown once.
2. Still under **Bot**, turn on both **Privileged Gateway Intents**:
   Message Content and Server Members, described below.
3. Under **OAuth2 → URL Generator**, tick the scopes `bot` and
   `applications.commands`, then the bot permissions: View Channels, Send
   Messages, Send Messages in Threads, Read Message History, Embed Links,
   Attach Files, Add Reactions, Use External Emojis, Manage Messages, Manage
   Nicknames, View Audit Log, Connect and Speak. Open the generated URL to
   add the bot to a server.

A permission left out only disables what needs it: the startup log names
anything missing, per server, and what it is for.

The bot needs two **privileged intents**, both enabled for the application at
*Discord Developer Portal → your app → Bot → Privileged Gateway Intents*:

- **Message Content.** Without it Discord delivers guild messages with an empty
  `content`, and everything that reads a message — the goodbye phrase, the
  triggers — goes quiet while the slash commands keep working.
- **Server Members.** Without it no nickname change is ever seen. Set
  `"track_changes": false` in the `nicknames` section of `config.json` to
  turn nickname tracking off and stop the bot asking for this one.

An intent the application was not granted is not a warning: Discord refuses the
gateway outright and the bot reconnects in a loop. The log says which toggle to
go and find when that happens.

### Starting it

Secrets come from the environment only ([why](src/core/docs/Operations.md#4-configuration)): `DISCORD_BOT_TOKEN`,
and optionally `ANTHROPIC_API_KEY` / `OPENAI_API_KEY` for the language model.
Without either key the model never answers, and the log says so at startup.
Either set them in the shell:

```powershell
$env:DISCORD_BOT_TOKEN = "your-token-here"
.\build\build\bin\Release\LatiBot.exe
```

or copy [.env.example](.env.example) to `.env` (git-ignored) and fill it in —
`LatiBot.exe` loads it on startup if present, without overriding a variable
the shell already set. Run it from the repo root so it finds both `.env` and
`config.json`, which the first run writes if it is missing (see
[Configuration](#configuration)).

In VS Code, **F5** does it all: `.vscode/launch.json` builds the executable,
installs it to `out\LatiBot-Debug\` or `out\LatiBot-Release\` (see
[On another machine](#on-another-machine)), and runs it from there, so each
folder needs its own `.env` and `config.json`, and keeps its own `data\`.
Stop the bot before launching the same configuration again, since installing
cannot replace a running executable.

### On another machine

A Release build runs anywhere with these three files, kept together in one
folder:

| File | |
|---|---|
| `LatiBot.exe` | the bot, with DPP, OpenSSL, zlib and opus linked into it |
| `dectalk.dll` | the speech engine |
| `dtalk_us.dic` | DECtalk's dictionary, which has to stay beside `dectalk.dll` |

`cmake --install build\build --config Release`, after a Release build, writes
them to `out\LatiBot-Release\` with `Install-Dependencies.ps1` and its readme: the
folder to copy to a server (the VS Code task *Install the bot (Release)* does
both). Running the bot from there, rather than from `build\`, also means a
build never fights the running executable. Put `config.json`, `.env` and
`data\` beside it, since the bot reads them from where it runs.

For music, add [yt-dlp](https://github.com/yt-dlp/yt-dlp) and
[ffmpeg](https://ffmpeg.org/) as `yt-dlp.exe` and `ffmpeg.exe` in the same
folder, or anywhere on `PATH` (`winget install yt-dlp.yt-dlp Gyan.FFmpeg`
puts both there). Without them everything else works, and `/music play`
says what is missing. YouTube also needs [Deno](https://deno.com/) 2.3 or
newer, beside the bot or on `PATH`, which yt-dlp solves YouTube's
JavaScript challenges with; without it the bot warns, and some videos fail.
Keep yt-dlp current: sites change, and an old one stops working with them.

**`Install-Dependencies.ps1`**, which the build puts beside `LatiBot.exe`,
does all of this on a server: yt-dlp, ffmpeg and Deno beside the bot,
bgutil's PO token provider, the Visual C++ Redistributable below, and
Firefox with a profile for the bot to sign in to YouTube with, so
age-restricted videos play. Run it again to update.
[Install-Dependencies.md](deploy/Install-Dependencies.md) explains it, and
how to sign the bot in.

The machine also needs the **Microsoft Visual C++ Redistributable** (x64), for
`MSVCP140.dll` and `VCRUNTIME140.dll`. Without it Windows refuses to start the
bot and names a missing DLL. It has to be at least as new as the compiler, so
install the latest:

```powershell
winget install Microsoft.VCRedist.2015+.x64
```

Then, in that folder: create `.env` with the line `DISCORD_BOT_TOKEN=<your
token>` (or set the variable), and run `LatiBot.exe`. The first run writes
`config.json` with the defaults, and `data/` for the database, its backups and
the certificate bundle, all beside where it was run from. Without a token it
stops, saying exactly where `.env` goes.

### Configuration

Settings for the whole bot go in `config.json`, in the directory the bot is
run from; everything per server is set with commands instead, and kept in the
database. When there is no `config.json`, the bot **writes one** with the
defaults and runs on them, so a fresh install has a file to edit; change it
and restart. It is git-ignored, since it holds
real Discord IDs. [config.example.json](config.example.json) is the same file,
for reading here, and a test keeps the two identical. The bot never writes
over a file that is there, and one it cannot read stops startup. A folder it
cannot write to is only a warning, and it runs on the defaults.

Every key is listed at its default except `log_level`, which is left out so
each build keeps its own; add it to set one. Keys can be removed too, since a
missing key is its default. A key the bot does not know, or a value of the
wrong type, stops startup with a message naming it, so a typo is never
silently ignored. JSON has no comments, so the table below is the
documentation.

Each feature's keys are in an object of their own, its **section**:
`"llm": {"model": ...}`. Below, `llm.model` means the key `model` in the
`llm` section. A file from before sections, with `llm_model` and the like at
the top, still works: each old key is read where it now belongs, and the log
says where to move it. Setting a key both ways stops startup. A section for a
module this build leaves out is ignored, with a warning. To use the trusted commands, including [`/logs`](docs/User_Guide.md#logs),
put your own user ID in `trusted_users`:

```json
"trusted_users": ["123456789012345678"]
```

| Key | Type | Default | What it does |
|---|---|---|---|
| `log_level` | text | `debug` in Debug, `info` in Release | how much is logged; see [Logging](#logging) |
| `database_path` | text | `data/bot.db` | the SQLite database; its folder is created if missing, and `nicknames.json` and `UrlReplacements.txt` from the Java bot are looked for beside it |
| `backup_directory` | text | `data/backups` | where database backups are written |
| `backups_to_keep` | whole number | `7` | how many backups to keep, oldest removed first; `0` turns backups off |
| `backup_interval_minutes` | whole number | `360` | how often a backup is taken; must be positive |
| `trusted_guilds` | list of IDs, as text | none | servers whose administrators may use DECtalk's host commands: `[:play]`, `[:log]`, `[:debug]`, `[:loadv]`, `[:setv]` |
| `trusted_users` | list of IDs, as text | none | users who may use those commands in any server, and the only ones who may choose the log channel (`/logs`) |
| `nicknames.track_changes` | true or false | `true` | watch for nickname changes, which needs the Server Members intent (above); was `track_nicknames` |
| `linkstats.emoji_copy_min_uses` | whole number | `1` | how many reactions an emote needs before the bot keeps its own copy of it, so link stats can still show it after its server deletes it; `0` turns copying off, and raising it deletes the copies that no longer qualify ([how](src/modules/linkstats/docs/Link_Stats.md#10-the-bots-own-copies-of-emojis)) |
| `llm.provider` | text | `anthropic` | `anthropic` or `openai`: whose model `llm.model` is; was `llm_provider` |
| `llm.model` | text | `claude-haiku-4-5` | the model a server uses until `/llm model` picks another; must be one the bot [knows the price of](docs/User_Guide.md#llm); was `llm_model` |
| `llm.check_model` | text | `claude-haiku-4-5` | the model that decides, in [conversation mode](docs/User_Guide.md#conversation-mode), whether a message is for the bot; it only answers yes or no, so the cheapest will do; must be one the bot knows the price of |
| `llm.spend_cap_daily_usd` | number | `2.0` | the language model stops answering, everywhere, once this much was spent in a UTC day |
| `llm.spend_cap_monthly_usd` | number | `20.0` | and once this much was spent in a UTC month |
| `llm.tool_rounds` | whole number | `4` | how many rounds of tools (its memory) the model may use in one reply; must be at least 1; was `llm_tool_rounds` |
| `music.ytdlp_path` | text | empty | where `yt-dlp.exe` is, for [music](src/modules/music/docs/Music.md); empty looks beside the bot, then on `PATH` |
| `music.ffmpeg_path` | text | empty | where `ffmpeg.exe` is, likewise |
| `music.deno_path` | text | empty | where `deno.exe` is, which yt-dlp needs for YouTube; likewise |
| `music.pot_provider_path` | text | empty | bgutil's PO token provider's `server` folder, which the bot runs ([Music.md §4.10](src/modules/music/docs/Music.md#410-po-tokens)); empty looks for `bgutil-ytdlp-pot-provider\server` beside the bot |
| `music.pot_provider_port` | number | `4416` | the port the provider listens on, on this machine only; 1 to 65535 |

IDs are written as strings, `["123456789012345678"]`, because a JSON number
cannot hold a Discord ID exactly; a number, or text that is not exactly an ID,
stops startup.

### Testing with a second bot account

`/linkstats recompute` finds old replacements by who posted them: the bot's own
account. A test bot running beside the production one has posted none, so
there is nothing for it to find. In a **Debug build**, point it at the
production account instead:

```powershell
$env:LATIBOT_DEBUG_RECOMPUTE_BOT_ID = "the production bot's user ID"
.\build\build\bin\Debug\LatiBot.exe
```

or put the same line in `.env`. Startup logs a warning while it is set. Run
`/linkstats recompute start` with `fresh:true` for any channels already
recomputed without it, since those are otherwise remembered as done. Only the
recompute is affected: the test bot still posts and tracks its own
replacements as itself. A Release build never reads the variable, and says so
if it is set.

A test bot in the same server as the production one also leaves its slash
commands beside the real ones, so every command shows twice. To remove them
when done testing:

```powershell
.\build\build\bin\Debug\LatiBot.exe --unregister-commands
```

from the folder holding its `.env`, or run the **Unregister the bot's
commands (Debug)** task, which installs the bot and runs it from
`out\LatiBot-Debug\`. It signs in as
whichever bot `DISCORD_BOT_TOKEN` belongs to, logs its name, deletes its global
commands and any a server has of its own, and exits. It never comes online,
and nothing else of the bot runs. Its next ordinary start registers them
again. It also takes a config file first, like an ordinary start:
`LatiBot.exe other.json --unregister-commands`.

### Logging

Lines go to **stderr**, one per message, timestamped first so they stay
greppable and sort chronologically:

```
2026-09-23T18:01:34Z [info] latios (42) ran /trigger add pattern="420" in guild 999
```

The level is whichever of these is set, last one winning:

| | Level | |
|---|---|---|
| Build default | `debug` in a Debug build, `info` in Release | nothing to configure |
| `config.json` | `"log_level": "debug"` | `trace`, `debug`, `info`, `warn`, `error`, `off` |
| Environment | `LATIBOT_LOG_LEVEL=debug` | also works from `.env`; overrides the file |

`LATIBOT_LOG_LEVEL` is the one to reach for while the bot is running badly,
since it needs no file edit and applies before the configuration is even read.

**What each level is for.** `info` is the running record: every command with
who ran it and what they passed, every reply the bot posts by itself, startup,
shutdown, schema changes and guilds joined. `debug` adds why — which stage
wanted what, which trigger matched and why it stayed quiet, what a panel button
decoded to, how long a command took. `trace` adds the content of every message
the bot sees, and DPP's own gateway chatter.

To keep the output to one file:

```powershell
.\build\build\bin\Release\LatiBot.exe 2> latibot.log
```

**A log channel.** `/logs set` posts the log in one Discord channel as well,
from a level of its own: a console at `info` and a channel at `debug` each get
their share. There is one for the whole bot, kept across restarts, and only
`trusted_users` can set it, since it covers every server. The token and API
keys are masked in anything posted.
[The user guide](docs/User_Guide.md#logs) has the details.

**Colour.** In a terminal, arguments are coloured by their type — numbers,
`true` and `false`, Discord ids, durations — along with the timestamp, the
level, and the `[dpp]` tag on lines forwarded from DPP. The text around them
stays the terminal's own colour. Callers do nothing for this: the logger sees
each argument's type and picks the colour itself, which is why ids are logged
as the snowflake rather than `id.str()`.

| `LATIBOT_LOG_COLOR` | |
|---|---|
| `auto` (default) | colour when stderr is a terminal, so a redirected log stays plain |
| `never` | no colour; `off`, `false` and `0` work too |
| `always` | colour even into a pipe or a file |

The widely used `NO_COLOR` convention is honoured as well, and an explicit
`LATIBOT_LOG_COLOR=always` overrides it. Both work from `.env`.

The colours themselves live in code: `palette` in
[src/core/include/core/util/log.hpp](src/core/include/core/util/log.hpp) says what each one is, and
`log_style` in the same file says which type gets which. Colouring another
type is one specialisation of `log_style`.

### What it does so far

Phases 1 to 5 are done: the framework, the features that keep records, URL
replacement with its reaction statistics, DECtalk speech, and the language
model. Music followed, through yt-dlp and ffmpeg.
[The user guide](docs/User_Guide.md) documents all of this properly —
options, replies and edge cases — and each feature has a
[spec](docs/User_Guide.md#feature-specs) of its own: what it is for, how
it is built, and what was decided and why.

| Command | What it does |
|---|---|
| `/ping` | round trip and gateway latency |
| `/say` | post as the bot, optionally as a reply |
| `/status` | set the bot's presence |
| `/join`, `/leave` | voice channel, following you or a named user |
| `/speak` | say something in the voice channel, in any of DECtalk's voices or a custom one |
| `/tts` | `stop`, `skip`, `limits` — speech, and how long it may run |
| `/voice` | `start`, `stop`, `grace`, `lab`, `list`, `delete` — voice sessions and custom voices |
| `/chat` | say something as a voice message |
| `/music`, `/m` | play music from a link, with a queue; speech pauses it |
| `/shutdown` | stop the bot |
| `/goodbye` | show, change or turn off the phrase that stops the bot |
| `/logs` | `set`, `level`, `off`, `show` — post the bot's log in one channel |
| `/trigger` | `add`, `edit`, `remove`, `list`, `panel` — automatic replies |
| `/bots` | `allow`, `deny`, `list` — which other bots the bot may hear |
| `/nickname` | change somebody's nickname, and record who did it |
| `/nicknames` | every nickname somebody has had here, paginated |
| `/midnight` | `list`, `add`, `edit`, `remove`, `toggle` — a message at midnight |
| `/links` | `enable`, `disable`, `list`, `set`, `remove`, `test`, `panel` — which links get a working preview |
| `/urltoggle` | have your own links left alone, or not |
| `/linkstats` | `top`, `user`, `reactions`, `duplicates`, `alias`, `recompute`, `images` — reactions on replaced links, and on images where counted |
| `/llm` | `status`, `on`, `off`, `model`, `settings`, and groups for the `personality`, `system` and `style` documents, advanced `trigger`s and the `blacklist` — the language model |
| `/memory` | `list`, `forget`, `clear` — what the language model remembers |

Without being asked: an administrator saying the goodbye phrase stops the bot,
trigger patterns get weighted replies with a per-channel cooldown, links to
sites with poor previews are posted again on a mirror that previews properly,
reactions on those are counted, every nickname change is recorded with
whoever made it, each midnight message posts once per local day, the database
backs itself up on a schedule, the bot leaves a voice channel once everyone
else has, and anything the bot lacks permission to do is
reported per server at startup as a warning rather than an error.

URL replacement is off in every server until someone with Manage Server runs
`/links enable` there, and the bot remembers the choice across restarts. It
watches for Discord to actually build the preview rather than
guessing from a timer, tries each mirror twice before moving on, and when none
works leaves a **Retry** button instead of deleting its message. Every link in a
message is handled, spoilers stay spoilered, and each of the Java bot's bugs
here has a test written against it.

Other bots are ignored unless `/bots allow` says otherwise, and even then a
trigger only answers one if it was added with `bots:true`. Hearing and answering
are separate on purpose: the first is a server-wide decision, the second belongs
to each trigger.

Nickname attribution is the part Discord's own audit log gets wrong: it records
the bot for anything the bot did. The history records the person instead, and
says **unknown** rather than guessing when nobody can be named. Dropping the
Java bot's `nicknames.json` into `data/` imports years of history, timezones
and all; its `UrlReplacements.txt` in the same place becomes each server's URL
rules, once, ready for `/links enable`. `/linkstats recompute` then reads years of channel history back
into the reaction statistics.

The language model is off in every server until someone with Manage Server
runs `/llm on` there, and needs an API key in the environment. Once on, it
answers anyone who @mentions it, replies to it, or starts a message with its
name, reading the recent conversation first; advanced triggers let it speak up
unprompted about a phrase; in a voice session's channel it also says the reply
out loud. It remembers what it chooses to across restarts, and its personality
is a document anyone can edit, with every version kept. It switches itself off
at $2 in a day or $20 in a month, worked out from the tokens it actually used.

**Still to come**, unscheduled — [the plan](docs/porting/Porting_Plan_Final.md), and
[what each feature should do](docs/Planned.md): emote statistics
and appearance tracking.

## Testing

Unit tests use [Catch2 v3](https://github.com/catchorg/Catch2) and run through
CTest:

```powershell
ctest --preset debug      # or: ctest --test-dir build\build -C Debug --output-on-failure
```

In VS Code, the TestMate C++ extension puts the whole suite under one node in
the **Testing** sidebar, grouped by component tag and then by source file, and
rebuilds the Debug tests before running them.

Tests live in `tests/` and in each module's `tests/`. The bot's code is in
static libraries, `latibot_core` and one per module, so `LatiBot.exe` and the
test executables (`latibot_core_tests`, `latibot_<module>_tests`,
`latibot_app_tests`) link the same code.
Each test carries one component tag (the core's `[db]`, `[config]`,
`[commands]`, `[events]`, `[ui]`, `[module]`, `[discord]`, `[ports]`, `[log]`,
`[util]`; `[app]`; or its module's, such as `[llm]` or `[links]`) plus
optional traits (`[coro]`, `[threads]`, `[fs]`, `[golden]`), which select
subsets:

```powershell
.\build\build\bin\Debug\latibot_core_tests.exe "[db]"           # one component
.\build\build\bin\Debug\latibot_core_tests.exe "[config]~[fs]"  # config, minus file I/O
```

`[live]` tests need a test bot token in `LATIBOT_TEST_TOKEN` and are excluded
from the test presets.

### Build presets

Every preset builds with Ninja Multi-Config, in a folder under `build\`, from
the dependencies in `build\conan` (step 4), and in the Visual Studio
environment (step 5). A workflow preset configures, builds and tests in one
command.

| Configure preset | Folder | What it's for | Workflow |
|---|---|---|---|
| `default` | `build\build` | the normal Debug/Release build, and the `compile_commands.json` clang-tidy reads | `debug`, `release` |
| `asan` | `build\build-asan` | our targets with `/fsanitize=address` (needs the ASan component) | `asan` |
| `fuzz` | `build\build-fuzz` | libFuzzer targets in `tests/fuzz` (needs the ASan component) | none: build, then run by hand |
| `core-only` | `build\build-core` | every module off: the core must build and pass on its own, as CI checks | `core-only` |

```powershell
# AddressSanitizer
cmake --workflow --preset asan

# Fuzzing (runs until stopped; -max_total_time=60 for a short run). New inputs
# go in the first folder, seeds come from the second; also fuzz_text and
# fuzz_legacy_parser.
cmake --preset fuzz; cmake --build --preset fuzz
New-Item -ItemType Directory -Force build\build-fuzz\corpus\fuzz_url_scan
.\build\build-fuzz\bin\Debug\fuzz_url_scan.exe build\build-fuzz\corpus\fuzz_url_scan tests\fuzz\corpus\fuzz_url_scan -max_total_time=60

# Every module, none, and each one left out in turn, in build\build-matrix.
# Run it before changing the core's public headers or a capability.
pwsh tools/Test-ModuleMatrix.ps1
pwsh tools/Test-ModuleMatrix.ps1 -Only llm      # just "everything but llm"

# clang-tidy and clang-format, through the scripts in tools/
pwsh tools/Invoke-ClangTidy.ps1                  # src/
pwsh tools/Invoke-ClangTidy.ps1 -IncludeTests    # src/ and tests/
pwsh tools/Invoke-ClangFormat.ps1 -Check         # report, change nothing
```

The clang-tidy script configures the default preset and reads its
`compile_commands.json`, Debug entries only, so it needs nothing beyond
step 4.

### VS Code tasks

`.vscode/tasks.json` wraps the common ones, so they are available from
**Run Task** with clickable output: build and test per configuration, the
AddressSanitizer run, clang-tidy over `src/` or a single file, clang-format,
the Conan install, a short fuzz run, installing the bot to
`out\LatiBot-Debug\` or `out\LatiBot-Release\`, and unregistering a test bot's commands. They call the same commands as above;
nothing is exclusive to the editor.

## Project layout

```
CMakeLists.txt      top-level build definition
CMakePresets.json   default / asan / fuzz / core-only presets, and their workflows
conanfile.py        Conan recipe: dpp, openssl, sqlite3, ctre, catch2
conan/dpp/          the recipe that builds DPP from the third_party/DPP submodule
config.example.json what the bot writes as config.json on its first run
.env.example        the environment variables, to copy to .env
cmake/              warnings, sanitizers, shared helpers, and DECtalk's build
tools/              dev environment setup, the module matrix, clang-tidy and clang-format wrappers
deploy/             Install-Dependencies.ps1, which sets a server up, and its readme; built beside LatiBot.exe
.github/workflows/  CI: build and test Debug, Release, AddressSanitizer and the core alone, and a secret scan
.vscode/            tasks, launch configurations, IntelliSense and the grouped test tree
src/app/main.cpp    entry point; LatiBot.exe also links the module list CMake writes
src/core/           the core, built as the latibot_core static library, with its specs in docs/
src/modules/        the feature modules, each a library of its own with its README, tests and spec
tests/              Catch2 tests, mocks, support, golden files and fuzz targets
docs/User_Guide.md  what the bot does, command by command; docs/Planned.md, what it will do
docs/modules/       the module plan (Module_Plan_Final.md) and its drafts
docs/porting/       the porting plan (Porting_Plan_Final.md) and its drafts, kept as the record
docs/ideas/         parked ideas
third_party/DPP     submodule: DPP v10.1.6, built from source
third_party/dectalk submodule: DECtalk (develop branch), built by cmake/dectalk.cmake
build/              build output (git-ignored)
data/               runtime database, backups and import files (git-ignored)
```

Comments in the code cite a feature's [spec](docs/User_Guide.md#feature-specs)
by file and section, as "Url_Replacement.md §2.4". The comments
inside the migrations' SQL still say "plan v4 9.7": shipped migrations are
never edited, and those mean
[Porting_Plan_Final.md](docs/porting/Porting_Plan_Final.md), whose drafts are
kept for history and numbered differently in places.

The original Java bot lives in `java-reference/` locally. It is deliberately
**not** tracked in git: it is large and contains personal data.

## Notes / gotchas

- **Why DPP is built from source:** it enables C++20 coroutines, and it avoids
  the outdated OpenSSL 1.1.1 / zlib 1.2.11 binaries DPP bundles for Windows.
  It is a Conan package, `conan/dpp`, built from the `third_party/DPP`
  submodule once per configuration and kept in Conan's cache, so build
  folders and CI reuse it instead of compiling it again. The package's
  `CMakeLists.txt` sets `CONAN_EXPORTED=ON`, so DPP uses the Conan-provided
  OpenSSL, zlib and opus.
- **DPP is linked statically**, into `LatiBot.exe` and the tests, so there
  is no `dpp.dll` to copy or forget (`dpp/*:shared` in `conanfile.py`). DPP's
  own build warns `Building of static library not supported on non UNIX
  systems`; it builds and links cleanly all the same, as `dppstatic.lib`, with
  `DPP_STATIC` defined on both sides so its headers declare no
  `__declspec(dllimport)`. If a DPP upgrade breaks that, `"dpp/*:shared":
  True` goes back to the DLL.
- **Voice support is forced on** (`HAVE_OPUS_OPUS_H`, `OPUS_LIBRARIES`), in
  `conan/dpp/CMakeLists.txt`. DPP only auto-detects opus on Windows when it
  uses its bundled binaries. The package build's log (`conan create
  conan/dpp`) should include `VOICE support will be enabled`.
- **DECtalk is built from the untouched submodule** by `cmake/dectalk.cmake`,
  as `dectalk.dll` plus the `dtalk_us.dic` dictionary it compiles beside it.
  Every DECtalk source is compiled with `cmake/dectalk_zeroed_heap.h`
  force-included, which zeroes its allocations: DECtalk reads heap memory it
  never wrote, and without that a Release build does not say the same thing
  twice. Each utterance runs on a fresh engine, since DECtalk's own reset
  leaves a memory engine silent. The plan's §21.16 and §21.17 have the detail.
- **Conan's OpenSSL ships no CA bundle.** Its `OPENSSLDIR` is an empty
  directory in the package cache, so every TLS handshake fails verification,
  which DPP reports as `Malformed HTTP response` — the request never gets far
  enough to have a status. `src/core/src/util/ca_certificates.cpp` fixes this at
  startup by exporting the Windows root store to `data/ca-bundle.pem` and
  setting `SSL_CERT_FILE`, which is what OpenSSL's default verify paths read.
  The roots therefore stay whatever Windows Update says they are, and nothing
  has to be vendored or installed alongside. Set `SSL_CERT_FILE` yourself to
  override it.
- **`WITH_OPENSSL3` on the `hpke` target**, in `conan/dpp/CMakeLists.txt`:
  DPP hardcodes `OPENSSL_VERSION` to 1.1.1f on Windows, which makes its
  `mlspp` dependency pick an OpenSSL 1.1 code path that doesn't compile
  against OpenSSL 3. Recheck this when upgrading DPP.
- **"CMAKE_CXX_STANDARD ... modified to 17" warnings** in the DPP package's
  build are expected. DPP sets 17 in its own scope but compiles the `dpp`
  target as C++20.
- **Three linker warnings in a clean build are expected**, none of them ours:
  `LNK4017` from `dectalk.def`'s `DESCRIPTION` line, in each configuration,
  and `LNK4075` from DPP's own link settings in Debug.
- **C++23 is `/std:c++23preview` until MSVC Build Tools 14.52**, then
  `/std:c++23`. `CMakeLists.txt` picks the switch by compiler version, since
  CMake and Conan would otherwise ask for `/std:c++latest`, which adds C++26
  draft features that change with every compiler update. Only our own code is
  C++23: DPP stays C++20, and the Conan packages are installed as C++20.
- **`CMAKE_CONFIGURATION_TYPES` is limited to `Debug;Release`.** Without that,
  the multi-config generator also expects `MinSizeRel`/`RelWithDebInfo`, which
  Conan hasn't installed.
- **Conan no longer writes `CMakeUserPresets.json`.** Our own
  `CMakePresets.json` is checked in and points at the toolchain in
  `build\conan`. An old `CMakeUserPresets.json` that includes
  `build/generators` is left from before; delete it (`DevEnvSetup.ps1` offers
  to).
- **DPP's headers are system headers**, as every Conan package's are
  (`/external:I` with `/external:W0`). Our code builds with `/W4 /WX`, and
  DPP's public headers produce C4251/C4100 warnings that we can't fix from
  here.
- **DPP's headers are precompiled** for `latibot_core` and the tests, which
  takes a clean build of our code from about 124 s to 72 s. clang-tidy
  analyses without it, so a file that forgets a DPP include still fails
  there.
- **Warnings are errors by default** for our targets
  (`-DLATIBOT_WARNINGS_AS_ERRORS=OFF` turns that off while experimenting).
- **No preset names a Visual Studio version.** The compiler is whichever the
  Conan profile names: `build\conan\conanbuild.bat` loads that Visual Studio's
  environment, and CMake Tools finds the installed one. Pinning a version is
  what broke the first CI run: GitHub's Windows image moved from VS 2022 to
  VS 2026.
- **The `asan` preset disables the STL's container annotations**
  (`_DISABLE_STL_ANNOTATION`). Conan's Catch2 is not instrumented, and mixing
  annotated and unannotated objects fails to link with `LNK2038`. The cost is
  overflow detection *inside* std containers; everything else ASan checks
  still works. It also copies `clang_rt.asan_dynamic-x86_64.dll` next to the
  binaries, since MSVC links that runtime dynamically and it is not on `PATH`.
- **clang-tidy must be Clang 20 or newer** for MSVC 14.51's headers; older
  ones stop at `error STL1000: Unexpected compiler version`. The copy in the
  VS 2026 install (LLVM 22) is new enough; the one in VS 2022 is not, so use
  the 2026 path:
  `& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools\VC\Tools\Llvm\x64\bin\clang-tidy.exe"`.
- **`IMPORTED_LOCATION not set for imported target "CONAN_LIB::…_RELEASE"
  configuration "Debug"`**, dozens of times, is what configuring a
  multi-config build printed before `latibot_map_conan_configs()` in
  `cmake/helpers.cmake`. CMake 4.4, answering the codemodel query VS Code's
  CMake Tools leaves in `build/build/.cmake/api/v1/query/`, asks each of
  Conan's per-configuration
  libraries where it lives in the *other* configuration. The helper points
  each at its own, which answers that without changing what is built. Any
  new `find_package` of a Conan package needs the helper called after it, in
  the same directory; if these errors come back, that is the likely cause.
- **To upgrade DPP:** `git -C third_party/DPP fetch --depth 1 origin tag vX.Y.Z`,
  check out that tag, set `version` in `conan/dpp/conanfile.py` to match, then
  `conan export conan/dpp` and the two `conan install` commands of step 4,
  which build the new DPP once. Commit the submodule and the recipe together.
- **To add or change a dependency:** edit `requirements()` in
  `conanfile.py`, then write the lockfile again, without DPP:
  `conan lock create . -s build_type=Debug -s compiler.cppstd=20 --lockfile-out=conan.lock`,
  the same with `-s build_type=Release --lockfile=conan.lock`, then
  `conan lock remove --requires="dpp/*" --lockfile=conan.lock --lockfile-out=conan.lock`.
  Commit `conan.lock` with the change, then run both `conan install`
  commands and reconfigure.
