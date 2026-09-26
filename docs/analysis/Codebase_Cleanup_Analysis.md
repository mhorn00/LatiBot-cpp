# Codebase cleanup analysis: guidance

Guidance for a full analysis of the LatiBot C++ port. The port was built over
many sessions with limited context in each, so it has probably collected
inconsistencies, duplication, loose ends and mistakes. The job is to find
them, prove them, and write them up so they can be fixed later in deliberate,
separate changes.

The task produces two things, in this order:

1. **The report**, `docs/analysis/Cleanup_Analysis_Report.md`: every
   finding, with its evidence, and the recommendations, including the
   places where logging falls short (§5.15).
2. **A comment pass over `src/`** (§8), done after the report is written.
   It corrects comments that are wrong and adds step-by-step comments inside
   the complex functions, to make the code easier to review. **Only
   comments change:** no code, no behaviour.

Nothing else changes: no code, tests, build, tools, config or other docs.
Nothing is committed unless asked; the report and the comment pass are left
for review as separate pieces of work.

---

## 1. Ground rules

These are specific to this repository and override anything generic below.

- **The repository is public.** The report is committed to
  `github.com/mhorn00/LatiBot-cpp`. Never put real Discord IDs, usernames,
  server names, nicknames or message text in it. That rules out quoting
  anything from `data/`, from `java-reference/LatiBot v1.1/nicknames.json`,
  `UrlReplacements.txt` or `YesNoAnswers.txt`, or from `data/bot.db`. Quote
  code, not data.
- **Never open the secret files**: `.env`, and
  `java-reference/LatiBot v1.1/bin/main/token.txt` and `openai_key.txt`. A
  content search across `java-reference/` or the repo root will print them,
  so exclude them from every search, for example with Grep's glob
  `!**/{token.txt,openai_key.txt,.env}`, or search `src/main/java` only.
- **Do not run the bot** or anything that connects to Discord. Behaviour that
  only real Discord can confirm is written up as Suspected with what would
  settle it.
- **Do not build the Debug `LatiBot` target while the bot is running.** Check
  with `Get-Process LatiBot` first. If it is running, use the Release and
  ASan builds, or ask.
- **Do not run the generators in their writing mode.** In particular,
  `tools/Update-TestCatalog.ps1` rewrites `docs/testing/Test_Catalog.md`, and
  `tools/Invoke-ClangFormat.ps1` without `-Check` rewrites sources (see §6).
  The one exception is at the end of the comment pass (§8).
- **Pin the report to a commit.** Record the commit the analysis started from
  (`git rev-parse --short HEAD`) at the top of the report. Cite every
  location as `path:line` against that commit, plus the function name, so the
  citations still lead somewhere after the comment pass moves lines.
- **Out of scope:**
  - `third_party/`: DPP, a git submodule, built from source.
  - `build/`, `build-asan/`, `build-fuzz/` and `build-tidy/`.
  - `data/`: runtime state and personal data.
  - `CMakeUserPresets.json`: local, ignored.
  - `java-reference/`, except as the reference for porting fidelity (§5.5).

---

## 2. What this codebase is

A C++20 port of a Java Discord bot. It uses **DPP** for Discord, **CMake**
with **Conan 2** for dependencies (SQLite, CTRE, Catch2, OpenSSL, zlib,
opus), and **MSVC** on Windows. The tooling is PowerShell 7. Python is not
installed locally and cppcheck has not been set up, so don't assume either.

**Where the port stands** (plan §0):

| Phases | State |
|---|---|
| 0–3: framework, basic commands, triggers, nicknames, midnight, backups, URL replacement, reaction statistics, backfill | done |
| 4: DECtalk, mixer, voice | not started |
| 5: LLM | not started |
| Music, emote statistics | unscheduled |

**Shape:**
- **One library, two programs.** `src/core/` builds the static library
  `latibot_core`, which both `LatiBot.exe` (`src/main.cpp`) and
  `latibot_tests.exe` link.
- **`bot`** (`src/core/bot.cpp`, the largest file) is the shell. It wires DPP
  events to the command registry, the message pipeline, the stores and the
  timers, and it is deliberately untested.
- **Functional core.** Features are pure functions or state machines that
  return actions as plain data: `events::action`, `events::embed_action`,
  `retry_plan`. The shell carries those actions out.
- **Ports.** `src/core/ports/` defines `clock`, `discord_gateway`,
  `http_client` and `tts_engine`, with hand-written mocks in `tests/mocks/`
  and no mocking framework.
- **Storage.** Per-guild state lives in SQLite, as numbered migrations in
  `src/core/db/migrations.cpp`, and each feature has its own store class.
- **Panels keep no state on the bot's side.** Their state rides in the
  `custom_id` as `view:page:argument` (`ui::page_state`), and
  `bot::on_component` / `bot::on_form` route by view name.
- **Command flags.** Commands declare their flags in `command_info`, per
  kind of message (result, refusal, post) and per subcommand, and reply only
  through `command::result`, `refusal` and `post` (plan §21.15).

**Size.** `src/` is about 15,000 lines across 90 files. `tests/` is about
8,400 lines, with roughly 460 tests. The largest files are `bot.cpp` (~950
lines), `commands/linkstats.cpp` (~820), `commands/trigger.cpp` (~630),
`commands/urlrepl.cpp` (~590), `events/reactions.cpp` (~530) and
`util/log.hpp` (~460, mostly header-only).

---

## 3. Sources of truth: read these before judging

| Document | What it is | How to use it |
|---|---|---|
| `docs/porting/Porting_Plan_Final.md` | The maintained design. Implemented sections have *As built* notes where the build departed from the text; §21 records corrections learned while implementing; §5.2 lists the migrations | A behaviour that differs from the Java bot or the original design, **and is recorded here**, is a decision, not a finding. It becomes a finding only if the code disagrees with what is recorded |
| `docs/porting/Porting_Plan_v1.md` … `v4.md`, `Porting_Initial_Analysis.md` | Superseded drafts | Historical. Do not report them as stale. Code comments cite "plan v4 §x" (see §5.13) |
| `docs/features/README.md` | The user-facing reference for what is built | Compare it against the code; mismatches are findings |
| `docs/features/Planned.md` | The behaviour spec for phases 4, 5 and beyond | Explains why some scaffolding exists |
| `docs/testing/README.md` | Test strategy, conventions, and a *Known gaps* list | Known gaps are already acknowledged. Check the list is still accurate rather than re-reporting its entries |
| `docs/testing/Test_Catalog.md` | Generated from the test sources | Check it is in sync (§6); never edit it by hand |
| `docs/ideas/Self_Hosted_Embeds.md` | An idea, not a commitment | Ignore unless the code references it |
| `README.md` | Setup, commands, environment variables, logging | Compare against the code, `.env.example` and `CMakePresets.json` |
| Code comments | Convention: comments explain **why** | A "why" that is no longer true is a real finding |

### Decisions not to reopen without strong evidence

Recommend reversing one of these only if you can show it causing a concrete
problem, and say which decision it is:

- **Plan §21.5: generic abstractions wait for a third example.** The trigger
  panel and the URL rule panel are knowingly not unified; `/llm settings`
  (phase 5) is meant to be the third. For duplication, say how many instances
  exist today, and recommend extracting only at three or more, or where the
  duplication has already caused divergence or bugs.
- **The functional core and thin shell**, and that `bot` stays untested. Do
  flag logic that has crept into `bot` (the testing README already names
  some).
- **Hand-written mocks, and no mocking framework.**
- **Secrets from the environment only**, with `.env` as a local convenience
  (plan §21.1).
- **Message flags per kind of message** (plan §21.15).
- **The parity deviations recorded in the plan's *As built* notes.**
  Examples: link replacement off per guild until enabled; `/urltoggle` as its
  own command; attribution refusing a mismatched link; webhook mode dropped;
  the 🔗 emoji instead of `:link:`.

---

## 4. Method

1. **Inventory first.** Walk every file in scope and record its purpose, the
   main types and functions it defines, and what depends on it. Group the
   files by module: `commands`, `config`, `db`, `discord`, `events`, `ports`,
   `ui`, `util`, then tests, tools, build files and docs. This becomes the
   appendix.
2. **Trace what is reachable**, starting from `main.cpp`, then the `bot`
   constructor and its four `register_*` functions:
   - **Slash commands**: which classes are added to the registry, and which
     subcommands each `build()` declares against what `execute()` dispatches
     on.
   - **Message pipeline stages** and their order (`register_stages`).
   - **DPP event handlers** (`register_events`): guild create, message
     create, update and delete, the four reaction events, member updates,
     audit log entries, slash commands, autocomplete, buttons, selects and
     forms.
   - **Timers** (`register_timers`): the midnight tick, the embed tracker's
     tick and the backup schedule.
   - **Startup work**: migrations, the nickname import, and the URL rules
     import on guild create.
   - **Detached coroutines** started through `bot::detach`.

   Anything not reachable from these is a candidate for unwired code.
3. **Verify before claiming.** Much of the wiring here is string-based, so a
   symbol search alone misses it. Check each of these in both directions:
   - Panel view names: every view a renderer encodes into a `custom_id` is
     routed in `bot::on_component` or `bot::on_form`, and every routed view
     is emitted somewhere.
   - `guild_settings` keys: written and read, with the same spelling.
   - Subcommand names: in `build()`, in the strings `execute()` compares
     against, and in `command_info::subcommand_responses`.
   - Option names: in `build()` and in every `get_parameter("…")`.
   - Columns: in `migrations.cpp` and in each store's SQL, including the
     column order a `read_row` expects.
   - Config keys: `config.json` keys in `bootstrap::from_json`, against the
     README.
   - Environment variables: those read by the code, against `.env.example`
     and the README.
   - Test tags: those used in `tests/`, against the lists in
     `tools/Update-TestCatalog.ps1` and `.vscode/settings.json`.
   - Build lists: every `.cpp` on disk against `src/CMakeLists.txt`,
     `tests/CMakeLists.txt` and `tests/fuzz/CMakeLists.txt`, since both are
     explicit lists.

   Mark every finding **Confirmed** (you traced it and say how) or
   **Suspected** (plausible, but it needs judgement or live Discord to
   settle).
4. **Separate scaffolding from dead code.** Some code exists for phases 4
   and 5:
   - the `http_client` and `tts_engine` ports and their mocks;
   - `discord/dpp_http_client`;
   - the `llm_*`, spend-cap and `llm_tool_rounds` config keys;
   - `trusted_guilds` and `trusted_users`, with `bootstrap::is_trusted`.

   Report these under WIRE as **planned**, citing the plan section that calls
   for them, unless the plan no longer does. Truly dead code is a separate
   and stronger finding.
5. **Write as you go.** The code is too large to hold at once. Write the
   report section by section, then re-read it at the end to merge duplicate
   findings and cross-reference related ones.

---

## 5. What to look for

Each category below includes **leads**: things noticed while writing this
guide. They are starting points, not findings. Verify each one before it goes
in the report, and look well beyond them.

### 5.1 Redundancy and duplication (DUP)

Duplicated logic that a shared helper would remove; repeated command-handler
boilerplate; two implementations of one concept. For each, give every
location and the shape of the shared version. Weigh it against §21.5.

Leads:
- **Option and subcommand helpers.** Several command files define their own
  `string_option`, `int_option` and `subcommand_of`. `linkstats.cpp` has
  `group_and_action`, and `registry.hpp` now provides `subcommand_path`.
  `invoker_permissions` exists in both `urlrepl.cpp` and `linkstats.cpp`. A
  `button()` helper exists in both `trigger.cpp` and `urlrepl.cpp`.
- **Panel builders.** `pick_menu`, `selection_row` and `footer_row` appear in
  both `trigger.cpp` and `urlrepl.cpp`. This is the §21.5 case: two
  instances today.
- **`embed_urls_of`** is defined in both `bot.cpp` and `url_replacer.cpp`.
- **Lowercasing.** `guild_settings.cpp` and `events/midnight.cpp` each have a
  local helper, and `util/text` has none.
- **Snowflake parsing.** `bootstrap`'s `require_snowflakes` uses
  `std::stoull`, which accepts `"123abc"`, while `parse_snowflake` in the
  same file is strict.
- **Store boilerplate**: the prepare, step and read loops, and how each store
  takes the database lock (see §5.8).

### 5.2 Mistakes and drift between sessions (BUG)

Logic errors, and places where one file assumes an interface or behaviour the
other side no longer has. Drift is the likeliest class here, because features
were changed in later sessions than the ones that built them.

Leads:
- **Recent cross-cutting changes may not have reached everywhere.** Check:
  - the per-kind message flags: renderers or comments that still talk about
    `ack()`, or set flags themselves;
  - URL replacement being off by default: paths that still assume it is on;
  - the debug recompute override.
- **Store SQL against the migrations**, especially migration 9's
  `message_flags` columns, and the column order in `row_columns` and
  `read_row`.
- **Time.** Code that reads the clock directly instead of through
  `ports::clock`. `registry.cpp` times commands with `steady_clock` for the
  log, which is probably fine; say so if so. Also mixed use of `sys_seconds`,
  `time_point` and raw integers for stored times.
- **Snowflakes.** `uint64_t` casts when binding, `dpp::snowflake{}` as
  "none", and `.empty()` checks.

### 5.3 Hanging, unwired and partial features (WIRE)

Code that is defined but never reached, registered things with no handler,
config nothing reads, and files excluded from the build or build entries
pointing at nothing. List every `TODO`, `FIXME`, `HACK` and `XXX` in `src/`,
`tests/`, `tools/` and `cmake/`, with its location and whether it is missing
functionality.

Leads:
- The string wiring in §4 step 3, in both directions.
- Mock methods and test support code nothing uses.
- The top-level `include/` directory is empty and untracked.
- The testing README's layout lists `tests/live/` and `tests/fixtures/`,
  which do not exist.
- Classify the phase 4 and 5 scaffolding as described in §4.

### 5.4 Simplification (SIMP)

Indirection that buys nothing, functions or files that should be split,
fragments that should be merged. Recommend C++20 features only where they
simplify something; the code already uses concepts, ranges, `std::span`,
`std::format` and designated initializers widely.

Leads:
- **`bot.cpp`.** It holds the component and form routing, helpers such as
  `intents_for`, `guild_of`, `embed_urls_of` and `update_panel`, the nickname
  handlers and the timers. Say what would move out and where, and why that is
  simpler rather than just shorter.
- **Long files.** Look at the length and cohesion of `linkstats.cpp` and
  `trigger.cpp`.
- **`command_info` initializers**, now that every constructor spells out
  `responses` and `subcommand_responses`.

### 5.5 Porting fidelity (PORT)

The reference is `java-reference/LatiBot v1.1/src/main/java/latibot/`: 39
files and about 2,800 lines. Map every Java command and listener to its C++
counterpart, or to the plan section that covers it.

- **Not findings:** anything the plan puts in phase 4, 5 or unscheduled. That
  includes the `audio/` commands (`SpeakCmd` and the music queue commands),
  `ChatTestCmd`, `ApiDriver`, and `EmoteStatsCmd` / `GetEmotesCmd`. Also not
  findings: deviations recorded in *As built* notes, such as `ToggleWebhooksCmd`,
  because webhook mode was dropped (§9.5).
- **Worth comparing closely:**
  - `MessageListener`: pipeline order, triggers, URL replacement, goodbye;
  - `NicknameListener` and `NicknameCmd` / `NicknamesCmd`;
  - `ReactionListener`;
  - `MidnightManager`;
  - `SayCmd`, `StatusCmd`, `PingCmd`, the join and leave commands, and
    `ShutdownCmd`;
  - `ReplaceUrlCmd` and `ToggleReplaceCmd`.
- **Flag each difference** as *looks deliberate* (and cite the plan if
  recorded) or *looks unintentional*.

### 5.6 Discord-specific correctness (DISC)

- **Registration against handlers.** Compare each `build()` payload with what
  `execute()` handles. `registry::add` validates override keys; confirm
  anything else that can drift.
- **The three-second deadline.** Find every command that awaits something
  slow before its first reply.
  - Leads: `/nickname` awaits the member edit before replying; `/say` with
    `reply:` fetches the message first; the `/linkstats` boards run their
    queries first.
  - Say whether each should defer. Deferring fixes ephemerality at defer
    time, so look at how it would interact with `command::result`.
- **Replies that bypass the helpers.** Search for `co_reply(`, `reply(` and
  `set_flags(` outside `command::result`, `refusal` and `post`. The ones in
  `bot.cpp` for Retry notes and form complaints are not command replies;
  confirm each is deliberate.
- **Discord's limits, and whether a test covers each renderer against them**
  (plan §21.4):
  - 2,000 characters of content: every list, board, profile, dry run and
    report renderer;
  - 5 rows of 5 components;
  - 25 select options and 25 autocomplete choices;
  - 100-character `custom_id`s;
  - 45-character modal labels.
- **Intents.** The bot requests the defaults, message content, and guild
  members when `track_nicknames` is on. Check each handler against the
  intent it needs: reactions, audit log entries, members, voice states.
- **Interaction types.** Check that buttons, selects, modals and
  autocomplete for an unknown or stale view get an answer rather than
  timing out.
- **Mentions.** Public messages must not ping. DPP sends
  `allowed_mentions.parse: []` by default; find anything that changes that.
- **Rate limits.** Look at the backfill's history and reactor paging, the
  embed tracker's edits, and bursts of posts.

### 5.7 Lifetime and async safety (LIFE)

- **The command event itself is safe.** DPP keeps the event alive until a
  coroutine handler finishes (`handle_coro` in
  `third_party/DPP/include/dpp/event_router.h`), so a command using `event`
  after an `await` is fine. Don't report that.
- **Do check every other reference a coroutine holds across a suspension:**
  - `const&` parameters of `dpp::task` functions;
  - lambdas capturing locals by reference. The recompute's `progress`
    lambda captures `&event`, `&discord` and `&request`, and is safe only
    while `recompute_start` is suspended in `run`;
  - `string_view` and `span` members;
  - `this` in `bot::detach` jobs, timers, and the detached `std::thread` in
    `stop_bot`.
- **Ownership.** Look for raw owning pointers, and for anything accessed
  after being moved from.

### 5.8 Concurrency (CONC)

DPP runs handlers on a thread pool, so events for the same guild can run
concurrently.

- **Shared mutable state.** Check each of these, and how it is protected:
  - `trigger_responder`'s cooldown map;
  - the `embed_tracker`, touched by its one-second tick and by message
    update events;
  - `pending_nicknames`;
  - the backfill's running-guild set;
  - the migration and import flags.
- **The database.** `db::database` offers `lock()`, and some store methods
  take it while others don't (for example, `url_rule_store::opted_out`
  does not, while `remove` does). Work out the actual policy: what SQLite's
  threading mode is, and what the lock is for. Then report every store
  method that breaks it.
- **Blocking work on event threads**: SQLite calls, backups and file I/O.

### 5.9 Error handling (ERR)

Describe the strategies in use before recommending a policy:
- exceptions: `db_error`, `config_error`, `registry_error`;
- `ports::result<T>`, with `ok()` and `error()`;
- `std::optional`;
- `std::variant<T, std::string>` for reasons shown to users: `build_rule`,
  `plan_retry`;
- `bool` returns;
- `bot::detach`'s catch-all.

Leads:
- A thrown slash command now gets a reply (`registry::dispatch`). Check what
  happens when a button, select or form handler in `bot.cpp` throws: a store
  call there, say.
- Errors logged but never shown to anyone, and the reverse.

### 5.10 Consistency and conventions (CONV)

- **Names.** Identifiers are snake_case. Panel view names mix styles:
  `trigpanel`, `urlpanel`, `linkboard`, `nicks`.
- **Reply text.** Refusals are lowercase and confirmations sentence case.
  Report places that break the pattern, if the pattern holds.
- **Logging.** The form of log lines: who did something is logged as a
  `user_label`, and IDs as snowflakes so they are coloured. Check new code
  follows this. Whether the right things are logged is §5.15.
- **File layout.** Each command file keeps an anonymous namespace of
  helpers. Check that pattern is followed.

### 5.11 Headers and build (BUILD)

- `#pragma once` everywhere.
- **Includes.** `misc-include-cleaner` is disabled in `.clang-tidy`, so
  nothing checks includes automatically. Spot-check missing direct includes
  and unused ones.
- **Code in headers.** Implementation that should live in a `.cpp`;
  `util/log.hpp` is large.
- **Dependencies.** Compare Conan's packages (`conanfile.py`) with what is
  used. Some serve only DPP, and that is fine.
- **The non-MSVC warning branch** in `cmake/warnings.cmake` is never built
  in CI.
- **Explicit source lists**, as in §4 step 3.
- **CMake 4.4 errors.** Configuring prints dozens of `IMPORTED_LOCATION`
  errors, and a build right after a CMakeLists change can miss new files.
  This is documented (plan §21.14) and is not a finding unless you find
  something new.

### 5.12 Configuration, secrets and security (CONF)

- **Config keys.** Each `config.json` key read by `bootstrap::from_json`,
  against the README.
- **Environment variables.** The ones the code reads, against
  `.env.example` and the README: `DISCORD_BOT_TOKEN`, `ANTHROPIC_API_KEY`,
  `OPENAI_API_KEY`, `LATIBOT_LOG_LEVEL`, `LATIBOT_LOG_COLOR`, `NO_COLOR`,
  `SSL_CERT_FILE`, `LATIBOT_DEBUG_RECOMPUTE_BOT_ID` and `LATIBOT_TEST_TOKEN`.
- **Magic numbers.** Discord error codes such as `50013`, and limits, that
  should be named constants.
- **SQL.** Built only from constants, with user input always bound? Check
  every `std::format` into a query.
- **Paths.** File paths built from config or user input.
- **Raw JSON.** The raw gateway JSON parsed in `guild_of`.

### 5.13 Stale documentation and comments (DOC)

- **Code comments cite "plan v4 §x"** while the maintained plan is
  `Porting_Plan_Final.md`. Check a sample, and several in `events/` and
  `commands/`, to see whether the section numbers still match, and report
  the pattern rather than every instance.
- **"Why" comments** describing behaviour that later changed.
- **The documents.** `README.md`, `docs/features/README.md`,
  `docs/testing/README.md` and the plan's §4 (project structure), against
  the tree as it is.

### 5.14 Tests (TEST)

- **Known gaps.** `docs/testing/README.md` lists them. Confirm each is still
  true, and don't re-report them as new.
- **Critical paths without tests.** Examples:
  - migrations applied from each earlier schema version;
  - every store's SQL;
  - the `execute()` paths, most of which need a cluster to reply to;
  - panel routing.
- **Tests that pin implementation details** rather than behaviour.
- **The fuzz targets' invariants**: whether they still hold, and what else
  takes untrusted text.

### 5.15 Logging (LOG)

Find where `info` or `debug` logging falls short. The aim is a log that
answers the questions someone will actually ask, **not more logging**. Every
proposed line must say who would read it and what question it answers. A
line nobody would miss is not worth adding.

**The policy, from `README.md` (*Logging*) and the code as it stands:**
- **`info` is the running record.** It covers:
  - every command, with who ran it and what they passed. `registry::dispatch`
    already logs this once for every command, so a command should log its
    *outcome* rather than repeat the invocation;
  - every state change, and who made it;
  - every message the bot posts on its own;
  - startup, shutdown, schema changes, guilds joined, and imports.
- **`debug` is the reasoning**: why a stage or handler decided not to act,
  what a panel's `custom_id` decoded to, what a timer found, and how long
  things took.
- **`trace` is message content** and DPP's gateway chatter.
- **`warn` and `error`** are failures, with enough context to act on.

**A gap is one of these:**
- **A state change with no `info` line** naming what changed and who changed
  it. Examples: a store write reached from a command, a panel or a timer.
  One change reachable from two places, such as a command and a panel,
  should be logged the same way from both. `switch_url_replacement` is the
  pattern: one function, one line, called from both.
- **A decision that returns silently** where someone asking "why didn't it
  do X?" would need a `debug` line to answer. This matters most for early
  returns in pipeline stages, event handlers and panel routing.
- **A failure dropped without a log**: an ignored `result`, a `catch` with
  nothing in it, or a Discord call whose error nobody sees.
- **Background work whose outcome is invisible**: timers, detached
  coroutines (`bot::detach`), the backfill, backups and imports.
- **Context missing from an existing line**: the guild, the user, the ID of
  the thing acted on.

**Also report the opposite**, sparingly:
- lines at the wrong level, such as `info` on every message;
- lines that repeat what `registry::dispatch` already logged;
- anything that could log a secret, or message content above `trace`.

**Where to look.** The message pipeline's stages, the reaction and
message-update handlers, `bot::on_component` and `on_form`, the embed
tracker's actions, and the timers. Also check the stores whose writes are
reached from more than one place.

**Each LOG finding proposes the actual line**: its level, its message and
arguments in the code's style, where it goes, and the question it answers.

---

## 6. Tooling to run

Record each result, with counts, in the appendix. Commands run from the
repository root in PowerShell.

| What | Command | Notes |
|---|---|---|
| Build | `cmake --build build --config Debug` and `--config Release` | MSVC runs `/W4 /permissive- /WX`, so a clean build has no warnings by construction. `-Wall` and friends do not apply |
| Tests | `ctest --preset debug`, `ctest --preset release`, `ctest --preset asan` | ASan needs `cmake --build build-asan --config Debug` first |
| Static analysis | `pwsh tools/Invoke-ClangTidy.ps1` | Covers `src/`, which is expected to be clean. Also run it with `-IncludeTests` once and report test findings separately: tests are not normally held to it |
| Formatting | `pwsh tools/Invoke-ClangFormat.ps1 -Check` | Only with `-Check`; without it, the script rewrites files |
| Test catalog in sync | `pwsh tools/Update-TestCatalog.ps1 -OutputPath <scratch file>`, then compare with `docs/testing/Test_Catalog.md` | Never write to the real path. The script also fails on mistagged tests and on test names ctest cannot pass |
| Fuzzing (optional) | `cmake --preset fuzz`, `cmake --build build-fuzz --config Debug`, then each `.\build-fuzz\bin\Debug\fuzz_*.exe -max_total_time=60` | Three targets: `fuzz_text`, `fuzz_url_scan`, `fuzz_legacy_parser` |
| Complex functions, for the comment pass | `clang-tidy -p build-tidy --config="{Checks: '-*,readability-function-cognitive-complexity', HeaderFilterRegex: '.*[\\/]src[\\/].*', CheckOptions: {readability-function-cognitive-complexity.Threshold: 12, readability-function-cognitive-complexity.DescribeBasicIncrements: false}}"` over `src/**/*.cpp` | The project's threshold is 25; 12 lists the functions worth a walkthrough. `tools/Common.ps1`'s `Get-LlvmTool` finds `clang-tidy.exe`, and running `Invoke-ClangTidy.ps1` once configures `build-tidy`. The quoting may need adjusting for PowerShell |

cppcheck and Python are not available. Say so rather than skipping silently.

---

## 7. The report

### Severity, for this bot

| Severity | Means |
|---|---|
| **Critical** | Can lose or corrupt stored data (the database, migrations, backups), expose a secret, crash the process, or make the bot act wrongly in Discord at scale: pinging people, replacing links where replacement is off, posting repeatedly |
| **High** | A feature misbehaves for users in a normal path, or a race or lifetime bug that is plausible in practice |
| **Medium** | An edge-case bug; a maintainability problem that is already causing drift; a missing test on logic that matters |
| **Low** | Consistency and small cleanups |

### Each finding

- **ID**: one prefix per category: `DUP`, `BUG`, `WIRE`, `SIMP`, `PORT`,
  `DISC`, `LIFE`, `CONC`, `ERR`, `CONV`, `BUILD`, `CONF`, `DOC`, `TEST`,
  `LOG`. For example `BUG-004`.
- **Title**
- **Severity** and **Status** (Confirmed or Suspected)
- **Location(s)**: `path/to/file.cpp:line`, all of them
- **Description**: what is wrong and why it matters. A short code excerpt
  only if it helps, and never data.
- **Evidence**: what you searched and read to reach the conclusion
- **Plan reference**: the plan section, when the finding touches a
  documented decision
- **Recommendation**: the concrete fix or refactor
- **Effort**: S, M or L
- **Risk of fix**: what could break, and which tests would show it
- **Related**: IDs of related findings

### Structure

1. **Executive summary.** The commit the analysis is pinned to, overall
   health, the most important findings, and counts by category and
   severity.
2. **Findings by category**, in the order of §5.
3. **Prioritized action plan**, in phases:
   - correctness and safety;
   - unwired and dead code;
   - duplication;
   - simplification and consistency;
   - logging;
   - docs.

   Note the dependencies between fixes. Each phase must leave the project
   building, all three ctest presets passing, clang-tidy clean over `src/`,
   and the test catalog regenerated, so each can be its own commit.
4. **Decisions needed.** Questions only the owner can answer: finish or
   remove something, which error policy to adopt, whether a parity
   difference is intended.
5. **Verified OK.** Areas examined and found sound, so the coverage is
   visible.
6. **The comment pass (§8)**, added once it is done:
   - the functions that got step comments, by `path:function`;
   - the comments corrected, each with one line on what was wrong;
   - comments found wrong because the *code* is wrong, cross-referenced to
     their findings.
7. **Appendix.** The inventory, the reachability map from §4, and the
   tooling results from §6.

Be specific and prove what you claim. Fewer well-evidenced findings beat
many vague ones. Leave out style nitpicks unless they show a real
inconsistency, and don't pad the report. When it is done, give a short
summary of the top findings in chat and a link to the report.

---

## 8. The comment pass

After the report is written, go back over `src/`. This is the only change to
code files the task makes, and it touches **comments only**. It does two
things.

### 8.1 Correct comments that are wrong

Find comments that describe what the code used to do. That includes:
- helpers that have been renamed or removed, such as `ack()`;
- old parameters or behaviour;
- counts that have changed, such as "four buttons";
- plan citations that no longer match.

For the `plan v4 §x` citations (§5.13), correct them all only if checking
shows what they should say. If the section numbers match the maintained
plan, that is `plan §x`. Otherwise, report the pattern and leave the
citations alone.

**If the comment is right and the code is wrong, do not change the comment
to match.** That is a finding: record it in the report and leave the comment
alone.

**Wrong comments outside `src/`** (in `tests/`, `tools/` or build files) go
in the report as DOC findings rather than being fixed here. Changing a test
file moves the line numbers the test catalog links to.

### 8.2 Add step comments inside complex functions

The purpose is to help the owner review the code. The functions that need it
are:
- the ones the complexity scan (§6) lists;
- long ones;
- subtle ones: state machines, SQL built from pieces, parsers, and
  coroutines whose suspension points constrain what must stay alive.

Starting points, to check against the scan:
- `bot::on_component` and its per-panel routing;
- `registry::dispatch` and `check_responses`;
- `backfill_service::scan_channel`, `scan_page` and `consider`;
- the `embed_tracker`: `on_embeds`, `tick` and its finishing logic;
- `classify` and `attribute` in `legacy_replacements`;
- `find_links` and its trimming in `util/url_scan`;
- `reaction_store`'s statistics query builder;
- `midnight_scheduler::tick`;
- `linkstats_command::recompute_start`;
- the `url_replacer` stage.

**How to write them:**
- **Mark the steps.** A short comment at the head of each logical step, not
  on every line. Say what the step is for and anything a reader would
  otherwise have to work out:
  - an ordering constraint;
  - an invariant being kept;
  - why an early return is right;
  - what must stay alive across an `await`.
- **Match the house style.** Full sentences; `//` inside functions, and
  `///` stays for declarations. Explain why rather than restate the code.
  Keep the density modest.
- **Make each comment stand alone.** No finding IDs, and no mention of this
  analysis or of review. A comment has to stay true after the report is
  forgotten.
- **Leave simple functions alone.** A function that reads clearly does not
  need a walkthrough.

### 8.3 Check that only comments changed

Before handing it over:

1. **Only comment lines differ.** This should print nothing:

   ```bash
   git diff -U0 -- src | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' | grep -vE '^[+-]\s*(//|$)'
   ```

   Anything it prints is a changed line that is not a whole-line comment.
   A corrected trailing comment on a line of code shows up here too; check
   those by eye. Anything else is a code change and needs undoing.
2. **Formatting.** `pwsh tools/Invoke-ClangFormat.ps1 -Check`. If a new
   comment is too long, running the script without `-Check` is allowed here
   to reflow it. It also regenerates the test catalog, which should come out
   unchanged. Then repeat step 1.
3. **Build and test.** Build Debug and Release, then run the `debug` and
   `release` ctest presets. A comment can still break a build, for example
   with a trailing backslash.
4. **Leave it uncommitted**, separate from the report, and list what changed
   in the report's comment pass section.
