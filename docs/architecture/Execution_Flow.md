# Execution flow

What runs, in what order, and on which thread, from `main()` to shutdown.
The flowcharts show decisions. The sequence diagrams show order in time,
where coroutines pause and threads hand work to each other.
[README.md](README.md) explains the notation.

DPP is a closed box throughout. What matters about it is which of our
functions it calls, and on which thread (section 3).

- [1. From `main()` to shutdown](#1-from-main-to-shutdown)
- [2. Building the bot](#2-building-the-bot)
- [3. Threads: who runs what](#3-threads-who-runs-what)
- [4. A message arrives](#4-a-message-arrives)
- [5. Inside the four stages](#5-inside-the-four-stages)
- [6. Link replacement: posting and watching](#6-link-replacement-posting-and-watching)
- [7. The language model answers](#7-the-language-model-answers)
- [8. A slash command](#8-a-slash-command)
- [9. Speech](#9-speech)
- [10. Panels: buttons, menus and forms](#10-panels-buttons-menus-and-forms)
- [11. Connecting, and joining servers](#11-connecting-and-joining-servers)
- [12. Nickname changes](#12-nickname-changes)
- [13. The timers](#13-the-timers)
- [14. Shutting down](#14-shutting-down)

## 1. From `main()` to shutdown

`src/main.cpp`, `bot::run()`

`main()` is short. It reads configuration in a set order, builds one `bot`,
and hands the main thread to DPP until the bot is told to stop. Every
exception anywhere in startup lands in the one `catch` at the end.

```mermaid
flowchart TB
    start(["main(argc, argv)"])
    path["config path = argv[1], or config.json"]
    dotenv["util::load_dotenv(.env)<br/>fills in variables the environment does not already have"]
    colors["util::apply_log_colors_from_environment()"]
    envlevel["log level from LATIBOT_LOG_LEVEL, if set"]
    load["config::bootstrap::load(path)"]
    exists{"config.json<br/>exists?"}
    write["write the defaults to it"]
    read["read and check it"]
    level["log().set_level(settings.log_level)"]
    certs["util::use_system_certificates()<br/>exports Windows' root certificates for OpenSSL"]
    secrets["config::secrets::from_environment()"]
    token{"DISCORD_BOT_TOKEN<br/>set?"}
    build["latibot::bot bot(settings, credentials)<br/>see section 2"]
    run["bot.run()<br/>cluster_.start(dpp::st_wait)"]
    dpp[["DPP's event loop, on the main thread<br/>returns only once the cluster shuts down"]]
    stopped["log: disconnected, LatiBot has stopped"]
    destroy["~bot(): members destroyed in reverse order<br/>see section 14"]
    ok(["return 0"])
    fatal["catch: log 'fatal: ...'"]
    fail(["return 1"])

    start --> path --> dotenv --> colors --> envlevel --> load --> exists
    exists -- no --> write --> level
    exists -- yes --> read --> level
    level --> certs --> secrets --> token
    token -- no --> fatal
    token -- yes --> build --> run --> dpp --> stopped --> destroy --> ok
    read -. "bad JSON or<br/>a bad value" .-> fatal
    build -. "database or<br/>migration error" .-> fatal
    fatal --> fail
```

The order matters in two places. The `.env` file is read before anything
looks at the environment. The log level is set before the configuration is
read, so reading it is logged at the level asked for.

## 2. Building the bot

`bot::bot()` in `src/core/bot.cpp`

The constructor has two halves. First the member initialisers build every
object, in the order drawn in [Classes.md §1](Classes.md#1-what-bot-owns).
Then the body prepares the database and connects everything to DPP. Nothing
is sent to Discord until `run()`.

```mermaid
---
config:
  flowchart:
    wrappingWidth: 450
---
flowchart TB
    subgraph init["Member initialisers, in declaration order"]
        direction TB
        i1["settings_, then database_: opens bot.db,<br/>creating data/ if it is missing"]
        i2["cluster_: dpp::cluster with the token and intents<br/>(Message Content always, Server Members if tracking nicknames)"]
        i3["adapters, stores, panels, pipeline_, the speech engine,<br/>which starts its worker thread"]
        i4["anthropic_, openai_: made only when their key is set"]
        i5["responder_, llm_stage_, llm_panels_, log_channel_"]
        i1 --> i2 --> i3 --> i4 --> i5
    end

    subgraph body["Constructor body"]
        direction TB
        b1["log: LatiBot 0.1.0 starting"]
        b2["db::migrate(database_)<br/>applies any migration newer than the file"]
        b3["llm::add_memory_tools()<br/>remember, recall, forget"]
        b4{"a log channel<br/>saved by /logs?"}
        b5["log_channel_.start()<br/>taps the logger from here on"]
        b6["import nicknames.json, if it is beside the database"]
        b7["replacements_.unsettled()<br/>kept in stranded_, by server"]
        b8["register_commands()<br/>22 commands into commands_"]
        b9["register_stages()<br/>goodbye, url replacement, triggers, language model"]
        b10["register_events()<br/>a handler for each DPP event, section 3"]
        b11["register_timers()<br/>section 13"]
        b1 --> b2 --> b3 --> b4
        b4 -- yes --> b5 --> b6
        b4 -- no --> b6
        b6 --> b7 --> b8 --> b9 --> b10 --> b11
    end

    init --> body
```

The replacements from the last run are read here, before the connection
starts, for a reason. Anything still `pending` or `retrying` at this point
must have been cut off by the last run, since this one has not posted
anything yet. They are settled once their server connects (section 11).

## 3. Threads: who runs what

Several threads run at once. Knowing which one is running a function
explains the mutexes you see in the code.

```mermaid
---
config:
  flowchart:
    wrappingWidth: 320
---
flowchart LR
    subgraph mainthread["Main thread"]
        evloop["DPP's event loop<br/>all websocket and HTTP sockets"]
        timers["timer callbacks<br/>register_timers()"]
    end
    subgraph pool["DPP's thread pool, 4 or more threads"]
        handlers["every event handler<br/>on_message_create, on_slashcommand, ..."]
        resumed["coroutines resuming<br/>after a REST call answers"]
    end
    subgraph voice["One DPP voice thread per voice connection"]
        courier["sends the audio packets"]
    end
    subgraph ours["Threads the bot starts"]
        worker["dectalk_engine's worker<br/>makes one utterance at a time, then<br/>runs the rest of the coroutine that<br/>asked for it, up to its next co_await"]
        goodbye["goodbye_<br/>the pause before shutting down"]
    end

    evloop -- "an event arrives:<br/>queued to the pool" --> handlers
    evloop -- "a REST reply arrives:<br/>queued to the pool" --> resumed
    evloop -- "each timer<br/>that is due" --> timers
    handlers -- "synthesize():<br/>queued" --> worker
    handlers -- "play(): audio handed to" --> courier
    handlers -- "stop_bot action" --> goodbye
```

What follows from this:

- **Two events can be handled at once.** A message and a button press, or
  two messages, can arrive together. That is why every object with state
  shared between handlers locks a mutex, for example `trigger_responder`,
  `embed_tracker`, `speech_queue`, `rate_limiter` and `database`.
- **A timer callback must be quick.** It runs on the event loop itself, so
  while it runs, no socket is read. The slow ones hand their work to a
  detached coroutine (`ui::detach`) and return.
- **A coroutine can change threads.** After `co_await` on a REST call, it
  carries on on a pool thread. After `co_await synthesize()`, it carries on
  on the speech worker, until its next `co_await`.
- **Nothing blocks a handler for long.** Slow work is `co_await`ed or
  detached, never waited on.

## 4. A message arrives

`on_message_create` in `bot::register_events`, `bot::describe`,
`pipeline::run`, `bot::carry_out`

Every message in every channel the bot can see comes through here.

```mermaid
flowchart TB
    arrive(["DPP: on_message_create<br/>on a pool thread"])
    describe["bot::describe(message, raw_event)<br/>makes an events::incoming_message"]
    d1["reads the author's nickname, roles, and whether they are a bot"]
    d2["bot_allowlist_.contains(): is it a bot this server allowed?"]
    d3["from DPP's cache: does the author have Administrator?"]
    d4["from the raw gateway frame: is it a reply to the bot?"]
    run["pipeline_.run(message)"]
    self{"from_self?"}
    bot{"from a bot this server<br/>has not allowed?"}
    ignore(["ignored"])
    each["for each stage, in order"]
    runstage["result = stage(current)<br/>a stage that throws is logged and skipped"]
    collect["add result.actions to the list<br/>current.answered |= result.answered"]
    consumed{"result.consumed?"}
    more{"more stages?"}
    carry["bot::carry_out(actions)"]
    visit{"each action is a"}
    send["dpp::cluster::message_create()<br/>logs the outcome when Discord answers"]
    replace["detach(post_replacement(...))<br/>section 6"]
    ask["detach(answer_with_llm(...))<br/>section 7"]
    stop["goodbye_ = jthread that waits, then<br/>cluster_.shutdown(), section 14"]

    arrive --> describe
    describe --- d1 & d2 & d3 & d4
    describe --> run --> self
    self -- yes --> ignore
    self -- no --> bot
    bot -- yes --> ignore
    bot -- no --> each --> runstage --> collect --> consumed
    consumed -- yes --> carry
    consumed -- no --> more
    more -- yes --> each
    more -- no --> carry
    carry --> visit
    visit -- send_message --> send
    visit -- replace_links --> replace
    visit -- ask_llm --> ask
    visit -- stop_bot --> stop
```

The stages only decide. Everything that talks to Discord happens in
`carry_out`, and apart from `send_message` it happens in a detached
coroutine. So the handler returns in microseconds, however long the model
takes to answer.

After the pipeline, the same handler gives the message to `media_tracker`.
In a server that counts reactions on images, an upload is recorded there and
then, and a message with links waits a minute for the preview that shows
whether it was an image (docs/features/Link_Stats.md §9).

## 5. Inside the four stages

`goodbye.cpp`, `url_replacer.cpp`, `triggers.cpp`, `llm/stage.cpp`

The four stages side by side. Each returns a `stage_result`, and *consumes*
the message when no later stage should see it.

```mermaid
---
config:
  flowchart:
    wrappingWidth: 260
---
flowchart LR
    subgraph goodbye["1. goodbye"]
        direction TB
        g1{"content matches this server's<br/>goodbye phrase?"}
        g2{"author has<br/>Administrator?"}
        g3["send_message 'ok bye bye!'<br/>stop_bot after 1.5 s<br/>consumed"]
        g0(["nothing"])
        g1 -- yes --> g2
        g1 -- no --> g0
        g2 -- no --> g0
        g2 -- yes --> g3
    end

    subgraph url["2. url replacement"]
        direction TB
        u1{"from a person, in a server,<br/>containing ://, previews not<br/>already hidden?"}
        u2{"replacement on in<br/>this server?"}
        u3["plan_replacements(content, rules)<br/>skips code, duplicates, past 5 links"]
        u4{"any links, and the author<br/>has not opted out?"}
        u5["drop links until the<br/>repost fits 2000 characters"]
        u6["replace_links<br/>not consumed"]
        u0(["nothing"])
        u1 -- no --> u0
        u1 -- yes --> u2
        u2 -- no --> u0
        u2 -- yes --> u3 --> u4
        u4 -- no --> u0
        u4 -- yes --> u5 --> u6
    end

    subgraph trig["3. triggers"]
        direction TB
        t1["for each of this server's triggers"]
        t2{"pattern matches, enabled,<br/>answers bots if from a bot?"}
        t3{"off cooldown<br/>in this channel?"}
        t4["choose a response by weight<br/>send_message, answered"]
        t1 --> t2
        t2 -- yes --> t3
        t3 -- yes --> t4
        t2 -- no --> t1
        t3 -- no --> t1
        t4 --> t1
    end

    subgraph llm["4. language model"]
        direction TB
        l1{"in a server, and<br/>/llm is on?"}
        l2{"addressed: mentioned, replied to,<br/>or starts with the bot's name?"}
        l3{"not from a bot, not answered yet,<br/>and an advanced trigger fires?"}
        l4{"the server's model has<br/>an API key?"}
        l5{"author or their role<br/>blacklisted?"}
        l6{"over the daily or<br/>monthly spend cap?"}
        l7{"under the per-user and<br/>per-channel rate limits?"}
        l8{"from a bot: bot_pacing<br/>allows another turn?"}
        l9["ask_llm, with any pacing wait<br/>consumed, answered"]
        l0(["nothing"])
        l1 -- no --> l0
        l1 -- yes --> l2
        l2 -- yes --> l4
        l2 -- no --> l3
        l3 -- no --> l0
        l3 -- yes --> l4
        l4 -- no --> l0
        l4 -- yes --> l5
        l5 -- yes --> l0
        l5 -- no --> l6
        l6 -- "yes: says so once,<br/>if addressed" --> l0
        l6 -- no --> l7
        l7 -- no --> l0
        l7 -- yes --> l8
        l8 -- no --> l0
        l8 -- "yes, or from a person" --> l9
    end

    goodbye --> url --> trig --> llm
```

Details the chart leaves out:

- **Triggers.** Every trigger that matches replies, not just the first.
  The cooldown is per trigger and per channel.
- **Addressed messages.** A message that addresses the bot is consumed
  from the moment the model and its key check out, even if a later check
  refuses it. This matters only if a stage is ever added after this one.
- **Pacing between bots.** Replies to another bot are spaced out and capped
  (`pacing_rules`). The wait travels in `ask_llm.wait`, and
  `bot::answer_with_llm` sleeps it off with `co_sleep` before answering.

## 6. Link replacement: posting and watching

`post_replacement` in `url_replacer.cpp`, `embed_tracker` in `embed_watch.cpp`

A repost is only useful if Discord gives it a preview, and Discord adds
previews by editing the message a moment later. So the bot posts, then
watches for that edit. If it doesn't come, the bot edits the repost to the
next mirror, which makes Discord try again.

```mermaid
sequenceDiagram
    autonumber
    participant bot as bot::carry_out
    participant post as post_replacement()
    participant gw as discord_gateway
    participant store as replacement_store
    participant tr as embed_tracker
    participant dpp as DPP
    participant tick as 1 s timer

    bot->>post: replace_links (detached coroutine)
    post->>gw: send_message(repost with first mirror)
    gw-->>post: our message id
    post->>store: record(pending, links)
    post->>gw: set_embeds_suppressed(original, true)
    post->>tr: watch(request)
    tr-->>post: actions (usually none yet)

    alt a preview arrives in time
        dpp->>bot: on_message_update(our message, embeds)
        bot->>tr: on_embeds(id, embed urls)
        tr->>store: set_state(ok)
        tr-->>bot: no actions: done
    else 6 seconds pass without one
        tick->>tr: tick()
        tr-->>tick: edit_replacement(next mirror, or the same one again)
        tick->>gw: edit_message (Discord looks again)
        Note over tr,tick: repeats: 2 tries per mirror, then the next mirror
        tick->>tr: tick(), every mirror tried
        tr->>store: set_state(failed)
        tr-->>tick: set_original_embeds(false), edit_replacement(failed, with Retry)
        tick->>gw: turn the original's preview back on
        tick->>gw: edit to the failure note and Retry button
    end
```

When someone presses **Retry**, `bot::retry_replacement` asks
`plan_retry` what to try. It answers the button press with the first attempt
and watches again, with one try per mirror. `embed_tracker` also keeps
edits that arrive before `watch()` is called (`early_`), since the edit
that adds the preview can overtake the reply to the post that created the
message.

## 7. The language model answers

`bot::answer_with_llm`, `responder::answer`, `run_tool_loop`

```mermaid
sequenceDiagram
    autonumber
    participant bot as bot
    participant r as responder
    participant gw as discord_gateway
    participant st as stores
    participant tl as run_tool_loop
    participant p as provider
    participant tools as tool_registry
    participant tts as dectalk_engine
    participant q as speech_queue

    bot->>bot: co_sleep(ask.wait), if pacing asked for one
    bot->>r: answer(ask_llm)
    r->>st: load_llm_settings, find_model
    r->>gw: start_typing(channel)
    r->>gw: get_messages(channel, before this one)
    gw-->>r: recent messages, the context
    r->>st: documents: system, personality, trigger style
    r->>st: memory_store search: relevant memories
    r->>r: build_request()
    r->>tl: run_tool_loop(provider, request, tools)
    loop
        tl->>p: complete(request)
        p->>p: HTTPS through http_client
        p-->>tl: response, with usage
        tl->>st: usage_store.record(cost)
        opt the model asked for tools, and rounds are left
            tl->>tools: run(each tool_call)
            tools->>st: memory_store add, search or remove
            tools-->>tl: tool_results, added as a turn
        end
    end
    tl-->>r: the final text, or the error
    alt the call failed
        r->>gw: send an apology, if the bot was addressed
    else there is text
        r->>gw: send_message, split to fit 2000 characters
        opt in a /voice session for this channel
            r->>tts: synthesize(sanitised text)
            tts-->>r: audio, on the worker thread
            r->>q: enqueue(audio)
        end
    end
```

Every call to the model is recorded in `llm_usage` as it happens, not once
at the end, so the spend caps count tool rounds too.

## 8. A slash command

`on_slashcommand` in `bot::register_events`, `registry::dispatch`

```mermaid
flowchart TB
    arrive(["DPP: on_slashcommand<br/>a coroutine handler on a pool thread"])
    dispatch["commands_.dispatch(name, event)"]
    log["log: who ran what, where"]
    find{"registry::find(name)<br/>a command or alias?"}
    unknown["reply privately:<br/>i don't have that command any more"]
    exec["co_await command->execute(event)"]
    threw{"threw?"}
    failed["reply with command_failed_reply"]
    done(["log how long it took"])

    arrive --> dispatch --> log --> find
    find -- no --> unknown
    find -- yes --> exec --> threw
    threw -- yes --> failed
    threw -- no --> done

    subgraph inside["Inside execute(), the usual shape"]
        direction TB
        e1["read the options: string_option, int_option, ..."]
        e2{"allowed, and<br/>does it make sense?"}
        e3["co_reply(refusal(...))<br/>private"]
        e4{"will it take<br/>over 3 seconds?"}
        e5["co_await defer(event)<br/>Discord shows 'thinking'"]
        e6["do the work: a store,<br/>a port, a render_ function"]
        e7["co_reply(result(...)), or<br/>answer_deferred(...)"]
        e1 --> e2
        e2 -- no --> e3
        e2 -- yes --> e4
        e4 -- yes --> e5 --> e6
        e4 -- no --> e6
        e6 --> e7
    end

    exec -.-> inside
```

Autocomplete (the suggestions shown while someone types an option) goes the
same way, through `registry::offer_completions` to the command's
`autocomplete()`.

## 9. Speech

`speak_command::execute`, `dectalk_engine`, `speech_queue`, `dpp_voice_output`

`/speak` in full, because it crosses the most threads. `/chat`, the voice
lab's Test button and the model's spoken replies use the same middle part.

```mermaid
sequenceDiagram
    autonumber
    participant cmd as speak_command (pool thread)
    participant shard as DPP shard
    participant q as speech_queue
    participant eng as dectalk_engine
    participant w as worker thread
    participant out as dpp_voice_output
    participant vc as DPP voice thread

    cmd->>cmd: speak_refusal (length), resolve_voice
    cmd->>cmd: plan_speak: the bot's channel, or the caller's
    cmd->>cmd: sanitize_speech (strips commands this user may not use)
    opt the bot is not in voice
        cmd->>shard: connect_voice(caller's channel)
    end
    cmd->>q: ticket(guild)
    cmd->>cmd: co_await defer(event)
    cmd->>eng: co_await synthesize(request)
    eng->>w: queue the job, wake the worker
    Note over cmd: suspended, the pool thread is free
    w->>w: DECtalk speaks into memory buffers
    w-->>cmd: promise fulfilled, cmd resumes on the worker thread
    cmd->>q: enqueue(guild, owner, ticket, to_discord(audio))
    alt ticket is out of date (a /tts stop happened)
        q-->>cmd: stopped, the audio is dropped
    else the connection is ready and nothing is waiting
        q->>out: play(audio, marker)
        out->>vc: send_audio_raw, insert_marker
        q-->>cmd: playing
    else not ready yet
        q-->>cmd: waiting, kept in the queue
        shard->>q: on_voice_ready, so on_ready(guild) plays what waited
    end
    cmd->>cmd: answer_deferred: ok
    vc->>q: on_voice_track_marker, so on_marker(guild, marker)
    Note over q: the utterance is finished, drop it from playing
```

Leaving voice, by `/leave`, `/voice stop`, the auto-leave, or being
disconnected, arrives as the bot's own `on_voice_state_update` with no
channel. `bot::on_voice_state` then ends the `/voice` session and clears
the speech queue and the auto-leave timer for that server.

## 10. Panels: buttons, menus and forms

`bot::on_component`, `bot::route_component`, `bot::on_form`, and the four
panel routers

A panel press arrives as a button or a menu choice. Its `custom_id`,
`view:page:argument`, says what to do. The answer either edits the panel in
place, or opens a form (a *modal*). Submitting the form arrives as a third
kind of event.

```mermaid
---
config:
  flowchart:
    wrappingWidth: 320
---
flowchart TB
    press(["DPP: on_button_click or on_select_click"])
    decode{"ui::decode(custom_id)<br/>readable?"}
    stale["answer privately: that's from<br/>an older version of me"]
    route["route_component(event, state, chosen)"]
    builtin{"nicks<br/>or urlretry?"}
    own["bot answers it:<br/>a history page, a Retry"]
    chain["trigger_panel, url_panel, voice_lab,<br/>llm_panels, on_linkstats_component:<br/>the first that returns true<br/>has handled it"]
    claimed{"claimed?"}
    answer{"the panel's answer"}
    update["ui::update_panel(event, render_...())<br/>the panel message is edited in place"]
    modal["event.dialog(form)<br/>Discord shows a form"]
    private["answer_privately(note)"]

    submit(["DPP: on_form_submit<br/>when the person presses Submit"])
    empty{"ui::form_fields(event)<br/>empty?"}
    refuse["answer privately: that form came back<br/>empty, so nothing was changed"]
    fchain["the four panels' on_form<br/>(link stats has no forms)"]
    apply["read the fields, check them,<br/>save through the store"]
    result["update_panel with a note,<br/>or a private refusal"]

    press --> decode
    decode -- no --> stale
    decode -- yes --> route --> builtin
    builtin -- yes --> own
    builtin -- no --> chain --> claimed
    claimed -- no --> stale
    claimed -- yes --> answer
    answer -- "paging, picking,<br/>toggling, deleting" --> update
    answer -- "Add, Edit, a settings group" --> modal
    answer -- "not allowed" --> private
    modal -. "the person fills it in" .-> submit
    submit --> empty
    empty -- yes --> refuse
    empty -- no --> fchain --> apply --> result
```

An exception thrown in any panel is caught in `on_component` or `on_form`,
logged, and answered with `command_failed_reply`. It never reaches DPP.

Why the empty-form check exists: DPP 10.1 sends each text field wrapped in a
label, and hands them back unwrapped when the form is submitted.
`ui::form_fields` reads both shapes. A submission with no readable fields
means that reading failed, and saving it would blank what was there (plan
§21.21).

The voice lab is the one panel with state between presses: its
`voice_drafts` holds each person's unsaved voice (see
[Classes.md §5](Classes.md#5-panels-buttons-menus-and-forms)).

## 11. Connecting, and joining servers

`bot::on_ready`, the `on_guild_create` handler

```mermaid
flowchart TB
    subgraph ready["on_ready: connected, or reconnected"]
        direction TB
        r1["log: connected as ..."]
        r2{"a /status was saved?"}
        r3["set_presence(saved status)"]
        r4{"first time this run?<br/>dpp::run_once"}
        r5["commands_.build_all()<br/>global_bulk_command_create()<br/>registers all 22 with Discord"]
        r6(["a reconnect: commands<br/>are already registered"])
        r1 --> r2
        r2 -- yes --> r3 --> r4
        r2 -- no --> r4
        r4 -- yes --> r5
        r4 -- no --> r6
    end

    subgraph guild["on_guild_create: once per server, after connecting,<br/>and whenever the bot is added to a new one"]
        direction TB
        g1["check_permissions()<br/>warns about anything missing, never fatal"]
        g2["reconcile_nicknames()<br/>records nicknames changed while the bot was off"]
        g3["import_url_rules()<br/>the Java bot's rules, once per server"]
        g4["settle_stranded_replacements()<br/>finishes what the last run left pending"]
        g5["triggers_.seed_defaults()<br/>the default triggers, for a new server"]
        g1 --> g2 --> g3 --> g4 --> g5
    end
```

## 12. Nickname changes

`bot::on_member_update`, `bot::on_audit_entry`, `bot::attribute_later`

A nickname change is recorded the moment it is seen. Who made it is filled
in later, when the audit log says, because Discord sends that as a separate
event and sometimes not at all.

```mermaid
---
config:
  flowchart:
    wrappingWidth: 320
---
flowchart TB
    upd(["DPP: on_guild_member_update"])
    claim{"pending_nicknames.claim()<br/>the change /nickname just made?"}
    skip(["already recorded, with who asked"])
    rec["record_nickname(source = seen)"]
    new{"it differs from the last one<br/>recorded, so a row was written?"}
    other(["a role, timeout or avatar<br/>change: nothing to do"])
    later["attribute_later(row)<br/>a one-shot 10 s timer"]

    audit(["DPP: on_guild_audit_log_entry_create"])
    entry["on_audit_entry(entry, guild)"]
    find{"a nick change matching a row<br/>still waiting, within 30 s?"}
    mayattr{"made by someone<br/>other than the bot?"}
    attr["nickname_store.attribute(row, who)"]
    none(["nothing"])

    fire["timer fires"]
    still{"row still<br/>unattributed?"}
    fetch["guild_auditlog_get()<br/>the last 25 member updates"]

    upd --> claim
    claim -- yes --> skip
    claim -- no --> rec --> new
    new -- no --> other
    new -- yes --> later
    audit --> entry --> find
    find -- no --> none
    find -- yes --> mayattr
    mayattr -- no --> none
    mayattr -- yes --> attr
    later -.-> fire --> still
    still -- no --> none
    still -- yes --> fetch
    fetch -- "each entry about this member" --> entry
```

Both DPP handlers are attached only when `track_nicknames` is on, because
the events need the privileged Server Members intent.

## 13. The timers

`bot::register_timers`, `bot::attribute_later`

All timers run on the main thread (section 3). Each is wrapped in
`guarded()`, which logs an exception rather than letting it stop the timer
for good.

```mermaid
flowchart LR
    dpp[["DPP timers<br/>on the main thread"]]

    m["every 30 s<br/>midnight_scheduler.tick()"]
    e["every 1 s<br/>embed_tracker.tick()"]
    l["every 2 s<br/>detach(log_channel.flush())"]
    a["every 5 s<br/>auto_leave.due(grace)"]
    b["every backup_interval, 6 h by default<br/>db::create_backup(), keeps backups_to_keep"]
    n["once, 10 s after a nickname change<br/>the audit log fallback, section 12"]
    c["every 60 s, once connected<br/>detach(emoji_copier.run_round())"]

    dpp --> m & e & l & a & b & n & c

    m -- "send_message actions,<br/>at local midnight" --> carry["bot::carry_out()"]
    e -- "edits for previews<br/>that did not arrive" --> carryE["carry_out(embed actions)<br/>detached"]
    l -- "posts what was logged,<br/>backing off on errors" --> gw["discord_gateway"]
    a -- "servers where the bot<br/>was alone for the grace period" --> leave["shard->disconnect_voice()"]
    b --> file[("data/backups/bot-*.db")]
    c -- "images from the CDN,<br/>then upload or delete" --> copies["http_client, then<br/>discord_gateway"]
```

The emoji copier's timer exists only when `emoji_copy_min_uses` is above 0.
A round does at most ten emojis, so a long backlog is worked through a
minute at a time (docs/features/Link_Stats.md §10).

The midnight tick polls the wall clock every 30 seconds instead of sleeping
until midnight. The Java bot worked out a delay from the wall clock and then
slept on a monotonic timer, so after the computer slept it posted at the
wrong time (plan §10).

## 14. Shutting down

`shutdown_command`, the `stop_bot` action, `~bot()`

```mermaid
---
config:
  flowchart:
    wrappingWidth: 320
---
flowchart TB
    cmd(["/shutdown"])
    phrase(["the goodbye phrase, from an Administrator"])
    reply["reply 'ok bye bye!'"]
    now["cluster_.shutdown()"]
    post["send_message 'ok bye bye!'"]
    wait["goodbye_ jthread waits 1.5 s,<br/>then cluster_.shutdown()"]
    ret["DPP's event loop ends:<br/>cluster_.start() returns"]
    log["run() logs: disconnected, LatiBot has stopped"]

    subgraph dtor["~bot(): members in reverse declaration order"]
        direction TB
        d1["goodbye_ joined, while the cluster it used still exists"]
        d2["log_channel_ untaps the logger:<br/>from here on, lines go to the console only"]
        d3["language model objects, then speech: dectalk_engine's<br/>worker is told to stop and joined"]
        d4["message features, stores"]
        d5["cluster_: DPP closes its connections and threads"]
        d6["guild_settings_, database_: bot.db closed"]
        d1 --> d2 --> d3 --> d4 --> d5 --> d6
    end

    back(["main() returns 0"])

    cmd --> reply --> now --> ret
    phrase --> post --> wait --> ret
    ret --> log --> dtor --> back
```

A crash does not go through any of this. That is why replacements left
`pending` are settled on the next start (section 2), rather than on the
way out.
