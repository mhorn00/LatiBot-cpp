# Test catalog

**Generated** by ``tools/Update-TestCatalog.ps1``. Do not edit by hand;
re-run the script after adding or retagging tests.

See [README.md](README.md) for the strategy, conventions and tag meanings.

470 test cases across 9 components, including 100 sections.

| Component | Test cases | Sections |
|---|---:|---:|
| [db](#db) | 104 | 11 |
| [config](#config) | 23 | 19 |
| [commands](#commands) | 111 | 26 |
| [events](#events) | 145 | 33 |
| [ui](#ui) | 11 | 0 |
| [discord](#discord) | 8 | 0 |
| [ports](#ports) | 7 | 0 |
| [log](#log) | 28 | 0 |
| [util](#util) | 33 | 11 |

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
| nothing is allowed until somebody says so |  |  | [tests/db/bot_allowlist_test.cpp:26](../../tests/db/bot_allowlist_test.cpp#L26) |
| an allowed bot is remembered and can be taken back |  |  | [tests/db/bot_allowlist_test.cpp:35](../../tests/db/bot_allowlist_test.cpp#L35) |
| allowing and denying report whether anything changed |  |  | [tests/db/bot_allowlist_test.cpp:45](../../tests/db/bot_allowlist_test.cpp#L45) |
| guilds keep their own allowlists |  |  | [tests/db/bot_allowlist_test.cpp:57](../../tests/db/bot_allowlist_test.cpp#L57) |
| for_guild lists everything allowed there |  |  | [tests/db/bot_allowlist_test.cpp:68](../../tests/db/bot_allowlist_test.cpp#L68) |
| opening an unwritable path reports the SQLite error |  |  | [tests/db/database_test.cpp:41](../../tests/db/database_test.cpp#L41) |
| values survive a bind and get round trip |  |  | [tests/db/database_test.cpp:46](../../tests/db/database_test.cpp#L46) |
| optional values bind as NULL or as the value |  |  | [tests/db/database_test.cpp:73](../../tests/db/database_test.cpp#L73) |
| a constraint violation throws with the SQLite code |  |  | [tests/db/database_test.cpp:86](../../tests/db/database_test.cpp#L86) |
| malformed SQL is reported, not executed |  |  | [tests/db/database_test.cpp:102](../../tests/db/database_test.cpp#L102) |
| a transaction commits or rolls back |  | 3 | [tests/db/database_test.cpp:109](../../tests/db/database_test.cpp#L109) |
| last_insert_rowid and changes report the previous statement |  |  | [tests/db/database_test.cpp:146](../../tests/db/database_test.cpp#L146) |
| concurrent writers are serialized by the connection lock | `threads` |  | [tests/db/database_test.cpp:160](../../tests/db/database_test.cpp#L160) |
| an added entry comes back as it went in |  |  | [tests/db/midnight_store_test.cpp:49](../../tests/db/midnight_store_test.cpp#L49) |
| entries belong to one guild |  |  | [tests/db/midnight_store_test.cpp:64](../../tests/db/midnight_store_test.cpp#L64) |
| only enabled entries are looked at on a tick |  |  | [tests/db/midnight_store_test.cpp:76](../../tests/db/midnight_store_test.cpp#L76) |
| a day can only be claimed once |  |  | [tests/db/midnight_store_test.cpp:89](../../tests/db/midnight_store_test.cpp#L89) |
| editing an entry leaves the day it last posted alone |  |  | [tests/db/midnight_store_test.cpp:99](../../tests/db/midnight_store_test.cpp#L99) |
| a tick posts an entry once and then leaves it alone |  |  | [tests/db/midnight_store_test.cpp:114](../../tests/db/midnight_store_test.cpp#L114) |
| a restart moments after posting does not post again |  |  | [tests/db/midnight_store_test.cpp:140](../../tests/db/midnight_store_test.cpp#L140) |
| a night the bot slept through is given up on, not posted at breakfast |  |  | [tests/db/midnight_store_test.cpp:159](../../tests/db/midnight_store_test.cpp#L159) |
| a restart a minute after midnight still posts |  |  | [tests/db/midnight_store_test.cpp:184](../../tests/db/midnight_store_test.cpp#L184) |
| each timezone posts at its own midnight |  |  | [tests/db/midnight_store_test.cpp:198](../../tests/db/midnight_store_test.cpp#L198) |
| a midnight message's flags survive a round trip and default to silent |  |  | [tests/db/midnight_store_test.cpp:217](../../tests/db/midnight_store_test.cpp#L217) |
| a midnight post carries its entry's flags |  |  | [tests/db/midnight_store_test.cpp:234](../../tests/db/midnight_store_test.cpp#L234) |
| a fresh database migrates to the current schema |  |  | [tests/db/migrations_test.cpp:36](../../tests/db/migrations_test.cpp#L36) |
| migrating twice is a no-op |  |  | [tests/db/migrations_test.cpp:49](../../tests/db/migrations_test.cpp#L49) |
| only migrations newer than user_version are applied |  |  | [tests/db/migrations_test.cpp:60](../../tests/db/migrations_test.cpp#L60) |
| a failing migration rolls back and keeps the previous version |  |  | [tests/db/migrations_test.cpp:74](../../tests/db/migrations_test.cpp#L74) |
| a gap in the migration versions is rejected |  |  | [tests/db/migrations_test.cpp:91](../../tests/db/migrations_test.cpp#L91) |
| the shipped schema is append-only and correctly numbered |  |  | [tests/db/migrations_test.cpp:104](../../tests/db/migrations_test.cpp#L104) |
| an existing database gains the allowlist without losing its triggers |  |  | [tests/db/migrations_test.cpp:116](../../tests/db/migrations_test.cpp#L116) |
| an import writes the history it read |  |  | [tests/db/nickname_import_test.cpp:42](../../tests/db/nickname_import_test.cpp#L42) |
| importing the same file twice adds nothing the second time |  |  | [tests/db/nickname_import_test.cpp:56](../../tests/db/nickname_import_test.cpp#L56) |
| an import does not disturb history the bot recorded itself |  |  | [tests/db/nickname_import_test.cpp:68](../../tests/db/nickname_import_test.cpp#L68) |
| a cleared nickname is imported once, not once per run |  |  | [tests/db/nickname_import_test.cpp:89](../../tests/db/nickname_import_test.cpp#L89) |
| no file to import is not a problem | `fs` |  | [tests/db/nickname_import_test.cpp:106](../../tests/db/nickname_import_test.cpp#L106) |
| a file beside the database is read and imported | `fs` |  | [tests/db/nickname_import_test.cpp:113](../../tests/db/nickname_import_test.cpp#L113) |
| a recorded change comes back as it went in |  |  | [tests/db/nickname_store_test.cpp:39](../../tests/db/nickname_store_test.cpp#L39) |
| a cleared nickname is stored as nothing, not as an empty string |  |  | [tests/db/nickname_store_test.cpp:60](../../tests/db/nickname_store_test.cpp#L60) |
| history reads newest first |  |  | [tests/db/nickname_store_test.cpp:72](../../tests/db/nickname_store_test.cpp#L72) |
| two changes in the same second keep the order they were recorded |  |  | [tests/db/nickname_store_test.cpp:86](../../tests/db/nickname_store_test.cpp#L86) |
| history is per guild |  |  | [tests/db/nickname_store_test.cpp:99](../../tests/db/nickname_store_test.cpp#L99) |
| the latest row is what a new sighting is compared against |  |  | [tests/db/nickname_store_test.cpp:113](../../tests/db/nickname_store_test.cpp#L113) |
| an audit entry finds the row it describes |  |  | [tests/db/nickname_store_test.cpp:126](../../tests/db/nickname_store_test.cpp#L126) |
| an audit entry does not attach itself to an older identical change |  |  | [tests/db/nickname_store_test.cpp:136](../../tests/db/nickname_store_test.cpp#L136) |
| an audit entry for a different nickname matches nothing |  |  | [tests/db/nickname_store_test.cpp:146](../../tests/db/nickname_store_test.cpp#L146) |
| a row that already names somebody is not offered for attribution |  |  | [tests/db/nickname_store_test.cpp:154](../../tests/db/nickname_store_test.cpp#L154) |
| attributing a row fills in the author and where it came from |  |  | [tests/db/nickname_store_test.cpp:164](../../tests/db/nickname_store_test.cpp#L164) |
| the first audit entry to attribute a row wins |  |  | [tests/db/nickname_store_test.cpp:177](../../tests/db/nickname_store_test.cpp#L177) |
| a change that did not go through can be taken back |  |  | [tests/db/nickname_store_test.cpp:190](../../tests/db/nickname_store_test.cpp#L190) |
| the members of a guild are listed once each |  |  | [tests/db/nickname_store_test.cpp:203](../../tests/db/nickname_store_test.cpp#L203) |
| an imported row keeps the text its timestamp was read from |  |  | [tests/db/nickname_store_test.cpp:218](../../tests/db/nickname_store_test.cpp#L218) |
| only reactions on our replacements are counted |  |  | [tests/db/reaction_store_test.cpp:77](../../tests/db/reaction_store_test.cpp#L77) |
| taking a reaction back removes it |  |  | [tests/db/reaction_store_test.cpp:89](../../tests/db/reaction_store_test.cpp#L89) |
| a moderator clearing reactions clears the counts |  |  | [tests/db/reaction_store_test.cpp:98](../../tests/db/reaction_store_test.cpp#L98) |
| received, given and self-reactions are counted apart |  | 5 | [tests/db/reaction_store_test.cpp:115](../../tests/db/reaction_store_test.cpp#L115) |
| a backfilled reaction is dated by its message |  |  | [tests/db/reaction_store_test.cpp:155](../../tests/db/reaction_store_test.cpp#L155) |
| a live reaction is dated when it was added |  |  | [tests/db/reaction_store_test.cpp:167](../../tests/db/reaction_store_test.cpp#L167) |
| rebuilding a message's reactions is safe to repeat |  | 1 | [tests/db/reaction_store_test.cpp:179](../../tests/db/reaction_store_test.cpp#L179) |
| an alias merges one emoji into another across all history, and can be undone |  |  | [tests/db/reaction_store_test.cpp:204](../../tests/db/reaction_store_test.cpp#L204) |
| alias chains are flattened as they are written |  | 1 | [tests/db/reaction_store_test.cpp:225](../../tests/db/reaction_store_test.cpp#L225) |
| an alias that would loop is refused |  |  | [tests/db/reaction_store_test.cpp:244](../../tests/db/reaction_store_test.cpp#L244) |
| aliases belong to one guild |  |  | [tests/db/reaction_store_test.cpp:252](../../tests/db/reaction_store_test.cpp#L252) |
| emojis are known by name once somebody has used them |  |  | [tests/db/reaction_store_test.cpp:262](../../tests/db/reaction_store_test.cpp#L262) |
| a replacement round-trips with its links in order |  |  | [tests/db/replacement_store_test.cpp:39](../../tests/db/replacement_store_test.cpp#L39) |
| an unattributed replacement stores no author |  |  | [tests/db/replacement_store_test.cpp:59](../../tests/db/replacement_store_test.cpp#L59) |
| state changes and retries are recorded |  |  | [tests/db/replacement_store_test.cpp:72](../../tests/db/replacement_store_test.cpp#L72) |
| recording a replacement again replaces its links |  |  | [tests/db/replacement_store_test.cpp:87](../../tests/db/replacement_store_test.cpp#L87) |
| replacement states have stable names |  |  | [tests/db/replacement_store_test.cpp:100](../../tests/db/replacement_store_test.cpp#L100) |
| a trigger survives a round trip with its responses |  |  | [tests/db/trigger_store_test.cpp:61](../../tests/db/trigger_store_test.cpp#L61) |
| guilds cannot see or change each other's triggers |  |  | [tests/db/trigger_store_test.cpp:80](../../tests/db/trigger_store_test.cpp#L80) |
| updating a trigger replaces its responses rather than adding to them |  |  | [tests/db/trigger_store_test.cpp:97](../../tests/db/trigger_store_test.cpp#L97) |
| removing a trigger takes its responses with it |  |  | [tests/db/trigger_store_test.cpp:113](../../tests/db/trigger_store_test.cpp#L113) |
| the defaults are seeded once per guild |  |  | [tests/db/trigger_store_test.cpp:125](../../tests/db/trigger_store_test.cpp#L125) |
| a matching message gets one of the trigger's responses |  | 1 | [tests/db/trigger_store_test.cpp:136](../../tests/db/trigger_store_test.cpp#L136) |
| a trigger is quiet until its cooldown has passed |  |  | [tests/db/trigger_store_test.cpp:158](../../tests/db/trigger_store_test.cpp#L158) |
| cooldowns are per channel |  |  | [tests/db/trigger_store_test.cpp:175](../../tests/db/trigger_store_test.cpp#L175) |
| messages arriving at once still get one reply per cooldown | `threads` |  | [tests/db/trigger_store_test.cpp:188](../../tests/db/trigger_store_test.cpp#L188) |
| a disabled trigger says nothing |  |  | [tests/db/trigger_store_test.cpp:227](../../tests/db/trigger_store_test.cpp#L227) |
| two triggers on one message both answer |  |  | [tests/db/trigger_store_test.cpp:239](../../tests/db/trigger_store_test.cpp#L239) |
| respond_to_bots survives a round trip and defaults to off |  |  | [tests/db/trigger_store_test.cpp:254](../../tests/db/trigger_store_test.cpp#L254) |
| a trigger only answers an allowed bot when it opts in |  |  | [tests/db/trigger_store_test.cpp:273](../../tests/db/trigger_store_test.cpp#L273) |
| a trigger that answers bots still answers humans |  |  | [tests/db/trigger_store_test.cpp:291](../../tests/db/trigger_store_test.cpp#L291) |
| a trigger's reply flags survive a round trip and default to silent |  |  | [tests/db/trigger_store_test.cpp:304](../../tests/db/trigger_store_test.cpp#L304) |
| triggers from before reply flags existed stay silent |  |  | [tests/db/trigger_store_test.cpp:327](../../tests/db/trigger_store_test.cpp#L327) |
| a trigger's reply carries its flags |  |  | [tests/db/trigger_store_test.cpp:344](../../tests/db/trigger_store_test.cpp#L344) |
| a rule comes back with its mirrors in order |  |  | [tests/db/url_rule_store_test.cpp:35](../../tests/db/url_rule_store_test.cpp#L35) |
| setting a rule replaces its mirrors, which is how reordering works |  |  | [tests/db/url_rule_store_test.cpp:48](../../tests/db/url_rule_store_test.cpp#L48) |
| rules belong to one guild |  |  | [tests/db/url_rule_store_test.cpp:59](../../tests/db/url_rule_store_test.cpp#L59) |
| removing a rule says whether there was one |  |  | [tests/db/url_rule_store_test.cpp:72](../../tests/db/url_rule_store_test.cpp#L72) |
| a mirror is remembered after its rule is gone |  |  | [tests/db/url_rule_store_test.cpp:81](../../tests/db/url_rule_store_test.cpp#L81) |
| an opt-out toggles, and is kept per guild |  |  | [tests/db/url_rule_store_test.cpp:93](../../tests/db/url_rule_store_test.cpp#L93) |
| replacement is off in a guild until it is turned on, per guild |  |  | [tests/db/url_rule_store_test.cpp:105](../../tests/db/url_rule_store_test.cpp#L105) |
| turning replacement on outlasts a restart | `fs` |  | [tests/db/url_rule_store_test.cpp:120](../../tests/db/url_rule_store_test.cpp#L120) |
| the Java rule file imports once, and never over an existing rule | `fs` |  | [tests/db/url_rule_store_test.cpp:137](../../tests/db/url_rule_store_test.cpp#L137) |
| a missing rule file is not an error | `fs` |  | [tests/db/url_rule_store_test.cpp:156](../../tests/db/url_rule_store_test.cpp#L156) |

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
| nickname tracking is on unless the config turns it off |  |  | [tests/unit/bootstrap_test.cpp:129](../../tests/unit/bootstrap_test.cpp#L129) |
| the log level is read from the config |  |  | [tests/unit/bootstrap_test.cpp:140](../../tests/unit/bootstrap_test.cpp#L140) |
| the build's default log level matches the build |  |  | [tests/unit/bootstrap_test.cpp:148](../../tests/unit/bootstrap_test.cpp#L148) |
| trust needs a listed user, or an admin in a listed server |  | 5 | [tests/unit/bootstrap_test.cpp:156](../../tests/unit/bootstrap_test.cpp#L156) |
| secrets come from the environment |  | 3 | [tests/unit/bootstrap_test.cpp:190](../../tests/unit/bootstrap_test.cpp#L190) |
| a Discord ID is read as digits and nothing else |  |  | [tests/unit/bootstrap_test.cpp:217](../../tests/unit/bootstrap_test.cpp#L217) |
| the recompute bot override is read by debug builds only |  | 4 | [tests/unit/bootstrap_test.cpp:232](../../tests/unit/bootstrap_test.cpp#L232) |
| loading the configuration applies the recompute bot override as the build allows | `fs` |  | [tests/unit/bootstrap_test.cpp:259](../../tests/unit/bootstrap_test.cpp#L259) |

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
| an empty allowlist explains itself |  |  | [tests/unit/bots_command_test.cpp:17](../../tests/unit/bots_command_test.cpp#L17) |
| allowed bots are listed by name where one is known |  |  | [tests/unit/bots_command_test.cpp:26](../../tests/unit/bots_command_test.cpp#L26) |
| a bot that has left is still listed, and says so |  |  | [tests/unit/bots_command_test.cpp:36](../../tests/unit/bots_command_test.cpp#L36) |
| the list says that hearing is not answering |  |  | [tests/unit/bots_command_test.cpp:46](../../tests/unit/bots_command_test.cpp#L46) |
| a command with no options logs as its name |  |  | [tests/unit/command_log_test.cpp:31](../../tests/unit/command_log_test.cpp#L31) |
| options are logged as name=value |  |  | [tests/unit/command_log_test.cpp:35](../../tests/unit/command_log_test.cpp#L35) |
| a subcommand reads as part of the command name |  |  | [tests/unit/command_log_test.cpp:42](../../tests/unit/command_log_test.cpp#L42) |
| every option type has a readable form |  |  | [tests/unit/command_log_test.cpp:54](../../tests/unit/command_log_test.cpp#L54) |
| an unfilled option says so rather than logging nothing |  |  | [tests/unit/command_log_test.cpp:69](../../tests/unit/command_log_test.cpp#L69) |
| newlines in a value never break the line |  |  | [tests/unit/command_log_test.cpp:76](../../tests/unit/command_log_test.cpp#L76) |
| a quote in a value is escaped |  |  | [tests/unit/command_log_test.cpp:89](../../tests/unit/command_log_test.cpp#L89) |
| a long value is cut, and says how long it really was |  |  | [tests/unit/command_log_test.cpp:96](../../tests/unit/command_log_test.cpp#L96) |
| a value that just fits is not cut |  |  | [tests/unit/command_log_test.cpp:108](../../tests/unit/command_log_test.cpp#L108) |
| a user is logged by name and id |  |  | [tests/unit/command_log_test.cpp:115](../../tests/unit/command_log_test.cpp#L115) |
| the option being typed into is found at the top level |  |  | [tests/unit/command_log_test.cpp:140](../../tests/unit/command_log_test.cpp#L140) |
| the option being typed into is found inside a subcommand |  |  | [tests/unit/command_log_test.cpp:148](../../tests/unit/command_log_test.cpp#L148) |
| nothing focused is nothing to complete |  |  | [tests/unit/command_log_test.cpp:164](../../tests/unit/command_log_test.cpp#L164) |
| a user's id keeps its colour inside name (id) |  |  | [tests/unit/command_log_test.cpp:171](../../tests/unit/command_log_test.cpp#L171) |
| every command's response flags pass registration |  |  | [tests/unit/command_responses_test.cpp:52](../../tests/unit/command_responses_test.cpp#L52) |
| the views meant for the room are public and the rest are private |  |  | [tests/unit/command_responses_test.cpp:74](../../tests/unit/command_responses_test.cpp#L74) |
| the URL dry run hides the previews of the links it shows |  |  | [tests/unit/command_responses_test.cpp:98](../../tests/unit/command_responses_test.cpp#L98) |
| the silent and previews options change only what they are given |  |  | [tests/unit/command_responses_test.cpp:110](../../tests/unit/command_responses_test.cpp#L110) |
| only a difference from silent with previews is worth describing |  |  | [tests/unit/command_responses_test.cpp:131](../../tests/unit/command_responses_test.cpp#L131) |
| dates are read as YYYY-MM-DD and must exist |  |  | [tests/unit/linkstats_command_test.cpp:54](../../tests/unit/linkstats_command_test.cpp#L54) |
| the leaderboard names people without pinging them |  |  | [tests/unit/linkstats_command_test.cpp:63](../../tests/unit/linkstats_command_test.cpp#L63) |
| the emoji leaderboard shows emojis rather than people |  |  | [tests/unit/linkstats_command_test.cpp:73](../../tests/unit/linkstats_command_test.cpp#L73) |
| an empty leaderboard says how to fill it |  |  | [tests/unit/linkstats_command_test.cpp:82](../../tests/unit/linkstats_command_test.cpp#L82) |
| a profile shows received, given and self apart |  |  | [tests/unit/linkstats_command_test.cpp:89](../../tests/unit/linkstats_command_test.cpp#L89) |
| a date range shows in the title as it was typed |  |  | [tests/unit/linkstats_command_test.cpp:98](../../tests/unit/linkstats_command_test.cpp#L98) |
| an emoji can be named rather than drawn |  |  | [tests/unit/linkstats_command_test.cpp:107](../../tests/unit/linkstats_command_test.cpp#L107) |
| link stats are open to everyone, with aliases in a group |  |  | [tests/unit/linkstats_command_test.cpp:123](../../tests/unit/linkstats_command_test.cpp#L123) |
| a recompute's report says what it found and what it could not read |  | 2 | [tests/unit/linkstats_command_test.cpp:136](../../tests/unit/linkstats_command_test.cpp#L136) |
| recompute is its own group, with a required start date |  |  | [tests/unit/linkstats_command_test.cpp:178](../../tests/unit/linkstats_command_test.cpp#L178) |
| a long leaderboard pages, and every page is the same board |  |  | [tests/unit/linkstats_command_test.cpp:197](../../tests/unit/linkstats_command_test.cpp#L197) |
| a board's filters survive the trip through a button |  |  | [tests/unit/linkstats_command_test.cpp:238](../../tests/unit/linkstats_command_test.cpp#L238) |
| a board can be limited to one site |  |  | [tests/unit/linkstats_command_test.cpp:263](../../tests/unit/linkstats_command_test.cpp#L263) |
| what a leaderboard ranks is read from its option |  |  | [tests/unit/linkstats_command_test.cpp:286](../../tests/unit/linkstats_command_test.cpp#L286) |
| an empty list says how to add one |  |  | [tests/unit/midnight_command_test.cpp:26](../../tests/unit/midnight_command_test.cpp#L26) |
| a listed entry names its channel, zone and message |  |  | [tests/unit/midnight_command_test.cpp:30](../../tests/unit/midnight_command_test.cpp#L30) |
| an entry that is off says so |  |  | [tests/unit/midnight_command_test.cpp:39](../../tests/unit/midnight_command_test.cpp#L39) |
| an entry that has posted says when |  |  | [tests/unit/midnight_command_test.cpp:49](../../tests/unit/midnight_command_test.cpp#L49) |
| every entry appears in the list |  |  | [tests/unit/midnight_command_test.cpp:59](../../tests/unit/midnight_command_test.cpp#L59) |
| an entry that notifies or hides previews says so |  |  | [tests/unit/midnight_command_test.cpp:69](../../tests/unit/midnight_command_test.cpp#L69) |
| an empty history says so rather than showing an empty page |  |  | [tests/unit/nickname_command_test.cpp:37](../../tests/unit/nickname_command_test.cpp#L37) |
| a history page shows its entries and where it is |  |  | [tests/unit/nickname_command_test.cpp:46](../../tests/unit/nickname_command_test.cpp#L46) |
| a long history pages, and the buttons remember whose it is |  |  | [tests/unit/nickname_command_test.cpp:56](../../tests/unit/nickname_command_test.cpp#L56) |
| a page number from a stale button is brought back in range |  |  | [tests/unit/nickname_command_test.cpp:80](../../tests/unit/nickname_command_test.cpp#L80) |
| a history is posted for the room, not just for whoever asked |  |  | [tests/unit/nickname_command_test.cpp:89](../../tests/unit/nickname_command_test.cpp#L89) |
| a history reply cannot ping the people it names |  |  | [tests/unit/nickname_command_test.cpp:96](../../tests/unit/nickname_command_test.cpp#L96) |
| only the missing bits of a requirement are reported |  |  | [tests/unit/preflight_test.cpp:24](../../tests/unit/preflight_test.cpp#L24) |
| a satisfied requirement is not reported |  |  | [tests/unit/preflight_test.cpp:36](../../tests/unit/preflight_test.cpp#L36) |
| administrator satisfies everything |  |  | [tests/unit/preflight_test.cpp:43](../../tests/unit/preflight_test.cpp#L43) |
| a requirement of nothing is always met |  |  | [tests/unit/preflight_test.cpp:49](../../tests/unit/preflight_test.cpp#L49) |
| permissions are described by name |  | 1 | [tests/unit/preflight_test.cpp:54](../../tests/unit/preflight_test.cpp#L54) |
| commands are found by name and by alias |  |  | [tests/unit/registry_test.cpp:56](../../tests/unit/registry_test.cpp#L56) |
| a duplicate name or alias is refused |  | 4 | [tests/unit/registry_test.cpp:67](../../tests/unit/registry_test.cpp#L67) |
| an empty name is refused |  |  | [tests/unit/registry_test.cpp:94](../../tests/unit/registry_test.cpp#L94) |
| every name and alias gets its own registration payload |  |  | [tests/unit/registry_test.cpp:99](../../tests/unit/registry_test.cpp#L99) |
| required permissions are the union of every command's |  |  | [tests/unit/registry_test.cpp:113](../../tests/unit/registry_test.cpp#L113) |
| dispatch runs the command registered under the name | `coro` |  | [tests/unit/registry_test.cpp:125](../../tests/unit/registry_test.cpp#L125) |
| an unknown command name is logged, not thrown | `coro` |  | [tests/unit/registry_test.cpp:141](../../tests/unit/registry_test.cpp#L141) |
| an exception from a handler is caught and logged | `coro` |  | [tests/unit/registry_test.cpp:152](../../tests/unit/registry_test.cpp#L152) |
| a subcommand's response flags override only what they name |  |  | [tests/unit/registry_test.cpp:207](../../tests/unit/registry_test.cpp#L207) |
| the subcommand an interaction ran is read as a path |  |  | [tests/unit/registry_test.cpp:219](../../tests/unit/registry_test.cpp#L219) |
| every subcommand a payload offers is listed by path |  |  | [tests/unit/registry_test.cpp:232](../../tests/unit/registry_test.cpp#L232) |
| replies carry the flags configured for the subcommand that ran |  |  | [tests/unit/registry_test.cpp:237](../../tests/unit/registry_test.cpp#L237) |
| response flags that could not work are refused at registration |  | 4 | [tests/unit/registry_test.cpp:252](../../tests/unit/registry_test.cpp#L252) |
| responses are one per line |  |  | [tests/unit/trigger_command_test.cpp:21](../../tests/unit/trigger_command_test.cpp#L21) |
| a leading number and bar sets the weight |  |  | [tests/unit/trigger_command_test.cpp:30](../../tests/unit/trigger_command_test.cpp#L30) |
| a bar that is not a weight stays part of the response |  |  | [tests/unit/trigger_command_test.cpp:40](../../tests/unit/trigger_command_test.cpp#L40) |
| blank lines are skipped |  |  | [tests/unit/trigger_command_test.cpp:51](../../tests/unit/trigger_command_test.cpp#L51) |
| nothing usable parses to nothing |  |  | [tests/unit/trigger_command_test.cpp:59](../../tests/unit/trigger_command_test.cpp#L59) |
| responses round trip through their text form |  |  | [tests/unit/trigger_command_test.cpp:66](../../tests/unit/trigger_command_test.cpp#L66) |
| a trigger describes itself in one line |  | 3 | [tests/unit/trigger_command_test.cpp:76](../../tests/unit/trigger_command_test.cpp#L76) |
| the modal keeps fields it cannot read rather than resetting them |  |  | [tests/unit/trigger_command_test.cpp:103](../../tests/unit/trigger_command_test.cpp#L103) |
| the modal applies the fields it can read |  |  | [tests/unit/trigger_command_test.cpp:125](../../tests/unit/trigger_command_test.cpp#L125) |
| the modal refuses a trigger that could not work |  | 2 | [tests/unit/trigger_command_test.cpp:140](../../tests/unit/trigger_command_test.cpp#L140) |
| the trigger modal fits inside Discord's limits |  |  | [tests/unit/trigger_command_test.cpp:158](../../tests/unit/trigger_command_test.cpp#L158) |
| a long trigger list pages |  |  | [tests/unit/trigger_command_test.cpp:175](../../tests/unit/trigger_command_test.cpp#L175) |
| a trigger that answers bots says so when described |  |  | [tests/unit/trigger_command_test.cpp:198](../../tests/unit/trigger_command_test.cpp#L198) |
| a trigger says when its replies notify or hide previews |  |  | [tests/unit/trigger_command_test.cpp:207](../../tests/unit/trigger_command_test.cpp#L207) |
| the panel offers to change how a trigger's replies are posted |  |  | [tests/unit/trigger_command_test.cpp:217](../../tests/unit/trigger_command_test.cpp#L217) |
| confirming a delete on the first or last page fits, and Cancel keeps the trigger picked |  |  | [tests/unit/trigger_command_test.cpp:257](../../tests/unit/trigger_command_test.cpp#L257) |
| each panel toggle flips one thing and names it for the log |  |  | [tests/unit/trigger_command_test.cpp:295](../../tests/unit/trigger_command_test.cpp#L295) |
| mirrors may be typed on one line or one per line |  |  | [tests/unit/urlrepl_command_test.cpp:53](../../tests/unit/urlrepl_command_test.cpp#L53) |
| the site is reduced to what links are matched by |  |  | [tests/unit/urlrepl_command_test.cpp:66](../../tests/unit/urlrepl_command_test.cpp#L66) |
| a mirror listed twice is kept once, in its first place |  |  | [tests/unit/urlrepl_command_test.cpp:70](../../tests/unit/urlrepl_command_test.cpp#L70) |
| rules that could not work are refused with a reason |  |  | [tests/unit/urlrepl_command_test.cpp:75](../../tests/unit/urlrepl_command_test.cpp#L75) |
| a rule describes itself in one line |  |  | [tests/unit/urlrepl_command_test.cpp:83](../../tests/unit/urlrepl_command_test.cpp#L83) |
| the dry run shows the post and accounts for every link |  |  | [tests/unit/urlrepl_command_test.cpp:92](../../tests/unit/urlrepl_command_test.cpp#L92) |
| the dry run says when the person running it has opted out |  |  | [tests/unit/urlrepl_command_test.cpp:103](../../tests/unit/urlrepl_command_test.cpp#L103) |
| the dry run works while replacement is off, and says that it is |  |  | [tests/unit/urlrepl_command_test.cpp:108](../../tests/unit/urlrepl_command_test.cpp#L108) |
| the dry run says when there is nothing to do |  |  | [tests/unit/urlrepl_command_test.cpp:118](../../tests/unit/urlrepl_command_test.cpp#L118) |
| a dry run of a long message stays under Discord's limit |  |  | [tests/unit/urlrepl_command_test.cpp:124](../../tests/unit/urlrepl_command_test.cpp#L124) |
| an empty list says how to start one |  |  | [tests/unit/urlrepl_command_test.cpp:140](../../tests/unit/urlrepl_command_test.cpp#L140) |
| a long list pages |  |  | [tests/unit/urlrepl_command_test.cpp:147](../../tests/unit/urlrepl_command_test.cpp#L147) |
| the list and the panel say whether replacement is on |  |  | [tests/unit/urlrepl_command_test.cpp:161](../../tests/unit/urlrepl_command_test.cpp#L161) |
| the panel's switch asks for the opposite of what is set |  |  | [tests/unit/urlrepl_command_test.cpp:173](../../tests/unit/urlrepl_command_test.cpp#L173) |
| turning replacement on or off says what changed |  |  | [tests/unit/urlrepl_command_test.cpp:198](../../tests/unit/urlrepl_command_test.cpp#L198) |
| the panel lists a page of rules with a menu to pick one |  |  | [tests/unit/urlrepl_command_test.cpp:217](../../tests/unit/urlrepl_command_test.cpp#L217) |
| picking a rule offers Edit and Delete for it |  |  | [tests/unit/urlrepl_command_test.cpp:233](../../tests/unit/urlrepl_command_test.cpp#L233) |
| confirming a delete on the first or last page fits, and Cancel keeps the rule picked |  |  | [tests/unit/urlrepl_command_test.cpp:251](../../tests/unit/urlrepl_command_test.cpp#L251) |
| the panel follows a rule to the page it sorts onto |  |  | [tests/unit/urlrepl_command_test.cpp:275](../../tests/unit/urlrepl_command_test.cpp#L275) |
| the URL rule modal fits inside Discord's limits |  |  | [tests/unit/urlrepl_command_test.cpp:286](../../tests/unit/urlrepl_command_test.cpp#L286) |
| the commands are registered the way Discord expects |  |  | [tests/unit/urlrepl_command_test.cpp:302](../../tests/unit/urlrepl_command_test.cpp#L302) |

## events

Message pipeline and triggers (`src/core/events`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| a recompute credits an old replacement to whoever posted the link | `coro` |  | [tests/db/backfill_test.cpp:107](../../tests/db/backfill_test.cpp#L107) |
| an old replacement is filed under the site its mirror stood in for | `coro` |  | [tests/db/backfill_test.cpp:136](../../tests/db/backfill_test.cpp#L136) |
| a replacement after somebody else's link is reported, not credited to them | `coro` |  | [tests/db/backfill_test.cpp:152](../../tests/db/backfill_test.cpp#L152) |
| a recompute is safe to run twice | `coro` |  | [tests/db/backfill_test.cpp:165](../../tests/db/backfill_test.cpp#L165) |
| a finished channel is not scanned again unless asked | `coro` |  | [tests/db/backfill_test.cpp:177](../../tests/db/backfill_test.cpp#L177) |
| the walk stops at the start of the range | `coro` |  | [tests/db/backfill_test.cpp:188](../../tests/db/backfill_test.cpp#L188) |
| the end of the range is where paging starts | `coro` |  | [tests/db/backfill_test.cpp:202](../../tests/db/backfill_test.cpp#L202) |
| a replacement the bot recorded itself is not re-attributed | `coro` |  | [tests/db/backfill_test.cpp:212](../../tests/db/backfill_test.cpp#L212) |
| messages in no known format are listed by id | `coro` |  | [tests/db/backfill_test.cpp:229](../../tests/db/backfill_test.cpp#L229) |
| a replacement with nobody to credit still counts its reactions | `coro` |  | [tests/db/backfill_test.cpp:240](../../tests/db/backfill_test.cpp#L240) |
| a channel the bot cannot read is reported and the rest carry on | `coro` |  | [tests/db/backfill_test.cpp:253](../../tests/db/backfill_test.cpp#L253) |
| a failed reaction lookup keeps the counts that were there | `coro` |  | [tests/db/backfill_test.cpp:267](../../tests/db/backfill_test.cpp#L267) |
| one recompute per guild, and it can be cancelled | `coro` |  | [tests/db/backfill_test.cpp:280](../../tests/db/backfill_test.cpp#L280) |
| without any known mirror there is nothing to recognise | `coro` |  | [tests/db/backfill_test.cpp:298](../../tests/db/backfill_test.cpp#L298) |
| a replacement is one link line per link |  |  | [tests/unit/embed_watch_test.cpp:117](../../tests/unit/embed_watch_test.cpp#L117) |
| the failure note names the mirrors that were tried |  |  | [tests/unit/embed_watch_test.cpp:127](../../tests/unit/embed_watch_test.cpp#L127) |
| a failure note turns its own previews off and carries Retry |  |  | [tests/unit/embed_watch_test.cpp:135](../../tests/unit/embed_watch_test.cpp#L135) |
| a working replacement has no button and its previews on |  |  | [tests/unit/embed_watch_test.cpp:149](../../tests/unit/embed_watch_test.cpp#L149) |
| with one link, any preview counts |  |  | [tests/unit/embed_watch_test.cpp:159](../../tests/unit/embed_watch_test.cpp#L159) |
| previews are matched to links by path |  |  | [tests/unit/embed_watch_test.cpp:165](../../tests/unit/embed_watch_test.cpp#L165) |
| several previews of one post do not spill onto the next link |  |  | [tests/unit/embed_watch_test.cpp:174](../../tests/unit/embed_watch_test.cpp#L174) |
| a preview that matches nothing goes to the first link still waiting |  |  | [tests/unit/embed_watch_test.cpp:185](../../tests/unit/embed_watch_test.cpp#L185) |
| a preview on the first try settles the replacement |  |  | [tests/unit/embed_watch_test.cpp:198](../../tests/unit/embed_watch_test.cpp#L198) |
| each mirror gets two tries, then the next one |  | 3 | [tests/unit/embed_watch_test.cpp:213](../../tests/unit/embed_watch_test.cpp#L213) |
| each link in a message is tracked on its own |  |  | [tests/unit/embed_watch_test.cpp:251](../../tests/unit/embed_watch_test.cpp#L251) |
| a preview that arrives before the watch starts is not lost |  |  | [tests/unit/embed_watch_test.cpp:276](../../tests/unit/embed_watch_test.cpp#L276) |
| an early preview is forgotten after a while |  |  | [tests/unit/embed_watch_test.cpp:288](../../tests/unit/embed_watch_test.cpp#L288) |
| previews already on the posted message count |  |  | [tests/unit/embed_watch_test.cpp:298](../../tests/unit/embed_watch_test.cpp#L298) |
| a deleted replacement is no longer followed |  |  | [tests/unit/embed_watch_test.cpp:307](../../tests/unit/embed_watch_test.cpp#L307) |
| Retry uses the rule as it is now, one try per mirror |  | 2 | [tests/unit/embed_watch_test.cpp:321](../../tests/unit/embed_watch_test.cpp#L321) |
| Retry says why there is nothing to retry |  | 5 | [tests/unit/embed_watch_test.cpp:362](../../tests/unit/embed_watch_test.cpp#L362) |
| posting sends the replacement, records it, and turns the original's preview off | `coro` |  | [tests/unit/embed_watch_test.cpp:403](../../tests/unit/embed_watch_test.cpp#L403) |
| a replacement that cannot be posted leaves the original alone | `coro` |  | [tests/unit/embed_watch_test.cpp:434](../../tests/unit/embed_watch_test.cpp#L434) |
| a failure's actions reach Discord | `coro` |  | [tests/unit/embed_watch_test.cpp:448](../../tests/unit/embed_watch_test.cpp#L448) |
| the stage asks for a replacement and lets the message carry on |  |  | [tests/unit/embed_watch_test.cpp:480](../../tests/unit/embed_watch_test.cpp#L480) |
| the stage replaces nothing until the server turns it on |  |  | [tests/unit/embed_watch_test.cpp:496](../../tests/unit/embed_watch_test.cpp#L496) |
| the stage leaves some messages alone |  | 5 | [tests/unit/embed_watch_test.cpp:516](../../tests/unit/embed_watch_test.cpp#L516) |
| a link too long to post is dropped rather than failing the post |  |  | [tests/unit/embed_watch_test.cpp:550](../../tests/unit/embed_watch_test.cpp#L550) |
| a message with a joke and a link gets both |  |  | [tests/unit/embed_watch_test.cpp:562](../../tests/unit/embed_watch_test.cpp#L562) |
| the goodbye phrase is recognised however it is typed |  | 1 | [tests/unit/goodbye_test.cpp:10](../../tests/unit/goodbye_test.cpp#L10) |
| the phrase has to be the whole message |  | 1 | [tests/unit/goodbye_test.cpp:21](../../tests/unit/goodbye_test.cpp#L21) |
| a cleared phrase turns the feature off |  |  | [tests/unit/goodbye_test.cpp:33](../../tests/unit/goodbye_test.cpp#L33) |
| a custom phrase replaces the default |  |  | [tests/unit/goodbye_test.cpp:42](../../tests/unit/goodbye_test.cpp#L42) |
| an empty message never matches a real phrase |  |  | [tests/unit/goodbye_test.cpp:47](../../tests/unit/goodbye_test.cpp#L47) |
| format 1: a copy of the original, sent as a reply |  |  | [tests/unit/legacy_replacements_test.cpp:62](../../tests/unit/legacy_replacements_test.cpp#L62) |
| format 2: webhook mode is counted and skipped |  |  | [tests/unit/legacy_replacements_test.cpp:68](../../tests/unit/legacy_replacements_test.cpp#L68) |
| format 3: a copy of the original as a plain message |  |  | [tests/unit/legacy_replacements_test.cpp:79](../../tests/unit/legacy_replacements_test.cpp#L79) |
| format 4: a dot linking to the mirror |  |  | [tests/unit/legacy_replacements_test.cpp:83](../../tests/unit/legacy_replacements_test.cpp#L83) |
| format 5: the link emoji and a dot |  |  | [tests/unit/legacy_replacements_test.cpp:87](../../tests/unit/legacy_replacements_test.cpp#L87) |
| format 6: the link emoji and an underscore, which is still the format |  |  | [tests/unit/legacy_replacements_test.cpp:92](../../tests/unit/legacy_replacements_test.cpp#L92) |
| the mirror links are collected whatever the format |  |  | [tests/unit/legacy_replacements_test.cpp:100](../../tests/unit/legacy_replacements_test.cpp#L100) |
| only the bot's messages with a known mirror count |  |  | [tests/unit/legacy_replacements_test.cpp:110](../../tests/unit/legacy_replacements_test.cpp#L110) |
| a shape nobody wrote down is reported, not guessed at |  |  | [tests/unit/legacy_replacements_test.cpp:121](../../tests/unit/legacy_replacements_test.cpp#L121) |
| a reply names its original |  | 2 | [tests/unit/legacy_replacements_test.cpp:137](../../tests/unit/legacy_replacements_test.cpp#L137) |
| the original is the nearest earlier link, past any chat |  |  | [tests/unit/legacy_replacements_test.cpp:159](../../tests/unit/legacy_replacements_test.cpp#L159) |
| a nearer link that is not ours does not take the credit |  |  | [tests/unit/legacy_replacements_test.cpp:173](../../tests/unit/legacy_replacements_test.cpp#L173) |
| with no matching link the replacement stays unattributed |  | 3 | [tests/unit/legacy_replacements_test.cpp:186](../../tests/unit/legacy_replacements_test.cpp#L186) |
| other bots' links are never the original |  |  | [tests/unit/legacy_replacements_test.cpp:213](../../tests/unit/legacy_replacements_test.cpp#L213) |
| a front-page link proves nothing about which message was answered |  |  | [tests/unit/legacy_replacements_test.cpp:222](../../tests/unit/legacy_replacements_test.cpp#L222) |
| a message's time comes from its id |  |  | [tests/unit/legacy_replacements_test.cpp:232](../../tests/unit/legacy_replacements_test.cpp#L232) |
| stages run in the order they were added |  |  | [tests/unit/message_pipeline_test.cpp:55](../../tests/unit/message_pipeline_test.cpp#L55) |
| a stage that consumes the message stops the ones after it |  |  | [tests/unit/message_pipeline_test.cpp:68](../../tests/unit/message_pipeline_test.cpp#L68) |
| the bot never answers itself, or a bot this guild has not allowed |  | 3 | [tests/unit/message_pipeline_test.cpp:83](../../tests/unit/message_pipeline_test.cpp#L83) |
| an allowed bot reaches the stages |  |  | [tests/unit/message_pipeline_test.cpp:115](../../tests/unit/message_pipeline_test.cpp#L115) |
| a stage that throws is logged and the rest still run |  |  | [tests/unit/message_pipeline_test.cpp:130](../../tests/unit/message_pipeline_test.cpp#L130) |
| an empty pipeline decides nothing |  |  | [tests/unit/message_pipeline_test.cpp:147](../../tests/unit/message_pipeline_test.cpp#L147) |
| the local date is the one where the entry lives, not where the bot runs |  |  | [tests/unit/midnight_test.cpp:41](../../tests/unit/midnight_test.cpp#L41) |
| a zone this machine does not know is refused rather than guessed at |  |  | [tests/unit/midnight_test.cpp:51](../../tests/unit/midnight_test.cpp#L51) |
| an entry fires just after local midnight |  |  | [tests/unit/midnight_test.cpp:62](../../tests/unit/midnight_test.cpp#L62) |
| an entry that has posted today does not post again |  |  | [tests/unit/midnight_test.cpp:72](../../tests/unit/midnight_test.cpp#L72) |
| a midnight the bot was not running for is skipped, not posted late |  |  | [tests/unit/midnight_test.cpp:83](../../tests/unit/midnight_test.cpp#L83) |
| a restart shortly after midnight still posts |  |  | [tests/unit/midnight_test.cpp:99](../../tests/unit/midnight_test.cpp#L99) |
| an entry off is an entry that does not post |  |  | [tests/unit/midnight_test.cpp:111](../../tests/unit/midnight_test.cpp#L111) |
| two entries in different timezones fire at different times |  |  | [tests/unit/midnight_test.cpp:121](../../tests/unit/midnight_test.cpp#L121) |
| an entry added this afternoon waits for the next midnight |  |  | [tests/unit/midnight_test.cpp:134](../../tests/unit/midnight_test.cpp#L134) |
| a spring-forward night still has a midnight to fire at |  |  | [tests/unit/midnight_test.cpp:148](../../tests/unit/midnight_test.cpp#L148) |
| a fall-back night does not post twice |  |  | [tests/unit/midnight_test.cpp:156](../../tests/unit/midnight_test.cpp#L156) |
| a bad timezone in the database keeps quiet rather than posting wrongly |  |  | [tests/unit/midnight_test.cpp:165](../../tests/unit/midnight_test.cpp#L165) |
| timezone completion matches anywhere in the name |  |  | [tests/unit/midnight_test.cpp:176](../../tests/unit/midnight_test.cpp#L176) |
| timezone completion never offers more than it is asked for |  |  | [tests/unit/midnight_test.cpp:183](../../tests/unit/midnight_test.cpp#L183) |
| timezone completion finds nothing for nonsense |  |  | [tests/unit/midnight_test.cpp:190](../../tests/unit/midnight_test.cpp#L190) |
| a winter timestamp is read as Central Standard Time |  |  | [tests/unit/nickname_import_test.cpp:34](../../tests/unit/nickname_import_test.cpp#L34) |
| a summer timestamp is read as Central Daylight Time |  |  | [tests/unit/nickname_import_test.cpp:39](../../tests/unit/nickname_import_test.cpp#L39) |
| the hour that happens twice each November takes the earlier one |  |  | [tests/unit/nickname_import_test.cpp:44](../../tests/unit/nickname_import_test.cpp#L44) |
| the hour that never happens each March is shifted forward |  |  | [tests/unit/nickname_import_test.cpp:51](../../tests/unit/nickname_import_test.cpp#L51) |
| dates before the 2007 rule change use the rules of their own year |  |  | [tests/unit/nickname_import_test.cpp:61](../../tests/unit/nickname_import_test.cpp#L61) |
| a timestamp that is not one is refused rather than guessed at |  |  | [tests/unit/nickname_import_test.cpp:68](../../tests/unit/nickname_import_test.cpp#L68) |
| an imported author is kept only when it is not the guess |  |  | [tests/unit/nickname_import_test.cpp:79](../../tests/unit/nickname_import_test.cpp#L79) |
| a member's entries are read with their guild and id |  |  | [tests/unit/nickname_import_test.cpp:94](../../tests/unit/nickname_import_test.cpp#L94) |
| an imported entry keeps the text its time was read from |  |  | [tests/unit/nickname_import_test.cpp:123](../../tests/unit/nickname_import_test.cpp#L123) |
| a cleared nickname imports as nothing rather than as an empty name |  |  | [tests/unit/nickname_import_test.cpp:140](../../tests/unit/nickname_import_test.cpp#L140) |
| one unreadable entry does not lose the rest |  |  | [tests/unit/nickname_import_test.cpp:154](../../tests/unit/nickname_import_test.cpp#L154) |
| malformed shapes are named rather than dropped quietly |  | 5 | [tests/unit/nickname_import_test.cpp:173](../../tests/unit/nickname_import_test.cpp#L173) |
| an empty file imports nothing and complains about nothing |  |  | [tests/unit/nickname_import_test.cpp:198](../../tests/unit/nickname_import_test.cpp#L198) |
| a first sighting is recorded only when there is a nickname to record |  |  | [tests/unit/nicknames_test.cpp:40](../../tests/unit/nicknames_test.cpp#L40) |
| the same nickname again is not a change |  |  | [tests/unit/nicknames_test.cpp:48](../../tests/unit/nicknames_test.cpp#L48) |
| clearing a nickname is a change |  |  | [tests/unit/nicknames_test.cpp:55](../../tests/unit/nicknames_test.cpp#L55) |
| an audit entry describes a row by member and resulting nickname |  |  | [tests/unit/nicknames_test.cpp:70](../../tests/unit/nicknames_test.cpp#L70) |
| an audit entry can describe a cleared nickname |  |  | [tests/unit/nicknames_test.cpp:79](../../tests/unit/nicknames_test.cpp#L79) |
| the bot is never recorded as the one who made a change |  |  | [tests/unit/nicknames_test.cpp:86](../../tests/unit/nicknames_test.cpp#L86) |
| an audit entry with no actor attributes nothing |  |  | [tests/unit/nicknames_test.cpp:95](../../tests/unit/nicknames_test.cpp#L95) |
| a row that already names somebody is left alone |  |  | [tests/unit/nicknames_test.cpp:99](../../tests/unit/nicknames_test.cpp#L99) |
| an audit entry's nickname arrives as JSON rather than as text |  |  | [tests/unit/nicknames_test.cpp:106](../../tests/unit/nicknames_test.cpp#L106) |
| an unreadable audit value is treated as no nickname |  |  | [tests/unit/nicknames_test.cpp:118](../../tests/unit/nicknames_test.cpp#L118) |
| a change the bot just made is claimed once |  |  | [tests/unit/nicknames_test.cpp:127](../../tests/unit/nicknames_test.cpp#L127) |
| an expectation only matches the change it was made for |  |  | [tests/unit/nicknames_test.cpp:140](../../tests/unit/nicknames_test.cpp#L140) |
| an expectation stops applying once it has expired |  |  | [tests/unit/nicknames_test.cpp:151](../../tests/unit/nicknames_test.cpp#L151) |
| expired expectations are cleared out as new ones arrive |  |  | [tests/unit/nicknames_test.cpp:161](../../tests/unit/nicknames_test.cpp#L161) |
| a change Discord refused stops being expected |  |  | [tests/unit/nicknames_test.cpp:172](../../tests/unit/nicknames_test.cpp#L172) |
| clearing a nickname is expected and claimed like any other change |  |  | [tests/unit/nicknames_test.cpp:182](../../tests/unit/nicknames_test.cpp#L182) |
| a cleared nickname reads as cleared rather than as a blank |  |  | [tests/unit/nicknames_test.cpp:195](../../tests/unit/nicknames_test.cpp#L195) |
| who changed it is a mention, unknown, or nothing |  |  | [tests/unit/nicknames_test.cpp:201](../../tests/unit/nicknames_test.cpp#L201) |
| a history line carries the nickname, the time and the author |  |  | [tests/unit/nicknames_test.cpp:215](../../tests/unit/nicknames_test.cpp#L215) |
| an imported history line says nothing about who |  |  | [tests/unit/nicknames_test.cpp:227](../../tests/unit/nicknames_test.cpp#L227) |
| the attachment spells out times rather than leaving markup in a file |  |  | [tests/unit/nicknames_test.cpp:234](../../tests/unit/nicknames_test.cpp#L234) |
| one entry is not described as one entries |  |  | [tests/unit/nicknames_test.cpp:253](../../tests/unit/nicknames_test.cpp#L253) |
| a reaction is keyed by id when custom, by itself when Unicode |  |  | [tests/unit/reactions_test.cpp:11](../../tests/unit/reactions_test.cpp#L11) |
| the colour-form selector does not make a second emoji |  |  | [tests/unit/reactions_test.cpp:20](../../tests/unit/reactions_test.cpp#L20) |
| typed emojis are understood in every form a command sees |  |  | [tests/unit/reactions_test.cpp:25](../../tests/unit/reactions_test.cpp#L25) |
| an emoji is shown the way Discord draws it |  |  | [tests/unit/reactions_test.cpp:38](../../tests/unit/reactions_test.cpp#L38) |
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
| a link to a site without a rule does not stop the others |  |  | [tests/unit/url_rules_test.cpp:31](../../tests/unit/url_rules_test.cpp#L31) |
| each link picks its mirror on its own |  |  | [tests/unit/url_rules_test.cpp:41](../../tests/unit/url_rules_test.cpp#L41) |
| a mirror index past the end uses the last mirror |  |  | [tests/unit/url_rules_test.cpp:52](../../tests/unit/url_rules_test.cpp#L52) |
| www and letter case do not hide a link from its rule |  |  | [tests/unit/url_rules_test.cpp:58](../../tests/unit/url_rules_test.cpp#L58) |
| the spoiler survives into the plan |  |  | [tests/unit/url_rules_test.cpp:64](../../tests/unit/url_rules_test.cpp#L64) |
| links Discord would not have embedded are left alone |  |  | [tests/unit/url_rules_test.cpp:70](../../tests/unit/url_rules_test.cpp#L70) |
| the same link twice is replaced once |  |  | [tests/unit/url_rules_test.cpp:75](../../tests/unit/url_rules_test.cpp#L75) |
| a message of links is capped |  |  | [tests/unit/url_rules_test.cpp:80](../../tests/unit/url_rules_test.cpp#L80) |
| every link gets a verdict, and the plan is the replaced ones |  |  | [tests/unit/url_rules_test.cpp:88](../../tests/unit/url_rules_test.cpp#L88) |
| no rules means no plan |  |  | [tests/unit/url_rules_test.cpp:111](../../tests/unit/url_rules_test.cpp#L111) |
| a mirror is its host plus an optional suffix |  |  | [tests/unit/url_rules_test.cpp:115](../../tests/unit/url_rules_test.cpp#L115) |
| a typed domain is reduced to its rule host |  |  | [tests/unit/url_rules_test.cpp:123](../../tests/unit/url_rules_test.cpp#L123) |
| the Java rule file is read line by line |  |  | [tests/unit/url_rules_test.cpp:129](../../tests/unit/url_rules_test.cpp#L129) |

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
| applying flags replaces the choosable ones and leaves the rest |  |  | [tests/unit/message_flags_test.cpp:12](../../tests/unit/message_flags_test.cpp#L12) |
| stored message flags are narrowed to what a channel message may carry |  |  | [tests/unit/message_flags_test.cpp:24](../../tests/unit/message_flags_test.cpp#L24) |
| flags are named for the log |  |  | [tests/unit/message_flags_test.cpp:32](../../tests/unit/message_flags_test.cpp#L32) |
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
| uncoloured output is exactly what std::format would give |  |  | [tests/unit/log_color_test.cpp:48](../../tests/unit/log_color_test.cpp#L48) |
| text in the format string stays the terminal's own colour |  |  | [tests/unit/log_color_test.cpp:56](../../tests/unit/log_color_test.cpp#L56) |
| a number is coloured as a number |  |  | [tests/unit/log_color_test.cpp:60](../../tests/unit/log_color_test.cpp#L60) |
| true and false are coloured differently |  |  | [tests/unit/log_color_test.cpp:67](../../tests/unit/log_color_test.cpp#L67) |
| a Discord id is coloured as an id |  |  | [tests/unit/log_color_test.cpp:73](../../tests/unit/log_color_test.cpp#L73) |
| an id already turned into a string is only a string |  |  | [tests/unit/log_color_test.cpp:79](../../tests/unit/log_color_test.cpp#L79) |
| strings and characters stay plain |  |  | [tests/unit/log_color_test.cpp:87](../../tests/unit/log_color_test.cpp#L87) |
| a duration has a colour of its own |  |  | [tests/unit/log_color_test.cpp:94](../../tests/unit/log_color_test.cpp#L94) |
| format specs still apply inside the colour |  |  | [tests/unit/log_color_test.cpp:98](../../tests/unit/log_color_test.cpp#L98) |
| a format colour cannot pass through falls back to a plain line |  |  | [tests/unit/log_color_test.cpp:106](../../tests/unit/log_color_test.cpp#L106) |
| a forwarded line's source tag is the coloured part |  |  | [tests/unit/log_color_test.cpp:113](../../tests/unit/log_color_test.cpp#L113) |
| the default palette is the one that was asked for |  |  | [tests/unit/log_color_test.cpp:122](../../tests/unit/log_color_test.cpp#L122) |
| paint_to colours only while a coloured line is being formatted |  |  | [tests/unit/log_color_test.cpp:137](../../tests/unit/log_color_test.cpp#L137) |
| an uncoloured line is the format the log has always had |  |  | [tests/unit/log_color_test.cpp:160](../../tests/unit/log_color_test.cpp#L160) |
| a coloured line colours the timestamp and the level |  |  | [tests/unit/log_color_test.cpp:166](../../tests/unit/log_color_test.cpp#L166) |
| each level has its own colour |  |  | [tests/unit/log_color_test.cpp:172](../../tests/unit/log_color_test.cpp#L172) |
| the colour setting accepts the obvious spellings |  |  | [tests/unit/log_color_test.cpp:185](../../tests/unit/log_color_test.cpp#L185) |
| colour follows the terminal unless told otherwise |  |  | [tests/unit/log_color_test.cpp:201](../../tests/unit/log_color_test.cpp#L201) |
| NO_COLOR turns colour off, and an explicit always overrides it |  |  | [tests/unit/log_color_test.cpp:207](../../tests/unit/log_color_test.cpp#L207) |
| never and always mean exactly that |  |  | [tests/unit/log_color_test.cpp:213](../../tests/unit/log_color_test.cpp#L213) |
| a replacement sink gets plain text even with colours on |  |  | [tests/unit/log_color_test.cpp:222](../../tests/unit/log_color_test.cpp#L222) |
| colours are off until something turns them on |  |  | [tests/unit/log_color_test.cpp:235](../../tests/unit/log_color_test.cpp#L235) |
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
| every link in a message is found, not just the first |  |  | [tests/unit/url_scan_test.cpp:42](../../tests/unit/url_scan_test.cpp#L42) |
| a spoiler is an odd number of || before the link |  | 4 | [tests/unit/url_scan_test.cpp:52](../../tests/unit/url_scan_test.cpp#L52) |
| trailing punctuation is not part of a link |  |  | [tests/unit/url_scan_test.cpp:86](../../tests/unit/url_scan_test.cpp#L86) |
| a closing bracket stays only when the link opened one |  |  | [tests/unit/url_scan_test.cpp:93](../../tests/unit/url_scan_test.cpp#L93) |
| an underscore at the end of a link is kept |  |  | [tests/unit/url_scan_test.cpp:100](../../tests/unit/url_scan_test.cpp#L100) |
| a link in angle brackets is marked as having its preview turned off |  |  | [tests/unit/url_scan_test.cpp:105](../../tests/unit/url_scan_test.cpp#L105) |
| links in code are marked as code |  | 3 | [tests/unit/url_scan_test.cpp:112](../../tests/unit/url_scan_test.cpp#L112) |
| a scheme glued to a word is not a link |  |  | [tests/unit/url_scan_test.cpp:134](../../tests/unit/url_scan_test.cpp#L134) |
| the scheme may be in any case |  |  | [tests/unit/url_scan_test.cpp:139](../../tests/unit/url_scan_test.cpp#L139) |
| offsets point back into the scanned text |  |  | [tests/unit/url_scan_test.cpp:143](../../tests/unit/url_scan_test.cpp#L143) |
| split_url separates every part |  |  | [tests/unit/url_scan_test.cpp:154](../../tests/unit/url_scan_test.cpp#L154) |
| rule_host reduces a host to what a rule is keyed by |  |  | [tests/unit/url_scan_test.cpp:168](../../tests/unit/url_scan_test.cpp#L168) |
| rehost keeps the path, query and fragment |  |  | [tests/unit/url_scan_test.cpp:176](../../tests/unit/url_scan_test.cpp#L176) |
| a translation suffix goes on the path, before the query |  | 3 | [tests/unit/url_scan_test.cpp:182](../../tests/unit/url_scan_test.cpp#L182) |
| 100 KB of link-shaped junk is scanned quickly |  |  | [tests/unit/url_scan_test.cpp:204](../../tests/unit/url_scan_test.cpp#L204) |
| one link followed by thousands of brackets is still linear |  |  | [tests/unit/url_scan_test.cpp:222](../../tests/unit/url_scan_test.cpp#L222) |
| scanning a typical message |  |  | [tests/unit/url_scan_test.cpp:234](../../tests/unit/url_scan_test.cpp#L234) |
| version string matches the version constants |  |  | [tests/unit/version_test.cpp:7](../../tests/unit/version_test.cpp#L7) |
