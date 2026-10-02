# Running the bot

What it takes to run the bot, and what it does before and around its
features: the command line, `config.json`, secrets and `.env`, TLS
certificates, the Discord intents, the database and its backups, and the
warnings about missing permissions.

This is the spec for those: what each is for, how it behaves, how it is
built, and what was decided and why. The root README has the setup steps,
the [configuration table](../../README.md#configuration) with every
`config.json` key, and [Logging](../../README.md#logging).
[Execution_Flow.md §1–§2](../architecture/Execution_Flow.md#1-from-main-to-shutdown)
follow startup, and [Classes.md §3](../architecture/Classes.md#3-the-database-and-the-stores)
and [§13](../architecture/Classes.md#13-configuration-and-logging) draw the
classes.

| | |
|---|---|
| **Code** | `src/main.cpp`, `src/core/config/{bootstrap,command_line,guild_settings}.*`, `src/core/util/{env,ca_certificates,log}.*`, `src/core/db/*`, `src/core/commands/preflight.*`; intents and timers in `src/core/bot.cpp` |
| **Tests** | `tests/unit/{bootstrap,command_line,env,ca_certificates,preflight,log}_test.cpp`, `tests/db/{database,migrations,backup,guild_settings}_test.cpp` |
| **Tables** | `guild_settings` (migration 1), and `PRAGMA user_version` for the schema's version |
| **Plan** | Replaces plan §5.1, §5.2, §7, §21.1–§21.3 and §21.7 |
| **Status** | Built in phase 0 and 1; `config.json` written when missing since 2026-09-27; the command line since 2026-09-30 |

## Contents

1. [Intent](#1-intent)
2. [Starting up](#2-starting-up)
3. [Discord intents](#3-discord-intents)
4. [Configuration](#4-configuration)
5. [The database](#5-the-database)
6. [Permission warnings](#6-permission-warnings)
7. [Decisions](#7-decisions)
8. [Limits](#8-limits)

## 1. Intent

The Java bot kept its state in flat files beside the working directory,
checked a fixed permission list against **one hard-coded server** and
exited if anything was missing, and set a fixed status on every start. The
port aims for a bot that:

- runs from a folder of four files plus a token;
- **fails loudly at startup** on a mistake an operator made (a typo in
  `config.json`, a missing token, an intent not granted);
- never exits over something a server did, such as a missing permission.

The repository is public, so no secret or real Discord id is ever in a
tracked file.

## 2. Starting up

In `main`, in order:

1. **The command line**, `LatiBot [config.json] [--unregister-commands]`,
   parsed by `config::command_line::parse`. The path and the flag may come
   in either order. An unknown option, or a second path, stops startup with
   the usage line rather than being read as a file name.
2. **`.env`**, from the working directory, if present. It never overrides a
   variable the environment already set, so CI and containers are
   unaffected.
3. **Log colours**, and **`LATIBOT_LOG_LEVEL`**, before the configuration is
   read, so reading it is logged at the level asked for.
4. **`config.json`**: read, or written with the defaults if missing (§4).
5. **Certificates** (below), before anything opens a connection.
6. **Secrets** from the environment. No `DISCORD_BOT_TOKEN` stops startup,
   saying where `.env` goes.
7. With `--unregister-commands`: delete the bot's commands and exit
   ([Commands_and_Panels.md §2.4](Commands_and_Panels.md#24-unregistering---unregister-commands)).
8. Otherwise build the `bot` and run it.

Anything thrown is logged as `fatal: …`, and the process exits 1.

**Certificates.** Conan's OpenSSL has an empty `OPENSSLDIR`, so every TLS
handshake fails verification. DPP reports that as
`Malformed HTTP response`, because the request never gets far enough to
have a status. `util::use_system_certificates` exports the Windows root
store to `data/ca-bundle.pem` and sets `SSL_CERT_FILE`, which OpenSSL reads
for its default paths. The roots stay whatever Windows Update says, and an
explicitly set `SSL_CERT_FILE` still wins.

**Timers.** Once running, the bot's work on a clock is cluster timers:

- the embed tracker, every second;
- feeding music to each voice connection playing it, every second
  ([Music.md §4.2](Music.md#42-the-mixer-and-why-pause_audio-cannot-do-this));
- the log channel, every 2 s;
- auto-leave, every 5 s;
- midnight, every 30 s;
- emoji copies, every minute;
- backups.

Each runs `guarded`: a throw is logged, and the timer keeps running.

## 3. Discord intents

Two privileged intents must be enabled for the application under *Bot →
Privileged Gateway Intents* in the developer portal:

| Intent | Without it |
|---|---|
| **Message Content** | Every guild message arrives with empty text. Slash commands keep working while the goodbye phrase, triggers, URL replacement and the model silently do nothing. DPP's startup warning is the only clue |
| **Server Members** | No nickname change is ever seen. Asked for only while `track_nicknames` is on |

**A privileged intent not granted is refused, not degraded.** Discord closes
the gateway with **4014** and the bot reconnects in a loop, which DPP logs
as `OOF! Error from underlying websocket: 4014`. The bot watches for 4014,
and logs the setting and the portal page by name. `track_nicknames: false`
is the way out if the Server Members toggle cannot be turned on.

## 4. Configuration

Three layers, separated by how often each changes and who changes it:

| Layer | Where | Changed by |
|---|---|---|
| **The bot as a whole** | `config.json` in the working directory, read once at startup | whoever runs it, then a restart |
| **Secrets** | the environment, optionally from `.env` | whoever runs it |
| **Each server** | `guild_settings` and the feature tables in the database | commands and panels, at run time |

**`config.json`.**

- **Missing:** written with the defaults, and the bot runs on them, since a
  release is only the executable. A folder that cannot be written to is a
  warning, and the bot runs on the defaults.
- **Rejected at startup, naming the problem:** an unknown key, a value of
  the wrong type, an unreadable file, a `llm_model` whose price the bot does
  not know, and an id that is a number or not exactly an id. Ids are
  **strings**, because a JSON number cannot hold a snowflake exactly.
- **Never overwritten**, and git-ignored, since it holds real ids.
  `config.example.json` is the same text, kept identical by a test.

The README's [configuration table](../../README.md#configuration) is the
reference for every key.

**Other programs.** Music runs yt-dlp and ffmpeg, and yt-dlp uses Deno for
YouTube ([Music.md §5](Music.md#5-dependencies-and-running-it)). Each is
looked for once at startup: at `ytdlp_path`, `ffmpeg_path` or `deno_path` in
`config.json` if set, else beside `LatiBot.exe`, else on `PATH`. Without
yt-dlp or ffmpeg the bot starts, warns, and `/music play` says what is
missing; without Deno it warns, and music plays what it can. Their versions
are logged, asked on a thread of its own so startup does not wait. The bot
also runs bgutil's PO token provider, when it is set up, for as long as it
runs ([Music.md §4.10](Music.md#410-po-tokens)).
`deploy/Install-Dependencies.ps1`, which the build copies beside
`LatiBot.exe`, installs all of them on a server.

**Secrets.** Secrets come from the environment only:

| Variable | Needed |
|---|---|
| `DISCORD_BOT_TOKEN` | always |
| `ANTHROPIC_API_KEY` | for Claude models |
| `OPENAI_API_KEY` | for GPT models |
| `LATIBOT_YTDLP_FIREFOX_PROFILE` | to sign music in to YouTube, for age-restricted videos: the folder of a Firefox profile kept for the bot ([Music.md §4.9](Music.md#49-signing-in-to-youtube)) |
| `LATIBOT_YTDLP_COOKIES` | the same, from a `cookies.txt` instead; the profile wins when both are set |

`.env.example` lists them. They are masked in anything the
[log channel](Log_Channel.md) posts. `LATIBOT_YTDLP_FIREFOX_PROFILE` and
`LATIBOT_YTDLP_COOKIES` name a folder or a file rather than holding a
secret: the startup log gives the path and how many cookies it holds, never
their values. `LATIBOT_LOG_LEVEL`,
`LATIBOT_LOG_COLOR` and `NO_COLOR` shape the log. In a Debug build,
`LATIBOT_DEBUG_RECOMPUTE_BOT_ID` points link stats' recompute at another
account's replacements; a Release build ignores it, and says so.

**Per-server settings.** `config::guild_settings` is a `(guild_id, key,
value)` table with typed getters that fall back to a caller-supplied
default. A value that cannot be parsed falls back rather than throwing,
since one hand-edited row should not take a feature down. Server id 0
(`config::bot_wide`) holds what belongs to the bot rather than a server:
the saved `/status` and the log channel.

## 5. The database

SQLite through our own thin wrapper (`db::database`, `statement`,
`transaction`):

- **one connection, behind a mutex**, since DPP calls in from many threads;
- `journal_mode=WAL`, `synchronous=NORMAL`, `foreign_keys=ON`, and a 5 s
  busy timeout;
- snowflakes as `INTEGER` (exact at 64 bits), and times as Unix seconds,
  UTC, bound and read directly by `statement`;
- FTS5 compiled in, for the model's memory.

**Migrations** run at startup, each inside a transaction, numbered by
`PRAGMA user_version`. A failing one rolls back and keeps the previous
version. They are **append-only**: a shipped migration is never edited,
since every install has already applied it. A migration that alters a
populated table has a test that migrates to the version before, inserts
rows, upgrades, and checks the rows survived.

| # | Name | Adds | Spec |
|---|---|---|---|
| 1 | `guild_settings` | per-server settings | this one |
| 2 | `triggers` | `triggers`, `trigger_responses` | [Triggers.md](Triggers.md) |
| 3 | `bot_allowlist` | `allowed_bots`; `triggers.respond_to_bots` | [Message_Pipeline.md](Message_Pipeline.md) |
| 4 | `nickname_history` | `nickname_history` | [Nicknames.md](Nicknames.md) |
| 5 | `midnight_messages` | `midnight_messages` | [Midnight.md](Midnight.md) |
| 6 | `url_replacement` | `url_rules`, `url_opt_outs`, `known_mirrors`, `replacement_messages`, `replacement_links` | [Url_Replacement.md](Url_Replacement.md) |
| 7 | `reaction_stats` | `reactions`, `reaction_log`, `emojis`, `emoji_aliases` | [Link_Stats.md](Link_Stats.md) |
| 8 | `backfill_progress` | `backfill_progress` | [Link_Stats.md](Link_Stats.md) |
| 9 | `message_flags` | `message_flags` on triggers and midnight messages | [Commands_and_Panels.md](Commands_and_Panels.md#3-message-flags) |
| 10 | `tts_voices` | `tts_voices` | [Speech.md](Speech.md) |
| 11 | `llm` | `llm_usage`, `llm_documents`, `llm_memory`, `llm_memory_search`, `llm_blacklist`, `llm_triggers` | [Language_Model.md](Language_Model.md) |
| 12 | `media_posts` | `replacement_messages.kind` | [Link_Stats.md](Link_Stats.md) |
| 13 | `emoji_copies` | `emoji_images`, `emoji_copies` | [Link_Stats.md](Link_Stats.md) |
| 14 | `emote_reactions` | `emote_reactions`, the view `counted_reactions` | [Link_Stats.md](Link_Stats.md) |
| 15 | `llm_aliases` | `llm_aliases` | [Language_Model.md](Language_Model.md#38-who-the-model-is-told-about) |

**Backups** use SQLite's online backup API, so a consistent copy is taken
while the bot runs, even during an open write transaction. Every
`backup_interval_minutes` (360) a cluster timer writes
`backup_directory/bot-YYYYMMDD-HHMMSS.db` and keeps the newest
`backups_to_keep` (7). A failure is logged and never stops the bot. Either
setting at 0 turns backups off, and the startup log says so.

**Importers.** The Java bot's `nicknames.json` and `UrlReplacements.txt` are
read from beside the database when present. See
[Nicknames.md §4](Nicknames.md#4-importing-the-java-history) and
[Url_Replacement.md §2.7](Url_Replacement.md#27-rules-from-the-java-bot).

## 6. Permission warnings

When the bot connects to a server, both at startup and when it joins one
later, it checks what it may do there against what its features need, and
logs a **warning** for anything missing. It never exits, and never disables
itself. `on_guild_create` fires for every server the bot is in when the
gateway connects, so one handler covers startup and later joins.

- **What is needed** is every command's `required_bot_permissions`, plus
  the passive features', each with what it is for:

  | Permission | For |
  |---|---|
  | View Channel, Send Messages | replying to messages |
  | View Audit Log | naming who changed a nickname |
  | Embed Links | showing link previews in URL replacements |
  | Manage Messages | turning off the original preview when a link is replaced |

- **Only what is missing** is reported, by name, such as
  `missing Manage Messages for turning off the original preview when a link
  is replaced`. Unknown bits are shown in hex rather than dropped.
- **Administrator** satisfies everything, as Discord treats it, so a server
  that granted it is never warned.
- Nothing is posted to Discord: this is the bot's own log, or the
  [log channel](Log_Channel.md).

Embed Links matters more than it looks. A replacement posted without it
shows no preview, which looks exactly like a broken mirror.

## 7. Decisions

| Date | Decision | Why |
|---|---|---|
| plan v4 | `config.json` for the bot, the environment for secrets, the database per server | Each changes at a different rate, edited by different people |
| plan v4 | Unknown keys rejected, not ignored | A typo is reported instead of silently doing nothing |
| plan v4 | Ids as strings in JSON | A JSON number cannot hold a snowflake exactly |
| plan v4 | Our own SQLite wrapper; one connection behind a mutex; WAL | Thread safety is a real difference from Java, where shared maps got away with it |
| plan v4 | Append-only migrations in a transaction, by `user_version` | Every install has applied what shipped |
| plan v4 | An unparseable per-server value falls back to its default | One hand-edited row should not take a feature down |
| plan v4 | Online backups on a timer, rotated | A copy can be taken while the bot runs |
| plan v4 | Permission warnings per server, never fatal; Administrator short-circuits | The Java bot exited over one server's missing permission |
| 2026-09-23 | `.env` beside the bot, never overriding the environment | Exporting three variables into every shell was tedious; the rule is unchanged (plan §21.1) |
| 2026-09-23 | Request the message content intent | Without it every message is empty (plan §21.2) |
| 2026-09-23 | Export the Windows root store for OpenSSL | Conan's OpenSSL ships no certificates, and DPP is not ours to patch (plan §21.3) |
| 2026-09-23 | Ask for Server Members only with `track_nicknames`; explain 4014 by name | A privileged intent not granted is refused, not degraded (plan §21.7) |
| 2026-09-25 | Timers are guarded, with a catch per item | One database error stopped midnight posts or replacement tracking until a restart (cleanup ERR-001) |
| 2026-09-25 | A trusted id that is not exactly an id stops startup | Malformed ids were accepted (cleanup BUG-002) |
| 2026-09-25 | `db::statement` binds snowflakes and times; `db/` may use `dpp::snowflake` (cleanup decision 5) | Every store converted by hand |
| 2026-09-27 | A missing `config.json` is written with the defaults; `config.example.json` kept identical by a test | A release is only the executable |
| 2026-09-30 | A command line: a config path and `--unregister-commands`; anything else refused | A mistyped option must not be read as a config file |
| 2026-09-30 | yt-dlp and ffmpeg are optional: missing, they cost music and nothing else | A bot that will not start over a music tool would be worse than one without music |

## 8. Limits

- `config.json` is read once. Changing it needs a restart.
- Backups go to the same disk as the database, by default.
- The bot shell (`bot`) is not unit-tested, by design: testing it would mean
  mocking `dpp::cluster`, which is what the ports exist to avoid. What that
  leaves untested is listed in [docs/testing/](../testing/README.md#known-gaps),
  and each spec names its own.
