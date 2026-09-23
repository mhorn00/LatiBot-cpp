# Test catalog

**Generated** by ``tools/Update-TestCatalog.ps1``. Do not edit by hand;
re-run the script after adding or retagging tests.

See [README.md](README.md) for the strategy, conventions and tag meanings.

145 test cases across 9 components, including 47 sections.

| Component | Test cases | Sections |
|---|---:|---:|
| [db](#db) | 31 | 4 |
| [config](#config) | 18 | 15 |
| [commands](#commands) | 29 | 20 |
| [events](#events) | 22 | 7 |
| [ui](#ui) | 11 | 0 |
| [discord](#discord) | 5 | 0 |
| [ports](#ports) | 7 | 0 |
| [log](#log) | 6 | 0 |
| [util](#util) | 16 | 1 |

## db

Database (`src/core/db`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| a backup is a complete, valid copy | `fs` |  | [tests/db/backup_test.cpp:48](../../tests/db/backup_test.cpp#L48) |
| backing up an in-memory database writes it to disk | `fs` |  | [tests/db/backup_test.cpp:62](../../tests/db/backup_test.cpp#L62) |
| an existing backup file is replaced | `fs` |  | [tests/db/backup_test.cpp:74](../../tests/db/backup_test.cpp#L74) |
| a backup taken while other threads write is consistent | `fs`, `threads` |  | [tests/db/backup_test.cpp:92](../../tests/db/backup_test.cpp#L92) |
| rotation keeps the newest backups | `fs` |  | [tests/db/backup_test.cpp:123](../../tests/db/backup_test.cpp#L123) |
| rotation ignores unrelated files | `fs` |  | [tests/db/backup_test.cpp:145](../../tests/db/backup_test.cpp#L145) |
| backup file names carry a sortable UTC timestamp | `fs` |  | [tests/db/backup_test.cpp:167](../../tests/db/backup_test.cpp#L167) |
| opening an unwritable path reports the SQLite error |  |  | [tests/db/database_test.cpp:41](../../tests/db/database_test.cpp#L41) |
| values survive a bind and get round trip |  |  | [tests/db/database_test.cpp:46](../../tests/db/database_test.cpp#L46) |
| optional values bind as NULL or as the value |  |  | [tests/db/database_test.cpp:73](../../tests/db/database_test.cpp#L73) |
| a constraint violation throws with the SQLite code |  |  | [tests/db/database_test.cpp:86](../../tests/db/database_test.cpp#L86) |
| malformed SQL is reported, not executed |  |  | [tests/db/database_test.cpp:102](../../tests/db/database_test.cpp#L102) |
| a transaction commits or rolls back |  | 3 | [tests/db/database_test.cpp:109](../../tests/db/database_test.cpp#L109) |
| last_insert_rowid and changes report the previous statement |  |  | [tests/db/database_test.cpp:146](../../tests/db/database_test.cpp#L146) |
| concurrent writers are serialized by the connection lock | `threads` |  | [tests/db/database_test.cpp:160](../../tests/db/database_test.cpp#L160) |
| a fresh database migrates to the current schema |  |  | [tests/db/migrations_test.cpp:35](../../tests/db/migrations_test.cpp#L35) |
| migrating twice is a no-op |  |  | [tests/db/migrations_test.cpp:48](../../tests/db/migrations_test.cpp#L48) |
| only migrations newer than user_version are applied |  |  | [tests/db/migrations_test.cpp:59](../../tests/db/migrations_test.cpp#L59) |
| a failing migration rolls back and keeps the previous version |  |  | [tests/db/migrations_test.cpp:73](../../tests/db/migrations_test.cpp#L73) |
| a gap in the migration versions is rejected |  |  | [tests/db/migrations_test.cpp:90](../../tests/db/migrations_test.cpp#L90) |
| the shipped schema is append-only and correctly numbered |  |  | [tests/db/migrations_test.cpp:103](../../tests/db/migrations_test.cpp#L103) |
| a trigger survives a round trip with its responses |  |  | [tests/db/trigger_store_test.cpp:49](../../tests/db/trigger_store_test.cpp#L49) |
| guilds cannot see or change each other's triggers |  |  | [tests/db/trigger_store_test.cpp:68](../../tests/db/trigger_store_test.cpp#L68) |
| updating a trigger replaces its responses rather than adding to them |  |  | [tests/db/trigger_store_test.cpp:85](../../tests/db/trigger_store_test.cpp#L85) |
| removing a trigger takes its responses with it |  |  | [tests/db/trigger_store_test.cpp:101](../../tests/db/trigger_store_test.cpp#L101) |
| the defaults are seeded once per guild |  |  | [tests/db/trigger_store_test.cpp:113](../../tests/db/trigger_store_test.cpp#L113) |
| a matching message gets one of the trigger's responses |  | 1 | [tests/db/trigger_store_test.cpp:124](../../tests/db/trigger_store_test.cpp#L124) |
| a trigger is quiet until its cooldown has passed |  |  | [tests/db/trigger_store_test.cpp:146](../../tests/db/trigger_store_test.cpp#L146) |
| cooldowns are per channel |  |  | [tests/db/trigger_store_test.cpp:163](../../tests/db/trigger_store_test.cpp#L163) |
| a disabled trigger says nothing |  |  | [tests/db/trigger_store_test.cpp:176](../../tests/db/trigger_store_test.cpp#L176) |
| two triggers on one message both answer |  |  | [tests/db/trigger_store_test.cpp:188](../../tests/db/trigger_store_test.cpp#L188) |

## config

Configuration (`src/core/config`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| an unset key falls back to the caller's default |  |  | [tests/db/guild_settings_test.cpp:28](../../tests/db/guild_settings_test.cpp#L28) |
| values survive a set and get round trip |  |  | [tests/db/guild_settings_test.cpp:38](../../tests/db/guild_settings_test.cpp#L38) |
| setting a key again replaces the value |  |  | [tests/db/guild_settings_test.cpp:52](../../tests/db/guild_settings_test.cpp#L52) |
| guilds do not see each other's settings |  |  | [tests/db/guild_settings_test.cpp:62](../../tests/db/guild_settings_test.cpp#L62) |
| erase removes a key and reports whether it existed |  |  | [tests/db/guild_settings_test.cpp:71](../../tests/db/guild_settings_test.cpp#L71) |
| a value that cannot be parsed falls back instead of throwing |  |  | [tests/db/guild_settings_test.cpp:81](../../tests/db/guild_settings_test.cpp#L81) |
| booleans accept the usual spellings |  |  | [tests/db/guild_settings_test.cpp:94](../../tests/db/guild_settings_test.cpp#L94) |
| partly numeric text is not accepted as a number |  |  | [tests/db/guild_settings_test.cpp:107](../../tests/db/guild_settings_test.cpp#L107) |
| all() lists everything set for one guild |  |  | [tests/db/guild_settings_test.cpp:117](../../tests/db/guild_settings_test.cpp#L117) |
| the goodbye phrase can be set, read back and turned off |  | 1 | [tests/db/guild_settings_test.cpp:130](../../tests/db/guild_settings_test.cpp#L130) |
| an empty config object gives the documented defaults |  |  | [tests/unit/bootstrap_test.cpp:45](../../tests/unit/bootstrap_test.cpp#L45) |
| a missing config file is not an error | `fs` |  | [tests/unit/bootstrap_test.cpp:57](../../tests/unit/bootstrap_test.cpp#L57) |
| values in the file replace the defaults | `fs` |  | [tests/unit/bootstrap_test.cpp:63](../../tests/unit/bootstrap_test.cpp#L63) |
| IDs written as JSON numbers are rejected |  |  | [tests/unit/bootstrap_test.cpp:92](../../tests/unit/bootstrap_test.cpp#L92) |
| bad config is reported with the key that caused it |  | 6 | [tests/unit/bootstrap_test.cpp:99](../../tests/unit/bootstrap_test.cpp#L99) |
| the log level is read from the config |  |  | [tests/unit/bootstrap_test.cpp:129](../../tests/unit/bootstrap_test.cpp#L129) |
| trust needs a listed user, or an admin in a listed server |  | 5 | [tests/unit/bootstrap_test.cpp:135](../../tests/unit/bootstrap_test.cpp#L135) |
| secrets come from the environment |  | 3 | [tests/unit/bootstrap_test.cpp:169](../../tests/unit/bootstrap_test.cpp#L169) |

## commands

Command framework (`src/core/commands`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| joining follows the target and moves only when it has to |  | 4 | [tests/unit/basic_commands_test.cpp:25](../../tests/unit/basic_commands_test.cpp#L25) |
| a target who left voice is not followed to their old channel |  |  | [tests/unit/basic_commands_test.cpp:50](../../tests/unit/basic_commands_test.cpp#L50) |
| say refuses a message that is only whitespace |  |  | [tests/unit/basic_commands_test.cpp:58](../../tests/unit/basic_commands_test.cpp#L58) |
| say replies only when given a message id |  | 5 | [tests/unit/basic_commands_test.cpp:65](../../tests/unit/basic_commands_test.cpp#L65) |
| status types are matched case-insensitively and fall back to playing |  | 1 | [tests/unit/basic_commands_test.cpp:96](../../tests/unit/basic_commands_test.cpp#L96) |
| a custom status carries its text in state, not name |  |  | [tests/unit/basic_commands_test.cpp:112](../../tests/unit/basic_commands_test.cpp#L112) |
| only the missing bits of a requirement are reported |  |  | [tests/unit/preflight_test.cpp:24](../../tests/unit/preflight_test.cpp#L24) |
| a satisfied requirement is not reported |  |  | [tests/unit/preflight_test.cpp:36](../../tests/unit/preflight_test.cpp#L36) |
| administrator satisfies everything |  |  | [tests/unit/preflight_test.cpp:43](../../tests/unit/preflight_test.cpp#L43) |
| a requirement of nothing is always met |  |  | [tests/unit/preflight_test.cpp:49](../../tests/unit/preflight_test.cpp#L49) |
| permissions are described by name |  | 1 | [tests/unit/preflight_test.cpp:54](../../tests/unit/preflight_test.cpp#L54) |
| commands are found by name and by alias |  |  | [tests/unit/registry_test.cpp:55](../../tests/unit/registry_test.cpp#L55) |
| a duplicate name or alias is refused |  | 4 | [tests/unit/registry_test.cpp:66](../../tests/unit/registry_test.cpp#L66) |
| an empty name is refused |  |  | [tests/unit/registry_test.cpp:93](../../tests/unit/registry_test.cpp#L93) |
| every name and alias gets its own registration payload |  |  | [tests/unit/registry_test.cpp:98](../../tests/unit/registry_test.cpp#L98) |
| required permissions are the union of every command's |  |  | [tests/unit/registry_test.cpp:112](../../tests/unit/registry_test.cpp#L112) |
| dispatch runs the command registered under the name | `coro` |  | [tests/unit/registry_test.cpp:124](../../tests/unit/registry_test.cpp#L124) |
| an unknown command name is logged, not thrown | `coro` |  | [tests/unit/registry_test.cpp:140](../../tests/unit/registry_test.cpp#L140) |
| an exception from a handler is caught and logged | `coro` |  | [tests/unit/registry_test.cpp:151](../../tests/unit/registry_test.cpp#L151) |
| responses are one per line |  |  | [tests/unit/trigger_command_test.cpp:14](../../tests/unit/trigger_command_test.cpp#L14) |
| a leading number and bar sets the weight |  |  | [tests/unit/trigger_command_test.cpp:23](../../tests/unit/trigger_command_test.cpp#L23) |
| a bar that is not a weight stays part of the response |  |  | [tests/unit/trigger_command_test.cpp:33](../../tests/unit/trigger_command_test.cpp#L33) |
| blank lines are skipped |  |  | [tests/unit/trigger_command_test.cpp:44](../../tests/unit/trigger_command_test.cpp#L44) |
| nothing usable parses to nothing |  |  | [tests/unit/trigger_command_test.cpp:52](../../tests/unit/trigger_command_test.cpp#L52) |
| responses round trip through their text form |  |  | [tests/unit/trigger_command_test.cpp:59](../../tests/unit/trigger_command_test.cpp#L59) |
| a trigger describes itself in one line |  | 3 | [tests/unit/trigger_command_test.cpp:69](../../tests/unit/trigger_command_test.cpp#L69) |
| the modal keeps fields it cannot read rather than resetting them |  |  | [tests/unit/trigger_command_test.cpp:96](../../tests/unit/trigger_command_test.cpp#L96) |
| the modal applies the fields it can read |  |  | [tests/unit/trigger_command_test.cpp:118](../../tests/unit/trigger_command_test.cpp#L118) |
| the modal refuses a trigger that could not work |  | 2 | [tests/unit/trigger_command_test.cpp:133](../../tests/unit/trigger_command_test.cpp#L133) |

## events

Message pipeline and triggers (`src/core/events`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| the goodbye phrase is recognised however it is typed |  | 1 | [tests/unit/goodbye_test.cpp:10](../../tests/unit/goodbye_test.cpp#L10) |
| the phrase has to be the whole message |  | 1 | [tests/unit/goodbye_test.cpp:21](../../tests/unit/goodbye_test.cpp#L21) |
| a cleared phrase turns the feature off |  |  | [tests/unit/goodbye_test.cpp:33](../../tests/unit/goodbye_test.cpp#L33) |
| a custom phrase replaces the default |  |  | [tests/unit/goodbye_test.cpp:42](../../tests/unit/goodbye_test.cpp#L42) |
| an empty message never matches a real phrase |  |  | [tests/unit/goodbye_test.cpp:47](../../tests/unit/goodbye_test.cpp#L47) |
| stages run in the order they were added |  |  | [tests/unit/message_pipeline_test.cpp:55](../../tests/unit/message_pipeline_test.cpp#L55) |
| a stage that consumes the message stops the ones after it |  |  | [tests/unit/message_pipeline_test.cpp:68](../../tests/unit/message_pipeline_test.cpp#L68) |
| the bot never answers itself or another bot |  | 2 | [tests/unit/message_pipeline_test.cpp:83](../../tests/unit/message_pipeline_test.cpp#L83) |
| a stage that throws is logged and the rest still run |  |  | [tests/unit/message_pipeline_test.cpp:105](../../tests/unit/message_pipeline_test.cpp#L105) |
| an empty pipeline decides nothing |  |  | [tests/unit/message_pipeline_test.cpp:122](../../tests/unit/message_pipeline_test.cpp#L122) |
| whole word matching ignores the middle of longer words |  |  | [tests/unit/triggers_test.cpp:19](../../tests/unit/triggers_test.cpp#L19) |
| a later occurrence still counts as a whole word |  |  | [tests/unit/triggers_test.cpp:29](../../tests/unit/triggers_test.cpp#L29) |
| substring matching does not care about boundaries |  |  | [tests/unit/triggers_test.cpp:35](../../tests/unit/triggers_test.cpp#L35) |
| matching ignores case on both sides |  |  | [tests/unit/triggers_test.cpp:40](../../tests/unit/triggers_test.cpp#L40) |
| a pattern with punctuation matches as a word |  |  | [tests/unit/triggers_test.cpp:45](../../tests/unit/triggers_test.cpp#L45) |
| an empty pattern never matches |  |  | [tests/unit/triggers_test.cpp:52](../../tests/unit/triggers_test.cpp#L52) |
| match modes parse from their stored and spoken names |  |  | [tests/unit/triggers_test.cpp:58](../../tests/unit/triggers_test.cpp#L58) |
| weighted responses are picked in proportion |  | 2 | [tests/unit/triggers_test.cpp:67](../../tests/unit/triggers_test.cpp#L67) |
| a trigger with nothing to say picks nothing |  | 1 | [tests/unit/triggers_test.cpp:96](../../tests/unit/triggers_test.cpp#L96) |
| a zero-weight response is skipped but its neighbours still work |  |  | [tests/unit/triggers_test.cpp:108](../../tests/unit/triggers_test.cpp#L108) |
| cooldowns are measured from the last reply |  |  | [tests/unit/triggers_test.cpp:120](../../tests/unit/triggers_test.cpp#L120) |
| a zero cooldown means no cooldown |  |  | [tests/unit/triggers_test.cpp:129](../../tests/unit/triggers_test.cpp#L129) |

## ui

Panels and paging (`src/core/ui`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| page state survives a round trip through a custom_id |  |  | [tests/unit/paginator_test.cpp:17](../../tests/unit/paginator_test.cpp#L17) |
| an argument containing the separator still round trips |  |  | [tests/unit/paginator_test.cpp:30](../../tests/unit/paginator_test.cpp#L30) |
| an id that would exceed Discord's limit is refused |  |  | [tests/unit/paginator_test.cpp:43](../../tests/unit/paginator_test.cpp#L43) |
| text that is not ours decodes to nothing |  |  | [tests/unit/paginator_test.cpp:50](../../tests/unit/paginator_test.cpp#L50) |
| an empty list is one page, not zero |  |  | [tests/unit/paginator_test.cpp:59](../../tests/unit/paginator_test.cpp#L59) |
| pages are counted by rounding up |  |  | [tests/unit/paginator_test.cpp:67](../../tests/unit/paginator_test.cpp#L67) |
| a stale page number is clamped rather than rejected |  |  | [tests/unit/paginator_test.cpp:75](../../tests/unit/paginator_test.cpp#L75) |
| the last page holds the remainder |  |  | [tests/unit/paginator_test.cpp:86](../../tests/unit/paginator_test.cpp#L86) |
| there is no paging row for a single page |  |  | [tests/unit/paginator_test.cpp:92](../../tests/unit/paginator_test.cpp#L92) |
| the paging row disables the direction it cannot go |  |  | [tests/unit/paginator_test.cpp:98](../../tests/unit/paginator_test.cpp#L98) |
| the paging buttons carry the neighbouring pages |  |  | [tests/unit/paginator_test.cpp:111](../../tests/unit/paginator_test.cpp#L111) |

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
| the HTTP mock replays responses in order and records requests | `coro` |  | [tests/unit/ports_test.cpp:103](../../tests/unit/ports_test.cpp#L103) |
| the TTS mock produces audio in proportion to the text | `coro` |  | [tests/unit/ports_test.cpp:132](../../tests/unit/ports_test.cpp#L132) |
| the TTS mock can fail once and records stops | `coro` |  | [tests/unit/ports_test.cpp:150](../../tests/unit/ports_test.cpp#L150) |

## log

Logging (`src/core/util/log`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| level names round trip |  |  | [tests/unit/log_test.cpp:17](../../tests/unit/log_test.cpp#L17) |
| level names are case-insensitive and unknown names are reported |  |  | [tests/unit/log_test.cpp:25](../../tests/unit/log_test.cpp#L25) |
| messages below the level are dropped |  |  | [tests/unit/log_test.cpp:32](../../tests/unit/log_test.cpp#L32) |
| off silences everything |  |  | [tests/unit/log_test.cpp:45](../../tests/unit/log_test.cpp#L45) |
| arguments are formatted into the message |  |  | [tests/unit/log_test.cpp:53](../../tests/unit/log_test.cpp#L53) |
| a message is never split between threads | `threads` |  | [tests/unit/log_test.cpp:61](../../tests/unit/log_test.cpp#L61) |

## util

Utilities (`src/core/util`, `src/core/version`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| the system root certificates export as a readable PEM bundle | `fs` |  | [tests/unit/ca_certificates_test.cpp:34](../../tests/unit/ca_certificates_test.cpp#L34) |
| exporting creates the directory it was given | `fs` |  | [tests/unit/ca_certificates_test.cpp:50](../../tests/unit/ca_certificates_test.cpp#L50) |
| parse_dotenv reads simple key-value lines |  |  | [tests/unit/env_test.cpp:7](../../tests/unit/env_test.cpp#L7) |
| parse_dotenv skips blank lines and comments |  |  | [tests/unit/env_test.cpp:15](../../tests/unit/env_test.cpp#L15) |
| parse_dotenv trims whitespace around key and value |  |  | [tests/unit/env_test.cpp:22](../../tests/unit/env_test.cpp#L22) |
| parse_dotenv strips a leading export |  |  | [tests/unit/env_test.cpp:30](../../tests/unit/env_test.cpp#L30) |
| parse_dotenv strips matching surrounding quotes |  |  | [tests/unit/env_test.cpp:37](../../tests/unit/env_test.cpp#L37) |
| parse_dotenv skips a line with no '=' |  |  | [tests/unit/env_test.cpp:46](../../tests/unit/env_test.cpp#L46) |
| parse_dotenv allows an empty value |  |  | [tests/unit/env_test.cpp:53](../../tests/unit/env_test.cpp#L53) |
| parse_dotenv handles a final line with no trailing newline |  |  | [tests/unit/env_test.cpp:60](../../tests/unit/env_test.cpp#L60) |
| parse_dotenv copes with CRLF line endings |  |  | [tests/unit/env_test.cpp:67](../../tests/unit/env_test.cpp#L67) |
| count_occurrences counts non-overlapping matches |  |  | [tests/unit/text_test.cpp:13](../../tests/unit/text_test.cpp#L13) |
| is_inside_spoiler follows an odd count of markers |  |  | [tests/unit/text_test.cpp:22](../../tests/unit/text_test.cpp#L22) |
| trim removes surrounding whitespace only |  | 1 | [tests/unit/text_test.cpp:36](../../tests/unit/text_test.cpp#L36) |
| is_blank treats whitespace as empty |  |  | [tests/unit/text_test.cpp:47](../../tests/unit/text_test.cpp#L47) |
| version string matches the version constants |  |  | [tests/unit/version_test.cpp:7](../../tests/unit/version_test.cpp#L7) |
