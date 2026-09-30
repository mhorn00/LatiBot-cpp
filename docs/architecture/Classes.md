# Classes and structs

The types behind the components in [Components.md](Components.md), one
diagram per area. Each box shows only the members that explain the design.
The header is always the whole truth, and the path under each heading says
where to find it. [README.md](README.md) explains the notation.

Class names are drawn without their namespace. The namespace is the folder:
`trigger_store` is `latibot::events::trigger_store` in `src/core/events/`.

- [1. What `bot` owns](#1-what-bot-owns)
- [2. Ports and what implements them](#2-ports-and-what-implements-them)
- [3. The database and the stores](#3-the-database-and-the-stores)
- [4. Slash commands](#4-slash-commands)
- [5. Panels: buttons, menus and forms](#5-panels-buttons-menus-and-forms)
- [6. The message pipeline](#6-the-message-pipeline)
- [7. Link replacement](#7-link-replacement)
- [8. Link stats and the recompute](#8-link-stats-and-the-recompute)
- [9. Speech](#9-speech)
- [10. The language model: deciding to answer](#10-the-language-model-deciding-to-answer)
- [11. The language model: answering](#11-the-language-model-answering)
- [12. Triggers, nicknames and midnight messages](#12-triggers-nicknames-and-midnight-messages)
- [13. Configuration and logging](#13-configuration-and-logging)

## 1. What `bot` owns

`src/core/bot.hpp`

`latibot::bot` holds one of everything as a member, by value. Nothing of
ours is global except the logger, and the one DECtalk utterance being made
at a time. Most of the objects below it
hold plain pointers to each other, and those pointers stay valid because
`bot` outlives all of them.

C++ builds members in the order they are declared and destroys them in the
reverse order, so the order here is the design. Each object is built after
everything it points to, and destroyed before it. This is a flowchart
rather than a class diagram because there is one class and the point is its
member order: read it top to bottom for construction, and bottom to top for
destruction.

```mermaid
---
config:
  flowchart:
    wrappingWidth: 600
---
flowchart TB
    bot(["latibot::bot"])

    storage["<b>Settings and storage</b><br/>settings_ : config::bootstrap<br/>database_ : db::database<br/>guild_settings_ : config::guild_settings"]
    discord["<b>Discord</b><br/>cluster_ : dpp::cluster<br/>commands_ : commands::registry, which owns the 22 commands<br/>gateway_ : discord::dpp_gateway<br/>http_ : discord::dpp_http_client<br/>raw_ : discord::raw_api<br/>clock_ : ports::system_clock"]
    message["<b>Message features</b><br/>bot_allowlist_, nicknames_, pending_nicknames_<br/>triggers_, trigger_panel_, trigger_responder_<br/>midnight_, midnight_scheduler_<br/>url_rules_, url_panel_, replacements_, reactions_<br/>backfill_progress_, backfill_, embed_tracker_<br/>pipeline_ : events::pipeline"]
    speech["<b>Speech</b><br/>tts_ : audio::dectalk_engine, starts its worker thread<br/>voice_output_, speech_ : audio::speech_queue<br/>voice_sessions_, auto_leave_<br/>voices_ : audio::voice_store, voice_drafts_, voice_lab_"]
    llm["<b>Language model</b><br/>llm_usage_, llm_documents_, llm_memories_<br/>llm_blacklist_, llm_triggers_, llm_tools_<br/>anthropic_, openai_ : unique_ptr to llm::provider, null without a key<br/>responder_ : llm::responder<br/>llm_stage_ : llm::llm_stage<br/>llm_panels_ : commands::llm_panels"]
    logging["<b>Logging</b><br/>log_destinations_ : events::log_destination_store<br/>log_channel_ : events::log_channel, taps the logger"]
    rest["<b>Last</b><br/>stranded_ : replacements the last run left, by server<br/>goodbye_ : std::jthread, the pause before shutting down"]

    bot --> storage --> discord --> message --> speech --> llm --> logging --> rest
```

Three consequences of the order are worth knowing:

- `goodbye_` is last, so it is destroyed first. Its thread is joined while
  the cluster it shuts down still exists.
- `log_channel_` is destroyed before the features. It unhooks itself from
  the logger as it goes, so anything logged during shutdown goes to the
  console only.
- `tts_` is declared after `cluster_`, so the speech engine's worker thread
  is stopped before the cluster is destroyed.

## 2. Ports and what implements them

`src/core/ports/`, `src/core/discord/`, `tests/mocks/`

A *port* is an interface for talking to something outside the bot. The bot
gets an implementation that uses DPP, and a test gets a mock that records
what it was asked and answers what the test told it to. A feature only ever
holds the interface, so it cannot tell which one it has. `llm::provider` is
a port too, for the model APIs.

The dotted line with a hollow triangle means **implements**.

```mermaid
classDiagram
    direction TB

    class clock {
        <<interface>>
        +now() system_clock time_point
        +steady_now() steady_clock time_point
    }
    class discord_gateway {
        <<interface>>
        +send_message(message) task~result~message~~
        +edit_message(message) task~result~message~~
        +delete_message(channel, message) task~result~void~~
        +set_embeds_suppressed(channel, message, bool) task~result~void~~
        +get_message(channel, message) task~result~message~~
        +get_messages(channel, before, limit) task~result~vector~message~~~
        +get_reaction_users(...) task~result~vector~snowflake~~~
        +start_typing(channel) task~result~void~~
    }
    class http_client {
        <<interface>>
        +send(http_request) task~result~http_response~~
    }
    class voice_output {
        <<interface>>
        +ready(guild) bool
        +play(guild, audio, marker) bool
        +skip(guild)
        +stop(guild)
    }
    class tts_engine {
        <<interface>>
        +synthesize(speech_request) task~result~pcm_audio~~
        +stop()
    }
    class provider {
        <<interface>>
        +name() string_view
        +complete(request) task~result~response~~
    }
    class result~T~ {
        -data_ : variant~T, api_error~
        +ok() bool
        +value() T
        +error() api_error
    }
    class api_error {
        <<struct>>
        +http_status : int
        +message : string
    }

    class system_clock
    class dpp_gateway
    class dpp_http_client
    class dpp_voice_output
    class dectalk_engine
    class anthropic_provider
    class openai_provider

    clock <|.. system_clock
    discord_gateway <|.. dpp_gateway
    http_client <|.. dpp_http_client
    voice_output <|.. dpp_voice_output
    tts_engine <|.. dectalk_engine
    provider <|.. anthropic_provider
    provider <|.. openai_provider
    anthropic_provider o-- http_client : sends through
    openai_provider o-- http_client : sends through
    result~T~ --> api_error : or

    class mock_clock
    class mock_discord
    class mock_http
    class mock_voice
    class mock_tts
    class mock_llm
    clock <|.. mock_clock
    discord_gateway <|.. mock_discord
    http_client <|.. mock_http
    voice_output <|.. mock_voice
    tts_engine <|.. mock_tts
    provider <|.. mock_llm
```

Everything a port returns is a `dpp::task`, a coroutine the caller
`co_await`s, holding a `result<T>`: either the value or an `api_error`,
never an exception. The adapters in `discord/` wrap DPP's own coroutine
calls (`co_message_create` and so on). `dectalk_engine` is ours: it queues
the job for its worker thread and resumes the caller when that thread
fulfils a promise (see [Execution_Flow.md §9](Execution_Flow.md#9-speech)).

`discord::raw_api` is not a port. It calls Discord endpoints that DPP has no
function for, and only `/chat` uses it, to send a voice message.

## 3. The database and the stores

`src/core/db/`, `src/core/config/guild_settings.hpp`, and each feature's store

Every table belongs to one *store*. A store holds a pointer to the
`database`, and turns rows into structs and structs into rows. Nothing
outside the stores writes SQL.

```mermaid
classDiagram
    direction LR

    class database {
        -handle_ : sqlite3 handle
        -mutex_ : recursive_mutex
        +execute(sql)
        +prepare(sql, args...) statement
        +last_insert_rowid() int64
        +changes() int
        +user_version() int
        +lock() unique_lock
    }
    class statement {
        -lock_ : unique_lock
        +bind(index, value) statement
        +bind_all(args...) statement
        +step() bool
        +run()
        +get~T~(column) T
    }
    class transaction {
        -lock_ : unique_lock
        +commit()
        +rollback()
    }
    class migration {
        <<struct>>
        +version : int
        +name : string_view
        +sql : string_view
    }
    class db_error {
        +code() int
    }

    database ..> statement : prepare() makes
    transaction o-- database
    database ..> migration : migrate() applies
    database ..> db_error : throws

    class guild_settings {
        +get(guild, key, fallback) string
        +get_int / get_bool / get_real()
        +set / set_int / set_bool / set_real()
        +erase(guild, key) bool
    }
    class log_destination_store

    class bot_allowlist
    class trigger_store
    class nickname_store
    class midnight_store
    class url_rule_store
    class replacement_store
    class reaction_store
    class backfill_progress_store
    class voice_store
    class document_store
    class memory_store
    class usage_store
    class blacklist_store
    class advanced_trigger_store

    guild_settings o-- database
    log_destination_store o-- guild_settings
    bot_allowlist o-- database
    trigger_store o-- database
    nickname_store o-- database
    midnight_store o-- database
    url_rule_store o-- database
    replacement_store o-- database
    reaction_store o-- database
    backfill_progress_store o-- database
    voice_store o-- database
    document_store o-- database
    memory_store o-- database
    usage_store o-- database
    blacklist_store o-- database
    advanced_trigger_store o-- database
```

How it fits together:

- **One connection, one lock.** `database` holds one SQLite connection and
  one `recursive_mutex`. A `statement` locks the mutex when it is prepared
  and unlocks it when it is destroyed. A `transaction` locks it too, so a
  store can run several statements as one.
- **Typed binding.** `bind` and `get<T>` convert the types the bot uses:
  `dpp::snowflake`, `std::chrono` times, `std::optional` (as NULL), strings,
  numbers and blobs.
- **Migrations.** `db::migrate` runs at startup and applies every migration
  newer than the file's `user_version`. Shipped migrations are never
  edited; a change is a new one.
- **Settings.** `guild_settings` is the key/value table, for features that
  need a single value per server rather than a table of their own.
  `config::bot_wide` (server id 0) is the key for values that apply to the
  whole bot, such as `/status`.

## 4. Slash commands

`src/core/commands/registry.hpp` and one header per command

Every slash command is a class derived from `command`. The `registry` owns
them, builds their Discord definitions when the bot connects, and sends each
invocation to the right one.

```mermaid
classDiagram
    direction LR

    class registry {
        -commands_ : vector~unique_ptr~command~~
        -by_name_ : map~string, command~
        +add(unique_ptr~command~)
        +find(name_or_alias) command
        +build_all(application_id) vector~slashcommand~
        +required_bot_permissions() uint64
        +dispatch(name, event) task
        +offer_completions(name, event)
    }
    class command {
        <<abstract>>
        +info()* command_info
        +build(name, application_id) slashcommand
        +execute(event)* task
        +autocomplete(event)
        +result(event, text) message
        +refusal(event, text) message
        +post(event, message) message
        +defer(event) task
        +answer_deferred(event, message) task
    }
    class command_info {
        <<struct>>
        +name : string
        +description : string
        +aliases : vector~string~
        +required_bot_permissions : uint64
        +default_member_permissions : optional
        +guild_only : bool
        +responses : response_flags
        +subcommand_responses : map
    }
    class response_flags {
        <<struct>>
        +result : message_flags
        +refusal : message_flags
        +post : message_flags
    }

    registry "1" *-- "22" command : owns
    command ..> command_info : describes itself with
    command_info --> response_flags

    command <|-- ping_command
    command <|-- say_command
    command <|-- status_command
    command <|-- join_command
    command <|-- leave_command
    command <|-- shutdown_command
    command <|-- goodbye_command
    command <|-- trigger_command
    command <|-- bots_command
    command <|-- nickname_command
    command <|-- nicknames_command
    command <|-- midnight_command
    command <|-- urlrepl_command
    command <|-- urltoggle_command
    command <|-- logs_command
    command <|-- llm_command
    command <|-- memory_command
    command <|-- speak_command
    command <|-- tts_command
    command <|-- chat_command
    command <|-- voice_command
    command <|-- linkstats_command
```

A command answers in one of three ways, and `command_info.responses` says
which Discord flags each gets: a `result` (usually only the invoker sees
it), a `refusal` (always private), or a `post` (seen by everyone). Commands
that take longer than Discord's 3 seconds call `defer` first, then
`answer_deferred`.

What each command is handed when `bot::register_commands` builds it:

| Command | Class | Handed | Opens a panel |
| --- | --- | --- | --- |
| `/ping` | `ping_command` | clock | |
| `/say` | `say_command` | `dpp::cluster` | |
| `/status` | `status_command` | `dpp::cluster`, guild_settings | |
| `/join`, `/leave` | `join_command`, `leave_command` | nothing | |
| `/shutdown` | `shutdown_command` | a function that shuts the cluster down | |
| `/goodbye` | `goodbye_command` | guild_settings | |
| `/trigger` | `trigger_command` | trigger_store | trigger panel |
| `/bots` | `bots_command` | bot_allowlist | |
| `/nickname` | `nickname_command` | nickname_store, pending_nicknames, clock, `dpp::cluster` | |
| `/nicknames` | `nicknames_command` | nickname_store | nickname history pages |
| `/midnight` | `midnight_command` | midnight_store, clock | |
| `/urlrepl` | `urlrepl_command` | url_rule_store | URL panel |
| `/urltoggle` | `urltoggle_command` | url_rule_store | |
| `/logs` | `logs_command` | bootstrap, log_destination_store, log_channel, discord_gateway | |
| `/llm` | `llm_command` | `llm_command_services` | settings panel, document forms |
| `/memory` | `memory_command` | `llm_command_services` | memory list pages |
| `/speak` | `speak_command` | `speech_services` | |
| `/tts` | `tts_command` | `speech_services` | |
| `/chat` | `chat_command` | `speech_services`, raw_api | |
| `/voice` | `voice_command` | voice_sessions, guild_settings, voice_store, voice_lab | voice lab |
| `/linkstats` | `linkstats_command` | reaction_store, `recompute_support` | leaderboard pages |

`speech_services`, `llm_command_services` and `recompute_support` are
structs of pointers, so a command that needs many things takes one argument
and a test can fill in only the fields it uses.

## 5. Panels: buttons, menus and forms

`src/core/ui/`, `src/core/commands/{trigger,urlrepl,voice_lab,llm}.hpp`

A panel is a message with buttons and menus that edits itself when used.
Nothing about an open panel is kept in memory. Everything a button needs is
written into its `custom_id` as `view:page:argument`. For example, `trigedit:0:42` is "edit
trigger 42, from the first page". That is why panels still
work after a restart.

```mermaid
classDiagram
    direction TB

    class page_state {
        <<struct>>
        +view : string
        +page : int
        +argument : string
    }
    class ui {
        <<functions>>
        +encode(page_state) optional~string~
        +decode(custom_id) optional~page_state~
        +controls(state, total, per_page) component
        +form_fields(form_submit) form_values
        +update_panel(event, message)
        +answer_privately(event, text)
        +detach(task, what) job
    }

    class trigger_panel {
        +on_component(event, state, chosen) bool
        +on_form(event, state) bool
        -toggle(event, id, trigger_toggle)
    }
    class url_panel {
        +on_component(event, state, chosen) bool
        +on_form(event, state) bool
    }
    class voice_lab {
        +open(guild, user, from) optional~message~
        +on_component(event, state, chosen) bool
        +on_form(event, state) bool
        -test(guild, user, custom_voice) task
        -save(event, draft, name)
    }
    class llm_panels {
        +settings_panel(guild) message
        +on_component(event, state, chosen) bool
        +on_form(event, state) bool
    }
    class voice_drafts {
        -drafts_ : map~guild and user, kept draft~
        +get(guild, user) voice_draft
        +put(guild, user, voice_draft)
    }
    class voice_draft {
        <<struct>>
        +voice : custom_voice
        +name : string
        +note : string
    }
    class speech_services {
        <<struct>>
        +engine : tts_engine
        +queue : speech_queue
        +settings : guild_settings
        +bootstrap : bootstrap
        +voices : voice_store
    }
    class llm_command_services {
        <<struct>>
        +settings, bootstrap, documents, triggers
        +blacklist, memories, usage, http, clock
        +has_provider(provider_kind) bool
    }

    ui ..> page_state
    trigger_panel ..> ui
    url_panel ..> ui
    voice_lab ..> ui
    llm_panels ..> ui
    trigger_panel o-- trigger_store
    url_panel o-- url_rule_store
    voice_lab o-- voice_drafts
    voice_lab o-- voice_store
    voice_lab *-- speech_services
    voice_drafts --> voice_draft
    llm_panels *-- llm_command_services
```

`bot::route_component` offers each button or menu press to the routers in
turn, and the first whose `on_component` returns `true` has handled it.
`bot::on_form` does the same for forms. The views each one claims:

| Router | Views (the first part of the `custom_id`) |
| --- | --- |
| `bot` itself | `nicks` (nickname history pages), `urlretry` (Retry on a replacement) |
| `trigger_panel` | `triggers`, `trigpanel`, `trigpick`, `trigedit`, `trigdel`, `trigyes`, `trigadd`, `trigonoff`, `trigbots`, `trigsilent`, `trigprev`; form `trigform` |
| `url_panel` | `urllist`, `urlpanel`, `urlpick`, `urledit`, `urldel`, `urlyes`, `urladd`, `urlswitch`; form `urlform` |
| `voice_lab` | `vlabedit`, `vlabbase`, `vlabtest`, `vlabsave`, `vlabreset`, `vlabopen`; forms `vlabform`, `vlabname` |
| `llm_panels` | `memlist`, `llmset`, `llmsetpick`, `llmswitch`; forms `llmsetform`, `llmdoc` |
| `on_linkstats_component` | `linkboard` (board pages), `linkdupes`, `linkkeep`, `linkmerge` (`/linkstats duplicates`) |

The voice lab is the one panel that keeps state in memory. A voice being
edited has too many numbers to fit in a 100-character `custom_id`, so
`voice_drafts` keeps each person's draft per server for 30 minutes.

## 6. The message pipeline

`src/core/events/message_pipeline.hpp`, and the stages in `goodbye.hpp`,
`url_replacer.hpp`, `triggers.hpp` and `llm/stage.hpp`

Every message goes through the same list of *stages*. A stage looks at an
`incoming_message` and returns a `stage_result`: the actions it wants, and
whether the message is used up (*consumed*), which stops the stages after
it. A stage never sends anything itself.

```mermaid
classDiagram
    direction TB

    class pipeline {
        -stages_ : vector~stage~
        +add(name, stage_fn)
        +run(incoming_message) vector~action~
        +stage_names() vector~string_view~
    }
    class stage {
        <<struct>>
        +name : string
        +handler : stage_fn
    }
    class incoming_message {
        <<struct>>
        +guild_id, channel_id, message_id, author_id
        +from_self, from_bot : bool
        +author_is_allowed_bot : bool
        +author_is_administrator : bool
        +embeds_suppressed : bool
        +content, author_name : string
        +author_roles : vector~snowflake~
        +mentions_bot, replies_to_bot : bool
        +answered : bool
    }
    class stage_result {
        <<struct>>
        +actions : vector~action~
        +consumed : bool
        +answered : bool
    }
    class action {
        <<variant>>
    }
    class send_message {
        <<struct>>
        +channel_id
        +content : string
        +flags : message_flags
        +what : string
    }
    class stop_bot {
        <<struct>>
        +after : milliseconds
    }
    class replace_links {
        <<struct>>
        +guild_id, channel_id, message_id, author_id
        +links : vector~planned_link~
    }
    class ask_llm {
        <<struct>>
        +guild_id, channel_id, message_id, author_id
        +author_name, content : string
        +trigger_id : int64
        +context_prompt : string
        +speak : bool
        +wait : seconds
    }

    class goodbye_stage {
        <<function>>
        returns a stage_fn
    }
    class url_replacer {
        +operator()(incoming_message) stage_result
    }
    class trigger_responder {
        +operator()(incoming_message) stage_result
    }
    class llm_stage {
        +operator()(incoming_message) stage_result
    }

    pipeline *-- stage
    stage ..> incoming_message : reads
    stage ..> stage_result : returns
    stage_result --> action
    action <|-- send_message
    action <|-- stop_bot
    action <|-- replace_links
    action <|-- ask_llm
    goodbye_stage ..> send_message
    goodbye_stage ..> stop_bot
    url_replacer ..> replace_links
    trigger_responder ..> send_message
    llm_stage ..> ask_llm
```

The four stages are added in this order in `bot::register_stages`:
**goodbye**, **url replacement**, **triggers**, **language model**. The
triangles from `action` stand for "is one of the variant's alternatives",
not inheritance. Each stage is stored as a `std::function`: a lambda
returned by `goodbye_stage`, a copy of `url_replacer`, and lambdas that call
the `trigger_responder` and `llm_stage` members of `bot`.

`answered` passes from stage to stage. When a trigger has already replied, the
language model's advanced triggers stay quiet (plan §14.3).

## 7. Link replacement

`src/core/events/{url_rules,url_replacer,embed_watch,replacements}.hpp`

A link to a site with a rule is reposted through a *mirror* that gives a
better preview. The bot then watches its repost until Discord adds the
preview, trying the next mirror if none arrives.

```mermaid
classDiagram
    direction TB

    class url_rule_store {
        +for_guild(guild) vector~url_rule~
        +find(guild, domain) optional~url_rule~
        +set(guild, url_rule)
        +rename(guild, previous, url_rule)
        +remove(guild, domain) bool
        +enabled(guild) bool
        +opted_out(guild, user) bool
        +known_mirrors(guild) mirror_map
    }
    class url_rule {
        <<struct>>
        +domain : string
        +mirrors : vector~mirror~
    }
    class mirror {
        <<struct>>
        +host : string
        +translate_suffix : string
    }
    class planned_link {
        <<struct>>
        +original_url : string
        +domain : string
        +spoilered : bool
        +mirrors : vector~mirror~
    }
    class url_replacer {
        +operator()(incoming_message) stage_result
    }
    class replacement_store {
        +record(replacement_record)
        +find(message) optional~replacement_record~
        +set_state(message, replacement_state) bool
        +mark_retried(message, state, at) bool
        +unsettled() vector~replacement_record~
    }
    class replacement_record {
        <<struct>>
        +message_id, guild_id, channel_id
        +original_message_id, original_author_id
        +state : replacement_state
        +created_at, retried_at
        +links : vector~planned_link~
    }
    class replacement_state {
        <<enumeration>>
        pending
        ok
        failed
        retrying
    }
    class embed_tracker {
        -watches_ : map~message, watch_state~
        -early_ : map~message, early_update~
        +watch(watch_request, embed_urls) vector~embed_action~
        +on_embeds(message, embed_urls) vector~embed_action~
        +tick() vector~embed_action~
        +settle(watch_request, embed_urls) vector~embed_action~
        +forget(message)
    }
    class watch_request {
        <<struct>>
        +guild_id, channel_id, message_id
        +original_message_id
        +links : vector~planned_link~
        +per_mirror : size_t
        +retry : bool
    }
    class watched_link {
        <<struct>>
        +link : planned_link
        +attempt : size_t
        +progress : link_progress
    }
    class embed_action {
        <<variant>>
    }
    class edit_replacement {
        <<struct>>
        +channel_id, message_id
        +content : string
        +failed : bool
    }
    class set_original_embeds {
        <<struct>>
        +channel_id, message_id
        +suppressed : bool
    }

    url_rule_store ..> url_rule
    url_rule *-- mirror
    url_replacer o-- url_rule_store
    url_replacer ..> planned_link : plans
    planned_link --> mirror
    replacement_store ..> replacement_record
    replacement_record --> replacement_state
    replacement_record --> planned_link
    embed_tracker o-- replacement_store
    embed_tracker ..> watch_request
    embed_tracker --> watched_link : per link watched
    watched_link --> planned_link
    embed_tracker ..> embed_action : returns
    embed_action <|-- edit_replacement
    embed_action <|-- set_original_embeds
```

The functions that join these up are free functions in `url_replacer.hpp`,
all coroutines over the `discord_gateway` port:

- `post_replacement` posts the repost, records it, hides the original's
  preview, and starts watching.
- `carry_out_embed_actions` performs what the tracker returned.
- `settle_stranded` finishes the ones the last run left mid-watch.
- `plan_retry` works out what the Retry button should try.

`embed_tracker` never talks to Discord. It is told about message edits and
clock ticks, and answers with `embed_action`s. Tests can therefore replay
any order of edits and timeouts without a connection.

## 8. Link stats and the recompute

`src/core/events/{reactions,backfill,legacy_replacements}.hpp`,
`src/core/commands/linkstats.hpp`

Reactions to the bot's reposts are counted, for `/linkstats`.
`/linkstats recompute` rebuilds those counts, and the record of which
reposts were whose, from channel history. That history includes years of
reposts made by the Java bot.

```mermaid
classDiagram
    direction TB

    class reaction_store {
        +add(message, user, emoji_ref, at) bool
        +remove(message, user, emoji_key, at) bool
        +replace_for_message(message, observed) int
        +set_alias(guild, emoji_key, canonical) optional~string~
        +leaderboard(guild, stat_query, limit, offset) vector~person_tally~
        +emoji_breakdown(guild, stat_query, ...) vector~emoji_tally~
        +total / people / emojis(guild, stat_query) int64
        +known_emojis(guild, filter, limit, emoji_listing) vector~emoji_tally~
        +similar_emojis(guild) groups of emoji_tally
    }
    class emoji_ref {
        <<struct>>
        +key : string
        +name : string
        +animated : bool
    }
    class stat_query {
        <<struct>>
        +kind : stat_kind
        +emoji_key : optional~string~
        +user_id : optional~snowflake~
        +since, until : optional~sys_seconds~
        +domain : optional~string~
    }
    class person_tally {
        <<struct>>
        +user_id
        +count : int64
    }
    class emoji_tally {
        <<struct>>
        +emoji : emoji_ref
        +count : int64
    }

    class backfill_service {
        -jobs_ : map~guild, cancel flag~
        +run(backfill_request, progress_fn) task~backfill_report~
        +begin(guild) bool
        +cancel(guild) bool
        +running(guild) bool
        -scan_channel(...) task
        -consider(scan, message, older, report) task
    }
    class backfill_request {
        <<struct>>
        +guild_id
        +channel_ids : vector~snowflake~
        +since, until
        +bot_id
        +fresh : bool
    }
    class backfill_report {
        <<struct>>
        +scanned, replacements, attributed : int64
        +reactions : int64
        +unparsed, mismatched, unread : vector~message_place~
        +learned_mirrors : host and site pairs
        +channels_done, channels_total
        +problems : vector~string~
        +cancelled : bool
    }
    class message_place {
        <<struct>>
        +channel_id
        +message_id
    }
    class backfill_progress_store {
        +find(guild, channel, since, until) optional~channel_progress~
        +save(guild, channel, since, until, progress, now)
    }
    class history_message {
        <<struct>>
        +id, author_id, webhook_id, replied_to
        +author_is_bot, is_system : bool
        +content : string
        +reactions : vector~reaction_count~
    }
    class legacy_match {
        <<struct>>
        +what : kind
        +format : legacy_format
        +mirror_urls : vector~string~
    }
    class linkstats_command

    reaction_store ..> emoji_ref
    reaction_store ..> stat_query
    reaction_store ..> person_tally
    reaction_store ..> emoji_tally
    emoji_tally --> emoji_ref
    backfill_service o-- discord_gateway
    backfill_service o-- url_rule_store
    backfill_service o-- replacement_store
    backfill_service o-- reaction_store
    backfill_service o-- backfill_progress_store
    backfill_service ..> backfill_request
    backfill_service ..> backfill_report
    backfill_report --> message_place
    backfill_service ..> history_message : reads pages of
    history_message ..> legacy_match : classify() gives
    linkstats_command o-- reaction_store
    linkstats_command o-- backfill_service
```

The recompute reads each channel a page at a time, newest first, through
`discord_gateway::get_messages`. For each message:

1. `describe_history` reduces it to a `history_message`.
2. `classify` decides whether it is one of the bot's reposts, in any of the
   formats the Java bot or this one ever used. A link to a mirror some rule
   once named settles it. Without one, the masked `[.](link)` shapes still
   do, and a copy of a message is `unconfirmed` until step 3 finds the link
   it replaced.
3. `attribute` works out whose message it replaced, and `replaced_links`
   which of its links stood in for which. A mirror no rule named is learned
   from that, and remembered in `known_mirrors`.

What went wrong is kept as `message_place`s, so the report and the reply
that follows it link to each message. See
[docs/features/Link_Stats.md](../features/Link_Stats.md) for the whole
feature.

`backfill_progress_store` remembers how far each channel got, so a cancelled
or crashed recompute picks up where it stopped.

## 9. Speech

`src/core/audio/`, `src/core/events/voice_sessions.hpp`,
`src/core/discord/dpp_voice_output.hpp`

Text becomes audio in `dectalk_engine`, on one worker thread of its own.
DECtalk's API blocks while it works and reports back through a callback that
can find only one utterance at a time, so the worker makes them one after
another. Synthesis is fast, hundreds of times real time, so each request is
answered with the whole utterance at once. The audio then waits in a
per-server `speech_queue` until the voice connection is ready, and plays in
order.

```mermaid
classDiagram
    direction TB

    class dectalk_engine {
        -queue_ : deque~unique_ptr~job~~
        -worker_ : jthread
        -cancel_below_ : uint64
        +synthesize(speech_request) task~result~pcm_audio~~
        +stop()
        -work(stop_token)
        -speak(speech_request, id) result~pcm_audio~
    }
    class job {
        <<struct>>
        +id : uint64
        +request : speech_request
        +done : promise~result~pcm_audio~~
    }
    class speech_request {
        <<struct>>
        +text : string
        +voice : voice_settings
        +max_duration : milliseconds
    }
    class voice_settings {
        <<struct>>
        +voice : string
        +rate : int
        +volume : int
        +custom_params : string
    }
    class pcm_audio {
        <<struct>>
        +samples : vector~int16~
        +sample_rate : uint32
        +channels : uint8
        +truncated : bool
        +duration() milliseconds
    }
    class speech_queue {
        -guilds_ : map~guild, guild_speech~
        +ticket(guild) uint64
        +enqueue(guild, owner, ticket, audio) speech_outcome
        +on_ready(guild)
        +on_marker(guild, marker)
        +skip(guild) bool
        +stop(guild) size_t
        +forget(guild)
    }
    class guild_speech {
        <<struct>>
        +generation : uint64
        +playing : deque~utterance~
        +waiting : deque~waiting_utterance~
    }
    class voice_output {
        <<interface>>
    }
    class custom_voice {
        <<struct>>
        +base : string
        +edits : vector of code and value pairs
        +set(code, value) bool
        +dv_parameters() string
    }
    class saved_voice {
        <<struct>>
        +name : string
        +voice : custom_voice
        +created_by
        +updated_at
    }
    class voice_store {
        +save(guild, saved_voice)
        +find(guild, name) optional~saved_voice~
        +list(guild) vector~saved_voice~
        +remove(guild, name) bool
    }
    class voice_sessions {
        -sessions_ : map~guild, voice_session~
        +start(voice_session)
        +end(guild) optional~voice_session~
        +find(guild) optional~voice_session~
    }
    class voice_session {
        <<struct>>
        +guild_id
        +voice_channel
        +text_channel
        +started_by
    }
    class auto_leave {
        -alone_since_ : map~guild, time_point~
        +observe(guild, in_voice, humans)
        +due(grace_for) vector~snowflake~
        +forget(guild)
    }

    dectalk_engine *-- job : queues
    job --> speech_request
    speech_request --> voice_settings
    dectalk_engine ..> pcm_audio : produces
    speech_queue *-- guild_speech : one per server
    speech_queue o-- voice_output : plays through
    saved_voice *-- custom_voice
    voice_store ..> saved_voice
    custom_voice ..> voice_settings : becomes custom_params
    voice_sessions --> voice_session
```

How the pieces are used:

- **Tickets.** `speech_queue::ticket` is taken before synthesis starts. If
  `/tts stop` runs while the audio is still being made, the ticket is out
  of date by the time `enqueue` is called, and the audio is dropped rather
  than played after the stop.
- **Markers.** `dpp_voice_output::play` sends the audio with a *marker*.
  DPP reports each marker as it is reached, which is how `on_marker` knows
  an utterance finished.
- **Custom voices.** A `custom_voice` is a built-in voice plus DECtalk `[:dv]`
  parameter edits. `voice_preamble` turns it into the command text put in
  front of what is spoken.
- **Voice sessions.** A `voice_session` (`/voice start`) is what makes the
  language model speak its replies aloud.

## 10. The language model: deciding to answer

`src/core/llm/{stage,guards,advanced_triggers,settings,spend}.hpp`

`llm_stage` is the pipeline's last stage. It answers with an `ask_llm` action
when someone addresses the bot, or when an *advanced trigger* fires. Before
that, the checks it owns have to pass.

```mermaid
classDiagram
    direction TB

    class llm_stage {
        -services_ : stage_services
        -matcher_ : advanced_trigger_matcher
        -users_ : rate_limiter
        -channels_ : rate_limiter
        -pacing_ : bot_pacing
        -notices_ : spend_notices
        +operator()(incoming_message) stage_result
        -admit(message, settings, addressed, result) optional~seconds~
        -over_spend_cap(message, addressed, result) bool
    }
    class stage_services {
        <<struct>>
        +settings : guild_settings
        +bootstrap : bootstrap
        +blacklist : blacklist_store
        +triggers : advanced_trigger_store
        +usage : usage_store
        +sessions : voice_sessions
        +has_provider(provider_kind) bool
        +me() bot_identity
    }
    class advanced_trigger_matcher {
        -last_fired_ : map~trigger and channel, time_point~
        +fire(triggers, channel, content) optional~advanced_trigger~
    }
    class advanced_trigger {
        <<struct>>
        +pattern : string
        +mode : match_mode
        +context_prompt : string
        +probability : double
        +cooldown : seconds
    }
    class rate_limiter {
        -taken_ : map~key, deque~time_point~~
        +try_take(key, limit) bool
    }
    class bot_pacing {
        -channels_ : map~channel, channel_state~
        +human_spoke(channel)
        +claim(guild, channel, pacing_rules) pacing_decision
    }
    class spend_notices {
        +first(guild, period) bool
    }
    class llm_settings {
        <<struct>>
        +enabled : bool
        +model : string
        +context_messages, context_tokens : int
        +max_output_tokens : int
        +user_per_minute, channel_per_minute : int
        +pacing : pacing_rules
    }
    class blacklist_store {
        +blocks(guild, user, roles) bool
    }
    class usage_store {
        +record(guild, model_info, usage, at) double
        +spent_between(from, to) double
    }

    llm_stage *-- stage_services
    llm_stage *-- advanced_trigger_matcher
    llm_stage *-- "2" rate_limiter : per user, per channel
    llm_stage *-- bot_pacing
    llm_stage *-- spend_notices
    llm_stage ..> llm_settings : load_llm_settings()
    advanced_trigger_matcher ..> advanced_trigger
    stage_services o-- blacklist_store
    stage_services o-- usage_store
    stage_services o-- advanced_trigger_store
```

The checks, in order, are drawn as a flowchart in
[Execution_Flow.md §5](Execution_Flow.md#5-inside-the-four-stages).
`llm_settings` is read from `guild_settings` for every message rather than
cached, so a change in `/llm settings` applies to the next message.

## 11. The language model: answering

`src/core/llm/{responder,provider,tools,prompt,documents,memory,models}.hpp`

`responder` turns an `ask_llm` into a reply. It gathers recent messages, the
server's documents and relevant memories, sends them to the model, runs any
tools the model asks for, and posts the result.

```mermaid
classDiagram
    direction TB

    class responder {
        -services_ : responder_services
        -me_ : function returning bot_identity
        +answer(ask_llm) task~answer_report~
        -recent_messages(ask, wanted, bot_id) task~vector~context_message~~
        -build_request(ask, settings, model, history, me, now) request
        -post(ask, parts, report) task
        -speak(ask, text) task
    }
    class responder_services {
        <<struct>>
        +discord : discord_gateway
        +clock : clock
        +documents : document_store
        +memories : memory_store
        +usage : usage_store
        +tools : tool_registry
        +provider_for(provider_kind) provider
        +engine : tts_engine
        +speech : speech_queue
    }
    class provider {
        <<interface>>
        +complete(request) task~result~response~~
    }
    class request {
        <<struct>>
        +model : string
        +stable_system : string
        +varying_system : string
        +conversation : vector~turn~
        +tools : vector~tool_definition~
        +allow_tools : bool
        +max_output_tokens : int
    }
    class response {
        <<struct>>
        +reply : turn
        +stop : stop_reason
        +used : usage
    }
    class turn {
        <<struct>>
        +from : speaker
        +text : string
        +calls : vector~tool_call~
        +results : vector~tool_result~
    }
    class tool_call {
        <<struct>>
        +id, name : string
        +input : json
    }
    class tool_result {
        <<struct>>
        +call_id : string
        +content : string
        +is_error : bool
    }
    class usage {
        <<struct>>
        +input_tokens, output_tokens
        +cache_write_tokens, cache_read_tokens
    }
    class tool_registry {
        -tools_ : vector~entry~
        +add(tool_definition, tool_handler)
        +definitions() vector~tool_definition~
        +run(tool_call, tool_context) tool_result
    }
    class document_store {
        +text(guild, document_kind) string
        +save(guild, kind, content, by, at, note) int
        +history(guild, kind) vector~document_version~
        +revert(guild, kind, number, by, at) optional~int~
    }
    class memory_store {
        +add(memory) int64
        +search(guild, text, limit) vector~memory~
        +list(guild, subject, offset, limit) vector~memory~
        +remove(id, guild) bool
    }
    class model_info {
        <<struct>>
        +id, label : string_view
        +provider : provider_kind
        +input_price, output_price
        +cache_write_price, cache_read_price
    }

    responder *-- responder_services
    responder_services o-- provider : via provider_for
    responder_services o-- tool_registry
    responder_services o-- document_store
    responder_services o-- memory_store
    responder ..> request : builds
    provider ..> request : takes
    provider ..> response : returns
    request --> turn
    response --> turn
    response --> usage
    turn --> tool_call
    turn --> tool_result
    tool_registry ..> tool_call : runs
    tool_registry ..> tool_result : answers with
    responder ..> model_info : prices usage with
```

A few things that are easy to miss:

- **The tool loop.** `run_tool_loop` (in `tools.hpp`) calls
  `provider::complete` repeatedly. Each time the model stops to use tools,
  the loop runs them through `tool_registry::run`, adds a `turn` with the
  results, and asks again, up to `llm_tool_rounds` times.
- **Tools.** The only tools are the model's memory: `remember`, `recall` and
  `forget`, added by `add_memory_tools` at startup.
- **Prompt caching.** The system prompt is in two parts. `stable_system`
  (the system document, personality and trigger style) is the same from call
  to call, which lets the APIs cache it. `varying_system` holds the
  memories and the date.
- **Providers.** `anthropic_provider` and `openai_provider` each translate
  `request` and `response` to and from their API's JSON (`anthropic_body`,
  `read_anthropic_reply` and so on), and send it through `http_client`.

## 12. Triggers, nicknames and midnight messages

`src/core/events/{triggers,nicknames,midnight,bot_allowlist}.hpp`

Three smaller features, each a store plus one class that does the work.

```mermaid
classDiagram
    direction LR

    class trigger {
        <<struct>>
        +id : int64
        +pattern : string
        +mode : match_mode
        +cooldown : seconds
        +enabled, respond_to_bots : bool
        +message_flags : message_flags
        +responses : vector~weighted_response~
    }
    class weighted_response {
        <<struct>>
        +text : string
        +weight : int
    }
    class trigger_store {
        +for_guild(guild) vector~trigger~
        +find(id, guild) optional~trigger~
        +add(trigger) int64
        +update(trigger) bool
        +remove(id, guild) bool
        +seed_defaults(guild) int
    }
    class trigger_responder {
        -last_fired_ : map~trigger and channel, time_point~
        +operator()(incoming_message) stage_result
    }
    trigger *-- weighted_response
    trigger_store ..> trigger
    trigger_responder o-- trigger_store
    trigger_responder o-- clock

    class nickname_change {
        <<struct>>
        +id : int64
        +guild_id, user_id
        +nickname : optional~string~
        +changed_at
        +changed_by : optional~snowflake~
        +source : nickname_source
    }
    class nickname_store {
        +record(nickname_change) int64
        +history(guild, user) vector~nickname_change~
        +latest(guild, user) optional~nickname_change~
        +unattributed(guild, user, nickname, now, window) optional~nickname_change~
        +attribute(id, changed_by, source) bool
    }
    class pending_nicknames {
        -expected_ : vector~expectation~
        +expect(guild, user, nickname, now)
        +claim(guild, user, nickname, now) bool
    }
    nickname_store ..> nickname_change

    class midnight_entry {
        <<struct>>
        +id : int64
        +channel_id
        +timezone : string
        +message : string
        +enabled : bool
        +last_fired_date : string
    }
    class midnight_store {
        +for_guild(guild) vector~midnight_entry~
        +enabled() vector~midnight_entry~
        +mark_fired(id, date) bool
    }
    class midnight_scheduler {
        -reported_misses_ : map~id, date~
        +tick() vector~action~
    }
    midnight_store ..> midnight_entry
    midnight_scheduler o-- midnight_store
    midnight_scheduler o-- clock

    class bot_allowlist {
        +allow(guild, bot) bool
        +deny(guild, bot) bool
        +contains(guild, bot) bool
    }
```

- **Triggers.** A trigger's cooldown is kept per channel, in memory, so a
  restart forgets it.
- **Nicknames.** When `/nickname` changes a nickname, it tells
  `pending_nicknames` to expect the gateway event that follows. When that
  event arrives it is recognised as the bot's own change, rather than
  recorded a second time without saying who asked.
- **Midnight messages.** `midnight_scheduler` compares each entry's local
  date in its own time zone with `last_fired_date`. It posts once per day,
  and never catches up on a day missed while the bot was off.
- **Allowed bots.** `bot_allowlist` has no class that does the work: `bot`
  asks it about every message's author in `describe()`.

## 13. Configuration and logging

`src/core/config/bootstrap.hpp`, `src/core/util/log.hpp`,
`src/core/events/log_channel.hpp`

```mermaid
classDiagram
    direction LR

    class bootstrap {
        <<struct>>
        +log_level : log_level
        +database_path, backup_directory : path
        +backups_to_keep : int
        +backup_interval : minutes
        +track_nicknames : bool
        +llm_provider, llm_model : string
        +spend_cap_daily_usd, spend_cap_monthly_usd : double
        +llm_tool_rounds : int
        +trusted_guilds, trusted_users : vector~snowflake~
        +load(path)$ bootstrap
        +is_trusted(guild, user, administrator) bool
    }
    class secrets {
        <<struct>>
        +discord_token : string
        +anthropic_key : optional~string~
        +openai_key : optional~string~
        +from_environment()$ secrets
    }
    class logger {
        +set_level(log_level)
        +set_sink(sink_fn)
        +set_tap(tap_fn, log_level)
        +trace / debug / info / warn / error(format, args...)
    }
    class log_channel {
        -buffer_ : log_buffer
        -destination_ : optional~log_destination~
        -backoff_ : seconds
        +start(log_destination)
        +stop()
        +status() log_channel_status
        +flush() task
    }
    class log_buffer {
        -lines_ : deque~string~
        -secrets_ : vector~string~
        +push(stamp, level, message)
        +take(max_messages) vector~string~
    }
    class log_destination {
        <<struct>>
        +guild_id
        +channel_id
        +level : log_level
    }
    class log_destination_store {
        +find() optional~log_destination~
        +save(log_destination)
        +clear() bool
    }

    logger ..> log_channel : tap sends each line to
    log_channel *-- log_buffer
    log_channel o-- discord_gateway
    log_channel --> log_destination
    log_destination_store ..> log_destination
    log_destination_store o-- guild_settings
```

- **`bootstrap`** is `config.json`: whatever cannot change while the bot runs.
  Settings a server can change live are in `guild_settings` instead.
- **`secrets`** only ever come from the environment, or a `.env` file that
  sets it.
- **`logger`** is the one global, reached through `util::log()`. It writes
  every line to the console. When `/logs` has set a channel, `log_channel`
  also installs a *tap* that copies each line into its `log_buffer`. The
  2-second timer then posts the buffer through the gateway. Before a line is
  posted, `log_buffer` masks anything that matches a secret.
