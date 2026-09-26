# Test catalog

**Generated** by ``tools/Update-TestCatalog.ps1``. Do not edit by hand;
re-run the script after adding or retagging tests.

See [README.md](README.md) for the strategy, conventions and tag meanings.

541 test cases across 10 components, including 118 sections.

| Component | Test cases | Sections |
|---|---:|---:|
| [db](#db) | 111 | 12 |
| [config](#config) | 23 | 19 |
| [commands](#commands) | 124 | 27 |
| [events](#events) | 150 | 37 |
| [ui](#ui) | 11 | 0 |
| [discord](#discord) | 8 | 0 |
| [audio](#audio) | 40 | 4 |
| [ports](#ports) | 7 | 0 |
| [log](#log) | 28 | 0 |
| [util](#util) | 39 | 19 |

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
| opening an unwritable path reports the SQLite error |  |  | [tests/db/database_test.cpp:46](../../tests/db/database_test.cpp#L46) |
| values survive a bind and get round trip |  |  | [tests/db/database_test.cpp:51](../../tests/db/database_test.cpp#L51) |
| optional values bind as NULL or as the value |  |  | [tests/db/database_test.cpp:78](../../tests/db/database_test.cpp#L78) |
| a constraint violation throws with the SQLite code |  |  | [tests/db/database_test.cpp:91](../../tests/db/database_test.cpp#L91) |
| malformed SQL is reported, not executed |  |  | [tests/db/database_test.cpp:107](../../tests/db/database_test.cpp#L107) |
| a transaction commits or rolls back |  | 3 | [tests/db/database_test.cpp:114](../../tests/db/database_test.cpp#L114) |
| snowflakes and times are stored as the integers they always were |  | 1 | [tests/db/database_test.cpp:151](../../tests/db/database_test.cpp#L151) |
| a rollback that fails is logged rather than thrown |  |  | [tests/db/database_test.cpp:180](../../tests/db/database_test.cpp#L180) |
| last_insert_rowid and changes report the previous statement |  |  | [tests/db/database_test.cpp:193](../../tests/db/database_test.cpp#L193) |
| concurrent writers are serialized by the connection lock | `threads` |  | [tests/db/database_test.cpp:207](../../tests/db/database_test.cpp#L207) |
| an added entry comes back as it went in |  |  | [tests/db/midnight_store_test.cpp:51](../../tests/db/midnight_store_test.cpp#L51) |
| entries belong to one guild |  |  | [tests/db/midnight_store_test.cpp:66](../../tests/db/midnight_store_test.cpp#L66) |
| only enabled entries are looked at on a tick |  |  | [tests/db/midnight_store_test.cpp:78](../../tests/db/midnight_store_test.cpp#L78) |
| a day can only be claimed once |  |  | [tests/db/midnight_store_test.cpp:91](../../tests/db/midnight_store_test.cpp#L91) |
| editing an entry leaves the day it last posted alone |  |  | [tests/db/midnight_store_test.cpp:101](../../tests/db/midnight_store_test.cpp#L101) |
| a tick posts an entry once and then leaves it alone |  |  | [tests/db/midnight_store_test.cpp:116](../../tests/db/midnight_store_test.cpp#L116) |
| an entry that cannot be claimed does not cost the others their post |  |  | [tests/db/midnight_store_test.cpp:142](../../tests/db/midnight_store_test.cpp#L142) |
| a restart moments after posting does not post again |  |  | [tests/db/midnight_store_test.cpp:172](../../tests/db/midnight_store_test.cpp#L172) |
| a night the bot slept through is given up on, not posted at breakfast |  |  | [tests/db/midnight_store_test.cpp:191](../../tests/db/midnight_store_test.cpp#L191) |
| a restart a minute after midnight still posts |  |  | [tests/db/midnight_store_test.cpp:216](../../tests/db/midnight_store_test.cpp#L216) |
| each timezone posts at its own midnight |  |  | [tests/db/midnight_store_test.cpp:230](../../tests/db/midnight_store_test.cpp#L230) |
| a midnight message's flags survive a round trip and default to silent |  |  | [tests/db/midnight_store_test.cpp:249](../../tests/db/midnight_store_test.cpp#L249) |
| a midnight post carries its entry's flags |  |  | [tests/db/midnight_store_test.cpp:266](../../tests/db/midnight_store_test.cpp#L266) |
| a fresh database migrates to the current schema |  |  | [tests/db/migrations_test.cpp:37](../../tests/db/migrations_test.cpp#L37) |
| migrating twice is a no-op |  |  | [tests/db/migrations_test.cpp:50](../../tests/db/migrations_test.cpp#L50) |
| only migrations newer than user_version are applied |  |  | [tests/db/migrations_test.cpp:61](../../tests/db/migrations_test.cpp#L61) |
| a failing migration rolls back and keeps the previous version |  |  | [tests/db/migrations_test.cpp:75](../../tests/db/migrations_test.cpp#L75) |
| a gap in the migration versions is rejected |  |  | [tests/db/migrations_test.cpp:92](../../tests/db/migrations_test.cpp#L92) |
| the shipped schema is append-only and correctly numbered |  |  | [tests/db/migrations_test.cpp:105](../../tests/db/migrations_test.cpp#L105) |
| an existing database gains the allowlist without losing its triggers |  |  | [tests/db/migrations_test.cpp:117](../../tests/db/migrations_test.cpp#L117) |
| an import writes the history it read |  |  | [tests/db/nickname_import_test.cpp:43](../../tests/db/nickname_import_test.cpp#L43) |
| importing the same file twice adds nothing the second time |  |  | [tests/db/nickname_import_test.cpp:57](../../tests/db/nickname_import_test.cpp#L57) |
| an import does not disturb history the bot recorded itself |  |  | [tests/db/nickname_import_test.cpp:69](../../tests/db/nickname_import_test.cpp#L69) |
| a cleared nickname is imported once, not once per run |  |  | [tests/db/nickname_import_test.cpp:90](../../tests/db/nickname_import_test.cpp#L90) |
| no file to import is not a problem | `fs` |  | [tests/db/nickname_import_test.cpp:107](../../tests/db/nickname_import_test.cpp#L107) |
| a file beside the database is read and imported | `fs` |  | [tests/db/nickname_import_test.cpp:114](../../tests/db/nickname_import_test.cpp#L114) |
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
| an imported row keeps the text its timestamp was read from |  |  | [tests/db/nickname_store_test.cpp:203](../../tests/db/nickname_store_test.cpp#L203) |
| only reactions on our replacements are counted |  |  | [tests/db/reaction_store_test.cpp:94](../../tests/db/reaction_store_test.cpp#L94) |
| taking a reaction back removes it |  |  | [tests/db/reaction_store_test.cpp:106](../../tests/db/reaction_store_test.cpp#L106) |
| a moderator clearing reactions clears the counts |  |  | [tests/db/reaction_store_test.cpp:115](../../tests/db/reaction_store_test.cpp#L115) |
| received, given and self-reactions are counted apart |  | 5 | [tests/db/reaction_store_test.cpp:132](../../tests/db/reaction_store_test.cpp#L132) |
| a backfilled reaction is dated by its message |  |  | [tests/db/reaction_store_test.cpp:172](../../tests/db/reaction_store_test.cpp#L172) |
| a live reaction is dated when it was added |  |  | [tests/db/reaction_store_test.cpp:184](../../tests/db/reaction_store_test.cpp#L184) |
| rebuilding a message's reactions is safe to repeat |  | 1 | [tests/db/reaction_store_test.cpp:196](../../tests/db/reaction_store_test.cpp#L196) |
| an alias merges one emoji into another across all history, and can be undone |  |  | [tests/db/reaction_store_test.cpp:222](../../tests/db/reaction_store_test.cpp#L222) |
| alias chains are flattened as they are written |  | 1 | [tests/db/reaction_store_test.cpp:243](../../tests/db/reaction_store_test.cpp#L243) |
| an alias that would loop is refused |  |  | [tests/db/reaction_store_test.cpp:262](../../tests/db/reaction_store_test.cpp#L262) |
| aliases belong to one guild |  |  | [tests/db/reaction_store_test.cpp:270](../../tests/db/reaction_store_test.cpp#L270) |
| emojis are known by name once somebody has used them |  |  | [tests/db/reaction_store_test.cpp:280](../../tests/db/reaction_store_test.cpp#L280) |
| a replacement round-trips with its links in order |  |  | [tests/db/replacement_store_test.cpp:41](../../tests/db/replacement_store_test.cpp#L41) |
| an unattributed replacement stores no author |  |  | [tests/db/replacement_store_test.cpp:61](../../tests/db/replacement_store_test.cpp#L61) |
| state changes and retries are recorded |  |  | [tests/db/replacement_store_test.cpp:74](../../tests/db/replacement_store_test.cpp#L74) |
| recording a replacement again replaces its links |  |  | [tests/db/replacement_store_test.cpp:89](../../tests/db/replacement_store_test.cpp#L89) |
| unsettled replacements are the pending and retrying ones, with their links |  |  | [tests/db/replacement_store_test.cpp:102](../../tests/db/replacement_store_test.cpp#L102) |
| replacement states have stable names |  |  | [tests/db/replacement_store_test.cpp:123](../../tests/db/replacement_store_test.cpp#L123) |
| a trigger survives a round trip with its responses |  |  | [tests/db/trigger_store_test.cpp:62](../../tests/db/trigger_store_test.cpp#L62) |
| each trigger in a guild gets its own responses, in order |  |  | [tests/db/trigger_store_test.cpp:81](../../tests/db/trigger_store_test.cpp#L81) |
| guilds cannot see or change each other's triggers |  |  | [tests/db/trigger_store_test.cpp:108](../../tests/db/trigger_store_test.cpp#L108) |
| updating a trigger replaces its responses rather than adding to them |  |  | [tests/db/trigger_store_test.cpp:125](../../tests/db/trigger_store_test.cpp#L125) |
| removing a trigger takes its responses with it |  |  | [tests/db/trigger_store_test.cpp:141](../../tests/db/trigger_store_test.cpp#L141) |
| the defaults are seeded once per guild |  |  | [tests/db/trigger_store_test.cpp:153](../../tests/db/trigger_store_test.cpp#L153) |
| a matching message gets one of the trigger's responses |  | 1 | [tests/db/trigger_store_test.cpp:164](../../tests/db/trigger_store_test.cpp#L164) |
| a trigger is quiet until its cooldown has passed |  |  | [tests/db/trigger_store_test.cpp:186](../../tests/db/trigger_store_test.cpp#L186) |
| cooldowns are per channel |  |  | [tests/db/trigger_store_test.cpp:203](../../tests/db/trigger_store_test.cpp#L203) |
| messages arriving at once still get one reply per cooldown | `threads` |  | [tests/db/trigger_store_test.cpp:216](../../tests/db/trigger_store_test.cpp#L216) |
| a disabled trigger says nothing |  |  | [tests/db/trigger_store_test.cpp:255](../../tests/db/trigger_store_test.cpp#L255) |
| two triggers on one message both answer |  |  | [tests/db/trigger_store_test.cpp:267](../../tests/db/trigger_store_test.cpp#L267) |
| respond_to_bots survives a round trip and defaults to off |  |  | [tests/db/trigger_store_test.cpp:282](../../tests/db/trigger_store_test.cpp#L282) |
| a trigger only answers an allowed bot when it opts in |  |  | [tests/db/trigger_store_test.cpp:301](../../tests/db/trigger_store_test.cpp#L301) |
| a trigger that answers bots still answers humans |  |  | [tests/db/trigger_store_test.cpp:319](../../tests/db/trigger_store_test.cpp#L319) |
| a trigger's reply flags survive a round trip and default to silent |  |  | [tests/db/trigger_store_test.cpp:332](../../tests/db/trigger_store_test.cpp#L332) |
| triggers from before reply flags existed stay silent |  |  | [tests/db/trigger_store_test.cpp:355](../../tests/db/trigger_store_test.cpp#L355) |
| a trigger's reply carries its flags |  |  | [tests/db/trigger_store_test.cpp:372](../../tests/db/trigger_store_test.cpp#L372) |
| a trigger's reply says which trigger it is from |  |  | [tests/db/trigger_store_test.cpp:386](../../tests/db/trigger_store_test.cpp#L386) |
| a rule comes back with its mirrors in order |  |  | [tests/db/url_rule_store_test.cpp:35](../../tests/db/url_rule_store_test.cpp#L35) |
| setting a rule replaces its mirrors, which is how reordering works |  |  | [tests/db/url_rule_store_test.cpp:48](../../tests/db/url_rule_store_test.cpp#L48) |
| renaming a rule moves it to the new site |  |  | [tests/db/url_rule_store_test.cpp:59](../../tests/db/url_rule_store_test.cpp#L59) |
| a rename that fails leaves the old rule where it was |  |  | [tests/db/url_rule_store_test.cpp:72](../../tests/db/url_rule_store_test.cpp#L72) |
| rules belong to one guild |  |  | [tests/db/url_rule_store_test.cpp:91](../../tests/db/url_rule_store_test.cpp#L91) |
| removing a rule says whether there was one |  |  | [tests/db/url_rule_store_test.cpp:104](../../tests/db/url_rule_store_test.cpp#L104) |
| a mirror is remembered after its rule is gone |  |  | [tests/db/url_rule_store_test.cpp:113](../../tests/db/url_rule_store_test.cpp#L113) |
| an opt-out toggles, and is kept per guild |  |  | [tests/db/url_rule_store_test.cpp:125](../../tests/db/url_rule_store_test.cpp#L125) |
| replacement is off in a guild until it is turned on, per guild |  |  | [tests/db/url_rule_store_test.cpp:137](../../tests/db/url_rule_store_test.cpp#L137) |
| turning replacement on outlasts a restart | `fs` |  | [tests/db/url_rule_store_test.cpp:152](../../tests/db/url_rule_store_test.cpp#L152) |
| the Java rule file imports once, and never over an existing rule | `fs` |  | [tests/db/url_rule_store_test.cpp:169](../../tests/db/url_rule_store_test.cpp#L169) |
| a missing rule file is not an error | `fs` |  | [tests/db/url_rule_store_test.cpp:188](../../tests/db/url_rule_store_test.cpp#L188) |

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
| a trusted ID that is not exactly an ID stops startup |  |  | [tests/unit/bootstrap_test.cpp:99](../../tests/unit/bootstrap_test.cpp#L99) |
| bad config is reported with the key that caused it |  | 6 | [tests/unit/bootstrap_test.cpp:113](../../tests/unit/bootstrap_test.cpp#L113) |
| nickname tracking is on unless the config turns it off |  |  | [tests/unit/bootstrap_test.cpp:143](../../tests/unit/bootstrap_test.cpp#L143) |
| the log level is read from the config |  |  | [tests/unit/bootstrap_test.cpp:154](../../tests/unit/bootstrap_test.cpp#L154) |
| the build's default log level matches the build |  |  | [tests/unit/bootstrap_test.cpp:162](../../tests/unit/bootstrap_test.cpp#L162) |
| trust needs a listed user, or an admin in a listed server |  | 5 | [tests/unit/bootstrap_test.cpp:170](../../tests/unit/bootstrap_test.cpp#L170) |
| secrets come from the environment |  | 3 | [tests/unit/bootstrap_test.cpp:204](../../tests/unit/bootstrap_test.cpp#L204) |
| the recompute bot override is read by debug builds only |  | 4 | [tests/unit/bootstrap_test.cpp:231](../../tests/unit/bootstrap_test.cpp#L231) |
| loading the configuration applies the recompute bot override as the build allows | `fs` |  | [tests/unit/bootstrap_test.cpp:258](../../tests/unit/bootstrap_test.cpp#L258) |

## commands

Command framework (`src/core/commands`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| joining follows the target and moves only when it has to |  | 4 | [tests/unit/basic_commands_test.cpp:28](../../tests/unit/basic_commands_test.cpp#L28) |
| a status is kept for the next start, and none is kept until one is set |  |  | [tests/unit/basic_commands_test.cpp:53](../../tests/unit/basic_commands_test.cpp#L53) |
| joining says whom it followed, as the Java bot did |  |  | [tests/unit/basic_commands_test.cpp:75](../../tests/unit/basic_commands_test.cpp#L75) |
| a target who left voice is not followed to their old channel |  |  | [tests/unit/basic_commands_test.cpp:81](../../tests/unit/basic_commands_test.cpp#L81) |
| say refuses a message that is only whitespace |  |  | [tests/unit/basic_commands_test.cpp:89](../../tests/unit/basic_commands_test.cpp#L89) |
| say replies only when given a message id |  | 6 | [tests/unit/basic_commands_test.cpp:96](../../tests/unit/basic_commands_test.cpp#L96) |
| status types are matched case-insensitively and fall back to playing |  | 1 | [tests/unit/basic_commands_test.cpp:131](../../tests/unit/basic_commands_test.cpp#L131) |
| a custom status carries its text in state, not name |  |  | [tests/unit/basic_commands_test.cpp:147](../../tests/unit/basic_commands_test.cpp#L147) |
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
| each option is read as the type it was declared with |  |  | [tests/unit/command_options_test.cpp:15](../../tests/unit/command_options_test.cpp#L15) |
| an option left out reads as nothing, not as false or zero |  |  | [tests/unit/command_options_test.cpp:26](../../tests/unit/command_options_test.cpp#L26) |
| an option of another type reads as nothing |  |  | [tests/unit/command_options_test.cpp:37](../../tests/unit/command_options_test.cpp#L37) |
| an invoker Discord sent no permissions for has none |  |  | [tests/unit/command_options_test.cpp:42](../../tests/unit/command_options_test.cpp#L42) |
| every command's response flags pass registration |  |  | [tests/unit/command_responses_test.cpp:52](../../tests/unit/command_responses_test.cpp#L52) |
| the views meant for the room are public and the rest are private |  |  | [tests/unit/command_responses_test.cpp:74](../../tests/unit/command_responses_test.cpp#L74) |
| the URL dry run hides the previews of the links it shows |  |  | [tests/unit/command_responses_test.cpp:113](../../tests/unit/command_responses_test.cpp#L113) |
| the silent and previews options change only what they are given |  |  | [tests/unit/command_responses_test.cpp:125](../../tests/unit/command_responses_test.cpp#L125) |
| only a difference from silent with previews is worth describing |  |  | [tests/unit/command_responses_test.cpp:146](../../tests/unit/command_responses_test.cpp#L146) |
| changing aliases and recomputing need Manage Server, and reading does not |  |  | [tests/unit/linkstats_command_test.cpp:58](../../tests/unit/linkstats_command_test.cpp#L58) |
| custom emojis that share a name are listed as likely duplicates |  |  | [tests/unit/linkstats_command_test.cpp:86](../../tests/unit/linkstats_command_test.cpp#L86) |
| aliases are listed as what counts as what, within Discord's limit |  |  | [tests/unit/linkstats_command_test.cpp:98](../../tests/unit/linkstats_command_test.cpp#L98) |
| dates are read as YYYY-MM-DD and must exist |  |  | [tests/unit/linkstats_command_test.cpp:119](../../tests/unit/linkstats_command_test.cpp#L119) |
| the leaderboard names people without pinging them |  |  | [tests/unit/linkstats_command_test.cpp:128](../../tests/unit/linkstats_command_test.cpp#L128) |
| the emoji leaderboard shows emojis rather than people |  |  | [tests/unit/linkstats_command_test.cpp:138](../../tests/unit/linkstats_command_test.cpp#L138) |
| an empty leaderboard says how to fill it |  |  | [tests/unit/linkstats_command_test.cpp:147](../../tests/unit/linkstats_command_test.cpp#L147) |
| a profile shows received, given and self apart |  |  | [tests/unit/linkstats_command_test.cpp:154](../../tests/unit/linkstats_command_test.cpp#L154) |
| a date range shows in the title as it was typed |  |  | [tests/unit/linkstats_command_test.cpp:163](../../tests/unit/linkstats_command_test.cpp#L163) |
| an emoji can be named rather than drawn |  |  | [tests/unit/linkstats_command_test.cpp:172](../../tests/unit/linkstats_command_test.cpp#L172) |
| link stats are open to everyone, with aliases in a group |  |  | [tests/unit/linkstats_command_test.cpp:188](../../tests/unit/linkstats_command_test.cpp#L188) |
| the longest site filter still leaves room for a board's paging |  |  | [tests/unit/linkstats_command_test.cpp:201](../../tests/unit/linkstats_command_test.cpp#L201) |
| a recompute's report says what it found and what it could not read |  | 2 | [tests/unit/linkstats_command_test.cpp:227](../../tests/unit/linkstats_command_test.cpp#L227) |
| recompute is its own group, with a required start date |  |  | [tests/unit/linkstats_command_test.cpp:269](../../tests/unit/linkstats_command_test.cpp#L269) |
| a long leaderboard pages, and every page is the same board |  |  | [tests/unit/linkstats_command_test.cpp:288](../../tests/unit/linkstats_command_test.cpp#L288) |
| a board's filters survive the trip through a button |  |  | [tests/unit/linkstats_command_test.cpp:329](../../tests/unit/linkstats_command_test.cpp#L329) |
| a board can be limited to one site |  |  | [tests/unit/linkstats_command_test.cpp:354](../../tests/unit/linkstats_command_test.cpp#L354) |
| what a leaderboard ranks is read from its option |  |  | [tests/unit/linkstats_command_test.cpp:377](../../tests/unit/linkstats_command_test.cpp#L377) |
| an empty list says how to add one |  |  | [tests/unit/midnight_command_test.cpp:26](../../tests/unit/midnight_command_test.cpp#L26) |
| a listed entry names its channel, zone and message |  |  | [tests/unit/midnight_command_test.cpp:30](../../tests/unit/midnight_command_test.cpp#L30) |
| an entry that is off says so |  |  | [tests/unit/midnight_command_test.cpp:39](../../tests/unit/midnight_command_test.cpp#L39) |
| an entry that has posted says when |  |  | [tests/unit/midnight_command_test.cpp:49](../../tests/unit/midnight_command_test.cpp#L49) |
| every entry appears in the list |  |  | [tests/unit/midnight_command_test.cpp:59](../../tests/unit/midnight_command_test.cpp#L59) |
| an entry that notifies or hides previews says so |  |  | [tests/unit/midnight_command_test.cpp:69](../../tests/unit/midnight_command_test.cpp#L69) |
| an empty history says so rather than showing an empty page |  |  | [tests/unit/nickname_command_test.cpp:38](../../tests/unit/nickname_command_test.cpp#L38) |
| a history page shows its entries and where it is |  |  | [tests/unit/nickname_command_test.cpp:47](../../tests/unit/nickname_command_test.cpp#L47) |
| a long history pages, and the buttons remember whose it is |  |  | [tests/unit/nickname_command_test.cpp:57](../../tests/unit/nickname_command_test.cpp#L57) |
| a page number from a stale button is brought back in range |  |  | [tests/unit/nickname_command_test.cpp:81](../../tests/unit/nickname_command_test.cpp#L81) |
| a history is posted for the room, not just for whoever asked |  |  | [tests/unit/nickname_command_test.cpp:90](../../tests/unit/nickname_command_test.cpp#L90) |
| a history reply cannot ping the people it names |  |  | [tests/unit/nickname_command_test.cpp:97](../../tests/unit/nickname_command_test.cpp#L97) |
| only the missing bits of a requirement are reported |  |  | [tests/unit/preflight_test.cpp:24](../../tests/unit/preflight_test.cpp#L24) |
| a satisfied requirement is not reported |  |  | [tests/unit/preflight_test.cpp:36](../../tests/unit/preflight_test.cpp#L36) |
| administrator satisfies everything |  |  | [tests/unit/preflight_test.cpp:43](../../tests/unit/preflight_test.cpp#L43) |
| a requirement of nothing is always met |  |  | [tests/unit/preflight_test.cpp:49](../../tests/unit/preflight_test.cpp#L49) |
| permissions are described by name |  | 1 | [tests/unit/preflight_test.cpp:54](../../tests/unit/preflight_test.cpp#L54) |
| commands are found by name and by alias |  |  | [tests/unit/registry_test.cpp:54](../../tests/unit/registry_test.cpp#L54) |
| a duplicate name or alias is refused |  | 4 | [tests/unit/registry_test.cpp:65](../../tests/unit/registry_test.cpp#L65) |
| an empty name is refused |  |  | [tests/unit/registry_test.cpp:89](../../tests/unit/registry_test.cpp#L89) |
| every name and alias gets its own registration payload |  |  | [tests/unit/registry_test.cpp:94](../../tests/unit/registry_test.cpp#L94) |
| required permissions are the union of every command's |  |  | [tests/unit/registry_test.cpp:108](../../tests/unit/registry_test.cpp#L108) |
| dispatch runs the command registered under the name | `coro` |  | [tests/unit/registry_test.cpp:120](../../tests/unit/registry_test.cpp#L120) |
| an unknown command name is logged, not thrown | `coro` |  | [tests/unit/registry_test.cpp:136](../../tests/unit/registry_test.cpp#L136) |
| an exception from a handler is caught and logged | `coro` |  | [tests/unit/registry_test.cpp:147](../../tests/unit/registry_test.cpp#L147) |
| a subcommand's response flags override only what they name |  |  | [tests/unit/registry_test.cpp:202](../../tests/unit/registry_test.cpp#L202) |
| the subcommand an interaction ran is read as a path |  |  | [tests/unit/registry_test.cpp:214](../../tests/unit/registry_test.cpp#L214) |
| every subcommand a payload offers is listed by path |  |  | [tests/unit/registry_test.cpp:227](../../tests/unit/registry_test.cpp#L227) |
| replies carry the flags configured for the subcommand that ran |  |  | [tests/unit/registry_test.cpp:232](../../tests/unit/registry_test.cpp#L232) |
| response flags that could not work are refused at registration |  | 4 | [tests/unit/registry_test.cpp:247](../../tests/unit/registry_test.cpp#L247) |
| responses are one per line |  |  | [tests/unit/trigger_command_test.cpp:24](../../tests/unit/trigger_command_test.cpp#L24) |
| a leading number and bar sets the weight |  |  | [tests/unit/trigger_command_test.cpp:33](../../tests/unit/trigger_command_test.cpp#L33) |
| a bar that is not a weight stays part of the response |  |  | [tests/unit/trigger_command_test.cpp:43](../../tests/unit/trigger_command_test.cpp#L43) |
| blank lines are skipped |  |  | [tests/unit/trigger_command_test.cpp:54](../../tests/unit/trigger_command_test.cpp#L54) |
| nothing usable parses to nothing |  |  | [tests/unit/trigger_command_test.cpp:62](../../tests/unit/trigger_command_test.cpp#L62) |
| responses round trip through their text form |  |  | [tests/unit/trigger_command_test.cpp:69](../../tests/unit/trigger_command_test.cpp#L69) |
| a trigger describes itself in one line |  | 3 | [tests/unit/trigger_command_test.cpp:79](../../tests/unit/trigger_command_test.cpp#L79) |
| the modal keeps fields it cannot read rather than resetting them |  |  | [tests/unit/trigger_command_test.cpp:106](../../tests/unit/trigger_command_test.cpp#L106) |
| the modal applies the fields it can read |  |  | [tests/unit/trigger_command_test.cpp:128](../../tests/unit/trigger_command_test.cpp#L128) |
| the modal refuses a trigger that could not work |  | 2 | [tests/unit/trigger_command_test.cpp:143](../../tests/unit/trigger_command_test.cpp#L143) |
| the trigger modal fits inside Discord's limits |  |  | [tests/unit/trigger_command_test.cpp:161](../../tests/unit/trigger_command_test.cpp#L161) |
| a long trigger list pages |  |  | [tests/unit/trigger_command_test.cpp:178](../../tests/unit/trigger_command_test.cpp#L178) |
| a trigger that answers bots says so when described |  |  | [tests/unit/trigger_command_test.cpp:201](../../tests/unit/trigger_command_test.cpp#L201) |
| a trigger says when its replies notify or hide previews |  |  | [tests/unit/trigger_command_test.cpp:210](../../tests/unit/trigger_command_test.cpp#L210) |
| the panel offers to change how a trigger's replies are posted |  |  | [tests/unit/trigger_command_test.cpp:220](../../tests/unit/trigger_command_test.cpp#L220) |
| confirming a delete on the first or last page fits, and Cancel keeps the trigger picked |  |  | [tests/unit/trigger_command_test.cpp:260](../../tests/unit/trigger_command_test.cpp#L260) |
| the longest pattern the command takes still fits the panel |  |  | [tests/unit/trigger_command_test.cpp:296](../../tests/unit/trigger_command_test.cpp#L296) |
| the trigger modal takes no more than the command does |  |  | [tests/unit/trigger_command_test.cpp:319](../../tests/unit/trigger_command_test.cpp#L319) |
| each panel toggle flips one thing and names it for the log |  |  | [tests/unit/trigger_command_test.cpp:336](../../tests/unit/trigger_command_test.cpp#L336) |
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
| turning replacement on or off says what changed |  |  | [tests/unit/urlrepl_command_test.cpp:196](../../tests/unit/urlrepl_command_test.cpp#L196) |
| the panel lists a page of rules with a menu to pick one |  |  | [tests/unit/urlrepl_command_test.cpp:215](../../tests/unit/urlrepl_command_test.cpp#L215) |
| picking a rule offers Edit and Delete for it |  |  | [tests/unit/urlrepl_command_test.cpp:231](../../tests/unit/urlrepl_command_test.cpp#L231) |
| confirming a delete on the first or last page fits, and Cancel keeps the rule picked |  |  | [tests/unit/urlrepl_command_test.cpp:249](../../tests/unit/urlrepl_command_test.cpp#L249) |
| the panel follows a rule to the page it sorts onto |  |  | [tests/unit/urlrepl_command_test.cpp:273](../../tests/unit/urlrepl_command_test.cpp#L273) |
| the URL rule modal fits inside Discord's limits |  |  | [tests/unit/urlrepl_command_test.cpp:284](../../tests/unit/urlrepl_command_test.cpp#L284) |
| anyone may opt themselves out, and only Manage Server may for somebody else |  |  | [tests/unit/urlrepl_command_test.cpp:300](../../tests/unit/urlrepl_command_test.cpp#L300) |
| the commands are registered the way Discord expects |  |  | [tests/unit/urlrepl_command_test.cpp:314](../../tests/unit/urlrepl_command_test.cpp#L314) |

## events

Messages, replacements, reactions, nicknames and midnight (`src/core/events`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| a recompute credits an old replacement to whoever posted the link | `coro` |  | [tests/db/backfill_test.cpp:105](../../tests/db/backfill_test.cpp#L105) |
| an old replacement is filed under the site its mirror stood in for | `coro` |  | [tests/db/backfill_test.cpp:134](../../tests/db/backfill_test.cpp#L134) |
| a replacement after somebody else's link is reported, not credited to them | `coro` |  | [tests/db/backfill_test.cpp:150](../../tests/db/backfill_test.cpp#L150) |
| a recompute is safe to run twice | `coro` |  | [tests/db/backfill_test.cpp:163](../../tests/db/backfill_test.cpp#L163) |
| a finished channel is not scanned again unless asked | `coro` |  | [tests/db/backfill_test.cpp:175](../../tests/db/backfill_test.cpp#L175) |
| the walk stops at the start of the range | `coro` |  | [tests/db/backfill_test.cpp:186](../../tests/db/backfill_test.cpp#L186) |
| the end of the range is where paging starts | `coro` |  | [tests/db/backfill_test.cpp:200](../../tests/db/backfill_test.cpp#L200) |
| a replacement the bot recorded itself is not re-attributed | `coro` |  | [tests/db/backfill_test.cpp:210](../../tests/db/backfill_test.cpp#L210) |
| messages in no known format are listed by id | `coro` |  | [tests/db/backfill_test.cpp:227](../../tests/db/backfill_test.cpp#L227) |
| a replacement with nobody to credit still counts its reactions | `coro` |  | [tests/db/backfill_test.cpp:238](../../tests/db/backfill_test.cpp#L238) |
| a channel the bot cannot read is reported and the rest carry on | `coro` |  | [tests/db/backfill_test.cpp:251](../../tests/db/backfill_test.cpp#L251) |
| a failed reaction lookup keeps the counts that were there | `coro` |  | [tests/db/backfill_test.cpp:265](../../tests/db/backfill_test.cpp#L265) |
| one recompute per guild, and it can be cancelled | `coro` |  | [tests/db/backfill_test.cpp:278](../../tests/db/backfill_test.cpp#L278) |
| without any known mirror there is nothing to recognise | `coro` |  | [tests/db/backfill_test.cpp:296](../../tests/db/backfill_test.cpp#L296) |
| a replacement is one link line per link |  |  | [tests/unit/embed_watch_test.cpp:120](../../tests/unit/embed_watch_test.cpp#L120) |
| the failure note names the mirrors that were tried |  |  | [tests/unit/embed_watch_test.cpp:130](../../tests/unit/embed_watch_test.cpp#L130) |
| a failure note turns its own previews off and carries Retry |  |  | [tests/unit/embed_watch_test.cpp:138](../../tests/unit/embed_watch_test.cpp#L138) |
| a working replacement has no button and its previews on |  |  | [tests/unit/embed_watch_test.cpp:152](../../tests/unit/embed_watch_test.cpp#L152) |
| with one link, any preview counts |  |  | [tests/unit/embed_watch_test.cpp:162](../../tests/unit/embed_watch_test.cpp#L162) |
| previews are matched to links by path |  |  | [tests/unit/embed_watch_test.cpp:168](../../tests/unit/embed_watch_test.cpp#L168) |
| several previews of one post do not spill onto the next link |  |  | [tests/unit/embed_watch_test.cpp:177](../../tests/unit/embed_watch_test.cpp#L177) |
| a preview that matches nothing goes to the first link still waiting |  |  | [tests/unit/embed_watch_test.cpp:188](../../tests/unit/embed_watch_test.cpp#L188) |
| a preview on the first try settles the replacement |  |  | [tests/unit/embed_watch_test.cpp:201](../../tests/unit/embed_watch_test.cpp#L201) |
| each mirror gets two tries, then the next one |  | 3 | [tests/unit/embed_watch_test.cpp:216](../../tests/unit/embed_watch_test.cpp#L216) |
| a watch whose ending cannot be recorded waits, and the others still finish |  |  | [tests/unit/embed_watch_test.cpp:254](../../tests/unit/embed_watch_test.cpp#L254) |
| each link in a message is tracked on its own |  |  | [tests/unit/embed_watch_test.cpp:307](../../tests/unit/embed_watch_test.cpp#L307) |
| a preview that arrives before the watch starts is not lost |  |  | [tests/unit/embed_watch_test.cpp:332](../../tests/unit/embed_watch_test.cpp#L332) |
| an early preview is forgotten after a while |  |  | [tests/unit/embed_watch_test.cpp:344](../../tests/unit/embed_watch_test.cpp#L344) |
| previews already on the posted message count |  |  | [tests/unit/embed_watch_test.cpp:354](../../tests/unit/embed_watch_test.cpp#L354) |
| a deleted replacement is no longer followed |  |  | [tests/unit/embed_watch_test.cpp:363](../../tests/unit/embed_watch_test.cpp#L363) |
| Retry uses the rule as it is now, one try per mirror |  | 2 | [tests/unit/embed_watch_test.cpp:377](../../tests/unit/embed_watch_test.cpp#L377) |
| Retry says why there is nothing to retry |  | 5 | [tests/unit/embed_watch_test.cpp:418](../../tests/unit/embed_watch_test.cpp#L418) |
| posting sends the replacement, records it, and turns the original's preview off | `coro` |  | [tests/unit/embed_watch_test.cpp:459](../../tests/unit/embed_watch_test.cpp#L459) |
| a replacement that cannot be posted leaves the original alone | `coro` |  | [tests/unit/embed_watch_test.cpp:490](../../tests/unit/embed_watch_test.cpp#L490) |
| a failure's actions reach Discord | `coro` |  | [tests/unit/embed_watch_test.cpp:504](../../tests/unit/embed_watch_test.cpp#L504) |
| a replacement stranded without a preview gets its note, and the original's preview back | `coro` |  | [tests/unit/embed_watch_test.cpp:543](../../tests/unit/embed_watch_test.cpp#L543) |
| a replacement stranded after its preview appeared is simply marked working | `coro` |  | [tests/unit/embed_watch_test.cpp:567](../../tests/unit/embed_watch_test.cpp#L567) |
| a Retry a restart cut off ends as a Retry would | `coro` | 2 | [tests/unit/embed_watch_test.cpp:580](../../tests/unit/embed_watch_test.cpp#L580) |
| a stranded replacement that is gone is marked failed, and one Discord will not show yet waits | `coro` | 2 | [tests/unit/embed_watch_test.cpp:606](../../tests/unit/embed_watch_test.cpp#L606) |
| the stage asks for a replacement and lets the message carry on |  |  | [tests/unit/embed_watch_test.cpp:643](../../tests/unit/embed_watch_test.cpp#L643) |
| the stage replaces nothing until the server turns it on |  |  | [tests/unit/embed_watch_test.cpp:659](../../tests/unit/embed_watch_test.cpp#L659) |
| the stage leaves some messages alone |  | 5 | [tests/unit/embed_watch_test.cpp:679](../../tests/unit/embed_watch_test.cpp#L679) |
| a link too long to post is dropped rather than failing the post |  |  | [tests/unit/embed_watch_test.cpp:713](../../tests/unit/embed_watch_test.cpp#L713) |
| a message with a joke and a link gets both |  |  | [tests/unit/embed_watch_test.cpp:725](../../tests/unit/embed_watch_test.cpp#L725) |
| the goodbye phrase is recognised however it is typed |  | 1 | [tests/unit/goodbye_test.cpp:10](../../tests/unit/goodbye_test.cpp#L10) |
| the phrase has to be the whole message |  | 1 | [tests/unit/goodbye_test.cpp:21](../../tests/unit/goodbye_test.cpp#L21) |
| a cleared phrase turns the feature off |  |  | [tests/unit/goodbye_test.cpp:33](../../tests/unit/goodbye_test.cpp#L33) |
| a custom phrase replaces the default |  |  | [tests/unit/goodbye_test.cpp:42](../../tests/unit/goodbye_test.cpp#L42) |
| an empty message never matches a real phrase |  |  | [tests/unit/goodbye_test.cpp:47](../../tests/unit/goodbye_test.cpp#L47) |
| format 1: a copy of the original, sent as a reply |  |  | [tests/unit/legacy_replacements_test.cpp:67](../../tests/unit/legacy_replacements_test.cpp#L67) |
| format 2: webhook mode is counted and skipped |  |  | [tests/unit/legacy_replacements_test.cpp:73](../../tests/unit/legacy_replacements_test.cpp#L73) |
| format 3: a copy of the original as a plain message |  |  | [tests/unit/legacy_replacements_test.cpp:84](../../tests/unit/legacy_replacements_test.cpp#L84) |
| format 4: a dot linking to the mirror |  |  | [tests/unit/legacy_replacements_test.cpp:88](../../tests/unit/legacy_replacements_test.cpp#L88) |
| format 5: the link emoji and a dot |  |  | [tests/unit/legacy_replacements_test.cpp:92](../../tests/unit/legacy_replacements_test.cpp#L92) |
| format 6: the link emoji and an underscore, which is still the format |  |  | [tests/unit/legacy_replacements_test.cpp:97](../../tests/unit/legacy_replacements_test.cpp#L97) |
| the mirror links are collected whatever the format |  |  | [tests/unit/legacy_replacements_test.cpp:105](../../tests/unit/legacy_replacements_test.cpp#L105) |
| only the bot's messages with a known mirror count |  |  | [tests/unit/legacy_replacements_test.cpp:115](../../tests/unit/legacy_replacements_test.cpp#L115) |
| a shape nobody wrote down is reported, not guessed at |  |  | [tests/unit/legacy_replacements_test.cpp:126](../../tests/unit/legacy_replacements_test.cpp#L126) |
| a reply names its original |  | 2 | [tests/unit/legacy_replacements_test.cpp:142](../../tests/unit/legacy_replacements_test.cpp#L142) |
| the original is the nearest earlier link, past any chat |  |  | [tests/unit/legacy_replacements_test.cpp:164](../../tests/unit/legacy_replacements_test.cpp#L164) |
| a nearer link that is not ours does not take the credit |  |  | [tests/unit/legacy_replacements_test.cpp:178](../../tests/unit/legacy_replacements_test.cpp#L178) |
| with no matching link the replacement stays unattributed |  | 3 | [tests/unit/legacy_replacements_test.cpp:191](../../tests/unit/legacy_replacements_test.cpp#L191) |
| other bots' links are never the original |  |  | [tests/unit/legacy_replacements_test.cpp:219](../../tests/unit/legacy_replacements_test.cpp#L219) |
| a front-page link proves nothing about which message was answered |  |  | [tests/unit/legacy_replacements_test.cpp:228](../../tests/unit/legacy_replacements_test.cpp#L228) |
| a message's time comes from its id |  |  | [tests/unit/legacy_replacements_test.cpp:238](../../tests/unit/legacy_replacements_test.cpp#L238) |
| stages run in the order they were added |  |  | [tests/unit/message_pipeline_test.cpp:51](../../tests/unit/message_pipeline_test.cpp#L51) |
| a stage that consumes the message stops the ones after it |  |  | [tests/unit/message_pipeline_test.cpp:64](../../tests/unit/message_pipeline_test.cpp#L64) |
| the bot never answers itself, or a bot this guild has not allowed |  | 3 | [tests/unit/message_pipeline_test.cpp:79](../../tests/unit/message_pipeline_test.cpp#L79) |
| an allowed bot reaches the stages |  |  | [tests/unit/message_pipeline_test.cpp:111](../../tests/unit/message_pipeline_test.cpp#L111) |
| a stage that throws is logged and the rest still run |  |  | [tests/unit/message_pipeline_test.cpp:126](../../tests/unit/message_pipeline_test.cpp#L126) |
| an empty pipeline decides nothing |  |  | [tests/unit/message_pipeline_test.cpp:143](../../tests/unit/message_pipeline_test.cpp#L143) |
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
| a link to a site without a rule does not stop the others |  |  | [tests/unit/url_rules_test.cpp:30](../../tests/unit/url_rules_test.cpp#L30) |
| each link picks its mirror on its own |  |  | [tests/unit/url_rules_test.cpp:40](../../tests/unit/url_rules_test.cpp#L40) |
| a mirror index past the end uses the last mirror |  |  | [tests/unit/url_rules_test.cpp:51](../../tests/unit/url_rules_test.cpp#L51) |
| www and letter case do not hide a link from its rule |  |  | [tests/unit/url_rules_test.cpp:57](../../tests/unit/url_rules_test.cpp#L57) |
| the spoiler survives into the plan |  |  | [tests/unit/url_rules_test.cpp:63](../../tests/unit/url_rules_test.cpp#L63) |
| links Discord would not have embedded are left alone |  |  | [tests/unit/url_rules_test.cpp:69](../../tests/unit/url_rules_test.cpp#L69) |
| the same link twice is replaced once |  |  | [tests/unit/url_rules_test.cpp:74](../../tests/unit/url_rules_test.cpp#L74) |
| a message of links is capped |  |  | [tests/unit/url_rules_test.cpp:79](../../tests/unit/url_rules_test.cpp#L79) |
| every link gets a verdict, and the plan is the replaced ones |  |  | [tests/unit/url_rules_test.cpp:87](../../tests/unit/url_rules_test.cpp#L87) |
| no rules means no plan |  |  | [tests/unit/url_rules_test.cpp:110](../../tests/unit/url_rules_test.cpp#L110) |
| a mirror is its host plus an optional suffix |  |  | [tests/unit/url_rules_test.cpp:114](../../tests/unit/url_rules_test.cpp#L114) |
| a typed domain is reduced to its rule host |  |  | [tests/unit/url_rules_test.cpp:122](../../tests/unit/url_rules_test.cpp#L122) |
| the Java rule file is read line by line |  |  | [tests/unit/url_rules_test.cpp:128](../../tests/unit/url_rules_test.cpp#L128) |

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

## audio

Speech and voice (`src/core/audio`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| DECtalk speaks a phrase at 11025 Hz mono | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:45](../../tests/unit/dectalk_engine_test.cpp#L45) |
| the same request gives the same audio every time | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:58](../../tests/unit/dectalk_engine_test.cpp#L58) |
| one request's inline settings do not reach the next | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:69](../../tests/unit/dectalk_engine_test.cpp#L69) |
| the voice and rate settings change the audio | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:82](../../tests/unit/dectalk_engine_test.cpp#L82) |
| volume scales the samples | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:93](../../tests/unit/dectalk_engine_test.cpp#L93) |
| an utterance stops at its maximum duration | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:103](../../tests/unit/dectalk_engine_test.cpp#L103) |
| an utterance that takes too long is abandoned | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:116](../../tests/unit/dectalk_engine_test.cpp#L116) |
| stop abandons the utterance being spoken and the queue | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:133](../../tests/unit/dectalk_engine_test.cpp#L133) |
| a missing dictionary fails the request instead of the process | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:154](../../tests/unit/dectalk_engine_test.cpp#L154) |
| DECtalk's audio matches the golden fingerprints | `golden`, `fs`, `coro`, `threads` |  | [tests/unit/dectalk_golden_test.cpp:91](../../tests/unit/dectalk_golden_test.cpp#L91) |
| plain text passes through untouched |  |  | [tests/unit/dectalk_sanitizer_test.cpp:29](../../tests/unit/dectalk_sanitizer_test.cpp#L29) |
| everyday commands are kept for everyone |  |  | [tests/unit/dectalk_sanitizer_test.cpp:35](../../tests/unit/dectalk_sanitizer_test.cpp#L35) |
| play, log, debug, loadv and setv are for trusted users only |  |  | [tests/unit/dectalk_sanitizer_test.cpp:46](../../tests/unit/dectalk_sanitizer_test.cpp#L46) |
| pause, resume and dv save are for nobody |  |  | [tests/unit/dectalk_sanitizer_test.cpp:61](../../tests/unit/dectalk_sanitizer_test.cpp#L61) |
| removed commands are reported by their full names |  |  | [tests/unit/dectalk_sanitizer_test.cpp:70](../../tests/unit/dectalk_sanitizer_test.cpp#L70) |
| a command is recognised by any unique prefix, in any case |  |  | [tests/unit/dectalk_sanitizer_test.cpp:77](../../tests/unit/dectalk_sanitizer_test.cpp#L77) |
| an ambiguous or unknown command is dropped |  |  | [tests/unit/dectalk_sanitizer_test.cpp:89](../../tests/unit/dectalk_sanitizer_test.cpp#L89) |
| chained commands are judged one by one |  |  | [tests/unit/dectalk_sanitizer_test.cpp:95](../../tests/unit/dectalk_sanitizer_test.cpp#L95) |
| spaces and extra brackets before the colon do not hide a command |  |  | [tests/unit/dectalk_sanitizer_test.cpp:101](../../tests/unit/dectalk_sanitizer_test.cpp#L101) |
| a quoted parameter can hold a closing bracket |  |  | [tests/unit/dectalk_sanitizer_test.cpp:108](../../tests/unit/dectalk_sanitizer_test.cpp#L108) |
| an unterminated command swallows the rest, as it does in DECtalk |  |  | [tests/unit/dectalk_sanitizer_test.cpp:118](../../tests/unit/dectalk_sanitizer_test.cpp#L118) |
| phoneme brackets are kept, and cannot hide a command |  |  | [tests/unit/dectalk_sanitizer_test.cpp:123](../../tests/unit/dectalk_sanitizer_test.cpp#L123) |
| control characters are removed before anything else is read |  |  | [tests/unit/dectalk_sanitizer_test.cpp:132](../../tests/unit/dectalk_sanitizer_test.cpp#L132) |
| parameters that could open or close anything are dropped |  |  | [tests/unit/dectalk_sanitizer_test.cpp:139](../../tests/unit/dectalk_sanitizer_test.cpp#L139) |
| sanitizing twice changes nothing more |  |  | [tests/unit/dectalk_sanitizer_test.cpp:147](../../tests/unit/dectalk_sanitizer_test.cpp#L147) |
| volume scales, clips and leaves 100 alone |  | 4 | [tests/unit/pcm_test.cpp:40](../../tests/unit/pcm_test.cpp#L40) |
| resampling keeps the length and doubles every sample into stereo |  |  | [tests/unit/pcm_test.cpp:61](../../tests/unit/pcm_test.cpp#L61) |
| resampling keeps the frequency |  |  | [tests/unit/pcm_test.cpp:71](../../tests/unit/pcm_test.cpp#L71) |
| resampling interpolates between the source samples |  |  | [tests/unit/pcm_test.cpp:81](../../tests/unit/pcm_test.cpp#L81) |
| resampling nothing gives nothing |  |  | [tests/unit/pcm_test.cpp:95](../../tests/unit/pcm_test.cpp#L95) |
| resampling a minute of speech |  |  | [tests/unit/pcm_test.cpp:100](../../tests/unit/pcm_test.cpp#L100) |
| the built-in voices are found by name, in any case |  |  | [tests/unit/voice_params_test.cpp:10](../../tests/unit/voice_params_test.cpp#L10) |
| the preamble selects the voice and says only what differs |  |  | [tests/unit/voice_params_test.cpp:18](../../tests/unit/voice_params_test.cpp#L18) |
| the preamble falls back to Paul and clamps the rate |  |  | [tests/unit/voice_params_test.cpp:25](../../tests/unit/voice_params_test.cpp#L25) |
| a WAV file has the RIFF header, then the samples little-endian |  |  | [tests/unit/wav_test.cpp:15](../../tests/unit/wav_test.cpp#L15) |
| a stereo WAV file counts both channels in its rates |  |  | [tests/unit/wav_test.cpp:40](../../tests/unit/wav_test.cpp#L40) |
| the waveform of silence is flat |  |  | [tests/unit/wav_test.cpp:49](../../tests/unit/wav_test.cpp#L49) |
| the waveform follows where the sound is |  |  | [tests/unit/wav_test.cpp:56](../../tests/unit/wav_test.cpp#L56) |
| a waveform of fewer samples than bars has one bar per sample |  |  | [tests/unit/wav_test.cpp:78](../../tests/unit/wav_test.cpp#L78) |
| the waveform is sent as base64 of its 256 bytes |  |  | [tests/unit/wav_test.cpp:83](../../tests/unit/wav_test.cpp#L83) |

## ports

Ports and mocks (`src/core/ports`, `tests/mocks`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| mock_clock moves both clocks together |  |  | [tests/unit/ports_test.cpp:36](../../tests/unit/ports_test.cpp#L36) |
| a coroutine feature runs against the Discord mock | `coro` |  | [tests/unit/ports_test.cpp:48](../../tests/unit/ports_test.cpp#L48) |
| the Discord mock can script a failure | `coro` |  | [tests/unit/ports_test.cpp:64](../../tests/unit/ports_test.cpp#L64) |
| the Discord mock hands out scripted history pages | `coro` |  | [tests/unit/ports_test.cpp:75](../../tests/unit/ports_test.cpp#L75) |
| the HTTP mock replays responses in order and records requests | `coro` |  | [tests/unit/ports_test.cpp:98](../../tests/unit/ports_test.cpp#L98) |
| the TTS mock produces audio in proportion to the text | `coro` |  | [tests/unit/ports_test.cpp:127](../../tests/unit/ports_test.cpp#L127) |
| the TTS mock can fail once and records stops | `coro` |  | [tests/unit/ports_test.cpp:145](../../tests/unit/ports_test.cpp#L145) |

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
| count_occurrences counts non-overlapping matches |  |  | [tests/unit/text_test.cpp:11](../../tests/unit/text_test.cpp#L11) |
| trim removes surrounding whitespace only |  | 1 | [tests/unit/text_test.cpp:20](../../tests/unit/text_test.cpp#L20) |
| is_blank treats whitespace as empty |  |  | [tests/unit/text_test.cpp:31](../../tests/unit/text_test.cpp#L31) |
| character_count counts characters, not bytes |  |  | [tests/unit/text_test.cpp:38](../../tests/unit/text_test.cpp#L38) |
| truncate cuts to a character limit and marks the cut |  | 4 | [tests/unit/text_test.cpp:47](../../tests/unit/text_test.cpp#L47) |
| a Discord ID is read as digits and nothing else |  |  | [tests/unit/text_test.cpp:72](../../tests/unit/text_test.cpp#L72) |
| to_lower lowercases ASCII letters and leaves everything else |  |  | [tests/unit/text_test.cpp:89](../../tests/unit/text_test.cpp#L89) |
| equals_ignoring_case compares ASCII case-insensitively |  |  | [tests/unit/text_test.cpp:97](../../tests/unit/text_test.cpp#L97) |
| lines splits on newlines, CRLF included, and keeps the last line |  |  | [tests/unit/text_test.cpp:105](../../tests/unit/text_test.cpp#L105) |
| every link in a message is found, not just the first |  |  | [tests/unit/url_scan_test.cpp:42](../../tests/unit/url_scan_test.cpp#L42) |
| a spoiler is an odd number of || before the link |  | 6 | [tests/unit/url_scan_test.cpp:52](../../tests/unit/url_scan_test.cpp#L52) |
| trailing punctuation is not part of a link |  |  | [tests/unit/url_scan_test.cpp:100](../../tests/unit/url_scan_test.cpp#L100) |
| a closing bracket stays only when the link opened one |  |  | [tests/unit/url_scan_test.cpp:107](../../tests/unit/url_scan_test.cpp#L107) |
| an underscore at the end of a link is kept |  |  | [tests/unit/url_scan_test.cpp:114](../../tests/unit/url_scan_test.cpp#L114) |
| a link in angle brackets is marked as having its preview turned off |  | 2 | [tests/unit/url_scan_test.cpp:119](../../tests/unit/url_scan_test.cpp#L119) |
| links in code are marked as code |  | 3 | [tests/unit/url_scan_test.cpp:141](../../tests/unit/url_scan_test.cpp#L141) |
| a code span runs to the next run of backticks as long as its own |  |  | [tests/unit/url_scan_test.cpp:163](../../tests/unit/url_scan_test.cpp#L163) |
| a scheme glued to a word is not a link |  |  | [tests/unit/url_scan_test.cpp:173](../../tests/unit/url_scan_test.cpp#L173) |
| the scheme may be in any case |  |  | [tests/unit/url_scan_test.cpp:178](../../tests/unit/url_scan_test.cpp#L178) |
| offsets point back into the scanned text |  |  | [tests/unit/url_scan_test.cpp:182](../../tests/unit/url_scan_test.cpp#L182) |
| split_url separates every part |  |  | [tests/unit/url_scan_test.cpp:193](../../tests/unit/url_scan_test.cpp#L193) |
| rule_host reduces a host to what a rule is keyed by |  |  | [tests/unit/url_scan_test.cpp:207](../../tests/unit/url_scan_test.cpp#L207) |
| rehost keeps the path, query and fragment |  |  | [tests/unit/url_scan_test.cpp:215](../../tests/unit/url_scan_test.cpp#L215) |
| a translation suffix goes on the path, before the query |  | 3 | [tests/unit/url_scan_test.cpp:221](../../tests/unit/url_scan_test.cpp#L221) |
| 100 KB of link-shaped junk is scanned quickly |  |  | [tests/unit/url_scan_test.cpp:243](../../tests/unit/url_scan_test.cpp#L243) |
| one link followed by thousands of brackets is still linear |  |  | [tests/unit/url_scan_test.cpp:261](../../tests/unit/url_scan_test.cpp#L261) |
| scanning a typical message |  |  | [tests/unit/url_scan_test.cpp:273](../../tests/unit/url_scan_test.cpp#L273) |
| version string matches the version constants |  |  | [tests/unit/version_test.cpp:7](../../tests/unit/version_test.cpp#L7) |
