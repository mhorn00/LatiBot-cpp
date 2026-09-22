# Test catalog

**Generated** by ``tools/Update-TestCatalog.ps1``. Do not edit by hand;
re-run the script after adding or retagging tests.

See [README.md](README.md) for the strategy, conventions and tag meanings.

80 test cases across 7 components, including 33 sections.

| Component | Test cases | Sections |
|---|---:|---:|
| [db](#db) | 21 | 3 |
| [config](#config) | 17 | 14 |
| [commands](#commands) | 19 | 15 |
| [discord](#discord) | 5 | 0 |
| [ports](#ports) | 7 | 0 |
| [log](#log) | 6 | 0 |
| [util](#util) | 5 | 1 |

## db

Database (`src/core/db`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| a backup is a complete, valid copy | `fs` |  | [tests/db/backup_test.cpp:48](../../tests/db/backup_test.cpp#L48) |
| backing up an in-memory database writes it to disk | `fs` |  | [tests/db/backup_test.cpp:62](../../tests/db/backup_test.cpp#L62) |
| an existing backup file is replaced | `fs` |  | [tests/db/backup_test.cpp:74](../../tests/db/backup_test.cpp#L74) |
| a backup taken while other threads write is consistent | `fs`, `threads` |  | [tests/db/backup_test.cpp:92](../../tests/db/backup_test.cpp#L92) |
| rotation keeps the newest backups | `fs` |  | [tests/db/backup_test.cpp:124](../../tests/db/backup_test.cpp#L124) |
| rotation ignores unrelated files | `fs` |  | [tests/db/backup_test.cpp:147](../../tests/db/backup_test.cpp#L147) |
| backup file names carry a sortable UTC timestamp | `fs` |  | [tests/db/backup_test.cpp:169](../../tests/db/backup_test.cpp#L169) |
| opening an unwritable path reports the SQLite error |  |  | [tests/db/database_test.cpp:41](../../tests/db/database_test.cpp#L41) |
| values survive a bind and get round trip |  |  | [tests/db/database_test.cpp:46](../../tests/db/database_test.cpp#L46) |
| optional values bind as NULL or as the value |  |  | [tests/db/database_test.cpp:74](../../tests/db/database_test.cpp#L74) |
| a constraint violation throws with the SQLite code |  |  | [tests/db/database_test.cpp:87](../../tests/db/database_test.cpp#L87) |
| malformed SQL is reported, not executed |  |  | [tests/db/database_test.cpp:103](../../tests/db/database_test.cpp#L103) |
| a transaction commits or rolls back |  | 3 | [tests/db/database_test.cpp:110](../../tests/db/database_test.cpp#L110) |
| last_insert_rowid and changes report the previous statement |  |  | [tests/db/database_test.cpp:147](../../tests/db/database_test.cpp#L147) |
| concurrent writers are serialized by the connection lock | `threads` |  | [tests/db/database_test.cpp:161](../../tests/db/database_test.cpp#L161) |
| a fresh database migrates to the current schema |  |  | [tests/db/migrations_test.cpp:36](../../tests/db/migrations_test.cpp#L36) |
| migrating twice is a no-op |  |  | [tests/db/migrations_test.cpp:49](../../tests/db/migrations_test.cpp#L49) |
| only migrations newer than user_version are applied |  |  | [tests/db/migrations_test.cpp:60](../../tests/db/migrations_test.cpp#L60) |
| a failing migration rolls back and keeps the previous version |  |  | [tests/db/migrations_test.cpp:74](../../tests/db/migrations_test.cpp#L74) |
| a gap in the migration versions is rejected |  |  | [tests/db/migrations_test.cpp:91](../../tests/db/migrations_test.cpp#L91) |
| the shipped schema is append-only and correctly numbered |  |  | [tests/db/migrations_test.cpp:104](../../tests/db/migrations_test.cpp#L104) |

## config

Configuration (`src/core/config`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| an unset key falls back to the caller's default |  |  | [tests/db/guild_settings_test.cpp:27](../../tests/db/guild_settings_test.cpp#L27) |
| values survive a set and get round trip |  |  | [tests/db/guild_settings_test.cpp:37](../../tests/db/guild_settings_test.cpp#L37) |
| setting a key again replaces the value |  |  | [tests/db/guild_settings_test.cpp:51](../../tests/db/guild_settings_test.cpp#L51) |
| guilds do not see each other's settings |  |  | [tests/db/guild_settings_test.cpp:61](../../tests/db/guild_settings_test.cpp#L61) |
| erase removes a key and reports whether it existed |  |  | [tests/db/guild_settings_test.cpp:70](../../tests/db/guild_settings_test.cpp#L70) |
| a value that cannot be parsed falls back instead of throwing |  |  | [tests/db/guild_settings_test.cpp:80](../../tests/db/guild_settings_test.cpp#L80) |
| booleans accept the usual spellings |  |  | [tests/db/guild_settings_test.cpp:93](../../tests/db/guild_settings_test.cpp#L93) |
| partly numeric text is not accepted as a number |  |  | [tests/db/guild_settings_test.cpp:106](../../tests/db/guild_settings_test.cpp#L106) |
| all() lists everything set for one guild |  |  | [tests/db/guild_settings_test.cpp:116](../../tests/db/guild_settings_test.cpp#L116) |
| an empty config object gives the documented defaults |  |  | [tests/unit/bootstrap_test.cpp:45](../../tests/unit/bootstrap_test.cpp#L45) |
| a missing config file is not an error | `fs` |  | [tests/unit/bootstrap_test.cpp:57](../../tests/unit/bootstrap_test.cpp#L57) |
| values in the file replace the defaults | `fs` |  | [tests/unit/bootstrap_test.cpp:63](../../tests/unit/bootstrap_test.cpp#L63) |
| IDs written as JSON numbers are rejected |  |  | [tests/unit/bootstrap_test.cpp:92](../../tests/unit/bootstrap_test.cpp#L92) |
| bad config is reported with the key that caused it |  | 6 | [tests/unit/bootstrap_test.cpp:101](../../tests/unit/bootstrap_test.cpp#L101) |
| the log level is read from the config |  |  | [tests/unit/bootstrap_test.cpp:132](../../tests/unit/bootstrap_test.cpp#L132) |
| trust needs a listed user, or an admin in a listed server |  | 5 | [tests/unit/bootstrap_test.cpp:140](../../tests/unit/bootstrap_test.cpp#L140) |
| secrets come from the environment |  | 3 | [tests/unit/bootstrap_test.cpp:174](../../tests/unit/bootstrap_test.cpp#L174) |

## commands

Command framework (`src/core/commands`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| joining follows the target and moves only when it has to |  | 4 | [tests/unit/basic_commands_test.cpp:25](../../tests/unit/basic_commands_test.cpp#L25) |
| a target who left voice is not followed to their old channel |  |  | [tests/unit/basic_commands_test.cpp:50](../../tests/unit/basic_commands_test.cpp#L50) |
| say refuses a message that is only whitespace |  |  | [tests/unit/basic_commands_test.cpp:58](../../tests/unit/basic_commands_test.cpp#L58) |
| say replies only when given a message id |  | 5 | [tests/unit/basic_commands_test.cpp:65](../../tests/unit/basic_commands_test.cpp#L65) |
| status types are matched case-insensitively and fall back to playing |  | 1 | [tests/unit/basic_commands_test.cpp:97](../../tests/unit/basic_commands_test.cpp#L97) |
| a custom status carries its text in state, not name |  |  | [tests/unit/basic_commands_test.cpp:113](../../tests/unit/basic_commands_test.cpp#L113) |
| only the missing bits of a requirement are reported |  |  | [tests/unit/preflight_test.cpp:24](../../tests/unit/preflight_test.cpp#L24) |
| a satisfied requirement is not reported |  |  | [tests/unit/preflight_test.cpp:36](../../tests/unit/preflight_test.cpp#L36) |
| administrator satisfies everything |  |  | [tests/unit/preflight_test.cpp:43](../../tests/unit/preflight_test.cpp#L43) |
| a requirement of nothing is always met |  |  | [tests/unit/preflight_test.cpp:49](../../tests/unit/preflight_test.cpp#L49) |
| permissions are described by name |  | 1 | [tests/unit/preflight_test.cpp:54](../../tests/unit/preflight_test.cpp#L54) |
| commands are found by name and by alias |  |  | [tests/unit/registry_test.cpp:56](../../tests/unit/registry_test.cpp#L56) |
| a duplicate name or alias is refused |  | 4 | [tests/unit/registry_test.cpp:67](../../tests/unit/registry_test.cpp#L67) |
| an empty name is refused |  |  | [tests/unit/registry_test.cpp:97](../../tests/unit/registry_test.cpp#L97) |
| every name and alias gets its own registration payload |  |  | [tests/unit/registry_test.cpp:102](../../tests/unit/registry_test.cpp#L102) |
| required permissions are the union of every command's |  |  | [tests/unit/registry_test.cpp:116](../../tests/unit/registry_test.cpp#L116) |
| dispatch runs the command registered under the name | `coro` |  | [tests/unit/registry_test.cpp:128](../../tests/unit/registry_test.cpp#L128) |
| an unknown command name is logged, not thrown | `coro` |  | [tests/unit/registry_test.cpp:144](../../tests/unit/registry_test.cpp#L144) |
| an exception from a handler is caught and logged | `coro` |  | [tests/unit/registry_test.cpp:155](../../tests/unit/registry_test.cpp#L155) |

## discord

Discord plumbing (`src/core/discord`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| a bare path gets the API version prefix |  |  | [tests/unit/raw_api_test.cpp:9](../../tests/unit/raw_api_test.cpp#L9) |
| a missing leading slash is added |  |  | [tests/unit/raw_api_test.cpp:13](../../tests/unit/raw_api_test.cpp#L13) |
| a path that already names the API version is left alone |  |  | [tests/unit/raw_api_test.cpp:17](../../tests/unit/raw_api_test.cpp#L17) |
| trailing slashes are trimmed |  |  | [tests/unit/raw_api_test.cpp:21](../../tests/unit/raw_api_test.cpp#L21) |
| an empty path becomes the API root |  |  | [tests/unit/raw_api_test.cpp:28](../../tests/unit/raw_api_test.cpp#L28) |

## ports

Ports and mocks (`src/core/ports`, `tests/mocks`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| mock_clock moves both clocks together |  |  | [tests/unit/ports_test.cpp:41](../../tests/unit/ports_test.cpp#L41) |
| a coroutine feature runs against the Discord mock | `coro` |  | [tests/unit/ports_test.cpp:53](../../tests/unit/ports_test.cpp#L53) |
| the Discord mock can script a failure | `coro` |  | [tests/unit/ports_test.cpp:69](../../tests/unit/ports_test.cpp#L69) |
| the Discord mock hands out scripted history pages | `coro` |  | [tests/unit/ports_test.cpp:80](../../tests/unit/ports_test.cpp#L80) |
| the HTTP mock replays responses in order and records requests | `coro` |  | [tests/unit/ports_test.cpp:105](../../tests/unit/ports_test.cpp#L105) |
| the TTS mock produces audio in proportion to the text | `coro` |  | [tests/unit/ports_test.cpp:134](../../tests/unit/ports_test.cpp#L134) |
| the TTS mock can fail once and records stops | `coro` |  | [tests/unit/ports_test.cpp:152](../../tests/unit/ports_test.cpp#L152) |

## log

Logging (`src/core/util/log`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| level names round trip |  |  | [tests/unit/log_test.cpp:17](../../tests/unit/log_test.cpp#L17) |
| level names are case-insensitive and unknown names are reported |  |  | [tests/unit/log_test.cpp:26](../../tests/unit/log_test.cpp#L26) |
| messages below the level are dropped |  |  | [tests/unit/log_test.cpp:33](../../tests/unit/log_test.cpp#L33) |
| off silences everything |  |  | [tests/unit/log_test.cpp:46](../../tests/unit/log_test.cpp#L46) |
| arguments are formatted into the message |  |  | [tests/unit/log_test.cpp:54](../../tests/unit/log_test.cpp#L54) |
| a message is never split between threads | `threads` |  | [tests/unit/log_test.cpp:63](../../tests/unit/log_test.cpp#L63) |

## util

Utilities (`src/core/util`, `src/core/version`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| count_occurrences counts non-overlapping matches |  |  | [tests/unit/text_test.cpp:13](../../tests/unit/text_test.cpp#L13) |
| is_inside_spoiler follows an odd count of markers |  |  | [tests/unit/text_test.cpp:22](../../tests/unit/text_test.cpp#L22) |
| trim removes surrounding whitespace only |  | 1 | [tests/unit/text_test.cpp:36](../../tests/unit/text_test.cpp#L36) |
| is_blank treats whitespace as empty |  |  | [tests/unit/text_test.cpp:47](../../tests/unit/text_test.cpp#L47) |
| version string matches the version constants |  |  | [tests/unit/version_test.cpp:7](../../tests/unit/version_test.cpp#L7) |
