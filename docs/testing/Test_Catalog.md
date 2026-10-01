# Test catalog

**Generated** by ``tools/Update-TestCatalog.ps1``. Do not edit by hand;
re-run the script after adding or retagging tests.

See [README.md](README.md) for the strategy, conventions and tag meanings.

873 test cases across 12 components, including 178 sections.

| Component | Test cases | Sections |
|---|---:|---:|
| [db](#db) | 147 | 19 |
| [config](#config) | 36 | 19 |
| [commands](#commands) | 195 | 54 |
| [events](#events) | 188 | 40 |
| [ui](#ui) | 11 | 0 |
| [discord](#discord) | 8 | 0 |
| [audio](#audio) | 71 | 12 |
| [music](#music) | 68 | 12 |
| [llm](#llm) | 58 | 0 |
| [ports](#ports) | 7 | 0 |
| [log](#log) | 32 | 0 |
| [util](#util) | 52 | 22 |

## db

Database (`src/core/db`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| a backup is a complete, valid copy | `fs` |  | [tests/db/backup_test.cpp:48](../../tests/db/backup_test.cpp#L48) |
| backing up an in-memory database writes it to disk | `fs` |  | [tests/db/backup_test.cpp:62](../../tests/db/backup_test.cpp#L62) |
| an existing backup file is replaced | `fs` |  | [tests/db/backup_test.cpp:74](../../tests/db/backup_test.cpp#L74) |
| a backup taken while other threads write is consistent | `fs`, `threads` |  | [tests/db/backup_test.cpp:92](../../tests/db/backup_test.cpp#L92) |
| rotation keeps the newest backups | `fs` |  | [tests/db/backup_test.cpp:124](../../tests/db/backup_test.cpp#L124) |
| rotation ignores unrelated files | `fs` |  | [tests/db/backup_test.cpp:146](../../tests/db/backup_test.cpp#L146) |
| backup file names carry a sortable UTC timestamp | `fs` |  | [tests/db/backup_test.cpp:168](../../tests/db/backup_test.cpp#L168) |
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
| every emote used enough is copied, most used first |  |  | [tests/db/emoji_copies_test.cpp:113](../../tests/db/emoji_copies_test.cpp#L113) |
| an emote merged by an alias gets one copy between its emojis |  | 1 | [tests/db/emoji_copies_test.cpp:127](../../tests/db/emoji_copies_test.cpp#L127) |
| copies nothing wants are pruned, once nothing is left to fetch |  |  | [tests/db/emoji_copies_test.cpp:153](../../tests/db/emoji_copies_test.cpp#L153) |
| an animated emoji is copied as a GIF, a still one as a PNG | `coro` |  | [tests/db/emoji_copies_test.cpp:178](../../tests/db/emoji_copies_test.cpp#L178) |
| the same image is copied once, and names stay unique | `coro` |  | [tests/db/emoji_copies_test.cpp:212](../../tests/db/emoji_copies_test.cpp#L212) |
| an emoji the CDN no longer has is lost, after both hosts are tried | `coro` |  | [tests/db/emoji_copies_test.cpp:237](../../tests/db/emoji_copies_test.cpp#L237) |
| the media proxy is tried when the CDN refuses | `coro` |  | [tests/db/emoji_copies_test.cpp:254](../../tests/db/emoji_copies_test.cpp#L254) |
| an image too big even when smaller is not uploaded | `coro` |  | [tests/db/emoji_copies_test.cpp:266](../../tests/db/emoji_copies_test.cpp#L266) |
| a failed download or upload is tried again the next day | `coro` | 2 | [tests/db/emoji_copies_test.cpp:282](../../tests/db/emoji_copies_test.cpp#L282) |
| raising the threshold deletes the copies that no longer qualify | `coro` |  | [tests/db/emoji_copies_test.cpp:306](../../tests/db/emoji_copies_test.cpp#L306) |
| with copying off, a round does nothing at all | `coro` |  | [tests/db/emoji_copies_test.cpp:324](../../tests/db/emoji_copies_test.cpp#L324) |
| the duplicates menus show each emote's picture once the bot has a copy |  |  | [tests/db/emoji_copies_test.cpp:333](../../tests/db/emoji_copies_test.cpp#L333) |
| a recorded call is priced, and counted in its day and month |  |  | [tests/db/llm_store_test.cpp:51](../../tests/db/llm_store_test.cpp#L51) |
| reaching a cap says which one, and the month outranks the day |  |  | [tests/db/llm_store_test.cpp:70](../../tests/db/llm_store_test.cpp#L70) |
| a cap notice is due once per guild and period |  |  | [tests/db/llm_store_test.cpp:94](../../tests/db/llm_store_test.cpp#L94) |
| a document nobody edited reads as its default |  |  | [tests/db/llm_store_test.cpp:106](../../tests/db/llm_store_test.cpp#L106) |
| every edit is a new version, per guild and per kind |  |  | [tests/db/llm_store_test.cpp:116](../../tests/db/llm_store_test.cpp#L116) |
| a revert saves the old text as a new version, and can itself be reverted |  |  | [tests/db/llm_store_test.cpp:134](../../tests/db/llm_store_test.cpp#L134) |
| memories are found by the words in them, only in their own guild |  |  | [tests/db/llm_store_test.cpp:156](../../tests/db/llm_store_test.cpp#L156) |
| a removed memory leaves the search index too |  |  | [tests/db/llm_store_test.cpp:172](../../tests/db/llm_store_test.cpp#L172) |
| memories list newest first, and clear by person or all at once |  |  | [tests/db/llm_store_test.cpp:184](../../tests/db/llm_store_test.cpp#L184) |
| the memories shown up front are about the author, then what matches |  |  | [tests/db/llm_store_test.cpp:205](../../tests/db/llm_store_test.cpp#L205) |
| the blacklist blocks a user or anyone with a role |  |  | [tests/db/llm_store_test.cpp:222](../../tests/db/llm_store_test.cpp#L222) |
| advanced triggers are stored per guild and edited in place |  |  | [tests/db/llm_store_test.cpp:243](../../tests/db/llm_store_test.cpp#L243) |
| a guild's model settings are read clamped, with the model falling back to the config's |  |  | [tests/db/llm_store_test.cpp:275](../../tests/db/llm_store_test.cpp#L275) |
| an upload is counted from the moment it is posted |  |  | [tests/db/media_posts_test.cpp:134](../../tests/db/media_posts_test.cpp#L134) |
| a link waits for its preview to show whether it was an image |  | 4 | [tests/db/media_posts_test.cpp:157](../../tests/db/media_posts_test.cpp#L157) |
| nothing is counted where images are off, or from bots |  |  | [tests/db/media_posts_test.cpp:188](../../tests/db/media_posts_test.cpp#L188) |
| statistics count links, images, or both |  |  | [tests/db/media_posts_test.cpp:207](../../tests/db/media_posts_test.cpp#L207) |
| an added entry comes back as it went in |  |  | [tests/db/midnight_store_test.cpp:51](../../tests/db/midnight_store_test.cpp#L51) |
| entries belong to one guild |  |  | [tests/db/midnight_store_test.cpp:66](../../tests/db/midnight_store_test.cpp#L66) |
| only enabled entries are looked at on a tick |  |  | [tests/db/midnight_store_test.cpp:78](../../tests/db/midnight_store_test.cpp#L78) |
| a day can only be claimed once |  |  | [tests/db/midnight_store_test.cpp:91](../../tests/db/midnight_store_test.cpp#L91) |
| editing an entry leaves the day it last posted alone |  |  | [tests/db/midnight_store_test.cpp:101](../../tests/db/midnight_store_test.cpp#L101) |
| a tick posts an entry once and then leaves it alone |  |  | [tests/db/midnight_store_test.cpp:116](../../tests/db/midnight_store_test.cpp#L116) |
| an entry that cannot be claimed does not cost the others their post |  |  | [tests/db/midnight_store_test.cpp:142](../../tests/db/midnight_store_test.cpp#L142) |
| a restart moments after posting does not post again |  |  | [tests/db/midnight_store_test.cpp:172](../../tests/db/midnight_store_test.cpp#L172) |
| a night the bot slept through is given up on, not posted at breakfast |  |  | [tests/db/midnight_store_test.cpp:192](../../tests/db/midnight_store_test.cpp#L192) |
| a restart a minute after midnight still posts |  |  | [tests/db/midnight_store_test.cpp:217](../../tests/db/midnight_store_test.cpp#L217) |
| each timezone posts at its own midnight |  |  | [tests/db/midnight_store_test.cpp:231](../../tests/db/midnight_store_test.cpp#L231) |
| a midnight message's flags survive a round trip and default to silent |  |  | [tests/db/midnight_store_test.cpp:250](../../tests/db/midnight_store_test.cpp#L250) |
| a midnight post carries its entry's flags |  |  | [tests/db/midnight_store_test.cpp:267](../../tests/db/midnight_store_test.cpp#L267) |
| a fresh database migrates to the current schema |  |  | [tests/db/migrations_test.cpp:37](../../tests/db/migrations_test.cpp#L37) |
| migrating twice is a no-op |  |  | [tests/db/migrations_test.cpp:50](../../tests/db/migrations_test.cpp#L50) |
| only migrations newer than user_version are applied |  |  | [tests/db/migrations_test.cpp:61](../../tests/db/migrations_test.cpp#L61) |
| a failing migration rolls back and keeps the previous version |  |  | [tests/db/migrations_test.cpp:75](../../tests/db/migrations_test.cpp#L75) |
| a gap in the migration versions is rejected |  |  | [tests/db/migrations_test.cpp:92](../../tests/db/migrations_test.cpp#L92) |
| the shipped schema is append-only and correctly numbered |  |  | [tests/db/migrations_test.cpp:105](../../tests/db/migrations_test.cpp#L105) |
| an existing database gains the allowlist without losing its triggers |  |  | [tests/db/migrations_test.cpp:118](../../tests/db/migrations_test.cpp#L118) |
| an import writes the history it read |  |  | [tests/db/nickname_import_test.cpp:43](../../tests/db/nickname_import_test.cpp#L43) |
| importing the same file twice adds nothing the second time |  |  | [tests/db/nickname_import_test.cpp:57](../../tests/db/nickname_import_test.cpp#L57) |
| an import does not disturb history the bot recorded itself |  |  | [tests/db/nickname_import_test.cpp:70](../../tests/db/nickname_import_test.cpp#L70) |
| a cleared nickname is imported once, not once per run |  |  | [tests/db/nickname_import_test.cpp:91](../../tests/db/nickname_import_test.cpp#L91) |
| no file to import is not a problem | `fs` |  | [tests/db/nickname_import_test.cpp:108](../../tests/db/nickname_import_test.cpp#L108) |
| a file beside the database is read and imported | `fs` |  | [tests/db/nickname_import_test.cpp:115](../../tests/db/nickname_import_test.cpp#L115) |
| a recorded change comes back as it went in |  |  | [tests/db/nickname_store_test.cpp:39](../../tests/db/nickname_store_test.cpp#L39) |
| a cleared nickname is stored as nothing, not as an empty string |  |  | [tests/db/nickname_store_test.cpp:60](../../tests/db/nickname_store_test.cpp#L60) |
| history reads newest first |  |  | [tests/db/nickname_store_test.cpp:73](../../tests/db/nickname_store_test.cpp#L73) |
| two changes in the same second keep the order they were recorded |  |  | [tests/db/nickname_store_test.cpp:87](../../tests/db/nickname_store_test.cpp#L87) |
| history is per guild |  |  | [tests/db/nickname_store_test.cpp:100](../../tests/db/nickname_store_test.cpp#L100) |
| the latest row is what a new sighting is compared against |  |  | [tests/db/nickname_store_test.cpp:114](../../tests/db/nickname_store_test.cpp#L114) |
| an audit entry finds the row it describes |  |  | [tests/db/nickname_store_test.cpp:127](../../tests/db/nickname_store_test.cpp#L127) |
| an audit entry does not attach itself to an older identical change |  |  | [tests/db/nickname_store_test.cpp:137](../../tests/db/nickname_store_test.cpp#L137) |
| an audit entry for a different nickname matches nothing |  |  | [tests/db/nickname_store_test.cpp:147](../../tests/db/nickname_store_test.cpp#L147) |
| a row that already names somebody is not offered for attribution |  |  | [tests/db/nickname_store_test.cpp:155](../../tests/db/nickname_store_test.cpp#L155) |
| attributing a row fills in the author and where it came from |  |  | [tests/db/nickname_store_test.cpp:165](../../tests/db/nickname_store_test.cpp#L165) |
| the first audit entry to attribute a row wins |  |  | [tests/db/nickname_store_test.cpp:178](../../tests/db/nickname_store_test.cpp#L178) |
| a change that did not go through can be taken back |  |  | [tests/db/nickname_store_test.cpp:191](../../tests/db/nickname_store_test.cpp#L191) |
| an imported row keeps the text its timestamp was read from |  |  | [tests/db/nickname_store_test.cpp:204](../../tests/db/nickname_store_test.cpp#L204) |
| only reactions on our replacements are counted |  |  | [tests/db/reaction_store_test.cpp:95](../../tests/db/reaction_store_test.cpp#L95) |
| taking a reaction back removes it |  |  | [tests/db/reaction_store_test.cpp:107](../../tests/db/reaction_store_test.cpp#L107) |
| a moderator clearing reactions clears the counts |  |  | [tests/db/reaction_store_test.cpp:116](../../tests/db/reaction_store_test.cpp#L116) |
| received, given and self-reactions are counted apart |  | 5 | [tests/db/reaction_store_test.cpp:133](../../tests/db/reaction_store_test.cpp#L133) |
| a backfilled reaction is dated by its message |  |  | [tests/db/reaction_store_test.cpp:173](../../tests/db/reaction_store_test.cpp#L173) |
| a live reaction is dated when it was added |  |  | [tests/db/reaction_store_test.cpp:186](../../tests/db/reaction_store_test.cpp#L186) |
| rebuilding a message's reactions is safe to repeat |  | 1 | [tests/db/reaction_store_test.cpp:198](../../tests/db/reaction_store_test.cpp#L198) |
| an alias merges one emoji into another across all history, and can be undone |  |  | [tests/db/reaction_store_test.cpp:224](../../tests/db/reaction_store_test.cpp#L224) |
| alias chains are flattened as they are written |  | 1 | [tests/db/reaction_store_test.cpp:245](../../tests/db/reaction_store_test.cpp#L245) |
| an alias that would loop is refused |  |  | [tests/db/reaction_store_test.cpp:264](../../tests/db/reaction_store_test.cpp#L264) |
| an emoji already merged into another is not quietly moved |  |  | [tests/db/reaction_store_test.cpp:272](../../tests/db/reaction_store_test.cpp#L272) |
| aliases belong to one guild |  |  | [tests/db/reaction_store_test.cpp:286](../../tests/db/reaction_store_test.cpp#L286) |
| emojis are known by name once somebody has used them |  |  | [tests/db/reaction_store_test.cpp:296](../../tests/db/reaction_store_test.cpp#L296) |
| emojis are listed apart, merged, or only the aliases |  |  | [tests/db/reaction_store_test.cpp:314](../../tests/db/reaction_store_test.cpp#L314) |
| names alike are the same ignoring case, or a letter or two apart |  |  | [tests/db/reaction_store_test.cpp:342](../../tests/db/reaction_store_test.cpp#L342) |
| emojis with names alike are grouped, and merged ones count as their keeper |  |  | [tests/db/reaction_store_test.cpp:363](../../tests/db/reaction_store_test.cpp#L363) |
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
| a matching message gets one of the trigger's responses |  | 1 | [tests/db/trigger_store_test.cpp:153](../../tests/db/trigger_store_test.cpp#L153) |
| a trigger is quiet until its cooldown has passed |  |  | [tests/db/trigger_store_test.cpp:175](../../tests/db/trigger_store_test.cpp#L175) |
| cooldowns are per channel |  |  | [tests/db/trigger_store_test.cpp:192](../../tests/db/trigger_store_test.cpp#L192) |
| messages arriving at once still get one reply per cooldown | `threads` |  | [tests/db/trigger_store_test.cpp:205](../../tests/db/trigger_store_test.cpp#L205) |
| a disabled trigger says nothing |  |  | [tests/db/trigger_store_test.cpp:244](../../tests/db/trigger_store_test.cpp#L244) |
| two triggers on one message both answer |  |  | [tests/db/trigger_store_test.cpp:256](../../tests/db/trigger_store_test.cpp#L256) |
| respond_to_bots survives a round trip and defaults to off |  |  | [tests/db/trigger_store_test.cpp:271](../../tests/db/trigger_store_test.cpp#L271) |
| a trigger only answers an allowed bot when it opts in |  |  | [tests/db/trigger_store_test.cpp:290](../../tests/db/trigger_store_test.cpp#L290) |
| a trigger that answers bots still answers humans |  |  | [tests/db/trigger_store_test.cpp:309](../../tests/db/trigger_store_test.cpp#L309) |
| a trigger's reply flags survive a round trip and default to silent |  |  | [tests/db/trigger_store_test.cpp:322](../../tests/db/trigger_store_test.cpp#L322) |
| triggers from before reply flags existed stay silent |  |  | [tests/db/trigger_store_test.cpp:345](../../tests/db/trigger_store_test.cpp#L345) |
| a trigger's reply carries its flags |  |  | [tests/db/trigger_store_test.cpp:362](../../tests/db/trigger_store_test.cpp#L362) |
| a trigger's reply says which trigger it is from |  |  | [tests/db/trigger_store_test.cpp:376](../../tests/db/trigger_store_test.cpp#L376) |
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
| a saved voice reads back as it was saved |  |  | [tests/db/voice_store_test.cpp:38](../../tests/db/voice_store_test.cpp#L38) |
| voice names are found in any case, and per guild |  |  | [tests/db/voice_store_test.cpp:50](../../tests/db/voice_store_test.cpp#L50) |
| saving under a name that exists replaces the voice but keeps its maker |  |  | [tests/db/voice_store_test.cpp:61](../../tests/db/voice_store_test.cpp#L61) |
| voices are listed by name and removed one at a time |  |  | [tests/db/voice_store_test.cpp:77](../../tests/db/voice_store_test.cpp#L77) |

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
| an empty config object gives the documented defaults |  |  | [tests/unit/bootstrap_test.cpp:55](../../tests/unit/bootstrap_test.cpp#L55) |
| a missing config file is written with the defaults | `fs` |  | [tests/unit/bootstrap_test.cpp:67](../../tests/unit/bootstrap_test.cpp#L67) |
| values in the file replace the defaults | `fs` |  | [tests/unit/bootstrap_test.cpp:79](../../tests/unit/bootstrap_test.cpp#L79) |
| IDs written as JSON numbers are rejected |  |  | [tests/unit/bootstrap_test.cpp:108](../../tests/unit/bootstrap_test.cpp#L108) |
| a trusted ID that is not exactly an ID stops startup |  |  | [tests/unit/bootstrap_test.cpp:116](../../tests/unit/bootstrap_test.cpp#L116) |
| bad config is reported with the key that caused it |  | 6 | [tests/unit/bootstrap_test.cpp:131](../../tests/unit/bootstrap_test.cpp#L131) |
| the model has to be one the bot can price, from the provider named |  |  | [tests/unit/bootstrap_test.cpp:162](../../tests/unit/bootstrap_test.cpp#L162) |
| emoji copies are kept for every emote used, unless the config says otherwise |  |  | [tests/unit/bootstrap_test.cpp:178](../../tests/unit/bootstrap_test.cpp#L178) |
| nickname tracking is on unless the config turns it off |  |  | [tests/unit/bootstrap_test.cpp:185](../../tests/unit/bootstrap_test.cpp#L185) |
| the log level is read from the config |  |  | [tests/unit/bootstrap_test.cpp:196](../../tests/unit/bootstrap_test.cpp#L196) |
| the build's default log level matches the build |  |  | [tests/unit/bootstrap_test.cpp:204](../../tests/unit/bootstrap_test.cpp#L204) |
| trust needs a listed user, or an admin in a listed server |  | 5 | [tests/unit/bootstrap_test.cpp:212](../../tests/unit/bootstrap_test.cpp#L212) |
| secrets come from the environment |  | 3 | [tests/unit/bootstrap_test.cpp:246](../../tests/unit/bootstrap_test.cpp#L246) |
| the recompute bot override is read by debug builds only |  | 4 | [tests/unit/bootstrap_test.cpp:275](../../tests/unit/bootstrap_test.cpp#L275) |
| loading the configuration applies the recompute bot override as the build allows | `fs` |  | [tests/unit/bootstrap_test.cpp:302](../../tests/unit/bootstrap_test.cpp#L302) |
| the written defaults load as the defaults |  |  | [tests/unit/bootstrap_test.cpp:314](../../tests/unit/bootstrap_test.cpp#L314) |
| the example config is exactly what the bot writes | `fs` |  | [tests/unit/bootstrap_test.cpp:336](../../tests/unit/bootstrap_test.cpp#L336) |
| a config file in a folder that does not exist yet is written there | `fs` |  | [tests/unit/bootstrap_test.cpp:345](../../tests/unit/bootstrap_test.cpp#L345) |
| an existing config file is never written over | `fs` |  | [tests/unit/bootstrap_test.cpp:354](../../tests/unit/bootstrap_test.cpp#L354) |
| a config file that cannot be written leaves the defaults | `fs` |  | [tests/unit/bootstrap_test.cpp:366](../../tests/unit/bootstrap_test.cpp#L366) |
| something at the config path that cannot be read stops startup | `fs` |  | [tests/unit/bootstrap_test.cpp:379](../../tests/unit/bootstrap_test.cpp#L379) |
| no arguments run the bot with config.json |  |  | [tests/unit/command_line_test.cpp:24](../../tests/unit/command_line_test.cpp#L24) |
| a lone argument is the config file |  |  | [tests/unit/command_line_test.cpp:31](../../tests/unit/command_line_test.cpp#L31) |
| the unregister flag goes before or after the config file |  |  | [tests/unit/command_line_test.cpp:38](../../tests/unit/command_line_test.cpp#L38) |
| an unknown option is refused rather than read as a config file |  |  | [tests/unit/command_line_test.cpp:52](../../tests/unit/command_line_test.cpp#L52) |
| two config files are refused |  |  | [tests/unit/command_line_test.cpp:57](../../tests/unit/command_line_test.cpp#L57) |

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
| a voice message answers the interaction with its duration and waveform |  |  | [tests/unit/chat_command_test.cpp:14](../../tests/unit/chat_command_test.cpp#L14) |
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
| the URL dry run hides the previews of the links it shows |  |  | [tests/unit/command_responses_test.cpp:120](../../tests/unit/command_responses_test.cpp#L120) |
| the silent and previews options change only what they are given |  |  | [tests/unit/command_responses_test.cpp:132](../../tests/unit/command_responses_test.cpp#L132) |
| only a difference from silent with previews is worth describing |  |  | [tests/unit/command_responses_test.cpp:153](../../tests/unit/command_responses_test.cpp#L153) |
| changing aliases and recomputing need Manage Server, and reading does not |  |  | [tests/unit/linkstats_command_test.cpp:66](../../tests/unit/linkstats_command_test.cpp#L66) |
| custom emojis with names alike are listed a group at a time |  | 3 | [tests/unit/linkstats_command_test.cpp:95](../../tests/unit/linkstats_command_test.cpp#L95) |
| emojis alike are merged from the list by somebody with Manage Server |  | 3 | [tests/unit/linkstats_command_test.cpp:135](../../tests/unit/linkstats_command_test.cpp#L135) |
| aliases are listed as what counts as what, within Discord's limit |  |  | [tests/unit/linkstats_command_test.cpp:180](../../tests/unit/linkstats_command_test.cpp#L180) |
| dates are read as YYYY-MM-DD and must exist |  |  | [tests/unit/linkstats_command_test.cpp:201](../../tests/unit/linkstats_command_test.cpp#L201) |
| the leaderboard names people without pinging them |  |  | [tests/unit/linkstats_command_test.cpp:210](../../tests/unit/linkstats_command_test.cpp#L210) |
| the emoji leaderboard shows emojis rather than people |  |  | [tests/unit/linkstats_command_test.cpp:222](../../tests/unit/linkstats_command_test.cpp#L222) |
| an empty leaderboard says how to fill it |  |  | [tests/unit/linkstats_command_test.cpp:231](../../tests/unit/linkstats_command_test.cpp#L231) |
| a profile shows received, given and self apart |  |  | [tests/unit/linkstats_command_test.cpp:238](../../tests/unit/linkstats_command_test.cpp#L238) |
| a date range shows in the title as it was typed |  |  | [tests/unit/linkstats_command_test.cpp:247](../../tests/unit/linkstats_command_test.cpp#L247) |
| an emoji can be named rather than drawn |  |  | [tests/unit/linkstats_command_test.cpp:256](../../tests/unit/linkstats_command_test.cpp#L256) |
| link stats are open to everyone, with aliases in a group |  |  | [tests/unit/linkstats_command_test.cpp:272](../../tests/unit/linkstats_command_test.cpp#L272) |
| the longest site filter still leaves room for a board's paging |  |  | [tests/unit/linkstats_command_test.cpp:285](../../tests/unit/linkstats_command_test.cpp#L285) |
| a recompute's report says what it found and what it could not read |  | 3 | [tests/unit/linkstats_command_test.cpp:311](../../tests/unit/linkstats_command_test.cpp#L311) |
| recompute is its own group, with a required start date |  |  | [tests/unit/linkstats_command_test.cpp:377](../../tests/unit/linkstats_command_test.cpp#L377) |
| a long leaderboard pages, and every page is the same board |  |  | [tests/unit/linkstats_command_test.cpp:396](../../tests/unit/linkstats_command_test.cpp#L396) |
| a board's filters survive the trip through a button |  |  | [tests/unit/linkstats_command_test.cpp:437](../../tests/unit/linkstats_command_test.cpp#L437) |
| a board can be limited to one site |  |  | [tests/unit/linkstats_command_test.cpp:462](../../tests/unit/linkstats_command_test.cpp#L462) |
| what a leaderboard ranks is read from its option |  |  | [tests/unit/linkstats_command_test.cpp:485](../../tests/unit/linkstats_command_test.cpp#L485) |
| a finished recompute is answered with a ping to whoever started it |  | 3 | [tests/unit/linkstats_command_test.cpp:493](../../tests/unit/linkstats_command_test.cpp#L493) |
| every reaction, by emoji, for everyone or for one person |  | 3 | [tests/unit/linkstats_command_test.cpp:543](../../tests/unit/linkstats_command_test.cpp#L543) |
| a page of one person's reactions stays theirs |  |  | [tests/unit/linkstats_command_test.cpp:584](../../tests/unit/linkstats_command_test.cpp#L584) |
| a board's buttons from before people could be named still page |  |  | [tests/unit/linkstats_command_test.cpp:611](../../tests/unit/linkstats_command_test.cpp#L611) |
| a board says what it counts: links, images, or both |  |  | [tests/unit/linkstats_command_test.cpp:625](../../tests/unit/linkstats_command_test.cpp#L625) |
| what a board counts survives the trip through a button |  |  | [tests/unit/linkstats_command_test.cpp:650](../../tests/unit/linkstats_command_test.cpp#L650) |
| images are turned on per server, and the boards can ask for them |  |  | [tests/unit/linkstats_command_test.cpp:666](../../tests/unit/linkstats_command_test.cpp#L666) |
| a recompute that counts images says what it found |  |  | [tests/unit/linkstats_command_test.cpp:691](../../tests/unit/linkstats_command_test.cpp#L691) |
| the personality is open to everyone until an admin narrows it to a role |  |  | [tests/unit/llm_command_test.cpp:67](../../tests/unit/llm_command_test.cpp#L67) |
| a document is cut into form parts between lines, and joins back the same |  |  | [tests/unit/llm_command_test.cpp:78](../../tests/unit/llm_command_test.cpp#L78) |
| a document too long for a form has no form, and a line longer than a part is cut |  |  | [tests/unit/llm_command_test.cpp:98](../../tests/unit/llm_command_test.cpp#L98) |
| the document form fits a modal and is filled with the current text |  |  | [tests/unit/llm_command_test.cpp:109](../../tests/unit/llm_command_test.cpp#L109) |
| a document is shown inline when short, and attached when not |  |  | [tests/unit/llm_command_test.cpp:120](../../tests/unit/llm_command_test.cpp#L120) |
| saving a large document warns that it is sent with every message |  |  | [tests/unit/llm_command_test.cpp:134](../../tests/unit/llm_command_test.cpp#L134) |
| history lists the newest versions first, with who and when |  |  | [tests/unit/llm_command_test.cpp:141](../../tests/unit/llm_command_test.cpp#L141) |
| the settings panel shows every setting and fits a message |  |  | [tests/unit/llm_command_test.cpp:155](../../tests/unit/llm_command_test.cpp#L155) |
| each settings form fits a modal and is filled with the current values |  |  | [tests/unit/llm_command_test.cpp:164](../../tests/unit/llm_command_test.cpp#L164) |
| a settings form is stored whole or not at all, naming what was out of range |  |  | [tests/unit/llm_command_test.cpp:173](../../tests/unit/llm_command_test.cpp#L173) |
| the memory list pages ten at a time, carrying whose list it is |  |  | [tests/unit/llm_command_test.cpp:189](../../tests/unit/llm_command_test.cpp#L189) |
| the status says what was spent against the caps |  |  | [tests/unit/llm_command_test.cpp:206](../../tests/unit/llm_command_test.cpp#L206) |
| the llm and memory commands register, within Discord's limits |  |  | [tests/unit/llm_command_test.cpp:219](../../tests/unit/llm_command_test.cpp#L219) |
| only the trusted users can choose where the log goes |  |  | [tests/unit/logs_command_test.cpp:36](../../tests/unit/logs_command_test.cpp#L36) |
| the log channel's state says where, from which level, and how it is going |  |  | [tests/unit/logs_command_test.cpp:50](../../tests/unit/logs_command_test.cpp#L50) |
| the logs command registers, with a level for every choice but off |  |  | [tests/unit/logs_command_test.cpp:72](../../tests/unit/logs_command_test.cpp#L72) |
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
| a form's fields are read however DPP lays them out |  | 2 | [tests/unit/panels_test.cpp:93](../../tests/unit/panels_test.cpp#L93) |
| the trigger panel adds a trigger as it was typed |  |  | [tests/unit/panels_test.cpp:149](../../tests/unit/panels_test.cpp#L149) |
| the trigger panel's form, sent back untouched, changes nothing |  |  | [tests/unit/panels_test.cpp:177](../../tests/unit/panels_test.cpp#L177) |
| the trigger panel's form saves what it can read, and says what it kept |  |  | [tests/unit/panels_test.cpp:199](../../tests/unit/panels_test.cpp#L199) |
| the trigger panel refuses a form that could not work, and keeps the trigger |  |  | [tests/unit/panels_test.cpp:216](../../tests/unit/panels_test.cpp#L216) |
| the trigger panel's buttons flip what they say, and say the new state |  |  | [tests/unit/panels_test.cpp:226](../../tests/unit/panels_test.cpp#L226) |
| the trigger panel deletes only once it is confirmed |  |  | [tests/unit/panels_test.cpp:252](../../tests/unit/panels_test.cpp#L252) |
| a trigger added past the first page is shown on its own page, picked |  |  | [tests/unit/panels_test.cpp:268](../../tests/unit/panels_test.cpp#L268) |
| the URL panel adds a rule |  |  | [tests/unit/panels_test.cpp:313](../../tests/unit/panels_test.cpp#L313) |
| the URL panel edits a rule's mirrors, and an untouched form changes nothing |  |  | [tests/unit/panels_test.cpp:327](../../tests/unit/panels_test.cpp#L327) |
| the URL panel renames a rule by editing its site |  |  | [tests/unit/panels_test.cpp:343](../../tests/unit/panels_test.cpp#L343) |
| the URL panel will not save over another site's rule |  | 2 | [tests/unit/panels_test.cpp:353](../../tests/unit/panels_test.cpp#L353) |
| the URL panel deletes a rule once confirmed, and turns replacement on and off |  |  | [tests/unit/panels_test.cpp:373](../../tests/unit/panels_test.cpp#L373) |
| the voice lab keeps what its forms set, and the Test says it in that voice |  |  | [tests/unit/panels_test.cpp:448](../../tests/unit/panels_test.cpp#L448) |
| the voice lab's text form replaces the whole voice |  |  | [tests/unit/panels_test.cpp:470](../../tests/unit/panels_test.cpp#L470) |
| the voice lab saves a voice, says when it has changed since, and opens it again |  |  | [tests/unit/panels_test.cpp:480](../../tests/unit/panels_test.cpp#L480) |
| the voice lab will not save over someone else's voice |  |  | [tests/unit/panels_test.cpp:504](../../tests/unit/panels_test.cpp#L504) |
| the language model's settings panel stores what its forms set |  |  | [tests/unit/panels_test.cpp:555](../../tests/unit/panels_test.cpp#L555) |
| the language model's settings panel switches it on and off, for Manage Server only |  |  | [tests/unit/panels_test.cpp:574](../../tests/unit/panels_test.cpp#L574) |
| a document's form saves what was typed, not blanks |  | 3 | [tests/unit/panels_test.cpp:587](../../tests/unit/panels_test.cpp#L587) |
| the memory list pages |  |  | [tests/unit/panels_test.cpp:620](../../tests/unit/panels_test.cpp#L620) |
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
| speech goes where the bot is, or joins whoever asked |  |  | [tests/unit/speak_command_test.cpp:45](../../tests/unit/speak_command_test.cpp#L45) |
| speech refuses blank text and text over the guild's limit |  |  | [tests/unit/speak_command_test.cpp:59](../../tests/unit/speak_command_test.cpp#L59) |
| speech limits default, are per guild, and are clamped |  |  | [tests/unit/speak_command_test.cpp:71](../../tests/unit/speak_command_test.cpp#L71) |
| speech is stopped by whoever asked for it, an admin or a trusted user |  |  | [tests/unit/speak_command_test.cpp:87](../../tests/unit/speak_command_test.cpp#L87) |
| the voice commands register, their flags checked against their subcommands |  |  | [tests/unit/speak_command_test.cpp:98](../../tests/unit/speak_command_test.cpp#L98) |
| the voice grace defaults to 30 seconds and is clamped |  |  | [tests/unit/speak_command_test.cpp:120](../../tests/unit/speak_command_test.cpp#L120) |
| responses are one per line |  |  | [tests/unit/trigger_command_test.cpp:24](../../tests/unit/trigger_command_test.cpp#L24) |
| a leading number and bar sets the weight |  |  | [tests/unit/trigger_command_test.cpp:33](../../tests/unit/trigger_command_test.cpp#L33) |
| a bar that is not a weight stays part of the response |  |  | [tests/unit/trigger_command_test.cpp:43](../../tests/unit/trigger_command_test.cpp#L43) |
| blank lines are skipped |  |  | [tests/unit/trigger_command_test.cpp:54](../../tests/unit/trigger_command_test.cpp#L54) |
| nothing usable parses to nothing |  |  | [tests/unit/trigger_command_test.cpp:62](../../tests/unit/trigger_command_test.cpp#L62) |
| responses round trip through their text form |  |  | [tests/unit/trigger_command_test.cpp:69](../../tests/unit/trigger_command_test.cpp#L69) |
| a trigger describes itself in one line |  | 3 | [tests/unit/trigger_command_test.cpp:79](../../tests/unit/trigger_command_test.cpp#L79) |
| the modal keeps fields it cannot read rather than resetting them |  |  | [tests/unit/trigger_command_test.cpp:106](../../tests/unit/trigger_command_test.cpp#L106) |
| the modal reads the mode however the panel writes it |  |  | [tests/unit/trigger_command_test.cpp:132](../../tests/unit/trigger_command_test.cpp#L132) |
| the modal applies the fields it can read |  |  | [tests/unit/trigger_command_test.cpp:153](../../tests/unit/trigger_command_test.cpp#L153) |
| the modal refuses a trigger that could not work |  | 2 | [tests/unit/trigger_command_test.cpp:169](../../tests/unit/trigger_command_test.cpp#L169) |
| the trigger modal fits inside Discord's limits |  |  | [tests/unit/trigger_command_test.cpp:187](../../tests/unit/trigger_command_test.cpp#L187) |
| a long trigger list pages |  |  | [tests/unit/trigger_command_test.cpp:204](../../tests/unit/trigger_command_test.cpp#L204) |
| a trigger that answers bots says so when described |  |  | [tests/unit/trigger_command_test.cpp:227](../../tests/unit/trigger_command_test.cpp#L227) |
| a trigger says when its replies notify or hide previews |  |  | [tests/unit/trigger_command_test.cpp:236](../../tests/unit/trigger_command_test.cpp#L236) |
| the panel offers to change how a trigger's replies are posted |  |  | [tests/unit/trigger_command_test.cpp:246](../../tests/unit/trigger_command_test.cpp#L246) |
| confirming a delete on the first or last page fits, and Cancel keeps the trigger picked |  |  | [tests/unit/trigger_command_test.cpp:286](../../tests/unit/trigger_command_test.cpp#L286) |
| the longest pattern the command takes still fits the panel |  |  | [tests/unit/trigger_command_test.cpp:322](../../tests/unit/trigger_command_test.cpp#L322) |
| a full page of the longest patterns still fits the panel and the list |  |  | [tests/unit/trigger_command_test.cpp:345](../../tests/unit/trigger_command_test.cpp#L345) |
| the trigger modal takes no more than the command does |  |  | [tests/unit/trigger_command_test.cpp:367](../../tests/unit/trigger_command_test.cpp#L367) |
| each panel toggle flips one thing and names it for the log |  |  | [tests/unit/trigger_command_test.cpp:384](../../tests/unit/trigger_command_test.cpp#L384) |
| unregistering deletes the global commands and every server's own | `coro` |  | [tests/unit/unregister_test.cpp:29](../../tests/unit/unregister_test.cpp#L29) |
| a set with no commands in it is not deleted | `coro` |  | [tests/unit/unregister_test.cpp:46](../../tests/unit/unregister_test.cpp#L46) |
| a refused deletion is reported and the other servers still cleared | `coro` |  | [tests/unit/unregister_test.cpp:60](../../tests/unit/unregister_test.cpp#L60) |
| the servers not being listable still leaves the global commands deleted | `coro` |  | [tests/unit/unregister_test.cpp:79](../../tests/unit/unregister_test.cpp#L79) |
| the global commands not being listable is reported and nothing global deleted | `coro` |  | [tests/unit/unregister_test.cpp:93](../../tests/unit/unregister_test.cpp#L93) |
| mirrors may be typed on one line or one per line |  |  | [tests/unit/urlrepl_command_test.cpp:54](../../tests/unit/urlrepl_command_test.cpp#L54) |
| the site is reduced to what links are matched by |  |  | [tests/unit/urlrepl_command_test.cpp:67](../../tests/unit/urlrepl_command_test.cpp#L67) |
| a mirror listed twice is kept once, in its first place |  |  | [tests/unit/urlrepl_command_test.cpp:71](../../tests/unit/urlrepl_command_test.cpp#L71) |
| rules that could not work are refused with a reason |  |  | [tests/unit/urlrepl_command_test.cpp:76](../../tests/unit/urlrepl_command_test.cpp#L76) |
| a rule describes itself in one line |  |  | [tests/unit/urlrepl_command_test.cpp:84](../../tests/unit/urlrepl_command_test.cpp#L84) |
| the dry run shows the post and accounts for every link |  |  | [tests/unit/urlrepl_command_test.cpp:93](../../tests/unit/urlrepl_command_test.cpp#L93) |
| the dry run says when the person running it has opted out |  |  | [tests/unit/urlrepl_command_test.cpp:104](../../tests/unit/urlrepl_command_test.cpp#L104) |
| the dry run works while replacement is off, and says that it is |  |  | [tests/unit/urlrepl_command_test.cpp:109](../../tests/unit/urlrepl_command_test.cpp#L109) |
| the dry run says when there is nothing to do |  |  | [tests/unit/urlrepl_command_test.cpp:119](../../tests/unit/urlrepl_command_test.cpp#L119) |
| a dry run of a long message stays under Discord's limit |  |  | [tests/unit/urlrepl_command_test.cpp:125](../../tests/unit/urlrepl_command_test.cpp#L125) |
| an empty list says how to start one |  |  | [tests/unit/urlrepl_command_test.cpp:141](../../tests/unit/urlrepl_command_test.cpp#L141) |
| a long list pages |  |  | [tests/unit/urlrepl_command_test.cpp:148](../../tests/unit/urlrepl_command_test.cpp#L148) |
| the list and the panel say whether replacement is on |  |  | [tests/unit/urlrepl_command_test.cpp:162](../../tests/unit/urlrepl_command_test.cpp#L162) |
| the panel's switch asks for the opposite of what is set |  |  | [tests/unit/urlrepl_command_test.cpp:174](../../tests/unit/urlrepl_command_test.cpp#L174) |
| turning replacement on or off says what changed |  |  | [tests/unit/urlrepl_command_test.cpp:197](../../tests/unit/urlrepl_command_test.cpp#L197) |
| the panel lists a page of rules with a menu to pick one |  |  | [tests/unit/urlrepl_command_test.cpp:216](../../tests/unit/urlrepl_command_test.cpp#L216) |
| picking a rule offers Edit and Delete for it |  |  | [tests/unit/urlrepl_command_test.cpp:232](../../tests/unit/urlrepl_command_test.cpp#L232) |
| confirming a delete on the first or last page fits, and Cancel keeps the rule picked |  |  | [tests/unit/urlrepl_command_test.cpp:250](../../tests/unit/urlrepl_command_test.cpp#L250) |
| the panel follows a rule to the page it sorts onto |  |  | [tests/unit/urlrepl_command_test.cpp:274](../../tests/unit/urlrepl_command_test.cpp#L274) |
| the URL rule modal fits inside Discord's limits |  |  | [tests/unit/urlrepl_command_test.cpp:285](../../tests/unit/urlrepl_command_test.cpp#L285) |
| anyone may opt themselves out, and only Manage Server may for somebody else |  |  | [tests/unit/urlrepl_command_test.cpp:301](../../tests/unit/urlrepl_command_test.cpp#L301) |
| a full page of rules with the most, longest mirrors still fits the panel and the list |  |  | [tests/unit/urlrepl_command_test.cpp:316](../../tests/unit/urlrepl_command_test.cpp#L316) |
| the commands are registered the way Discord expects |  |  | [tests/unit/urlrepl_command_test.cpp:331](../../tests/unit/urlrepl_command_test.cpp#L331) |
| the voice lab shows the voice as groups and as inline commands |  |  | [tests/unit/voice_lab_test.cpp:44](../../tests/unit/voice_lab_test.cpp#L44) |
| an untouched voice says so, and a note shows once under it |  |  | [tests/unit/voice_lab_test.cpp:55](../../tests/unit/voice_lab_test.cpp#L55) |
| the voice lab says which voice it is editing, and whether it still matches what is saved |  | 4 | [tests/unit/voice_lab_test.cpp:66](../../tests/unit/voice_lab_test.cpp#L66) |
| the voice lab offers the server's saved voices, the one being edited picked |  | 3 | [tests/unit/voice_lab_test.cpp:88](../../tests/unit/voice_lab_test.cpp#L88) |
| every voice lab form fits in a modal |  |  | [tests/unit/voice_lab_test.cpp:129](../../tests/unit/voice_lab_test.cpp#L129) |
| a group's form sets, clears and clamps its parameters |  |  | [tests/unit/voice_lab_test.cpp:147](../../tests/unit/voice_lab_test.cpp#L147) |
| the raw form replaces the whole voice |  |  | [tests/unit/voice_lab_test.cpp:156](../../tests/unit/voice_lab_test.cpp#L156) |
| a raw form that came back without its field leaves the voice alone |  |  | [tests/unit/voice_lab_test.cpp:166](../../tests/unit/voice_lab_test.cpp#L166) |
| only whoever made a voice, or an admin, may change it |  |  | [tests/unit/voice_lab_test.cpp:177](../../tests/unit/voice_lab_test.cpp#L177) |
| a draft is kept per person for half an hour after it was last touched |  |  | [tests/unit/voice_lab_test.cpp:184](../../tests/unit/voice_lab_test.cpp#L184) |

## events

Messages, replacements, reactions, nicknames and midnight (`src/core/events`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| a recompute credits an old replacement to whoever posted the link | `coro` |  | [tests/db/backfill_test.cpp:112](../../tests/db/backfill_test.cpp#L112) |
| an old replacement is filed under the site its mirror stood in for | `coro` |  | [tests/db/backfill_test.cpp:141](../../tests/db/backfill_test.cpp#L141) |
| a replacement after somebody else's link is reported, not credited to them | `coro` |  | [tests/db/backfill_test.cpp:157](../../tests/db/backfill_test.cpp#L157) |
| a recompute is safe to run twice | `coro` |  | [tests/db/backfill_test.cpp:171](../../tests/db/backfill_test.cpp#L171) |
| a finished channel is not scanned again unless asked | `coro` |  | [tests/db/backfill_test.cpp:183](../../tests/db/backfill_test.cpp#L183) |
| the walk stops at the start of the range | `coro` |  | [tests/db/backfill_test.cpp:194](../../tests/db/backfill_test.cpp#L194) |
| the end of the range is where paging starts | `coro` |  | [tests/db/backfill_test.cpp:208](../../tests/db/backfill_test.cpp#L208) |
| a replacement the bot recorded itself is not re-attributed | `coro` |  | [tests/db/backfill_test.cpp:218](../../tests/db/backfill_test.cpp#L218) |
| messages in no known format are listed with where they are | `coro` |  | [tests/db/backfill_test.cpp:235](../../tests/db/backfill_test.cpp#L235) |
| a replacement with nobody to credit still counts its reactions | `coro` |  | [tests/db/backfill_test.cpp:248](../../tests/db/backfill_test.cpp#L248) |
| a channel the bot cannot read is reported and the rest carry on | `coro` |  | [tests/db/backfill_test.cpp:261](../../tests/db/backfill_test.cpp#L261) |
| a failed reaction lookup keeps the counts that were there | `coro` |  | [tests/db/backfill_test.cpp:275](../../tests/db/backfill_test.cpp#L275) |
| one recompute per guild, and it can be cancelled | `coro` |  | [tests/db/backfill_test.cpp:289](../../tests/db/backfill_test.cpp#L289) |
| an old mirror no rule remembers is found by what it answered, and remembered | `coro` |  | [tests/db/backfill_test.cpp:307](../../tests/db/backfill_test.cpp#L307) |
| a masked replacement is recognised by its shape, whatever its mirror | `coro` |  | [tests/db/backfill_test.cpp:332](../../tests/db/backfill_test.cpp#L332) |
| the bot's own links are not replacements unless they answered one | `coro` | 3 | [tests/db/backfill_test.cpp:345](../../tests/db/backfill_test.cpp#L345) |
| where images are counted, a recompute finds them and their reactions | `coro` |  | [tests/db/backfill_test.cpp:404](../../tests/db/backfill_test.cpp#L404) |
| where images are not counted, a recompute leaves them alone | `coro` |  | [tests/db/backfill_test.cpp:432](../../tests/db/backfill_test.cpp#L432) |
| a GIF moves when it has more than one frame |  |  | [tests/db/emoji_copies_test.cpp:91](../../tests/db/emoji_copies_test.cpp#L91) |
| images are told apart by their SHA-256 |  |  | [tests/db/emoji_copies_test.cpp:97](../../tests/db/emoji_copies_test.cpp#L97) |
| a copy's name is one Discord accepts |  |  | [tests/db/emoji_copies_test.cpp:102](../../tests/db/emoji_copies_test.cpp#L102) |
| an image or a video counts, whatever its kind |  |  | [tests/db/media_posts_test.cpp:89](../../tests/db/media_posts_test.cpp#L89) |
| only a link's own picture or video counts, not a site's preview |  |  | [tests/db/media_posts_test.cpp:102](../../tests/db/media_posts_test.cpp#L102) |
| a message has media when something attached or embedded is |  |  | [tests/db/media_posts_test.cpp:114](../../tests/db/media_posts_test.cpp#L114) |
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
| format 1: a copy of the original, sent as a reply |  |  | [tests/unit/legacy_replacements_test.cpp:69](../../tests/unit/legacy_replacements_test.cpp#L69) |
| format 2: webhook mode is counted and skipped |  |  | [tests/unit/legacy_replacements_test.cpp:75](../../tests/unit/legacy_replacements_test.cpp#L75) |
| format 3: a copy of the original as a plain message |  |  | [tests/unit/legacy_replacements_test.cpp:86](../../tests/unit/legacy_replacements_test.cpp#L86) |
| format 4: a dot linking to the mirror |  |  | [tests/unit/legacy_replacements_test.cpp:90](../../tests/unit/legacy_replacements_test.cpp#L90) |
| format 5: the link emoji and a dot |  |  | [tests/unit/legacy_replacements_test.cpp:94](../../tests/unit/legacy_replacements_test.cpp#L94) |
| format 6: the link emoji and an underscore, which is still the format |  |  | [tests/unit/legacy_replacements_test.cpp:99](../../tests/unit/legacy_replacements_test.cpp#L99) |
| the mirror links are collected whatever the format |  |  | [tests/unit/legacy_replacements_test.cpp:107](../../tests/unit/legacy_replacements_test.cpp#L107) |
| only the bot's own messages with links count |  |  | [tests/unit/legacy_replacements_test.cpp:117](../../tests/unit/legacy_replacements_test.cpp#L117) |
| a shape nobody wrote down is reported, not guessed at |  |  | [tests/unit/legacy_replacements_test.cpp:127](../../tests/unit/legacy_replacements_test.cpp#L127) |
| the masked shapes are replacements whatever their mirror |  |  | [tests/unit/legacy_replacements_test.cpp:143](../../tests/unit/legacy_replacements_test.cpp#L143) |
| a copy on no known mirror waits for what it answered |  |  | [tests/unit/legacy_replacements_test.cpp:154](../../tests/unit/legacy_replacements_test.cpp#L154) |
| what a replacement replaced is the same path on another host |  |  | [tests/unit/legacy_replacements_test.cpp:170](../../tests/unit/legacy_replacements_test.cpp#L170) |
| a copy on no known mirror is credited only by a link it replaced |  |  | [tests/unit/legacy_replacements_test.cpp:183](../../tests/unit/legacy_replacements_test.cpp#L183) |
| a reply names its original |  | 2 | [tests/unit/legacy_replacements_test.cpp:204](../../tests/unit/legacy_replacements_test.cpp#L204) |
| the original is the nearest earlier link, past any chat |  |  | [tests/unit/legacy_replacements_test.cpp:226](../../tests/unit/legacy_replacements_test.cpp#L226) |
| a nearer link that is not ours does not take the credit |  |  | [tests/unit/legacy_replacements_test.cpp:240](../../tests/unit/legacy_replacements_test.cpp#L240) |
| with no matching link the replacement stays unattributed |  | 3 | [tests/unit/legacy_replacements_test.cpp:253](../../tests/unit/legacy_replacements_test.cpp#L253) |
| other bots' links are never the original |  |  | [tests/unit/legacy_replacements_test.cpp:281](../../tests/unit/legacy_replacements_test.cpp#L281) |
| a front-page link proves nothing about which message was answered |  |  | [tests/unit/legacy_replacements_test.cpp:290](../../tests/unit/legacy_replacements_test.cpp#L290) |
| a message's time comes from its id |  |  | [tests/unit/legacy_replacements_test.cpp:300](../../tests/unit/legacy_replacements_test.cpp#L300) |
| a waiting line has its time and level, in a code block |  |  | [tests/unit/log_channel_test.cpp:51](../../tests/unit/log_channel_test.cpp#L51) |
| lines are packed into as few messages as fit, in order |  |  | [tests/unit/log_channel_test.cpp:59](../../tests/unit/log_channel_test.cpp#L59) |
| what does not fit this time keeps waiting |  |  | [tests/unit/log_channel_test.cpp:77](../../tests/unit/log_channel_test.cpp#L77) |
| secrets are masked wherever they appear, and short ones left alone |  |  | [tests/unit/log_channel_test.cpp:88](../../tests/unit/log_channel_test.cpp#L88) |
| nothing a line holds can close its code block |  |  | [tests/unit/log_channel_test.cpp:99](../../tests/unit/log_channel_test.cpp#L99) |
| a very long line is cut to fit one message |  |  | [tests/unit/log_channel_test.cpp:112](../../tests/unit/log_channel_test.cpp#L112) |
| a flood keeps its start and says how much was dropped |  |  | [tests/unit/log_channel_test.cpp:123](../../tests/unit/log_channel_test.cpp#L123) |
| the log channel is kept bot-wide and can be cleared |  |  | [tests/unit/log_channel_test.cpp:142](../../tests/unit/log_channel_test.cpp#L142) |
| a stored level that cannot be read is info, and the channel is kept |  |  | [tests/unit/log_channel_test.cpp:159](../../tests/unit/log_channel_test.cpp#L159) |
| the log is posted to its channel, silently, from its level up | `coro` |  | [tests/unit/log_channel_test.cpp:177](../../tests/unit/log_channel_test.cpp#L177) |
| the level can change without moving the channel | `coro` |  | [tests/unit/log_channel_test.cpp:200](../../tests/unit/log_channel_test.cpp#L200) |
| a failed post waits before trying again, longer each time | `coro` |  | [tests/unit/log_channel_test.cpp:214](../../tests/unit/log_channel_test.cpp#L214) |
| the first failure is logged, and the lines it lost are counted | `coro` |  | [tests/unit/log_channel_test.cpp:253](../../tests/unit/log_channel_test.cpp#L253) |
| the backoff stops growing at its longest | `coro` |  | [tests/unit/log_channel_test.cpp:271](../../tests/unit/log_channel_test.cpp#L271) |
| stopping throws away what was waiting and stops taking lines | `coro` |  | [tests/unit/log_channel_test.cpp:289](../../tests/unit/log_channel_test.cpp#L289) |
| a log channel unhooks itself from the logger when it goes |  |  | [tests/unit/log_channel_test.cpp:305](../../tests/unit/log_channel_test.cpp#L305) |
| the first message says what the channel will get |  |  | [tests/unit/log_channel_test.cpp:318](../../tests/unit/log_channel_test.cpp#L318) |
| stages run in the order they were added |  |  | [tests/unit/message_pipeline_test.cpp:51](../../tests/unit/message_pipeline_test.cpp#L51) |
| a stage that consumes the message stops the ones after it |  |  | [tests/unit/message_pipeline_test.cpp:64](../../tests/unit/message_pipeline_test.cpp#L64) |
| the bot never answers itself, or a bot this guild has not allowed |  | 3 | [tests/unit/message_pipeline_test.cpp:79](../../tests/unit/message_pipeline_test.cpp#L79) |
| an allowed bot reaches the stages |  |  | [tests/unit/message_pipeline_test.cpp:112](../../tests/unit/message_pipeline_test.cpp#L112) |
| a stage that throws is logged and the rest still run |  |  | [tests/unit/message_pipeline_test.cpp:127](../../tests/unit/message_pipeline_test.cpp#L127) |
| an empty pipeline decides nothing |  |  | [tests/unit/message_pipeline_test.cpp:144](../../tests/unit/message_pipeline_test.cpp#L144) |
| the local date is the one where the entry lives, not where the bot runs |  |  | [tests/unit/midnight_test.cpp:41](../../tests/unit/midnight_test.cpp#L41) |
| a zone this machine does not know is refused rather than guessed at |  |  | [tests/unit/midnight_test.cpp:51](../../tests/unit/midnight_test.cpp#L51) |
| an entry fires just after local midnight |  |  | [tests/unit/midnight_test.cpp:62](../../tests/unit/midnight_test.cpp#L62) |
| an entry that has posted today does not post again |  |  | [tests/unit/midnight_test.cpp:72](../../tests/unit/midnight_test.cpp#L72) |
| a midnight the bot was not running for is skipped, not posted late |  |  | [tests/unit/midnight_test.cpp:84](../../tests/unit/midnight_test.cpp#L84) |
| a restart shortly after midnight still posts |  |  | [tests/unit/midnight_test.cpp:100](../../tests/unit/midnight_test.cpp#L100) |
| an entry off is an entry that does not post |  |  | [tests/unit/midnight_test.cpp:112](../../tests/unit/midnight_test.cpp#L112) |
| two entries in different timezones fire at different times |  |  | [tests/unit/midnight_test.cpp:122](../../tests/unit/midnight_test.cpp#L122) |
| an entry added this afternoon waits for the next midnight |  |  | [tests/unit/midnight_test.cpp:135](../../tests/unit/midnight_test.cpp#L135) |
| a spring-forward night still has a midnight to fire at |  |  | [tests/unit/midnight_test.cpp:149](../../tests/unit/midnight_test.cpp#L149) |
| a fall-back night does not post twice |  |  | [tests/unit/midnight_test.cpp:157](../../tests/unit/midnight_test.cpp#L157) |
| a bad timezone in the database keeps quiet rather than posting wrongly |  |  | [tests/unit/midnight_test.cpp:166](../../tests/unit/midnight_test.cpp#L166) |
| timezone completion matches anywhere in the name |  |  | [tests/unit/midnight_test.cpp:177](../../tests/unit/midnight_test.cpp#L177) |
| timezone completion never offers more than it is asked for |  |  | [tests/unit/midnight_test.cpp:184](../../tests/unit/midnight_test.cpp#L184) |
| timezone completion finds nothing for nonsense |  |  | [tests/unit/midnight_test.cpp:191](../../tests/unit/midnight_test.cpp#L191) |
| a winter timestamp is read as Central Standard Time |  |  | [tests/unit/nickname_import_test.cpp:34](../../tests/unit/nickname_import_test.cpp#L34) |
| a summer timestamp is read as Central Daylight Time |  |  | [tests/unit/nickname_import_test.cpp:39](../../tests/unit/nickname_import_test.cpp#L39) |
| the hour that happens twice each November takes the earlier one |  |  | [tests/unit/nickname_import_test.cpp:44](../../tests/unit/nickname_import_test.cpp#L44) |
| the hour that never happens each March is shifted forward |  |  | [tests/unit/nickname_import_test.cpp:52](../../tests/unit/nickname_import_test.cpp#L52) |
| dates before the 2007 rule change use the rules of their own year |  |  | [tests/unit/nickname_import_test.cpp:62](../../tests/unit/nickname_import_test.cpp#L62) |
| a timestamp that is not one is refused rather than guessed at |  |  | [tests/unit/nickname_import_test.cpp:69](../../tests/unit/nickname_import_test.cpp#L69) |
| an imported author is kept only when it is not the guess |  |  | [tests/unit/nickname_import_test.cpp:80](../../tests/unit/nickname_import_test.cpp#L80) |
| a member's entries are read with their guild and id |  |  | [tests/unit/nickname_import_test.cpp:95](../../tests/unit/nickname_import_test.cpp#L95) |
| an imported entry keeps the text its time was read from |  |  | [tests/unit/nickname_import_test.cpp:124](../../tests/unit/nickname_import_test.cpp#L124) |
| a cleared nickname imports as nothing rather than as an empty name |  |  | [tests/unit/nickname_import_test.cpp:141](../../tests/unit/nickname_import_test.cpp#L141) |
| one unreadable entry does not lose the rest |  |  | [tests/unit/nickname_import_test.cpp:155](../../tests/unit/nickname_import_test.cpp#L155) |
| malformed shapes are named rather than dropped quietly |  | 5 | [tests/unit/nickname_import_test.cpp:174](../../tests/unit/nickname_import_test.cpp#L174) |
| an empty file imports nothing and complains about nothing |  |  | [tests/unit/nickname_import_test.cpp:199](../../tests/unit/nickname_import_test.cpp#L199) |
| a first sighting is recorded only when there is a nickname to record |  |  | [tests/unit/nicknames_test.cpp:40](../../tests/unit/nicknames_test.cpp#L40) |
| the same nickname again is not a change |  |  | [tests/unit/nicknames_test.cpp:48](../../tests/unit/nicknames_test.cpp#L48) |
| clearing a nickname is a change |  |  | [tests/unit/nicknames_test.cpp:55](../../tests/unit/nicknames_test.cpp#L55) |
| an audit entry describes a row by member and resulting nickname |  |  | [tests/unit/nicknames_test.cpp:70](../../tests/unit/nicknames_test.cpp#L70) |
| an audit entry can describe a cleared nickname |  |  | [tests/unit/nicknames_test.cpp:79](../../tests/unit/nicknames_test.cpp#L79) |
| the bot is never recorded as the one who made a change |  |  | [tests/unit/nicknames_test.cpp:86](../../tests/unit/nicknames_test.cpp#L86) |
| an audit entry with no actor attributes nothing |  |  | [tests/unit/nicknames_test.cpp:95](../../tests/unit/nicknames_test.cpp#L95) |
| a row that already names somebody is left alone |  |  | [tests/unit/nicknames_test.cpp:99](../../tests/unit/nicknames_test.cpp#L99) |
| an audit entry's nickname arrives as JSON rather than as text |  |  | [tests/unit/nicknames_test.cpp:106](../../tests/unit/nicknames_test.cpp#L106) |
| an unreadable audit value is treated as no nickname |  |  | [tests/unit/nicknames_test.cpp:120](../../tests/unit/nicknames_test.cpp#L120) |
| a change the bot just made is claimed once |  |  | [tests/unit/nicknames_test.cpp:129](../../tests/unit/nicknames_test.cpp#L129) |
| an expectation only matches the change it was made for |  |  | [tests/unit/nicknames_test.cpp:142](../../tests/unit/nicknames_test.cpp#L142) |
| an expectation stops applying once it has expired |  |  | [tests/unit/nicknames_test.cpp:153](../../tests/unit/nicknames_test.cpp#L153) |
| expired expectations are cleared out as new ones arrive |  |  | [tests/unit/nicknames_test.cpp:163](../../tests/unit/nicknames_test.cpp#L163) |
| a change Discord refused stops being expected |  |  | [tests/unit/nicknames_test.cpp:174](../../tests/unit/nicknames_test.cpp#L174) |
| clearing a nickname is expected and claimed like any other change |  |  | [tests/unit/nicknames_test.cpp:184](../../tests/unit/nicknames_test.cpp#L184) |
| a cleared nickname reads as cleared rather than as a blank |  |  | [tests/unit/nicknames_test.cpp:197](../../tests/unit/nicknames_test.cpp#L197) |
| who changed it is a mention, unknown, or nothing |  |  | [tests/unit/nicknames_test.cpp:203](../../tests/unit/nicknames_test.cpp#L203) |
| a history line carries the nickname, the time and the author |  |  | [tests/unit/nicknames_test.cpp:217](../../tests/unit/nicknames_test.cpp#L217) |
| an imported history line says nothing about who |  |  | [tests/unit/nicknames_test.cpp:229](../../tests/unit/nicknames_test.cpp#L229) |
| the attachment spells out times rather than leaving markup in a file |  |  | [tests/unit/nicknames_test.cpp:236](../../tests/unit/nicknames_test.cpp#L236) |
| one entry is not described as one entries |  |  | [tests/unit/nicknames_test.cpp:255](../../tests/unit/nicknames_test.cpp#L255) |
| a reaction is keyed by id when custom, by itself when Unicode |  |  | [tests/unit/reactions_test.cpp:12](../../tests/unit/reactions_test.cpp#L12) |
| the colour-form selector does not make a second emoji |  |  | [tests/unit/reactions_test.cpp:21](../../tests/unit/reactions_test.cpp#L21) |
| typed emojis are understood in every form a command sees |  |  | [tests/unit/reactions_test.cpp:26](../../tests/unit/reactions_test.cpp#L26) |
| an emoji is shown the way Discord draws it |  |  | [tests/unit/reactions_test.cpp:39](../../tests/unit/reactions_test.cpp#L39) |
| whole word matching ignores the middle of longer words |  |  | [tests/unit/triggers_test.cpp:19](../../tests/unit/triggers_test.cpp#L19) |
| a later occurrence still counts as a whole word |  |  | [tests/unit/triggers_test.cpp:29](../../tests/unit/triggers_test.cpp#L29) |
| substring matching does not care about boundaries |  |  | [tests/unit/triggers_test.cpp:35](../../tests/unit/triggers_test.cpp#L35) |
| matching ignores case on both sides |  |  | [tests/unit/triggers_test.cpp:40](../../tests/unit/triggers_test.cpp#L40) |
| a pattern with punctuation matches as a word |  |  | [tests/unit/triggers_test.cpp:45](../../tests/unit/triggers_test.cpp#L45) |
| an empty pattern never matches |  |  | [tests/unit/triggers_test.cpp:52](../../tests/unit/triggers_test.cpp#L52) |
| match modes parse from their stored and spoken names |  |  | [tests/unit/triggers_test.cpp:58](../../tests/unit/triggers_test.cpp#L58) |
| weighted responses are picked in proportion |  | 2 | [tests/unit/triggers_test.cpp:68](../../tests/unit/triggers_test.cpp#L68) |
| a trigger with nothing to say picks nothing |  | 1 | [tests/unit/triggers_test.cpp:97](../../tests/unit/triggers_test.cpp#L97) |
| a zero-weight response is skipped but its neighbours still work |  |  | [tests/unit/triggers_test.cpp:109](../../tests/unit/triggers_test.cpp#L109) |
| cooldowns are measured from the last reply |  |  | [tests/unit/triggers_test.cpp:121](../../tests/unit/triggers_test.cpp#L121) |
| a zero cooldown means no cooldown |  |  | [tests/unit/triggers_test.cpp:130](../../tests/unit/triggers_test.cpp#L130) |
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
| a voice session is kept until it ends, one per guild |  |  | [tests/unit/voice_sessions_test.cpp:28](../../tests/unit/voice_sessions_test.cpp#L28) |
| a session follows the bot when it is moved |  |  | [tests/unit/voice_sessions_test.cpp:49](../../tests/unit/voice_sessions_test.cpp#L49) |
| the bot leaves once it has been alone for the grace period |  |  | [tests/unit/voice_sessions_test.cpp:61](../../tests/unit/voice_sessions_test.cpp#L61) |
| someone coming back within the grace period keeps the bot |  |  | [tests/unit/voice_sessions_test.cpp:76](../../tests/unit/voice_sessions_test.cpp#L76) |
| being seen alone again does not restart the wait |  |  | [tests/unit/voice_sessions_test.cpp:93](../../tests/unit/voice_sessions_test.cpp#L93) |
| a bot that is not in voice, or has left, is not waited on |  |  | [tests/unit/voice_sessions_test.cpp:105](../../tests/unit/voice_sessions_test.cpp#L105) |
| each guild waits its own grace period |  |  | [tests/unit/voice_sessions_test.cpp:117](../../tests/unit/voice_sessions_test.cpp#L117) |

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
| every parameter is in exactly one group of at most five |  |  | [tests/unit/custom_voice_test.cpp:17](../../tests/unit/custom_voice_test.cpp#L17) |
| edits are clamped to DECtalk's limits and written in table order |  |  | [tests/unit/custom_voice_test.cpp:37](../../tests/unit/custom_voice_test.cpp#L37) |
| a voice reads back from [:dv] text, with or without brackets |  |  | [tests/unit/custom_voice_test.cpp:49](../../tests/unit/custom_voice_test.cpp#L49) |
| what cannot be read is reported and skipped |  |  | [tests/unit/custom_voice_test.cpp:68](../../tests/unit/custom_voice_test.cpp#L68) |
| a custom voice's preamble is rebuilt, not pasted |  |  | [tests/unit/custom_voice_test.cpp:84](../../tests/unit/custom_voice_test.cpp#L84) |
| custom voice names are short, plain and never a built-in's |  |  | [tests/unit/custom_voice_test.cpp:91](../../tests/unit/custom_voice_test.cpp#L91) |
| DECtalk speaks a phrase at 11025 Hz mono | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:46](../../tests/unit/dectalk_engine_test.cpp#L46) |
| the same request gives the same audio every time | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:59](../../tests/unit/dectalk_engine_test.cpp#L59) |
| one request's inline settings do not reach the next | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:70](../../tests/unit/dectalk_engine_test.cpp#L70) |
| the voice and rate settings change the audio | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:83](../../tests/unit/dectalk_engine_test.cpp#L83) |
| a custom voice's edits change the voice it is built on | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:94](../../tests/unit/dectalk_engine_test.cpp#L94) |
| volume scales the samples | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:109](../../tests/unit/dectalk_engine_test.cpp#L109) |
| an utterance stops at its maximum duration | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:119](../../tests/unit/dectalk_engine_test.cpp#L119) |
| an utterance that takes too long is abandoned | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:132](../../tests/unit/dectalk_engine_test.cpp#L132) |
| stop abandons the utterance being spoken and the queue | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:150](../../tests/unit/dectalk_engine_test.cpp#L150) |
| a missing dictionary fails the request instead of the process | `coro`, `threads` |  | [tests/unit/dectalk_engine_test.cpp:171](../../tests/unit/dectalk_engine_test.cpp#L171) |
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
| trailing silence is cut to a fixed tail |  | 4 | [tests/unit/pcm_test.cpp:61](../../tests/unit/pcm_test.cpp#L61) |
| resampling keeps the length and doubles every sample into stereo |  |  | [tests/unit/pcm_test.cpp:86](../../tests/unit/pcm_test.cpp#L86) |
| resampling keeps the frequency |  |  | [tests/unit/pcm_test.cpp:96](../../tests/unit/pcm_test.cpp#L96) |
| resampling interpolates between the source samples |  |  | [tests/unit/pcm_test.cpp:106](../../tests/unit/pcm_test.cpp#L106) |
| resampling nothing gives nothing |  |  | [tests/unit/pcm_test.cpp:120](../../tests/unit/pcm_test.cpp#L120) |
| resampling a minute of speech |  |  | [tests/unit/pcm_test.cpp:125](../../tests/unit/pcm_test.cpp#L125) |
| speech plays at once on a ready connection, each followed by its marker |  |  | [tests/unit/speech_queue_test.cpp:41](../../tests/unit/speech_queue_test.cpp#L41) |
| a finished utterance's marker moves the queue on |  |  | [tests/unit/speech_queue_test.cpp:55](../../tests/unit/speech_queue_test.cpp#L55) |
| markers that are not the queue's, or for another guild, change nothing |  |  | [tests/unit/speech_queue_test.cpp:72](../../tests/unit/speech_queue_test.cpp#L72) |
| speech waits for a connection still being set up, then plays in order |  |  | [tests/unit/speech_queue_test.cpp:83](../../tests/unit/speech_queue_test.cpp#L83) |
| new speech queues behind speech still waiting, even once connected |  |  | [tests/unit/speech_queue_test.cpp:102](../../tests/unit/speech_queue_test.cpp#L102) |
| skip drops only the utterance playing now |  |  | [tests/unit/speech_queue_test.cpp:117](../../tests/unit/speech_queue_test.cpp#L117) |
| skip with nothing playing does nothing |  |  | [tests/unit/speech_queue_test.cpp:132](../../tests/unit/speech_queue_test.cpp#L132) |
| stop drops everything, playing and waiting |  |  | [tests/unit/speech_queue_test.cpp:138](../../tests/unit/speech_queue_test.cpp#L138) |
| stop also stops speech still being synthesized |  |  | [tests/unit/speech_queue_test.cpp:155](../../tests/unit/speech_queue_test.cpp#L155) |
| stopping one guild leaves another alone |  |  | [tests/unit/speech_queue_test.cpp:169](../../tests/unit/speech_queue_test.cpp#L169) |
| forgetting a guild drops its speech without touching the connection |  |  | [tests/unit/speech_queue_test.cpp:180](../../tests/unit/speech_queue_test.cpp#L180) |
| music is kept a few seconds ahead, in whole packets |  |  | [tests/unit/voice_mixer_test.cpp:102](../../tests/unit/voice_mixer_test.cpp#L102) |
| speech interrupts music at once, and music resumes exactly where it stopped |  |  | [tests/unit/voice_mixer_test.cpp:118](../../tests/unit/voice_mixer_test.cpp#L118) |
| music waits for every utterance queued, not just the first |  |  | [tests/unit/voice_mixer_test.cpp:141](../../tests/unit/voice_mixer_test.cpp#L141) |
| stopping or skipping speech lets the music back in |  | 2 | [tests/unit/voice_mixer_test.cpp:157](../../tests/unit/voice_mixer_test.cpp#L157) |
| stopping speech when there is none leaves the music alone |  |  | [tests/unit/voice_mixer_test.cpp:177](../../tests/unit/voice_mixer_test.cpp#L177) |
| a track's end marker is reported once it has been heard |  |  | [tests/unit/voice_mixer_test.cpp:187](../../tests/unit/voice_mixer_test.cpp#L187) |
| a track end taken back by speech is still reported, after the speech |  |  | [tests/unit/voice_mixer_test.cpp:202](../../tests/unit/voice_mixer_test.cpp#L202) |
| pausing music stops it at once, and resuming loses nothing |  |  | [tests/unit/voice_mixer_test.cpp:217](../../tests/unit/voice_mixer_test.cpp#L217) |
| dropping music clears it, but never speech |  | 2 | [tests/unit/voice_mixer_test.cpp:233](../../tests/unit/voice_mixer_test.cpp#L233) |
| a new connection sends a track's end marker again |  |  | [tests/unit/voice_mixer_test.cpp:251](../../tests/unit/voice_mixer_test.cpp#L251) |
| an idle source is no longer visited until woken |  |  | [tests/unit/voice_mixer_test.cpp:266](../../tests/unit/voice_mixer_test.cpp#L266) |
| nothing is fed without a connection, or once the guild is forgotten |  |  | [tests/unit/voice_mixer_test.cpp:279](../../tests/unit/voice_mixer_test.cpp#L279) |
| the built-in voices are found by name, in any case |  |  | [tests/unit/voice_params_test.cpp:11](../../tests/unit/voice_params_test.cpp#L11) |
| the preamble selects the voice and says only what differs |  |  | [tests/unit/voice_params_test.cpp:19](../../tests/unit/voice_params_test.cpp#L19) |
| the preamble falls back to Paul and clamps the rate |  |  | [tests/unit/voice_params_test.cpp:26](../../tests/unit/voice_params_test.cpp#L26) |
| a WAV file has the RIFF header, then the samples little-endian |  |  | [tests/unit/wav_test.cpp:15](../../tests/unit/wav_test.cpp#L15) |
| a stereo WAV file counts both channels in its rates |  |  | [tests/unit/wav_test.cpp:40](../../tests/unit/wav_test.cpp#L40) |
| the waveform of silence is flat |  |  | [tests/unit/wav_test.cpp:49](../../tests/unit/wav_test.cpp#L49) |
| the waveform follows where the sound is |  |  | [tests/unit/wav_test.cpp:56](../../tests/unit/wav_test.cpp#L56) |
| a waveform of fewer samples than bars has one bar per sample |  |  | [tests/unit/wav_test.cpp:78](../../tests/unit/wav_test.cpp#L78) |
| the waveform is sent as base64 of its 256 bytes |  |  | [tests/unit/wav_test.cpp:83](../../tests/unit/wav_test.cpp#L83) |

## music

Music (`src/core/music`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| durations read as minutes and seconds, and hours past an hour |  |  | [tests/unit/music_command_test.cpp:52](../../tests/unit/music_command_test.cpp#L52) |
| a track is named safely, with its length or that it is live |  |  | [tests/unit/music_command_test.cpp:59](../../tests/unit/music_command_test.cpp#L59) |
| tracks over the limit are left out, and live streams never are |  |  | [tests/unit/music_command_test.cpp:82](../../tests/unit/music_command_test.cpp#L82) |
| the reply to /music play says what happened |  | 5 | [tests/unit/music_command_test.cpp:99](../../tests/unit/music_command_test.cpp#L99) |
| now playing shows the track, where it is, and who queued it |  |  | [tests/unit/music_command_test.cpp:153](../../tests/unit/music_command_test.cpp#L153) |
| the queue pages ten at a time, within Discord's limits, pinging nobody |  |  | [tests/unit/music_command_test.cpp:169](../../tests/unit/music_command_test.cpp#L169) |
| an empty queue says so, with no buttons |  |  | [tests/unit/music_command_test.cpp:192](../../tests/unit/music_command_test.cpp#L192) |
| volume and track limit have defaults, and stored values are kept in range |  |  | [tests/unit/music_command_test.cpp:202](../../tests/unit/music_command_test.cpp#L202) |
| the music command registers, with m as its alias |  |  | [tests/unit/music_command_test.cpp:218](../../tests/unit/music_command_test.cpp#L218) |
| an http or https link is taken as it is |  |  | [tests/unit/music_links_test.cpp:16](../../tests/unit/music_links_test.cpp#L16) |
| text that is not a link is refused, with the reason |  |  | [tests/unit/music_links_test.cpp:22](../../tests/unit/music_links_test.cpp#L22) |
| a link that looks like an option is still a link or nothing |  |  | [tests/unit/music_links_test.cpp:31](../../tests/unit/music_links_test.cpp#L31) |
| links into the host's own network are refused |  |  | [tests/unit/music_links_test.cpp:37](../../tests/unit/music_links_test.cpp#L37) |
| the host is read without credentials, port or brackets |  |  | [tests/unit/music_links_test.cpp:48](../../tests/unit/music_links_test.cpp#L48) |
| private IPv4 ranges |  |  | [tests/unit/music_links_test.cpp:54](../../tests/unit/music_links_test.cpp#L54) |
| private IPv6 ranges, and IPv4 inside IPv6 |  |  | [tests/unit/music_links_test.cpp:72](../../tests/unit/music_links_test.cpp#L72) |
| names of the machine itself resolve as private without asking DNS |  |  | [tests/unit/music_links_test.cpp:84](../../tests/unit/music_links_test.cpp#L84) |
| adding to a quiet server starts playing |  |  | [tests/unit/music_player_test.cpp:85](../../tests/unit/music_player_test.cpp#L85) |
| the queue plays in order, moving on when each track has been heard |  |  | [tests/unit/music_player_test.cpp:94](../../tests/unit/music_player_test.cpp#L94) |
| repeating a track plays it again, from a fresh fetch |  |  | [tests/unit/music_player_test.cpp:111](../../tests/unit/music_player_test.cpp#L111) |
| skip moves on with repeat on |  |  | [tests/unit/music_player_test.cpp:123](../../tests/unit/music_player_test.cpp#L123) |
| a track that cannot be fetched is noted, and the next one plays |  |  | [tests/unit/music_player_test.cpp:140](../../tests/unit/music_player_test.cpp#L140) |
| a track that breaks mid-way plays what it had, then moves on, never repeating |  |  | [tests/unit/music_player_test.cpp:155](../../tests/unit/music_player_test.cpp#L155) |
| play now plays at once, and the interrupted track starts over after it |  |  | [tests/unit/music_player_test.cpp:168](../../tests/unit/music_player_test.cpp#L168) |
| pausing holds the music, and time into the track with it |  |  | [tests/unit/music_player_test.cpp:185](../../tests/unit/music_player_test.cpp#L185) |
| pause, skip and stop with nothing playing say so |  |  | [tests/unit/music_player_test.cpp:202](../../tests/unit/music_player_test.cpp#L202) |
| stop ends the music and empties the queue |  |  | [tests/unit/music_player_test.cpp:209](../../tests/unit/music_player_test.cpp#L209) |
| clear, shuffle and remove work on what is queued |  |  | [tests/unit/music_player_test.cpp:223](../../tests/unit/music_player_test.cpp#L223) |
| the track limit cuts a long track off, with a note, but not a live stream |  |  | [tests/unit/music_player_test.cpp:235](../../tests/unit/music_player_test.cpp#L235) |
| the volume scales the samples |  |  | [tests/unit/music_player_test.cpp:253](../../tests/unit/music_player_test.cpp#L253) |
| each server has its own queue |  |  | [tests/unit/music_player_test.cpp:268](../../tests/unit/music_player_test.cpp#L268) |
| leaving forgets the queue |  |  | [tests/unit/music_player_test.cpp:282](../../tests/unit/music_player_test.cpp#L282) |
| speech pauses the music, which carries on after it |  |  | [tests/unit/music_player_test.cpp:293](../../tests/unit/music_player_test.cpp#L293) |
| markers are told apart |  |  | [tests/unit/music_player_test.cpp:309](../../tests/unit/music_player_test.cpp#L309) |
| end adds at the back |  |  | [tests/unit/music_queue_test.cpp:44](../../tests/unit/music_queue_test.cpp#L44) |
| next keeps a playlist in its own order |  |  | [tests/unit/music_queue_test.cpp:53](../../tests/unit/music_queue_test.cpp#L53) |
| now keeps the interrupted track, to play again from the start |  |  | [tests/unit/music_queue_test.cpp:61](../../tests/unit/music_queue_test.cpp#L61) |
| now with nothing playing just plays |  |  | [tests/unit/music_queue_test.cpp:73](../../tests/unit/music_queue_test.cpp#L73) |
| a full queue takes what fits, and says how many did not |  |  | [tests/unit/music_queue_test.cpp:80](../../tests/unit/music_queue_test.cpp#L80) |
| the queue limit is 500, and a playlist adds at most 100 |  |  | [tests/unit/music_queue_test.cpp:90](../../tests/unit/music_queue_test.cpp#L90) |
| moving on with repeat off plays the queue in order, then stops |  |  | [tests/unit/music_queue_test.cpp:95](../../tests/unit/music_queue_test.cpp#L95) |
| repeating a track plays it again when it finishes |  |  | [tests/unit/music_queue_test.cpp:103](../../tests/unit/music_queue_test.cpp#L103) |
| skip moves on even when the track repeats |  |  | [tests/unit/music_queue_test.cpp:111](../../tests/unit/music_queue_test.cpp#L111) |
| a track that failed never repeats |  | 2 | [tests/unit/music_queue_test.cpp:118](../../tests/unit/music_queue_test.cpp#L118) |
| repeating the queue sends each finished or skipped track to the back |  |  | [tests/unit/music_queue_test.cpp:132](../../tests/unit/music_queue_test.cpp#L132) |
| peeking at what plays next agrees with moving on |  |  | [tests/unit/music_queue_test.cpp:146](../../tests/unit/music_queue_test.cpp#L146) |
| remove counts from 1, as the queue is shown |  |  | [tests/unit/music_queue_test.cpp:164](../../tests/unit/music_queue_test.cpp#L164) |
| clear empties the queue and leaves the current track |  |  | [tests/unit/music_queue_test.cpp:173](../../tests/unit/music_queue_test.cpp#L173) |
| shuffle keeps every track, and never the current one |  |  | [tests/unit/music_queue_test.cpp:180](../../tests/unit/music_queue_test.cpp#L180) |
| repeat modes by name, and in turn |  |  | [tests/unit/music_queue_test.cpp:199](../../tests/unit/music_queue_test.cpp#L199) |
| the running time adds what is known and counts what is not |  |  | [tests/unit/music_queue_test.cpp:209](../../tests/unit/music_queue_test.cpp#L209) |
| yt-dlp reads a real link, and yt-dlp piped into ffmpeg plays it | `live` |  | [tests/unit/yt_dlp_live_test.cpp:27](../../tests/unit/yt_dlp_live_test.cpp#L27) |
| the link always follows --, and no config file is read |  |  | [tests/unit/yt_dlp_test.cpp:55](../../tests/unit/yt_dlp_test.cpp#L55) |
| reading a link asks for JSON, a flat playlist, and at most so many entries |  |  | [tests/unit/yt_dlp_test.cpp:66](../../tests/unit/yt_dlp_test.cpp#L66) |
| fetching writes the best audio to stdout, and says where ffmpeg is |  |  | [tests/unit/yt_dlp_test.cpp:76](../../tests/unit/yt_dlp_test.cpp#L76) |
| decoding reads a pipe and writes 48 kHz stereo 16-bit samples |  |  | [tests/unit/yt_dlp_test.cpp:88](../../tests/unit/yt_dlp_test.cpp#L88) |
| a single track's details |  |  | [tests/unit/yt_dlp_test.cpp:104](../../tests/unit/yt_dlp_test.cpp#L104) |
| a live stream has no length |  |  | [tests/unit/yt_dlp_test.cpp:119](../../tests/unit/yt_dlp_test.cpp#L119) |
| a playlist's entries, in order, up to the limit |  |  | [tests/unit/yt_dlp_test.cpp:126](../../tests/unit/yt_dlp_test.cpp#L126) |
| answers with nothing playable are errors |  |  | [tests/unit/yt_dlp_test.cpp:150](../../tests/unit/yt_dlp_test.cpp#L150) |
| yt-dlp's error line is what is shown |  |  | [tests/unit/yt_dlp_test.cpp:157](../../tests/unit/yt_dlp_test.cpp#L157) |
| the resolver runs yt-dlp and reads what it says | `threads`, `coro` | 5 | [tests/unit/yt_dlp_test.cpp:164](../../tests/unit/yt_dlp_test.cpp#L164) |
| a link yt-dlp takes too long over is given up on | `threads` |  | [tests/unit/yt_dlp_test.cpp:197](../../tests/unit/yt_dlp_test.cpp#L197) |
| a stream delivers every sample, in order, then finishes | `threads` |  | [tests/unit/yt_dlp_test.cpp:204](../../tests/unit/yt_dlp_test.cpp#L204) |
| a stream fails with the first program's error | `threads` |  | [tests/unit/yt_dlp_test.cpp:214](../../tests/unit/yt_dlp_test.cpp#L214) |
| a stream that produces nothing for too long has failed | `threads` |  | [tests/unit/yt_dlp_test.cpp:221](../../tests/unit/yt_dlp_test.cpp#L221) |
| a stream whose program cannot start has failed at once |  |  | [tests/unit/yt_dlp_test.cpp:228](../../tests/unit/yt_dlp_test.cpp#L228) |
| dropping a stream mid-way ends its programs | `threads` |  | [tests/unit/yt_dlp_test.cpp:234](../../tests/unit/yt_dlp_test.cpp#L234) |

## llm

The language model (`src/core/llm`)

| Test | Traits | Sections | Source |
|---|---|---:|---|
| a message is addressed by a mention, a reply, or starting with the bot's name |  |  | [tests/unit/llm_answer_test.cpp:166](../../tests/unit/llm_answer_test.cpp#L166) |
| an addressed message is handed to the model and consumed |  |  | [tests/unit/llm_answer_test.cpp:187](../../tests/unit/llm_answer_test.cpp#L187) |
| the model stays quiet in a guild that has not turned it on, or has no key |  |  | [tests/unit/llm_answer_test.cpp:201](../../tests/unit/llm_answer_test.cpp#L201) |
| an advanced trigger asks the model to speak up, unless a simple trigger already answered |  |  | [tests/unit/llm_answer_test.cpp:213](../../tests/unit/llm_answer_test.cpp#L213) |
| a blacklisted user or role is not answered, and the message is still consumed |  |  | [tests/unit/llm_answer_test.cpp:241](../../tests/unit/llm_answer_test.cpp#L241) |
| past a spend cap the bot says so once, then stays quiet |  |  | [tests/unit/llm_answer_test.cpp:253](../../tests/unit/llm_answer_test.cpp#L253) |
| one person asking too often is rate limited, per minute |  |  | [tests/unit/llm_answer_test.cpp:273](../../tests/unit/llm_answer_test.cpp#L273) |
| another bot is answered at the pace the guild set, until a person speaks |  |  | [tests/unit/llm_answer_test.cpp:289](../../tests/unit/llm_answer_test.cpp#L289) |
| a message in a voice session's text channel is answered out loud too |  |  | [tests/unit/llm_answer_test.cpp:312](../../tests/unit/llm_answer_test.cpp#L312) |
| an answer reads the channel, builds the prompt, records the spend and replies | `coro` |  | [tests/unit/llm_answer_test.cpp:326](../../tests/unit/llm_answer_test.cpp#L326) |
| the model can remember something about the person it is answering | `coro` |  | [tests/unit/llm_answer_test.cpp:370](../../tests/unit/llm_answer_test.cpp#L370) |
| the model may forget only what is about, or was saved for, whoever it is answering |  |  | [tests/unit/llm_answer_test.cpp:384](../../tests/unit/llm_answer_test.cpp#L384) |
| remember refuses what is too long, or a server that is full |  |  | [tests/unit/llm_answer_test.cpp:401](../../tests/unit/llm_answer_test.cpp#L401) |
| when the model fails, someone who asked hears so and a trigger stays silent | `coro` |  | [tests/unit/llm_answer_test.cpp:415](../../tests/unit/llm_answer_test.cpp#L415) |
| an advanced trigger's reply follows the style document, and posts silently | `coro` |  | [tests/unit/llm_answer_test.cpp:432](../../tests/unit/llm_answer_test.cpp#L432) |
| a spoken answer is sanitized as the model's, posted as spoken, and queued in voice | `coro` |  | [tests/unit/llm_answer_test.cpp:452](../../tests/unit/llm_answer_test.cpp#L452) |
| a long answer is posted as several messages, only the first a reply | `coro` |  | [tests/unit/llm_answer_test.cpp:470](../../tests/unit/llm_answer_test.cpp#L470) |
| the conversation keeps the newest messages that fit the token budget |  |  | [tests/unit/llm_answer_test.cpp:489](../../tests/unit/llm_answer_test.cpp#L489) |
| a transcript line cannot pass itself off as someone else speaking |  |  | [tests/unit/llm_answer_test.cpp:509](../../tests/unit/llm_answer_test.cpp#L509) |
| the fixed rules come first, then the system document, then the personality |  |  | [tests/unit/llm_answer_test.cpp:519](../../tests/unit/llm_answer_test.cpp#L519) |
| a reply too long for one message is split on line breaks, three messages at most |  |  | [tests/unit/llm_answer_test.cpp:532](../../tests/unit/llm_answer_test.cpp#L532) |
| a rate limit allows so many per window, then frees up as they age |  |  | [tests/unit/llm_guards_test.cpp:44](../../tests/unit/llm_guards_test.cpp#L44) |
| bot turns in a row stop at the limit until a person speaks |  |  | [tests/unit/llm_guards_test.cpp:67](../../tests/unit/llm_guards_test.cpp#L67) |
| a bot turn soon after the last one waits out the delay |  |  | [tests/unit/llm_guards_test.cpp:82](../../tests/unit/llm_guards_test.cpp#L82) |
| the day's bot turns are capped per guild, and come back the next day |  |  | [tests/unit/llm_guards_test.cpp:94](../../tests/unit/llm_guards_test.cpp#L94) |
| pacing can insist on a person first, or refuse bots entirely |  |  | [tests/unit/llm_guards_test.cpp:107](../../tests/unit/llm_guards_test.cpp#L107) |
| an advanced trigger fires on its roll, then waits out its cooldown in that channel |  |  | [tests/unit/llm_guards_test.cpp:124](../../tests/unit/llm_guards_test.cpp#L124) |
| an advanced trigger needs its pattern, and to be on |  |  | [tests/unit/llm_guards_test.cpp:141](../../tests/unit/llm_guards_test.cpp#L141) |
| a setting out of its range is refused with the range |  |  | [tests/unit/llm_guards_test.cpp:155](../../tests/unit/llm_guards_test.cpp#L155) |
| every setting fits a modal: labels short enough, and at most five to a form |  |  | [tests/unit/llm_guards_test.cpp:174](../../tests/unit/llm_guards_test.cpp#L174) |
| a diff shows removed and added lines, and only the unchanged lines near them |  |  | [tests/unit/llm_guards_test.cpp:192](../../tests/unit/llm_guards_test.cpp#L192) |
| a memory search is made of the message's words, quoted, and never of FTS syntax |  |  | [tests/unit/llm_guards_test.cpp:198](../../tests/unit/llm_guards_test.cpp#L198) |
| token estimates are a quarter of the characters, rounded up |  |  | [tests/unit/llm_guards_test.cpp:205](../../tests/unit/llm_guards_test.cpp#L205) |
| every model has a price, and the ids are the API's own |  |  | [tests/unit/llm_provider_test.cpp:52](../../tests/unit/llm_provider_test.cpp#L52) |
| a call costs its tokens at the model's prices, cache included |  |  | [tests/unit/llm_provider_test.cpp:64](../../tests/unit/llm_provider_test.cpp#L64) |
| provider names are read case-insensitively |  |  | [tests/unit/llm_provider_test.cpp:74](../../tests/unit/llm_provider_test.cpp#L74) |
| an Anthropic request caches the stable instructions and nothing after them |  |  | [tests/unit/llm_provider_test.cpp:84](../../tests/unit/llm_provider_test.cpp#L84) |
| an Anthropic request never sends temperature, and sends effort only to models that take it |  |  | [tests/unit/llm_provider_test.cpp:96](../../tests/unit/llm_provider_test.cpp#L96) |
| the last Anthropic round forbids tools but still declares them |  |  | [tests/unit/llm_provider_test.cpp:110](../../tests/unit/llm_provider_test.cpp#L110) |
| an Anthropic request sends tool calls and their results in the API's shape |  |  | [tests/unit/llm_provider_test.cpp:119](../../tests/unit/llm_provider_test.cpp#L119) |
| an assistant turn Anthropic wrote goes back exactly as it came, thinking included |  |  | [tests/unit/llm_provider_test.cpp:139](../../tests/unit/llm_provider_test.cpp#L139) |
| an Anthropic reply is read into text, calls, usage and a stop reason |  |  | [tests/unit/llm_provider_test.cpp:160](../../tests/unit/llm_provider_test.cpp#L160) |
| an Anthropic error carries the status and the API's own message |  |  | [tests/unit/llm_provider_test.cpp:182](../../tests/unit/llm_provider_test.cpp#L182) |
| an Anthropic reply of the wrong shape is an error, not a crash |  |  | [tests/unit/llm_provider_test.cpp:190](../../tests/unit/llm_provider_test.cpp#L190) |
| the Anthropic provider sends its key and version, and posts to the Messages API | `coro` |  | [tests/unit/llm_provider_test.cpp:202](../../tests/unit/llm_provider_test.cpp#L202) |
| a transport failure reaches the caller as an error | `coro` |  | [tests/unit/llm_provider_test.cpp:221](../../tests/unit/llm_provider_test.cpp#L221) |
| an OpenAI request puts the instructions in one system message, stable part first |  |  | [tests/unit/llm_provider_test.cpp:236](../../tests/unit/llm_provider_test.cpp#L236) |
| an OpenAI request sends each tool result as its own message |  |  | [tests/unit/llm_provider_test.cpp:249](../../tests/unit/llm_provider_test.cpp#L249) |
| an OpenAI reply is read into calls, and cached input is counted apart |  |  | [tests/unit/llm_provider_test.cpp:265](../../tests/unit/llm_provider_test.cpp#L265) |
| an OpenAI refusal is a refusal, with its explanation as the text |  |  | [tests/unit/llm_provider_test.cpp:287](../../tests/unit/llm_provider_test.cpp#L287) |
| an OpenAI error carries the API's message |  |  | [tests/unit/llm_provider_test.cpp:295](../../tests/unit/llm_provider_test.cpp#L295) |
| the OpenAI provider authenticates with a bearer token | `coro` |  | [tests/unit/llm_provider_test.cpp:301](../../tests/unit/llm_provider_test.cpp#L301) |
| a tool registered twice is refused |  |  | [tests/unit/llm_tools_test.cpp:44](../../tests/unit/llm_tools_test.cpp#L44) |
| an unknown tool, or one that throws, is an error the model reads |  |  | [tests/unit/llm_tools_test.cpp:49](../../tests/unit/llm_tools_test.cpp#L49) |
| the tool loop runs what the model asks for and hands the result back | `coro` |  | [tests/unit/llm_tools_test.cpp:64](../../tests/unit/llm_tools_test.cpp#L64) |
| after the last round of tools the model has to answer | `coro` |  | [tests/unit/llm_tools_test.cpp:93](../../tests/unit/llm_tools_test.cpp#L93) |
| a failure mid-loop is reported, and what was spent before it still counted | `coro` |  | [tests/unit/llm_tools_test.cpp:113](../../tests/unit/llm_tools_test.cpp#L113) |
| when the last turn says nothing, what was said along the way is kept | `coro` |  | [tests/unit/llm_tools_test.cpp:128](../../tests/unit/llm_tools_test.cpp#L128) |

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
| level names round trip |  |  | [tests/unit/log_test.cpp:19](../../tests/unit/log_test.cpp#L19) |
| level names are case-insensitive and unknown names are reported |  |  | [tests/unit/log_test.cpp:27](../../tests/unit/log_test.cpp#L27) |
| messages below the level are dropped |  |  | [tests/unit/log_test.cpp:34](../../tests/unit/log_test.cpp#L34) |
| off silences everything |  |  | [tests/unit/log_test.cpp:47](../../tests/unit/log_test.cpp#L47) |
| arguments are formatted into the message |  |  | [tests/unit/log_test.cpp:55](../../tests/unit/log_test.cpp#L55) |
| a message is never split between threads | `threads` |  | [tests/unit/log_test.cpp:63](../../tests/unit/log_test.cpp#L63) |
| a tap gets lines below the logger's own level when it asks for them |  |  | [tests/unit/log_test.cpp:114](../../tests/unit/log_test.cpp#L114) |
| a tap above the logger's level leaves out what it did not ask for |  |  | [tests/unit/log_test.cpp:131](../../tests/unit/log_test.cpp#L131) |
| a removed tap gets nothing more |  |  | [tests/unit/log_test.cpp:143](../../tests/unit/log_test.cpp#L143) |
| colour is stripped and the text kept |  |  | [tests/unit/log_test.cpp:155](../../tests/unit/log_test.cpp#L155) |

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
| text from outside is made plain |  |  | [tests/unit/music_command_test.cpp:73](../../tests/unit/music_command_test.cpp#L73) |
| an argument with nothing special is left as it is |  |  | [tests/unit/process_test.cpp:52](../../tests/unit/process_test.cpp#L52) |
| spaces, quotes and backslashes before them are quoted |  |  | [tests/unit/process_test.cpp:58](../../tests/unit/process_test.cpp#L58) |
| the command line starts with the program, quoted |  |  | [tests/unit/process_test.cpp:66](../../tests/unit/process_test.cpp#L66) |
| a program reads back exactly the arguments it was given | `threads` |  | [tests/unit/process_test.cpp:71](../../tests/unit/process_test.cpp#L71) |
| run collects stdout, stderr and the exit code | `threads` | 3 | [tests/unit/process_test.cpp:85](../../tests/unit/process_test.cpp#L85) |
| a program that runs too long is killed | `threads` |  | [tests/unit/process_test.cpp:102](../../tests/unit/process_test.cpp#L102) |
| output past the limit kills the program | `threads` |  | [tests/unit/process_test.cpp:109](../../tests/unit/process_test.cpp#L109) |
| a program that cannot be found is refused |  |  | [tests/unit/process_test.cpp:114](../../tests/unit/process_test.cpp#L114) |
| one program's output is the next one's input | `threads` |  | [tests/unit/process_test.cpp:118](../../tests/unit/process_test.cpp#L118) |
| stderr lines say which program in the pipeline wrote them | `threads` |  | [tests/unit/process_test.cpp:135](../../tests/unit/process_test.cpp#L135) |
| killing a pipeline ends its programs and what they started | `threads` |  | [tests/unit/process_test.cpp:154](../../tests/unit/process_test.cpp#L154) |
| count_occurrences counts non-overlapping matches |  |  | [tests/unit/text_test.cpp:11](../../tests/unit/text_test.cpp#L11) |
| trim removes surrounding whitespace only |  | 1 | [tests/unit/text_test.cpp:20](../../tests/unit/text_test.cpp#L20) |
| is_blank treats whitespace as empty |  |  | [tests/unit/text_test.cpp:31](../../tests/unit/text_test.cpp#L31) |
| character_count counts characters, not bytes |  |  | [tests/unit/text_test.cpp:38](../../tests/unit/text_test.cpp#L38) |
| truncate cuts to a character limit and marks the cut |  | 4 | [tests/unit/text_test.cpp:47](../../tests/unit/text_test.cpp#L47) |
| lines that fit are kept whole, and lines that do not are cut evenly |  |  | [tests/unit/text_test.cpp:72](../../tests/unit/text_test.cpp#L72) |
| a Discord ID is read as digits and nothing else |  |  | [tests/unit/text_test.cpp:85](../../tests/unit/text_test.cpp#L85) |
| to_lower lowercases ASCII letters and leaves everything else |  |  | [tests/unit/text_test.cpp:102](../../tests/unit/text_test.cpp#L102) |
| equals_ignoring_case compares ASCII case-insensitively |  |  | [tests/unit/text_test.cpp:110](../../tests/unit/text_test.cpp#L110) |
| lines splits on newlines, CRLF included, and keeps the last line |  |  | [tests/unit/text_test.cpp:118](../../tests/unit/text_test.cpp#L118) |
| every link in a message is found, not just the first |  |  | [tests/unit/url_scan_test.cpp:43](../../tests/unit/url_scan_test.cpp#L43) |
| a spoiler is an odd number of || before the link |  | 6 | [tests/unit/url_scan_test.cpp:53](../../tests/unit/url_scan_test.cpp#L53) |
| trailing punctuation is not part of a link |  |  | [tests/unit/url_scan_test.cpp:101](../../tests/unit/url_scan_test.cpp#L101) |
| a closing bracket stays only when the link opened one |  |  | [tests/unit/url_scan_test.cpp:108](../../tests/unit/url_scan_test.cpp#L108) |
| an underscore at the end of a link is kept |  |  | [tests/unit/url_scan_test.cpp:115](../../tests/unit/url_scan_test.cpp#L115) |
| a link in angle brackets is marked as having its preview turned off |  | 2 | [tests/unit/url_scan_test.cpp:120](../../tests/unit/url_scan_test.cpp#L120) |
| links in code are marked as code |  | 3 | [tests/unit/url_scan_test.cpp:142](../../tests/unit/url_scan_test.cpp#L142) |
| a code span runs to the next run of backticks as long as its own |  |  | [tests/unit/url_scan_test.cpp:164](../../tests/unit/url_scan_test.cpp#L164) |
| a scheme glued to a word is not a link |  |  | [tests/unit/url_scan_test.cpp:174](../../tests/unit/url_scan_test.cpp#L174) |
| the scheme may be in any case |  |  | [tests/unit/url_scan_test.cpp:179](../../tests/unit/url_scan_test.cpp#L179) |
| offsets point back into the scanned text |  |  | [tests/unit/url_scan_test.cpp:183](../../tests/unit/url_scan_test.cpp#L183) |
| split_url separates every part |  |  | [tests/unit/url_scan_test.cpp:194](../../tests/unit/url_scan_test.cpp#L194) |
| rule_host reduces a host to what a rule is keyed by |  |  | [tests/unit/url_scan_test.cpp:208](../../tests/unit/url_scan_test.cpp#L208) |
| rehost keeps the path, query and fragment |  |  | [tests/unit/url_scan_test.cpp:216](../../tests/unit/url_scan_test.cpp#L216) |
| a translation suffix goes on the path, before the query |  | 3 | [tests/unit/url_scan_test.cpp:222](../../tests/unit/url_scan_test.cpp#L222) |
| 100 KB of link-shaped junk is scanned quickly |  |  | [tests/unit/url_scan_test.cpp:244](../../tests/unit/url_scan_test.cpp#L244) |
| one link followed by thousands of brackets is still linear |  |  | [tests/unit/url_scan_test.cpp:262](../../tests/unit/url_scan_test.cpp#L262) |
| scanning a typical message |  |  | [tests/unit/url_scan_test.cpp:274](../../tests/unit/url_scan_test.cpp#L274) |
| version string matches the version constants |  |  | [tests/unit/version_test.cpp:7](../../tests/unit/version_test.cpp#L7) |
