# Codebase cleanup analysis: report

Written to the guidance in
[Codebase_Cleanup_Analysis.md](Codebase_Cleanup_Analysis.md).

**Pinned to commit `1cd9fec`.** Every `path:line` below refers to that
commit; function names are given too, so a location can still be found after
lines move.

> **Status: complete.** Written over two passes (see the progress log at
> the end). The comment pass that follows it is recorded in §6.
>
> **Resolved.** Every finding has since been fixed or decided; the table
> below gives the commit for each. Line numbers in the findings still refer
> to `1cd9fec`.

## Resolution

Done in severity order, then down the action plan. After the last commit,
Debug, Release and ASan each pass 500 of 500 tests, clang-tidy is clean over
`src/` and `tests/`, formatting and the catalog are clean, and a 30-second
run of each fuzz target found nothing.

| Finding | Commit | What was done |
|---|---|---|
| (comment pass, §6) | `3a06e65` | comments only |
| CONC-002 | `9c1477c` | one lock around each trigger's cooldown; a race test that fails without it; `forget_cooldowns` removed |
| DUP-002 | `ca67713` | `tests/support/discord_limits.hpp`, used by every renderer's test |
| DISC-001 | `450e5dd` | Cancel carries the item and reselects it |
| DISC-002 | `9e61d13` | `util::truncate` for menu labels; the form takes what the command takes |
| ERR-001 | `364826c` | `guarded()` timers, and per-item catches in both ticks |
| LOG-002 | `4478b97` | sends log "posted" or "could not post" after Discord answers |
| TEST-002 step 1 | `850d1cf` | `urltoggle_refusal`, `linkstats_refusal`, the two untested renderers |
| BUG-003 | `7c2868c` | stranded replacements read at startup and settled on first `guild_create` (decision 7: settle at once) |
| TEST-003 | `2812d96` | `latibot_fuzz_core` with coverage; a real `fuzz_text`; seed corpora |
| ERR-002 | `dda644e` | failing or unclaimed components and forms are answered |
| CONC-001 | `2aac21c` | lock in `erase`; the policy in `database.hpp` |
| BUG-002 | `ce07389` | `util::parse_snowflake` everywhere text becomes an ID |
| BUG-001, WIRE-001 | `d21c211` | suppression judged untrimmed; markers in code skipped; `is_inside_spoiler` removed |
| BUG-004 | `902ddd7` | `url_rule_store::rename`; panel logs only what happened |
| LIFE-001 | `373bef1` | the goodbye thread is a joined `std::jthread` member |
| DISC-004 | `15cc35d` | `command::defer` and `answer_deferred`; the failure reply edits a deferred response |
| BUILD-001 | `ca9830e` | widening check back on |
| TEST-001 | `e8575f6` | `-IncludeTests` completes and is clean |
| WIRE-002, WIRE-004, WIRE-005 | `1d565f1` | accessors and `members` removed; the rest commented (WIRE-003 needed nothing) |
| DUP-001, DUP-004 | `82b2626` | `util::to_lower`, `equals_ignoring_case`, `lines` |
| DUP-005 | `37a4721` | `commands/options` |
| SIMP-001 | `85a612e` | one query for a guild's responses; emoji names joined |
| LOG-001 | `2e4baee` | failed rollback logged |
| CONV-001 | `49dbe52` | `result` in `ports`; view names written down; "i" |
| CONF-001 | `70cb79a` | configuration table in the README |
| DOC-002 (and DOC-001's pointer) | `b7acb33` | plan §4, testing README, catalog heading, `.env.example`; DOC-001 and DOC-003 were the comment pass |
| DISC-003 | `d966e8e` | decision 8: `domain` capped at 40 |
| PORT-001, PORT-002 | `f1ccd95` | decisions 1–2: `/join`, `/leave`, `/shutdown` public, `/join` names whom it followed; `/say` keeps Manage Messages; recorded in plan §6 |
| PORT-003 | `86b8f6d` | decision 3: the last `/status` is kept and restored on connect |
| BUILD-002 | `fc2a569` | decision 6: CI checks formatting and builds and tests Release too; ASan and clang-tidy stay local |
| DUP-003 | `5f0e239` | decision 5: `db::statement` binds snowflakes and times |
| TEST-002 step 2 | — | decision 4: not now; listed under *Known gaps* in the testing README |

**Not verified here, since that needs Discord:** the goodbye shutdown
(LIFE-001), the deferred `/nickname` and `/say reply:` (DISC-004), the
restored status (PORT-003), the stranded-replacement sweep's hookup
(BUG-003), and the new CI jobs (BUILD-002), which run on the next push.

---

## 1. Executive summary

**The codebase is in good health.** It builds with no warnings under `/W4
/WX`. All 464 tests pass in Debug, Release and AddressSanitizer.
clang-tidy is clean over `src/`, formatting is clean, and the test catalog
is in sync. The design the plan describes is the design the code has: a
functional core returning actions, ports with hand-written mocks, one
serialised database connection, and panels whose state lives in the
`custom_id`. Nothing is Critical. There is no data-loss path, no
exposed secret, and no way for the bot to act at scale where it should
not.

**39 findings:**

| Category | High | Medium | Low | Total |
|---|---:|---:|---:|---:|
| DUP duplication | | | 5 | 5 |
| BUG mistakes | | 1 | 3 | 4 |
| WIRE unwired | | | 5 | 5 |
| SIMP simplification | | | 1 | 1 |
| PORT parity | | | 3 | 3 |
| DISC Discord | 1 | 1 | 2 | 4 |
| LIFE lifetime | | | 1 | 1 |
| CONC concurrency | 1 | | 1 | 2 |
| ERR error handling | | 1 | 1 | 2 |
| CONV conventions | | | 1 | 1 |
| BUILD build | | | 2 | 2 |
| CONF config | | | 1 | 1 |
| DOC docs | | | 3 | 3 |
| TEST tests | | 2 | 1 | 3 |
| LOG logging | | 1 | 1 | 2 |
| **Total** | **2** | **6** | **31** | **39** |

**The eight that matter most:**
1. **CONC-002 (High):** the trigger responder's cooldown map and random
   engine are shared across DPP's thread pool with no lock. Several people
   posting "420" at 4:20, the one thing it exists for, can corrupt the map
   or reply twice.
2. **DISC-001 (High):** in both panels, deleting on the first or last page
   of a multi-page list sends two buttons with the same `custom_id`, which
   Discord rejects. With two pages, Delete fails on every page.
3. **ERR-001 (Medium):** DPP drops a repeating timer for good if its
   callback throws once. The midnight and embed-tracker timers call SQLite
   without a `try`, so one database error ends midnight posts, or all
   replacement tracking, until a restart.
4. **BUG-003 (Medium):** a restart during a preview watch leaves the
   replacement `pending` for ever: no Retry button, and possibly no
   preview anywhere.
5. **DISC-002 (Medium):** a trigger pattern over 100 characters, which
   `/trigger add` accepts, breaks the trigger panel for the whole server.
6. **LOG-002 (Medium):** trigger replies and midnight posts are sent
   fire-and-forget, and logged as sent before they are. A failed midnight
   post loses that day's message with no trace.
7. **TEST-002 (Medium):** no command's `execute()` is tested. That
   includes the three Manage Server checks that Discord's permissions
   cannot make for us.
8. **TEST-003 (Medium):** the fuzzers get no coverage feedback from the
   library they fuzz, and `fuzz_text` checks a tautology.

**The rest** is mostly duplication with an obvious shared home (DUP-001 to
DUP-005: text helpers, option readers, store conversions, a line splitter,
the component-limit test checks), and scaffolding for phases 4 and 5 that
is correctly unused (WIRE-003). There are also a few doc and comment drifts
(DOC-001 to DOC-003) and three parity changes nobody recorded
(PORT-001 to PORT-003), which need a decision rather than a fix. The
Medium and High items are all S-effort except BUG-003 and TEST-002, which
are M.

---

## 2. Findings

### 2.1 Redundancy and duplication (DUP)

**Below the §21.5 threshold.** These are duplicated in two places only. Plan
§21.5 says to wait for a third example before extracting, so no action is
recommended unless one copy is being changed anyway:
- `to_dpp(ports::http_method)` in `discord/raw_api.cpp:12` and
  `discord/dpp_http_client.cpp:12`, identical switch statements. Both
  build the same "HTTP transport error N" message as well.

#### DUP-001: Text helpers written again in almost every file that needs one

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):**
  - **Lowercasing a string**, written as a private helper six times:
    `commands/basic.cpp:64` (`lowercased`), `config/guild_settings.cpp:14`
    (`lowercase`), `events/midnight.cpp:31`, `events/reactions.cpp:43`,
    `events/triggers.cpp:22` and `util/log.cpp:29` (all `lowercased`).
  - **The same thing inline**, twice: `util/url_scan.cpp:207` in
    `rule_host`, and `events/goodbye.cpp:35` in the phrase normalisation.
  - **Case-insensitive equality**, twice: `commands/linkstats.cpp:55` and
    `util/url_scan.cpp:99`, both named `equals_ignoring_case`.
  - **Trimming**: `util/env.cpp:12` has a private `trim` beside
    `util::trim` (`util/text.cpp:22`). Its whitespace set is `" \t\r\n"`,
    while `util::trim` also strips `\f` and `\v`.
- **Description:** `util/text` exists for exactly these helpers but offers
  neither lowercasing nor a case-insensitive compare, so each file wrote its
  own. The copies agree today; they are also the kind of code where one
  copy gets a fix and the others do not. The last is the first sign of
  that: a `.env` line ending in a form feed is trimmed differently from any
  other text in the bot.
- **Evidence:** a search for `tolower` and `toupper` across `src/`, and for
  every definition named `lowercase`, `lowercased` or `trim`.
- **Recommendation:** Add `std::string to_lower(std::string_view)` and
  `bool equals_ignoring_case(std::string_view, std::string_view)` to
  `util/text`, with tests. Replace the ten copies with them, and replace
  `env.cpp`'s `trim` with `util::trim`. That is well past the third-example
  threshold (plan §21.5).
- **Effort:** S.
- **Risk of fix:** Low. The existing tests for triggers, reactions,
  midnight's timezone search, config parsing and logging cover every
  caller.
- **Related:** none.

#### DUP-002: Discord's component limits are checked separately in each test file

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):**
  - `tests/unit/urlrepl_command_test.cpp:47`, `check_components_fit`: rows,
    buttons per row, `custom_id`, label and option lengths;
  - `tests/unit/trigger_command_test.cpp:181` and `:231`: the modal's label
    and placeholder limits inline, and row counts inline;
  - `tests/unit/embed_watch_test.cpp:142`: one `custom_id` length.
- **Description:** Plan §21.4 records that Discord enforces these limits
  only at the API, so tests are the only guard. Each test file checks a
  different subset. None checks that custom IDs are unique within a message,
  which is how DISC-001 went unnoticed. The linkstats board and the nickname
  history renderers get no component check at all.
- **Evidence:** searched `tests/unit/` for the limit constants and length
  comparisons.
- **Recommendation:** Add a `tests/support/discord_limits.hpp` with one
  `check_message_fits(const dpp::message&)` and one
  `check_modal_fits(const dpp::interaction_modal_response&)`. They should
  cover rows ≤ 5, components per row ≤ 5, unique custom IDs, lengths
  (custom ID ≤ 100, button label ≤ 80, modal label ≤ 45, placeholder ≤ 100),
  select options ≤ 25 and content ≤ 2000. Call them from every renderer's
  test.
- **Effort:** S.
- **Risk of fix:** None; it may surface more cases like DISC-001, which is
  the point.
- **Related:** DISC-001.

#### DUP-003: Every store converts snowflakes and times by hand

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):**
  - **Helpers defined privately in each store:**
    - `id_or_null`, three times: `events/nicknames.cpp:46`,
      `events/reactions.cpp:56`, `events/replacements.cpp:14`;
    - `seconds_or_null`, twice: `events/backfill.cpp:21`,
      `events/reactions.cpp:49`;
    - `snowflake_or_null`: `events/replacements.cpp:21`;
    - `to_unix` and `from_unix`: `events/nicknames.cpp:14`.
  - **Casts written at every call site:**
    - 85 `static_cast<std::uint64_t>(…)` on snowflakes being bound,
      across ten store files (21 in `reactions.cpp` alone);
    - 12 inline `time_since_epoch().count()` conversions.
- **Description:** `db::statement` binds integers, text, blobs and optionals,
  but not the two types every store actually has: `dpp::snowflake` and a
  `std::chrono` time. So each store casts at every bind, and wraps each
  optional snowflake or time in a helper of its own. The casts are noise that
  hides the SQL. The helpers are the same few lines written up to three
  times.
- **Evidence:** searched `src/` for the helper names, for the cast, and for
  `time_since_epoch().count()`.
- **Recommendation:** Teach `db::statement` to bind and `get<>`:
  - `dpp::snowflake` and `std::optional<dpp::snowflake>`;
  - `std::chrono::sys_seconds`, and `system_clock::time_point` truncated to
    seconds, stored as Unix seconds as they are today.

  Then delete the helpers and the casts. That couples `db/` to DPP's
  `snowflake.h`, which every other layer already includes. If keeping `db/`
  free of DPP matters, put the overloads in a small `db/discord_types.hpp`
  instead.
- **Effort:** M (mechanical, across ten files).
- **Risk of fix:** Low. The store tests cover every column. The stored
  values do not change: the same integers are written.
- **Related:** DUP-001.

#### DUP-004: The same line-splitting loop is written three times

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `util/env.cpp:60` (`parse_dotenv`),
  `commands/trigger.cpp:69` (`parse_responses`), and
  `events/url_rules.cpp:260` (`parse_legacy_rules`).
- **Description:** Each walks text with the same subtle loop,
  `while (at <= text.size())` with `find('\n')` and an
  `at = … text.size() + 1` sentinel, to visit every line, the last one
  included. Only `parse_dotenv` handles `\r\n` explicitly, through its
  trim, and all three trim the line themselves. It is exactly the kind of
  loop that is easy to get subtly wrong in a fourth copy.
- **Evidence:** read the three functions.
- **Recommendation:** Add `util::lines(std::string_view)`, returning
  `std::vector<std::string_view>` without the `\r`, or a ranges view, to
  `util/text`, with tests for a trailing newline, CRLF and an empty
  input. Use it in all three.
- **Effort:** S.
- **Risk of fix:** Low. Each caller has tests for its format.
- **Related:** DUP-001.

#### DUP-005: Every command file reads its options with its own helpers

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** private helpers in `commands/*.cpp`:

  | Helper | Defined in |
  |---|---|
  | `string_option` | `basic.cpp`, `linkstats.cpp`, `midnight.cpp`, `trigger.cpp`, `urlrepl.cpp` |
  | `subcommand_of` | `bots.cpp`, `midnight.cpp`, `trigger.cpp`, `urlrepl.cpp`, plus `group_and_action` in `linkstats.cpp` |
  | `int_option` | `midnight.cpp`, `trigger.cpp` |
  | `user_option` | `basic.cpp` (returns a snowflake), `nickname.cpp` (returns a name as well) |
  | `invoker_permissions` | `linkstats.cpp`, `urlrepl.cpp` |

  Boolean options are read inline with `std::get_if<bool>` at about half a
  dozen sites (`trigger.cpp`, `linkstats.cpp`, `basic.cpp`'s `/goodbye`),
  besides `message_options.cpp`.
- **Description:** `registry.hpp` now provides `subcommand_path`, which
  covers what the four `subcommand_of` copies do and handles groups too, so
  `group_and_action` is covered as well. The rest are one- to five-line
  helpers repeated verbatim. They are also where option handling would
  change if Discord's payloads changed.
- **Evidence:** searched each helper's definition across `commands/`.
- **Recommendation:** Add `commands/options.hpp` with `string_option`,
  `int_option`, `bool_option` (returning `std::optional<bool>`),
  `snowflake_option` and `invoker_permissions`. Replace `subcommand_of` and
  `group_and_action` with `subcommand_path`. `nickname.cpp`'s richer
  `user_option` can stay local.
- **Effort:** S.
- **Risk of fix:** Low. Each command's tests exercise its options.
- **Related:** DUP-001.

### 2.2 Mistakes and drift (BUG)

#### BUG-001: Two ways the link scanner reads a message differently from Discord

- **Severity:** Low. **Status:** Confirmed for the scanner's behaviour;
  Discord's side is from its documented rendering and was not checked live.
- **Location(s):** `util/url_scan.cpp:107` (`find_links`), `:75`
  (`trim_link`), `:134` (spoiler counting).
- **Description:**
  1. **A suppressed link with punctuation inside the brackets.** For
     `<https://x.com/a/status/1.>`, the pattern stops at `>` and `trim_link`
     then drops the trailing `.`. The link's end then falls on the `.` rather
     than the `>`, so `embed_suppressed` is false. The bot replaces a link
     whose preview its author deliberately turned off. Discord treats
     everything between the brackets as the link, punctuation included.
  2. **`||` inside code counts as a spoiler marker.** Markers are counted
     over the whole text, code spans included. In
     ``use `a || b` then https://x.com/a/status/1``, one marker precedes the
     link, so the replacement is posted spoilered. Discord does not read
     `||` inside code as a spoiler.
- **Evidence:** traced both inputs through `find_links`. The code spans are
  computed (`code_spans`), but used only for `in_code`, never for marker
  counting. `embed_suppressed` compares against the trimmed end.
- **Recommendation:**
  - For 1, decide suppression from the untrimmed match: if the character
    after the raw match is `>` and the one before its start is `<`, the link
    is suppressed and keeps its punctuation.
  - For 2, skip markers that fall inside a code span when counting.
  - Add a scanner test for each. The fuzz target's spoiler invariant
    (`fuzz_url_scan`) checks against `is_inside_spoiler`, which has the same
    blind spot, so it needs the same change.
- **Effort:** S.
- **Risk of fix:** Low. `url_scan_test.cpp` covers the rest of the scanner;
  run the fuzz target for a minute afterwards.
- **Related:** WIRE-001.

#### BUG-002: Trusted IDs in `config.json` accept malformed text

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `config/bootstrap.cpp:39-63` (`require_snowflakes`); the
  strict parser beside it is `parse_snowflake`.
- **Description:** `trusted_guilds` and `trusted_users` are read with
  `std::stoull`. It accepts leading whitespace and a trailing mess, so
  `"12345abc"` becomes `12345`. It also accepts a minus sign, so `"-1"`
  wraps to 18446744073709551615. A mistyped ID therefore loads as some other
  number rather than stopping startup, which is what the rest of the config
  loader does for bad input. The same file gained a strict `parse_snowflake`
  for `LATIBOT_DEBUG_RECOMPUTE_BOT_ID`, and its test pins exactly these
  cases. These lists will gate the host-touching DECtalk commands in phase 4
  (`bootstrap::is_trusted`), so a silent misparse there is worth closing
  before then.
- **Evidence:** read `require_snowflakes`. `parse_snowflake`'s tests in
  `tests/unit/bootstrap_test.cpp` show the stricter behaviour already
  exists.
- **Recommendation:** Use `parse_snowflake` in `require_snowflakes`, keeping
  its error message. Add a config test with `"12345abc"` and `"-1"`.

  Text is turned into a snowflake in four places, each with its own
  parser: `parse_snowflake` and `require_snowflakes` in `bootstrap.cpp`,
  `read_id` in `events/nickname_import.cpp`, and `plan_say` in
  `commands/basic.cpp`. Move `parse_snowflake` to `util/text` and use it in
  all four. DPP's own `dpp::snowflake(std::string_view)`, used on button
  arguments in `bot.cpp`, never throws; it yields 0 for junk, so those
  call sites are safe as they are.
- **Effort:** S.
- **Risk of fix:** A `config.json` that loads today with a malformed ID
  would stop loading. That is the point, but worth a line in the commit
  message.
- **Related:** DUP-001.

#### BUG-003: A restart during a preview watch strands the replacement

- **Severity:** Medium. **Status:** Confirmed that nothing handles it; how
  often it happens is not measured.
- **Location(s):**
  - `events/embed_watch.hpp` (`embed_tracker`, which keeps its watches in
    memory only);
  - `events/url_replacer.cpp:104` (the row is recorded `pending`);
  - `bot.cpp:634` (Retry sets `retrying`);
  - `events/url_replacer.cpp:163-167`: `plan_retry` refuses both states.
- **Description:** A replacement is recorded `pending` when posted, and the
  original's preview is turned off at once. The watch that later marks it
  `ok` or `failed` lives only in `embed_tracker`'s memory. It can run for
  twelve seconds per mirror, so up to about a minute for a four-mirror rule.
  If the bot stops in that window:
  - the row stays `pending` forever;
  - the replacement keeps whichever mirror it was on;
  - the original stays suppressed;
  - no Retry button is ever added.

  If that mirror never embedded, the link has no preview anywhere, and
  nothing in the bot will fix it. Any Retry button pressed on it gets "that
  one's already working". The same applies to a restart during a Retry:
  the row stays `retrying`. Plan §9.4 promises that Retry survives a
  restart, which it does for rows that reached `failed`, but not for these.
- **Evidence:**
  - searched `src/` for every write of `pending` and `retrying`, and for any
    code reading those states at startup (there is none);
  - read `embed_tracker`, which has no persistence;
  - `bot`'s `on_guild_create` and `register_timers` start no sweep.
- **Recommendation:** Add a startup sweep, per guild on `guild_create` or once
  on `ready`. Find `pending` and `retrying` rows younger than a day, fetch
  each message with `discord_gateway::get_message`, and settle it the way
  the tracker would:
  - if it has previews, mark it `ok`;
  - if not, turn the original's preview back on, and edit in the failure
    note with its Retry button.

  The tracker's `finish` already decides this, so the sweep can build a
  `watch_request` and hand `embed_tracker::watch` the embeds it fetched.
  Log how many were settled at `info`.
- **Effort:** M.
- **Risk of fix:** Low for the sweep itself. Test it with the Discord mock:
  a stale `pending` row with no embeds should end `failed`, with a failure
  note and the original's preview back on.
- **Related:** none.

#### BUG-004: Renaming a URL rule in the panel is two transactions

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `bot.cpp` (`on_url_form`): `url_rules_.remove(guild,
  previous)` followed by `url_rules_.set(guild, rule)`.
- **Description:** When the form changes a rule's site, the old rule is
  deleted in one transaction and the new one written in another. If the
  second fails, the rule is gone rather than renamed, and the panel then
  re-renders without it. That takes a database error, hence Low, but the
  store already runs `set` in a transaction, so making the rename atomic
  costs almost nothing. Nearby, the trigger form ignores
  `trigger_store::update`'s result and logs "updated" even when the trigger
  was deleted from another client in the meantime. That one is harmless:
  the panel re-renders without it. The trigger panel's delete also logs
  "removed" before calling `remove`, and whether or not a row went. The
  URL panel logs only when something was removed.
- **Evidence:** read `on_url_form` and `url_rule_store::set`.
- **Recommendation:** Add
  `url_rule_store::rename(guild, previous, rule)`, doing both in one
  transaction, and call it from `on_url_form`. Have the trigger form log
  "gone before it could be saved" when `update` returns false. Log the
  trigger panel's delete only when `remove` returns true.
- **Effort:** S.
- **Risk of fix:** Low. Add a store test for `rename`.
- **Related:** none.

### 2.3 Hanging, unwired and partial features (WIRE)

#### WIRE-001: `util::is_inside_spoiler` is only used by tests

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `util/text.hpp:21`, `util/text.cpp:35`. Callers:
  `tests/unit/text_test.cpp`, `tests/fuzz/fuzz_text.cpp`,
  `tests/fuzz/fuzz_url_scan.cpp`.
- **Description:** Written in phase 0 for the scanner, which now counts
  markers incrementally with `count_occurrences` instead (`url_scan.cpp:134`,
  so a message with many links stays linear). Nothing in `src/` calls it.
  It survives as the oracle `fuzz_url_scan` checks the scanner against,
  which is a legitimate job, but a test oracle does not belong in the
  library's public header. It also shares BUG-001's blind spot for code
  spans.
- **Evidence:** a search for `is_inside_spoiler` across `src/` and `tests/`.
- **Recommendation:** Move it into the fuzz target and `text_test.cpp` as a
  local reference implementation, or keep it and say in its comment that it
  is the reference the scanner is checked against. Either way, fix it with
  BUG-001.
- **Effort:** S.
- **Risk of fix:** None.
- **Related:** BUG-001.

#### WIRE-002: Most of `guild_settings` is unused outside its tests

- **Severity:** Low. **Status:** Confirmed; whether it is wanted is a
  decision.
- **Location(s):** `config/guild_settings.hpp:34-47`: `get_int`,
  `get_real`, `set_int`, `set_real`, `erase` and `all`.
- **Description:** Production code uses `get`, `find`, `set`, `get_bool` and
  `set_bool` (the goodbye phrase, the URL rules import flag, the URL
  replacement switch). The other six methods are only called by
  `tests/db/guild_settings_test.cpp`. `all()` says it is "for a settings
  panel to display", and the plan's `/llm settings` (phase 5) is the obvious
  user of the numeric getters. So this is scaffolding rather than dead code,
  but nothing records that.
- **Evidence:** a search for each method name across `src/`, excluding
  `guild_settings.cpp`.
- **Recommendation:** Keep them, and note in the class comment that the
  numeric accessors, `erase` and `all` wait for `/llm settings`. Fix
  CONC-001 now either way.
- **Effort:** S.
- **Risk of fix:** None.
- **Related:** CONC-001.

#### WIRE-003: Scaffolding for phases 4 and 5 that nothing reaches yet

- **Severity:** Low (informational). **Status:** Confirmed.
- **Description:** Built in phase 0 or alongside other work, ahead of the
  features that will use it. All of it is planned, most of it is tested, and
  none of it is reachable from `main` today:

  | What | Where | For |
  |---|---|---|
  | `ports::http_client`, `discord::dpp_http_client`, `tests/mocks/mock_http.hpp` | `ports/http_client.hpp`, `discord/dpp_http_client.*`; constructed as `bot::http_` | the LLM providers (plan §14) |
  | `ports::tts_engine`, `tests/mocks/mock_tts.hpp` | `ports/tts_engine.hpp` | DECtalk (plan §12) |
  | `discord::raw_api` (`request`, `multipart`) | `discord/raw_api.*`; constructed as `bot::raw_` | endpoints DPP lacks, such as voice messages (plan §5.5, §12.8) |
  | `discord_gateway::delete_message` | `ports/discord_gateway.hpp`, `discord/dpp_gateway.cpp:36` | no feature uses it yet |
  | `bootstrap::is_trusted`, `trusted_guilds`, `trusted_users` | `config/bootstrap.*` | the host-touching DECtalk commands (plan §12.5) |
  | `llm_provider`, `llm_model`, the spend caps, `llm_tool_rounds` | `config/bootstrap.*` | the LLM (plan §14) |
  | numeric `guild_settings` accessors | see WIRE-002 | `/llm settings` |
  | command aliases (`command_info::aliases`, one payload per alias) | `commands/registry.*` | no command declares one. The Java bot's only aliases were `np` and `q`, for the music commands (unscheduled, plan §15) |

- **Evidence:** searched for each name across `src/`. The only references
  are declarations, the `bot` members that construct them, and tests.
- **Recommendation:** Keep all of it. Nothing to do beyond CONF-001, which
  documents the config keys as not yet used.
  `discord_gateway::delete_message` is the one item with no planned user;
  keep it, since removing a port method just to add it back is churn.
- **Effort:** none.
- **Related:** WIRE-002, WIRE-004, CONF-001.

#### WIRE-004: `bot`'s eight public accessors are never called

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `bot.hpp:41-48`: `commands()`, `database()`,
  `guild_settings()`, `gateway()`, `http()`, `raw()`, `clock()`,
  `settings()`.
- **Description:** `main` constructs a `bot` and calls `run()`, and nothing
  else holds one; the tests never build a `bot`. The accessors hand out
  mutable references to the shell's internals, the database included, with
  no user. They widen the one class the project deliberately keeps thin and
  untested (plan §17.3; `docs/testing/README.md`, *Known gaps*).
- **Evidence:** searched `src/` and `tests/` for each accessor.
- **Recommendation:** Remove them. If a future feature needs one of these
  objects, pass it in the way the commands and stages already receive
  theirs.
- **Effort:** S.
- **Risk of fix:** None: a build proves nothing used them.
- **Related:** WIRE-003.

#### WIRE-005: Small functions only tests call

- **Severity:** Low. **Status:** Confirmed.
- **Location(s)** and what each is:

  | Function | Where | Called from |
  |---|---|---|
  | `trigger_responder::forget_cooldowns` | `events/triggers.cpp:304` | nothing, not even a test |
  | `nickname_store::members` | `events/nicknames.cpp:301` | tests only |
  | `nickname_store::count` | `events/nicknames.cpp:314` | `tests/db/nickname_store_test.cpp` |
  | `pending_nicknames::size` | `events/nicknames.cpp:197` | tests only |

- **Description:** These read like features, but nothing in the bot uses
  them. `count` and `size` are reasonable test observers. `members` and
  `forget_cooldowns` have no user at all.
- **Evidence:** searched `src/` and `tests/` for each name.
- **Recommendation:**
  - Remove `forget_cooldowns`. With CONC-002 fixed, clearing the map would
    also need the lock, so it is better gone.
  - Remove `members` unless a planned feature needs it (none is named in
    the plan).
  - Keep `count` and `size`, noting in their comments that they exist for
    tests.
- **Effort:** S.
- **Risk of fix:** None; the build proves it.
- **Related:** CONC-002, WIRE-003.

### 2.4 Simplification (SIMP)

#### SIMP-001: Two hot paths run a query per item

- **Severity:** Low. **Status:** Confirmed in the code; not measured.
- **Location(s):**
  - `events/triggers.cpp:123` (`trigger_store::for_guild`), called for
    every message by `trigger_responder::operator()`;
  - `events/reactions.cpp:486` (`reaction_store::known_emojis`), called on every
    keystroke of `/linkstats`'s emoji autocomplete, and by
    `likely_duplicates`.
- **Description:**
  - **Triggers:** every message in a guild loads all its triggers, then runs
    one more query per trigger for its responses. That is 1 + N statements
    on the event thread, under the database lock, before the first pattern
    is tested.
  - **Emoji autocomplete:** the autocomplete groups every reaction in the
    guild, then calls `describe` once per emoji until it has 25 matching
    names. With a filter that matches little, that is one query for every
    emoji the guild has ever used, on each keystroke, inside the
    three-second autocomplete window.

  At today's scale, a handful of triggers and some dozens of emojis, both
  are fast. Both grow with a guild's history.
- **Evidence:** read both functions and their callers.
- **Recommendation:**
  - **Triggers:** fetch the responses for all of a guild's triggers in one
    query (`WHERE trigger_id IN (SELECT id FROM triggers WHERE guild_id =
    ?)`) and group them in C++. Only when that is not enough, cache per
    guild, invalidated on write.
  - **Emojis:** `LEFT JOIN emojis` in the grouping query, so the names come
    back with the counts. Filter by name in SQL with `LIKE`, since
    autocomplete filters case-insensitively.
- **Effort:** S each.
- **Risk of fix:** Low. The store tests cover both results.
- **Related:** none.

### 2.5 Porting fidelity (PORT)

Every Java command and listener was mapped to its C++ counterpart or to
the plan section that covers it; the table is in §7.2. Every difference
below was checked against the plan, its *As built* notes and the initial
analysis. None of these three is recorded there. The plans called the
basic commands "direct ports, no design questions" (v1 to v4, §6). Each
looks like drift rather than a decision, but is the owner's call.

#### PORT-001: Replies that were public in the Java bot are private now

- **Severity:** Low. **Status:** Confirmed; whether it is wanted is a
  decision.
- **Location(s):** `commands/basic.cpp` (`/join`, `/leave`, `/shutdown`),
  `commands/urlrepl.cpp` (`/urlrepl set` and `remove`, `/urltoggle`). All
  take the default `result = dpp::m_ephemeral`.
- **Description:** In the Java bot:
  - `/join` answered publicly and named the person it followed ("ok joining
    Alice", "ok moving to Alice"); now it answers "ok joining" to the caller
    only.
  - `/leave` ("ok bye") and `/shutdown` ("ok bye bye!") were public.
  - `/replaceurl`'s confirmations ("Urls with 'x.com' will now be replaced
    with …") and `/toggle`'s ("URL replacement disabled for @user") were
    public.

  All of these are ephemeral now. For the voice commands, the change hides
  from the room why the bot just walked in or out. The feature docs present
  "ephemeral by default" as the rule, but no plan section decided it for
  these commands. With per-kind flags (plan §21.15), changing any of them
  back is one line in the constructor.
- **Evidence:** read `JoinVoiceCmd.java`, `LeaveVoiceCmd.java`,
  `ShutdownCmd.java`, `ReplaceUrlCmd.java` and `ToggleReplaceCmd.java`
  (no `setEphemeral` on these replies). Compared with the C++ constructors,
  and with plan §6 and §9.5.
- **Recommendation:** Decide per command (see *Decisions needed*). The
  likely candidates for public are `/join`, `/leave` and `/shutdown`, since
  the room sees the effect anyway. If `/join` goes public, put the followed
  person's name back in its reply. Either way, record the decision in plan
  §6.
- **Effort:** S.
- **Risk of fix:** None.
- **Related:** PORT-002.

#### PORT-002: `/say` needs Manage Messages, where the Java bot asked for Manage Roles

- **Severity:** Low. **Status:** Confirmed; whether it is wanted is a
  decision.
- **Location(s):** `commands/basic.cpp:180` (`say_command`,
  `default_member_permissions = p_manage_messages`); Java
  `SayCmd.java:42` (`Permission.MANAGE_ROLES`).
- **Description:** The default permission for speaking as the bot changed,
  and nothing records why. In most servers the two permissions go to
  different roles, so the set of people who can use `/say` changed on the
  first deploy. Manage Messages is arguably the better fit (it is about
  messages), but that is a decision to make on purpose.
- **Evidence:** compared the two declarations. Plan §6 and the feature docs
  state the new default without mentioning the old one.
- **Recommendation:** Keep Manage Messages, or restore Manage Roles, and
  note which in plan §6. Admins can override either way in the server's
  Integrations settings.
- **Effort:** S.
- **Risk of fix:** None.
- **Related:** PORT-001.

#### PORT-003: The bot starts with no status, and forgets `/status` on restart

- **Severity:** Low. **Status:** Confirmed; whether it is wanted is a
  decision.
- **Location(s):** `commands/basic.cpp:271` (`status_command` sets the
  presence and stores nothing); `bot.cpp` (`on_ready` sets no presence).
  Java: `LatiBot.java:68` (`setActivity(Activity.customStatus(…))` at
  every start).
- **Description:** The Java bot always came up with the same custom status.
  The port starts with none. `/status` sets one until the next restart and
  then it is gone, so a restart silently clears whatever the owner chose.
  The feature docs do not say the status is temporary.
- **Evidence:** searched `src/` for `set_presence`; the only call is the
  command's. Read `LatiBot.java`'s builder.
- **Recommendation:** Store the last `/status` text and type, in
  `guild_settings` under guild 0 or in a small `bot_settings` table, and
  apply it in `on_ready`. Or add a `startup_status` key to `config.json`.
  Otherwise, say in the `/status` docs that it lasts until the next
  restart.
- **Effort:** S.
- **Risk of fix:** Low.
- **Related:** CONF-001.

### 2.6 Discord-specific correctness (DISC)

#### DISC-001: Deleting from a panel's first or last page sends two buttons with the same `custom_id`

- **Severity:** High: a panel action fails in a normal path. **Status:**
  Confirmed in the code. That Discord rejects the reply is from its API
  rules (custom IDs must be unique within a message) and was not checked
  live.
- **Location(s):**
  - `commands/trigger.cpp:425` (`selection_row`'s Cancel) and `:524`
    (`footer_row`'s paging);
  - `commands/urlrepl.cpp:269` and `:299`, the same pair;
  - `ui/paginator.cpp`, `controls`.
- **Description:** Pressing **Delete** in the trigger or URL rule panel
  re-renders it with a confirmation row. That row's **Cancel** button is
  encoded as `{view = panel, page = current, argument = ""}`: `trigpanel:0:`
  or `urlpanel:0:` on the first page. When the list has more than one page,
  the footer also carries the ◀ and ▶ buttons, encoded by `ui::controls` with
  the same view and argument.
  - On the first page, ◀ is clamped to page 0, so it is `panel:0:`.
  - On the last page, ▶ is clamped to the last page, so it is
    `panel:<last>:`.

  Either way, one message holds two components with the same `custom_id`.
  Discord refuses the update, so pressing Delete appears to do nothing and
  the client reports that the interaction failed. With exactly two pages,
  every page is the first or the last. The URL panel reaches two pages at
  six rules and the trigger panel at nine. `/urlrepl remove` and
  `/trigger remove` still work.
- **Evidence:** traced `render_trigger_panel(…, confirming_delete=true)` and
  `render_url_panel(…, true)` on page 0 with more than one page of items.
  The panel tests' `check_components_fit`
  (`tests/unit/urlrepl_command_test.cpp:47`) checks lengths and counts but
  not uniqueness, and no test renders a confirmation on a multi-page panel.
- **Recommendation:**
  - Give Cancel an ID nothing else can produce. For example, carry the
    selected item as the argument (`urlpanel:0:x.com`); the router ignores
    the argument for the panel view today, and could reselect the item
    instead, which is nicer anyway.
  - Add a uniqueness check to the component-limit helper. Given DUP-002,
    make it one shared helper, and run it over every renderer's output,
    confirmation states included.
- **Effort:** S.
- **Risk of fix:** Low. Re-run the panel tests. Try Delete then Cancel on
  page 1 of a two-page URL panel in Discord.
- **Related:** DUP-002.

#### DISC-002: A trigger pattern over 100 characters breaks the trigger panel for the whole server

- **Severity:** Medium. **Status:** Confirmed in the code. The API rejection
  is from Discord's documented limit (select option labels are 100
  characters at most); not checked live.
- **Location(s):**
  - `commands/trigger.cpp:400` (`pick_menu` uses `entry.pattern` as the
    option label, unshortened);
  - `:167` and `:180` (`/trigger add` and `edit` accept `pattern` up to 200);
  - `:584` (the panel's form sets no maximum at all).
- **Description:** A pattern of 101 to 200 characters passes `/trigger add`.
  From then on, every render of the trigger panel whose page includes that
  trigger carries an over-long option label. Discord rejects the message,
  so `/trigger panel` fails, and so does paging onto that page. The panel
  form allows a pattern of any length, so it can create the same state.
  Nothing tells the admin why. `/trigger list` still works, and
  `/trigger edit` or `/trigger remove` can repair it, but only if they
  guess the cause. The URL panel does not have this problem: its label is
  the domain, which is capped at 100, and it shortens the description.
- **Evidence:** read `pick_menu`, the option definitions and `trigger_form`.
  The panel test uses short patterns. DUP-002's missing shared checker
  would have caught it on one long-pattern case.
- **Recommendation:**
  - Shorten the label when building the menu (the pattern is shown in full
    in the lines above it). Shorten by characters, not bytes, so a
    multi-byte character is not split. A small
    `util::truncate(std::string_view, std::size_t chars)` would also serve
    `urlrepl.cpp`'s `pick_menu` description, which today uses
    `resize(97)` on bytes.
  - Add `set_max_length(200)` to the form's pattern field, matching the
    command.
  - Add a panel test with a 200-character pattern, run through DUP-002's
    checker.
- **Effort:** S.
- **Risk of fix:** None.
- **Related:** DUP-002, DISC-001.

#### DISC-003: A long `domain` filter silently removes a leaderboard's paging

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `commands/linkstats.cpp` (`encode_board`, and
  `render_board`'s call to `ui::controls`); `ui/paginator.cpp`
  (`controls` returns nothing when `encode` refuses).
- **Description:** A board's filters ride in its buttons' `custom_id` as
  `linkboard:<page>:<board>;<emoji>;<domain>;<since>;<until>`, which is
  capped at 100 characters. The `domain` option accepts up to 100
  characters, and an emoji key can be around 30 bytes. With a domain longer
  than about 40 characters, `encode` refuses, `controls` returns
  `std::nullopt`, and the board shows its first ten rows with no ◀ / ▶ and no
  explanation. Domains are almost always short, hence Low.
- **Evidence:** added up the encoded fields' maximum sizes, and traced
  `controls`' early return.
- **Recommendation:** When the state will not fit, say so rather than drop
  it: add "_filters too long to page; narrow them to see more_" to the
  board. Or cap `domain` at 40 characters in the option, which still covers
  real sites.
- **Effort:** S.
- **Risk of fix:** None.
- **Related:** DUP-002.

#### DISC-004: Two commands wait on Discord before their first reply

- **Severity:** Low. **Status:** Suspected: whether the three-second
  window is ever missed in practice was not measured.
- **Location(s):**
  - `commands/nickname.cpp` (`nickname_command::execute` awaits
    `co_guild_edit_member` before replying);
  - `commands/basic.cpp` (`/say` with `reply:` awaits `co_message_get`
    before replying).
- **Description:** Discord gives an interaction three seconds for its first
  response. Both commands make a REST call first, and DPP queues REST calls
  behind its rate limiter. A busy moment, or a route that is being rate
  limited, can push the reply past the window. The user then sees "The
  application did not respond" even though the nickname was changed or the
  message sent. Every other command replies from local data.
  `/linkstats`'s boards query SQLite first, which is local and fast.
- **Evidence:** read both handlers. No command in `src/` uses
  `co_thinking`.
- **Recommendation:** Defer both. That means `co_thinking` with the
  result's ephemeral bit (`responses_for(event).result & dpp::m_ephemeral`),
  then `co_edit_original_response` with the outcome. A `command::defer`
  helper beside `result` and `refusal` would keep the flag decision in
  `command_info`. It would also let `registry::dispatch`'s failure reply
  know to edit rather than follow up (plan §21.15).
- **Effort:** S.
- **Risk of fix:** Low. Ephemerality is fixed at defer time, which the
  helper handles.
- **Related:** ERR-002.

### 2.7 Lifetime and async safety (LIFE)

#### LIFE-001: The goodbye thread can outlive the bot during shutdown

- **Severity:** Low. **Status:** Suspected: it depends on DPP's
  shutdown ordering, which was not traced end to end.
- **Location(s):** `bot.cpp:904` (`carry_out`'s `stop_bot` branch), which starts
  a detached `std::thread` that captures `this`, sleeps, then calls
  `cluster_.shutdown()`.
- **Description:** `cluster_.shutdown()` makes `cluster_.start(st_wait)`
  return in `bot::run`. `main` then returns, and the `bot`, `cluster_`
  included, is destroyed. The detached thread may still be inside
  `shutdown()`, or returning from it, while `~bot` runs. At worst this is a
  crash on the way out, after the goodbye was delivered, so the user never
  sees it. It is still undefined behaviour, and the kind that turns into a
  confusing crash report. `/shutdown` avoids it by calling
  `request_shutdown_` from its coroutine.
- **Evidence:** read the `stop_bot` branch, `bot::run` and `main`.
- **Recommendation:** Schedule the delayed shutdown on DPP rather than on a
  raw thread. Use a one-shot `cluster_.start_timer` that stops itself and
  calls `shutdown()`, as `attribute_later` already does. Alternatively,
  keep a `std::jthread` member and join it in `~bot`.
- **Effort:** S.
- **Risk of fix:** Low. Check that the goodbye message still arrives before
  the process ends.
- **Related:** none.

### 2.8 Concurrency (CONC)

**The database's locking policy**, reconstructed from `db/database.hpp`,
since nothing states it in one place:
- There is one SQLite connection, opened `SQLITE_OPEN_FULLMUTEX`, and
  guarded by a `std::recursive_mutex`.
- Every `statement` holds that mutex from `prepare` until it is destroyed,
  so a single statement is always atomic.
- A store method that runs **more than one** operation and needs them to
  agree must take `db_->lock()` or a `db::transaction` first. That covers a
  write followed by `changes()` or `last_insert_rowid()`, a read-then-write,
  and several statements that must be consistent. Otherwise another thread's
  statement can land in between, and `changes()` would then report *that*
  statement.

Every store method matching that description was checked. All of them hold
the lock except the one below.

#### CONC-001: `guild_settings::erase` reads `changes()` without the lock

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `config/guild_settings.cpp:105` (`guild_settings::erase`).
- **Description:** `erase` runs the `DELETE` as a temporary statement, which
  releases the lock at the end of the line, then calls `db_->changes()`.
  Another thread's write in between makes the return value describe the
  wrong statement. Nothing in `src/` calls `erase` today, only
  `guild_settings_test.cpp`, so there is no live bug. It is the one store
  method that breaks the policy above, and it is part of an API meant for
  later use (WIRE-002).
- **Evidence:** every `changes()` and `last_insert_rowid()` call in `src/`
  was checked against its enclosing function. All others take `lock()` or a
  `transaction`.
- **Recommendation:** Add `const auto guard = db_->lock();` as the first
  line, as every other store does. Also state the policy in `database.hpp`'s
  class comment, so the next store gets it right.
- **Effort:** S.
- **Risk of fix:** None.
- **Related:** WIRE-002.

#### CONC-002: The trigger responder's cooldowns are unguarded shared state

- **Severity:** High. **Status:** Confirmed in the code. The crash itself
  was not reproduced.
- **Location(s):** `events/triggers.cpp:256-302`
  (`trigger_responder::operator()`); the state is `last_fired_` and `roll_`
  in `events/triggers.hpp:150-152`.
- **Description:** DPP 10.1.6 hands every gateway event to a thread pool of
  at least four threads (`cluster.h:295`, and `queue_work` in
  `events/message_create.cpp:41`), so two messages are routinely processed
  at once. For each message, the responder reads
  `last_fired_.find(key)`, then writes `last_fired_[key] = now` into a
  `std::map`, and calls the shared `std::mt19937_64` in `roll_`. None of
  this is locked.
  - **Concurrent inserts corrupt the map**, which is undefined behaviour and
    can crash the process.
  - **The cooldown can be bypassed.** Check-then-set is not atomic, so two
    messages can both see the trigger as off cooldown and both get a reply.

  The trigger most likely to hit this is the one it was written for: at
  4:20, several people post "420" in the same channel within moments. The
  other stateful classes each guard their state with a mutex:
  `embed_tracker`, `pending_nicknames` and `backfill_service`. This one is
  the exception.
- **Evidence:**
  - read `trigger_responder` and its members;
  - searched `src/` for every mutex;
  - read DPP's dispatch in `third_party/DPP/src/dpp/events/message_create.cpp`
    and the pool size in `cluster.h`;
  - `bot.cpp` calls the responder from `on_message_create` through the
    pipeline (`register_stages`), with no lock around it.
- **Recommendation:** Give `trigger_responder` a `std::mutex`. Hold it
  around the cooldown check-and-set and the roll, so each trigger's decision
  is atomic. Do the store read before taking the lock, to keep SQLite out of
  it. Add a `[threads]` test that fires the same trigger from several
  threads and expects exactly one reply per cooldown.
- **Effort:** S.
- **Risk of fix:** Low. The existing trigger tests cover the behaviour, and
  the new test covers the race.
- **Related:** none.

### 2.9 Error handling (ERR)

#### ERR-001: One database error stops the midnight posts, or all replacement tracking, until a restart

- **Severity:** Medium. **Status:** Confirmed in the code, and in DPP's
  source.
- **Location(s):** `bot.cpp:410` (midnight timer) and `:418` (embed tracker
  timer), in `register_timers`. DPP: `third_party/DPP/src/dpp/cluster/timer.cpp:57-100`
  (`tick_timers`) and `socketengine.cpp:114` (its `try`).
- **Description:** DPP runs timer callbacks on its socket engine thread.
  `tick_timers` pops a timer off its queue, calls it, and only then pushes
  it back for the next tick. The `try`/`catch` is around the whole of
  `tick_timers`, in `socket_engine_base::prune`. So a callback that throws
  is never re-queued: **a repeating timer that throws once is gone for the
  life of the process**, with one `[dpp]` error line in the log.
  - The midnight timer calls `midnight_scheduler::tick`, which reads and
    writes SQLite (`enabled()`, `mark_fired`).
  - The embed tracker's one-second timer calls `embed_tracker::tick`, whose
    `finish` writes replacement states.

  Neither callback has a `try`. A single `db_error` (a full disk, an I/O
  error, a locked file during a backup copy elsewhere) therefore ends
  midnight messages, or leaves every later replacement `pending` for ever
  (BUG-003's symptom, for all of them), until someone restarts the bot. The
  backup timer in the same function does wrap its work in `try`/`catch`,
  so the risk was known there but not carried over.
- **Evidence:** read the three timer callbacks, and DPP's `tick_timers` and
  `prune`.
- **Recommendation:** Wrap both callbacks as the backup timer is wrapped:
  catch `std::exception`, log at `error` with what was being done
  ("midnight tick failed: …", "embed tracker tick failed: …"), and carry
  on. A small `guarded(name, fn)` helper in `bot.cpp` would cover all three
  and `attribute_later`'s timer.
- **Effort:** S.
- **Risk of fix:** None.
- **Related:** BUG-003, ERR-002.

#### ERR-002: A failing button, menu or form gets no answer

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `bot.cpp` (`on_component`, `on_trigger_component`,
  `on_url_component`, `on_form`, `on_url_form`), registered at `bot.cpp:398-402`.
- **Description:** Since the last change, a slash command that throws gets
  a reply (`registry::dispatch`). Buttons, select menus and forms do not.
  Their handlers call the stores directly, and an exception escapes to
  DPP's thread pool. The pool catches it and logs "Uncaught exception in
  thread pool" at *warning* (`thread_pool.cpp:51`), which reaches our log
  as a `[dpp]` warning without saying which panel or button. The person
  who pressed it sees Discord's "This interaction failed". The same
  silence applies to a button whose view nobody routes, for example one
  left from an older build: `on_component` logs it at `debug` and sends
  nothing.
- **Evidence:** read the handlers and DPP's `thread_pool.cpp`.
- **Recommendation:** Wrap `on_component` and `on_form` the way `dispatch`
  wraps commands. Log at `error` with the view name and the person, and
  answer ephemerally with `commands::command_failed_reply`. For an
  unrouted view, answer "that button is from an older version of me; run
  the command again". It is cheap, and it turns a silent failure into an
  instruction.
- **Effort:** S.
- **Risk of fix:** Low. Replying twice is the one thing to avoid, so answer
  only from the `catch` or the unrouted branch.
- **Related:** ERR-001.

### 2.10 Consistency and conventions (CONV)

#### CONV-001: Three small naming inconsistencies

- **Severity:** Low. **Status:** Confirmed.
- **Location(s)** and what each is:
  - `ports/result.hpp`: `result<T>` and `api_error` are in namespace
    `latibot`, while everything else in `ports/` is in `latibot::ports`.
    Callers write `result<…>` next to `ports::discord_gateway`, which reads
    as if they came from different places.
  - Panel view names mix three styles: abbreviated prefixes (`trigpanel`,
    `trigpick`, `urlpanel`), whole words (`triggers`, `linkboard`), and a
    nickname (`nicks`). They are persisted in `custom_id`s already sent, so
    this is only about new ones.
  - `/linkstats`'s date refusal says "isn't a date **I** can read"
    (`commands/linkstats.cpp:83`, `:90`). Every other refusal in
    `commands/` uses the lowercase "i" voice ("i don't know that
    subcommand").
- **Evidence:** read each against its neighbours.
- **Recommendation:**
  - Move `result` and `api_error` into `latibot::ports`, a mechanical
    rename the build checks.
  - Write the view-name convention down (short prefix per panel, then the
    action: `urlpanel`, `urledit`), and follow it for new panels. Leave the
    existing ones, since renaming them breaks panels that are already open.
  - Change the two "I"s.
- **Effort:** S.
- **Risk of fix:** None beyond the rename.
- **Related:** none.

### 2.11 Headers and build (BUILD)

#### BUILD-001: A clang-tidy check is still off for a crash in a version no longer used

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `.clang-tidy`, which disables
  `bugprone-implicit-widening-of-multiplication-result` with the note
  "crashes clang-tidy 19.1.5 on DPP's cache.h";
  `tools/Common.ps1:42` (`Get-LlvmTool`, `MinimumMajor = 20`).
- **Description:** The tools now refuse any clang-tidy older than 20,
  because MSVC 14.51's headers reject older Clang with STL1000. The copy
  that ships with Visual Studio here is 22.1.3. Run on its own over all 45
  source files, `bot.cpp` and its `cache.h` include among them, the check
  completes without crashing and reports nothing. So the reason for
  disabling it no longer applies, and the note names a version the project
  cannot use.
- **Evidence:** `clang-tidy --version` reports 22.1.3. Ran
  `--config="{Checks: '-*,bugprone-implicit-widening-of-multiplication-result'}"`
  over `src/**/*.cpp` against `build-tidy`: exit 0, no diagnostics.
- **Recommendation:** Remove the `-bugprone-implicit-widening-…` line and
  its note. It costs nothing today and guards the arithmetic the audio code
  in phase 4 will be full of.
- **Effort:** S.
- **Risk of fix:** None; it is clean now.
- **Related:** TEST-001.

#### BUILD-002: CI enforces less than the project's definition of done

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `.github/workflows/ci.yml`.
- **Description:** CI does four things:
  - checks the test catalog is in sync;
  - installs Debug dependencies;
  - builds Debug and runs `ctest --preset debug`;
  - runs gitleaks.

  Every phase in the plan is declared done when the tests pass in Debug,
  Release *and* ASan, clang-tidy is clean and the formatting is checked
  (plan §0; `docs/testing/README.md`, *Tests are part of done*). Only the
  first of those is enforced. Formatting is the cheapest to add, since
  `Invoke-ClangFormat.ps1 -Check` exists and the VS Code task *Format:
  check only* already runs it locally. A Release build would catch
  `NDEBUG`-only differences, for example `default_log_level`, and code under
  `#ifdef NDEBUG`.
- **Evidence:** read `ci.yml`. Compared with the checks listed in plan §0
  and the testing README.
- **Recommendation:**
  - Add a formatting step: `pwsh tools/Invoke-ClangFormat.ps1 -Check`. It
    needs the VC LLVM component, which the Windows image carries.
  - Add a Release configuration, as a matrix entry with its own
    `conan install -s build_type=Release`.
  - Keep ASan and clang-tidy local unless CI time allows, and say so in the
    testing README's tooling table, so "done" means the same thing in both
    places.
- **Effort:** S.
- **Risk of fix:** CI time. The Conan cache key already covers the
  dependencies.
- **Related:** BUILD-001.

### 2.12 Configuration, secrets and security (CONF)

#### CONF-001: `config.json` has no complete reference or example

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `config/bootstrap.cpp:74-78` (`reject_unknown_keys`, 13
  keys); `README.md` and `docs/features/README.md`.
- **Description:** The loader accepts 13 keys and rejects any other.
  - **Documented:** `log_level`, `track_nicknames`, and the three backup
    keys, each in passing where its feature is described.
  - **Not documented anywhere a user would look:** `database_path`,
    `trusted_guilds`, `trusted_users`, `llm_provider`, `llm_model`,
    `spend_cap_daily_usd`, `spend_cap_monthly_usd` and `llm_tool_rounds`.

  There is a `.env.example` for the environment, but no
  `config.example.json`. A rejected key produces an "unknown config key"
  error, but nothing lists the known ones, their defaults, or that the LLM
  keys do nothing until phase 5.
- **Evidence:** searched `README.md` and `docs/` for each of the 13 keys.
  No `config.json` or example file is tracked.
- **Recommendation:** Add a *Configuration* table to `README.md`, or a
  commented `config.example.json`, listing every key with its type, default
  and effect. Mark `trusted_*` and the LLM keys as read but not yet used.
- **Effort:** S.
- **Risk of fix:** None.
- **Related:** WIRE-002.

### 2.13 Stale documentation and comments (DOC)

#### DOC-001: 146 code comments cite "plan v4", and one of them points nowhere

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** 146 comments across `src/`, 34 distinct sections. The
  broken one is `config/bootstrap.cpp:222` ("plan v4 §2.8").
- **Description:** The maintained plan is `Porting_Plan_Final.md`; v4 is a
  superseded draft. Every cited section was compared by heading. For §5 to
  §20, the Final plan kept v4's numbering, so all of those still lead to
  the right place. Three do not:
  - **§2.5** (`events/nicknames.hpp:135`) and **§21.2** (`bot.cpp:283`)
    exist only in the Final plan, so the "v4" label is wrong but the
    section is right.
  - **§2.8** exists in neither plan. The comment is about Administrator
    counting only in a trusted server, which is the Final plan's §2.4,
    "Administrator is per server".

  A reader following "plan v4" opens the draft, which has no §2.5, §2.8 or
  §21.
- **Evidence:** a tally of every `plan v4 §x` citation. Each cited heading
  was looked up in `Porting_Plan_v4.md` and `Porting_Plan_Final.md`.
- **Recommendation:** In the comment pass (guidance §8.1), replace
  "plan v4 §" with "plan §" everywhere, now that every number but one is
  confirmed to match the Final plan, and correct §2.8 to §2.4. Say in
  `README.md` or the plan's preface that "plan §x" means
  `Porting_Plan_Final.md`.
- **Effort:** S (mechanical).
- **Risk of fix:** None; comments only.
- **Related:** DOC-002.

#### DOC-002: The layout and scope descriptions have fallen behind the tree

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `docs/porting/Porting_Plan_Final.md` §4;
  `docs/testing/README.md` (layout block, component tag table, *Live
  tests*); `README.md` (layout: "mocks, fixtures and fuzz targets");
  `tools/Update-TestCatalog.ps1:29` (the `[events]` description).
- **Description:**
  - **Plan §4:**
    - it omits `discord/message_flags.*`, `commands/message_options.*` and
      `ports/result.hpp`;
    - it lists `data/import/`, which nothing reads (imports come from
      `data/` itself);
    - it marks `ui/panel.*` and `modal_forms.*` "(when a second panel
      exists)". The second panel exists, and §21.5 in the same document
      says a third.
  - **`tests/live/` and `tests/fixtures/`** appear in the plan's tree, the
    testing README's layout and the README. Neither directory exists, no
    `[live]` test exists, and nothing reads `LATIBOT_TEST_TOKEN`. The
    testing README describes live tests in the present tense, as if they
    ran.
  - **`[events]`** is described as "the message pipeline, goodbye,
    triggers" in the testing README, and as "Message pipeline and
    triggers" in the catalog script, which prints it as the catalog's
    heading. It now also covers URL replacement, reactions, nicknames,
    midnight and the backfill.
  - **`SSL_CERT_DIR`** is honoured by `use_system_certificates` but
    documented nowhere.
- **Evidence:** compared each listing with the tree, and searched `src/`
  for `LATIBOT_TEST_TOKEN`.
- **Recommendation:**
  - Update plan §4. Mark `tests/live/` and `tests/fixtures/` as planned,
    and change the panel note to "third", to match §21.5.
  - Change the *Live tests* paragraph to the future tense: "will be tagged
    `[live]`…".
  - Widen the `[events]` description in both places.
  - Mention `SSL_CERT_DIR` next to `SSL_CERT_FILE` in `.env.example`.
- **Effort:** S.
- **Risk of fix:** None. Regenerate the catalog after changing the script.
- **Related:** DOC-001, CONF-001.

#### DOC-003: Code comments that no longer match their code

- **Severity:** Low. **Status:** Confirmed. These are fixed in the comment
  pass (§6) rather than in a separate change.
- **Location(s)** and what is wrong:
  - `bot.hpp` (`register_timers`' comment): "the two things that happen on
    a clock", midnight and backups. There are three: the embed tracker's
    one-second tick too.
  - `ui/paginator.hpp`:
    - `page_state::view`'s example names "nicknames", but the view is
      `nicks`;
    - `controls` "returns an empty component" when it returns
      `std::nullopt`.
  - `db/database.cpp` (`transaction::~transaction`): "once the logger
    exists…" (LOG-001).
  - `.clang-tidy`: the clang-tidy 19.1.5 note (BUILD-001). That is outside
    `src/`, so it is fixed with BUILD-001.
- **Evidence:** read each against the code it describes.
- **Recommendation:** Correct them in the comment pass.
- **Effort:** S.
- **Risk of fix:** None.
- **Related:** DOC-001, LOG-001, BUILD-001.

### 2.14 Tests (TEST)

#### TEST-001: clang-tidy cannot analyse the tests

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `tools/Invoke-ClangTidy.ps1` (`-IncludeTests`); the 36
  initializers are in `tests/db/backfill_test.cpp` (8),
  `tests/db/reaction_store_test.cpp` (8),
  `tests/unit/trigger_command_test.cpp` (8),
  `tests/unit/linkstats_command_test.cpp` (6), and one each in
  `nickname_store_test.cpp`, `trigger_store_test.cpp`,
  `message_pipeline_test.cpp`, `nickname_command_test.cpp`,
  `nicknames_test.cpp` and `registry_test.cpp`.
- **Description:** The script documents `-IncludeTests` as a supported mode,
  but it cannot complete. The tests write designated initializers that leave
  fields out, as in `{.guild_id = …, .pattern = …, .responses = …}`. That is
  valid C++ and MSVC accepts it. clang, which clang-tidy compiles with, warns
  with `-Wmissing-designated-field-initializers`, and `/WX` in the compile
  database makes that an error. `src/` avoids the problem by always listing
  every field. Because the mode fails, the test code has never been held to
  clang-tidy, and about 65 other diagnostics have piled up there (§7.1).
  Most are trivial; a few are useful: unused `using` declarations, and a
  copied `std::string` parameter.
- **Evidence:** the run in §7.1, and `tidy-all` output reporting "Found
  compiler error(s)".
- **Recommendation:** Pick one of these:
  - Turn the warning off for test translation units only, for example
    `-Wno-missing-designated-field-initializers` passed through
    `tests/CMakeLists.txt` when the compiler is clang. Omitting fields is
    reasonable in tests.
  - Or drop `-IncludeTests` from the script and its documentation.

  If the mode is kept, fix the ~65 diagnostics once, or relax the checks
  that do not suit test code (`readability-function-cognitive-complexity`
  on Catch2 bodies) in a `tests/.clang-tidy`.
- **Effort:** S, then M to clear the diagnostics.
- **Risk of fix:** None to behaviour. Re-run `-IncludeTests` to confirm it
  completes.
- **Related:** none yet.

#### TEST-002: No command's `execute()` is tested, including the permission checks inside them

- **Severity:** Medium. **Status:** Confirmed.
- **Location(s):** every `*_command::execute` in `src/core/commands/`; the
  in-handler checks are in `urlrepl.cpp` (`urltoggle_command::execute`)
  and `linkstats.cpp` (`alias`, `recompute`).
- **Description:** The command tests cover the pure pieces well: parsing,
  renderers, `plan_say` and `plan_join`, `build()` payloads and the
  response flags. But no test drives a single `execute()`. Replying needs
  `event.co_reply`, and that needs a `dpp::cluster`. So none of these is
  checked:
  - which branch answers with which text and which kind (result or
    refusal);
  - that `/urlrepl set` actually stores the rule;
  - that the three checks Discord's default permissions *cannot* make are
    enforced. Those are:
    - `/urltoggle user:` needs Manage Server;
    - `/linkstats alias add | remove` needs Manage Server;
    - `/linkstats recompute` needs Manage Server.

    These live inside the handlers, because default permissions are per
    command (plan §21.13), and nothing tests them.

  Three renderers have no test at all: `render_trigger_list`,
  `render_duplicates` and `render_aliases`. The testing README's *Known
  gaps* lists the untested shell but not this.
- **Evidence:** searched `tests/` for `.execute(`; the only hits are
  `database::execute`. Searched `tests/` for each `render_*` name.
- **Recommendation:** Two steps, smallest first:
  1. **Pull each permission decision into a pure function**, as
     `plan_say` and `plan_join` already are. For example,
     `may_toggle_for(invoker, target, permissions)` and
     `may_manage_stats(permissions)`. Test them directly, and add tests for
     the three untested renderers.
  2. **Give commands a reply seam** so their `execute()` can run against a
     fake. That could be a small `ports::interaction` holding `reply`,
     `follow_up` and `edit_original` that `command::execute` receives, or a
     `dispatch` overload that takes one. A fake recording the messages
     would then let one test per command walk its branches. This is the
     larger change; step 1 closes the security-relevant part on its own.

  Add the gap to *Known gaps* until step 2 lands.
- **Effort:** S for step 1, M to L for step 2.
- **Risk of fix:** Step 2 touches every command's signature. It is
  mechanical, and the build finds every site.
- **Related:** DISC-004, ERR-002.

#### TEST-003: The fuzzers get no coverage feedback from the code they fuzz, and one cannot fail

- **Severity:** Medium: three targets the project counts on (plan §0)
  are much weaker than they look. **Status:** Confirmed.
- **Location(s):** `tests/fuzz/CMakeLists.txt:7-24`
  (`latibot_add_fuzzer`); `CMakePresets.json`'s `fuzz` preset;
  `tests/fuzz/fuzz_text.cpp`.
- **Description:**
  1. **No coverage from the library.** libFuzzer steers by coverage. The
     compiler inserts counters into the code it builds with
     `/fsanitize=fuzzer`, and inputs that reach new counters are kept and
     mutated further. Here `/fsanitize=fuzzer` is added only to each fuzz
     executable, whose single source file is the harness. `latibot_core`,
     where the scanner and the parsers live, gets `/fsanitize=address` from
     the preset but no coverage instrumentation. So the fuzzers mutate
     almost blind. The one-minute runs in this pass show it:
     - `fuzz_text` finished 12 million runs still at 9 coverage features,
       with a 3-byte corpus;
     - libFuzzer's suggested dictionaries for the other two contain
       arbitrary bytes rather than anything URL- or markup-like.
  2. **`fuzz_text` checks a tautology.** It asserts that
     `is_inside_spoiler(text)` equals `count_occurrences(text, "||") % 2 == 1`.
     But `is_inside_spoiler` *is* that expression (`util/text.cpp:35`), so
     no input can make it fail.
- **Evidence:** read the fuzz `CMakeLists.txt` and the preset. Ran each
  target for 60 s against this commit; results are in §7.1. Read
  `fuzz_text.cpp` against `util/text.cpp`.
- **Recommendation:**
  - When `LATIBOT_BUILD_FUZZERS` is on, instrument `latibot_core` for
    coverage too. On MSVC, `/fsanitize-coverage=inline-8bit-counters
    /fsanitize-coverage=edge /fsanitize-coverage=trace-cmp` on the library
    target; elsewhere, `-fsanitize=fuzzer-no-link`. Then confirm the feature
    counts climb well past the harness's own.
  - Replace `fuzz_text`'s assertion with properties that are not
    definitions. For example: `trim` returns a substring with no leading or
    trailing whitespace; `count_occurrences` never exceeds
    `size / needle.size()`. Or retire it once DUP-001's helpers exist and
    fuzz those.
  - Seed each target with a small corpus of real-looking inputs, such as
    messages with links, spoilers and code, in `tests/fuzz/corpus/`.
- **Effort:** S for the flags; S for the corpus and `fuzz_text`.
- **Risk of fix:** None to the bot; fuzz builds only.
- **Related:** WIRE-001, BUG-001.

### 2.15 Logging (LOG)

#### LOG-001: A failed rollback is swallowed silently, and its comment is out of date

- **Severity:** Low. **Status:** Confirmed.
- **Location(s):** `db/database.cpp:122` (`transaction::~transaction`).
- **Description:** When a transaction is abandoned, the destructor runs
  `ROLLBACK` inside `try { … } catch (...) {}`. The comment says "Once the
  logger exists (plan v4 §19, phase 0e), this should log the failure." The
  logger has existed since phase 0. A failed rollback leaves the connection
  mid-transaction, and every later write then fails with "cannot start a
  transaction within a transaction". Someone reading the log would see only
  that second error, never the first.
- **Evidence:** read the destructor; `util::log()` is available to
  `db/migrations.cpp` in the same library, so there is no dependency
  reason to keep it out.
- **Recommendation:** In the `catch`, log at `error`:
  `util::log().error("could not roll back a transaction: {}", error.what())`
  from a `catch (const std::exception& error)`, keeping `catch (...)` as a
  fallback with a fixed message. It answers "why is every write failing
  after that error?". Then drop the out-of-date sentence from the comment.
- **Effort:** S.
- **Risk of fix:** None. Logging cannot throw out of the destructor, as long
  as the call stays inside the `try`.
- **Related:** none.

#### LOG-002: A failed trigger reply or midnight post is never logged, and the log says it succeeded

- **Severity:** Medium: for midnight, the day's message is lost with no
  trace. **Status:** Confirmed.
- **Location(s):** `bot.cpp:894` (`carry_out`, the `send_message` branch):
  `cluster_.message_create(reply)` with no callback, followed at once by
  `util::log().info("replied in channel …")`.
- **Description:** Every `send_message` action goes through here: trigger
  replies, the goodbye reply and midnight posts. The send is
  fire-and-forget, so a refusal from Discord is never seen: a missing Send
  Messages in that channel, content over 2,000 characters from a long
  trigger response, a deleted channel. Meanwhile the `info` line says
  "replied" before the request has even left. For midnight this matters
  twice over. `midnight_scheduler::tick` marks the day as posted *before*
  posting (deliberately, so a crash cannot double-post), so a failed send
  loses that day's message, and the log records it as sent. The question
  it would answer is the one the plan expects: "why didn't the midnight
  message go out?"
- **Evidence:** read `carry_out`, and `midnight_scheduler::tick`'s
  claim-then-post order.
- **Recommendation:** Pass a completion callback to `message_create`. On
  error, log at `warn`: `"could not post in channel {}: {}"`, with the
  channel and Discord's message, and the trigger or midnight entry ID if
  `send_message` carries one. Reword the existing `info` line to "posting
  in channel {}", or move it into the callback's success branch as
  "posted". The latter is the running record the README promises.
- **Effort:** S.
- **Risk of fix:** None.
- **Related:** ERR-001.

---

## 3. Prioritized action plan

Every phase leaves the project in the same state as now:
- Debug and Release build with no warnings;
- `ctest --preset debug`, `release` and `asan` pass;
- clang-tidy is clean over `src/`;
- `Invoke-ClangFormat.ps1 -Check` passes;
- the test catalog is regenerated.

Each numbered item can be its own commit. Effort is in brackets.

### Phase 1: correctness and safety

1. **DUP-002** [S]: the shared component-limit checker, with the
   uniqueness check, first. Items 3 and 4 are proved with it.
2. **CONC-002** [S]: a mutex in `trigger_responder`, and a `[threads]`
   test.
3. **DISC-001** [S]: a unique Cancel ID in both panels. Test the
   confirmation state on the first and last page.
4. **DISC-002** [S]: shorten the trigger menu's labels (a small
   character-aware `util::truncate`), and cap the form's pattern at 200.
5. **ERR-001** [S]: guard the midnight, embed-tracker and
   `attribute_later` timers the way the backup timer is guarded.
6. **LOG-002** [S]: a completion callback on `carry_out`'s sends, logging
   failures at `warn`.
7. **ERR-002** [S]: `on_component` and `on_form` answer when they throw or
   do not recognise a view.
8. **TEST-002, step 1** [S]: pull the three Manage Server checks into pure
   functions and test them, with the three untested renderers.
9. **BUG-003** [M]: the startup sweep for `pending` and `retrying`
   replacements. It depends on item 5, so a failing sweep does not take a
   timer with it.
10. **Smaller fixes, in any order:** CONC-001 [S], BUG-002 [S] (with
    `parse_snowflake` moved to `util`), BUG-001 [S], BUG-004 [S], LIFE-001
    [S], DISC-004 [S], DISC-003 [S].

### Phase 2: the tools that guard the rest

11. **TEST-003** [S]: coverage for `latibot_core` in fuzz builds, a real
    property for `fuzz_text`, and seed corpora. Re-run each target and
    record the feature counts.
12. **BUILD-001** [S]: re-enable the widening check.
13. **TEST-001** [S to M]: make `-IncludeTests` complete, then clear or
    relax its findings.
14. **BUILD-002** [S]: a format check and a Release configuration in CI.

### Phase 3: unwired code

15. **WIRE-004** and **WIRE-005** [S]: remove `bot`'s accessors,
    `forget_cooldowns` and `nickname_store::members`. Do this after item 2,
    or together with it.
16. **WIRE-001** [S]: move `is_inside_spoiler` into the tests as the
    reference implementation, together with item 10's BUG-001.
17. **WIRE-002** and **WIRE-003** [S]: comments only, saying what each
    piece of scaffolding waits for.

### Phase 4: duplication

18. **DUP-001** [S] and **DUP-004** [S]: `util::to_lower`,
    `equals_ignoring_case` and `lines`, and the callers moved over.
19. **DUP-005** [S]: `commands/options.hpp`, with `subcommand_path`
    replacing the four `subcommand_of` copies.
20. **DUP-003** [M]: snowflakes and times bound and read directly by
    `db::statement` (see *Decisions needed*).
21. **SIMP-001** [S]: batch the trigger responses; join the emoji names.

### Phase 5: consistency, logging and docs

22. **LOG-001** [S] and **CONV-001** [S].
23. **CONF-001** [S]: the configuration table or example file.
24. **DOC-002** [S]: plan §4 and the testing README. DOC-001 and DOC-003
    are done by the comment pass (§6).
25. **PORT-001** to **PORT-003**: whatever is decided below, recorded in
    plan §6.
26. **TEST-002, step 2** [M to L]: the interaction seam for testing
    `execute()`, if wanted. It builds on DISC-004's `defer` helper.

---

## 4. Decisions needed

1. **PORT-001: which replies should be public again?** `/join` (and
   restore "ok joining *name*"), `/leave`, `/shutdown`, `/urlrepl set` and
   `remove`, `/urltoggle`. The recommendation is public for the three
   voice and shutdown commands, private for the rest.
2. **PORT-002: `/say`'s default permission.** Keep Manage Messages or
   restore Manage Roles.
3. **PORT-003: the bot's status across restarts.** Remember the last
   `/status`, take a default from `config.json`, or document that it is
   temporary.
4. **TEST-002, step 2: is the interaction seam worth its size?** It makes
   every command's `execute()` testable, and changes every command's
   signature. Step 1 covers the security-relevant part without it.
5. **DUP-003: may `db/` depend on DPP's `snowflake.h`?** Or should the
   snowflake binding live in a separate `db/discord_types.hpp`?
6. **BUILD-002: what CI runs.** Formatting and Release are cheap. Should
   ASan and clang-tidy stay local?
7. **BUG-003: how a stranded replacement is settled.** Settle it at once
   from a fetched copy (recommended), or restart its watch as if newly
   posted.
8. **DISC-003: cap `domain` at 40 characters**, or keep 100 and show a
   note when paging does not fit.

---

## 5. Verified OK

- **The logger** (`util/log.*`). Coloured formatting falls back to plain
  text on a `format_error`, lines are written whole under one lock, and
  `log.hpp`'s size is templates and `constexpr` that have to live in a
  header. No findings beyond DUP-001's `lowercased`.
- **`.env` loading and certificates** (`util/env.cpp`,
  `util/ca_certificates.cpp`). The real environment always wins over
  `.env`. `_dupenv_s` is used on MSVC. The Windows store enumeration frees
  every context correctly. An empty export is refused rather than written.
- **The URL scanner** (`util/url_scan.cpp`), apart from BUG-001. Every step
  is linear. Brackets are balanced with running counts. IPv6 hosts keep
  their colons. `rehost` never doubles a translation suffix or a slash.
- **The database wrapper** (`db/database.*`, `db/statement.*`):
  - statements finalize before their lock is released;
  - moves transfer the lock;
  - paths are passed to SQLite as UTF-8;
  - text binds use `SQLITE_TRANSIENT`.

  No coroutine holds a statement, a `lock()` or a `transaction` across a
  `co_await`. The only database calls in coroutine files are the backfill
  progress store's, which are synchronous functions. That matters because
  the mutex is recursive and thread-owned: a coroutine resuming on another
  DPP thread and releasing it there would be undefined behaviour.
- **Migrations** (`db/migrations.cpp`). There are nine, append-only, each
  in its own transaction, with a gap check. The column lists match the plan's
  §5.2.
- **Ports** (`ports/*`). They are small and match their users. `result<T>`
  and `result<void>` are used consistently by the gateway. The mocks
  implement every method.
- **The DPP adapters** (`discord/dpp_gateway.cpp`, `dpp_http_client.cpp`,
  `raw_api.cpp`):
  - `set_embeds_suppressed` uses the flags-only edit, the one Discord allows
    on other people's messages;
  - `get_messages` sorts DPP's map newest first, as the backfill expects;
  - `get_reaction_users` returns DPP's unordered map as it comes, which is
    safe because the backfill pages with `after = max(after, id)`
    (`backfill.cpp:388`) rather than the last element.
- **The paginator** (`ui/paginator.cpp`) itself:
  - `encode` refuses an ID over 100 characters rather than truncating it;
  - `decode` rejects anything that is not `view:page:argument` with a
    non-negative page;
  - pages are clamped, so a stale button from a longer list still lands
    somewhere.

  DISC-001 is in how two panels combine its output, not in the paginator.
- **The message pipeline** (`events/message_pipeline.cpp`). It skips
  itself and unallowed bots before any stage runs, and catches each stage's
  exceptions on its own so one broken stage cannot silence the rest. It
  logs a stage only when it wants something.
- **Trigger matching** (`events/triggers.cpp`): whole-word matching checks
  every occurrence; weighted choice ignores zero weights; the cooldown
  maths is monotonic. Concurrency is a separate matter (CONC-002).
- **Midnight** (`events/midnight.cpp`). The claim-then-post order stops a
  double post across a crash. A missed day is reported once. Its state is
  only touched from its own timer.
- **Nickname tracking and the Java import** (`events/nicknames.cpp`,
  `nickname_import.cpp`):
  - attribution is a single conditional `UPDATE`, so two audit entries
    cannot both win;
  - the import checks every field, reports problems instead of throwing,
    and treats both daylight-saving edge cases deliberately.
- **URL rules and planning** (`events/url_rules.cpp`). Rule edits run in a
  transaction, and every mirror a rule has used is remembered.
  `explain_links` checks suppressed, in-code, no-rule, duplicate and
  over-limit in a stated order.
- **The embed tracker** (`events/embed_watch.cpp`):
  - one mutex guards the watches and the early-update buffer;
  - the buffer is bounded (256 entries) and pruned;
  - the only nested lock is tracker then database, and no path takes them
    the other way round.
- **The reaction store** (`events/reactions.cpp`). Statistics SQL is built
  only from constants, with every user-supplied value bound. Recording
  ignores reactions on messages that are not replacements in the same
  statement. `replace_for_message` keeps existing rows, and with them their
  timestamps.
- **The legacy parser and backfill** (`events/legacy_replacements.cpp`,
  `backfill.cpp`):
  - every reference a backfill coroutine holds lives in a frame that is
    suspended awaiting it;
  - reactor paging is correct with DPP's unordered map (see *The DPP
    adapters* above);
  - cancellation and progress work as described.
- **The command registry** (`commands/registry.cpp`). Names and aliases
  are refused on a clash without half-registering. Response flags are
  validated against each command's real subcommands at `add`. A thrown
  command now gets an answer, by reply or by follow-up.
- **Command wiring** (§7.3). Every declared option is read, every option
  read is declared, and every panel view is both emitted and routed.
- **Reply sizes.** Every text renderer either has a fixed number of lines
  or stops before Discord's 2,000 characters (`render_test`,
  `render_duplicates`, `render_aliases`, the boards and the lists).
- **Build and CI basics:**
  - warnings are errors for our targets only;
  - DPP's headers are marked `SYSTEM`;
  - the version string is pinned to its constants by a test;
  - CI runs gitleaks over the full history.
- **Java parity of the message flow.** Every behaviour of
  `MessageListener` either survives or is changed by a recorded decision:
  - triggers no longer stop URL replacement (plan §5.4);
  - a failed replacement leaves a note with Retry instead of deleting
    itself (§9.4);
  - opt-outs persist (§9.5);
  - webhook mode is gone (§9.5).
- **Backups** (`db/backup.cpp`). They use the online backup API under the
  connection lock, and rotate by name, which sorts chronologically. The
  timer in `bot.cpp` catches and logs failures.

---

## 6. The comment pass

Done after the report, following guidance §8. **Only comments changed**:
64 files under `src/`, 276 lines added and 144 removed, left uncommitted
for review.

**Checked:**
- the diff filter from guidance §8.3 prints nothing, so every changed line
  is a whole-line comment;
- `Invoke-ClangFormat.ps1 -Check` passes on all 150 files;
- Debug and Release build with no warnings;
- `ctest --preset debug` and `release` pass 464 of 464.

**Comments corrected:**
- **"plan v4 §x" becomes "plan §x", 135 times** (DOC-001). That covers
  every citation in a `//` or `///` comment, three of which wrap across
  lines (`commands/urlrepl.cpp`, `commands/urlrepl.hpp`, `main.cpp`).
  - `config/bootstrap.cpp`: §2.8, which exists in no plan, becomes §2.4.
  - **Left alone:** the 11 citations inside the migration SQL in
    `db/migrations.cpp`. They are part of shipped migrations' text, which
    is never edited.
- `bot.hpp` (`register_timers`): "the two things" becomes the three timers,
  the embed tracker's included (DOC-003).
- `ui/paginator.hpp`: `page_state::view`'s example is `nicks`, not
  `nicknames`. `controls` "returns nothing", and now says it also returns
  nothing when the state does not fit (DISC-003).
- `db/database.cpp` (`transaction::~transaction`): the out-of-date "once
  the logger exists" becomes a statement that the failure should be logged
  and is not, and what that costs (LOG-001).
- `commands/trigger.hpp`: the panel note now says `/urlrepl`'s panel is
  the second and `/llm settings` will be the third (plan §21.5).

**Step comments added**, by function. Most of the other functions the
complexity scan listed (§7.1) were already commented step by step, and were
left alone.
- `util/url_scan.cpp`: `code_spans`, `find_links`
- `util/env.cpp`: `parse_dotenv`
- `events/legacy_replacements.cpp`: `classify`, `attribute`
- `events/url_rules.cpp`: `parse_legacy_rules`
- `events/backfill.cpp`: `backfill_service::scan_page`, `consider`
- `events/embed_watch.cpp`: `render_failure`, `embed_tracker::watch`,
  `on_embeds`, `tick`, `finish`
- `events/url_replacer.cpp`: `url_replacer::operator()`,
  `carry_out_embed_actions`
- `events/reactions.cpp`: `parse_emoji`
- `events/midnight.cpp`: `midnight_scheduler::tick`
- `commands/linkstats.cpp`: `linkstats_command::autocomplete`,
  `decode_board`, `recompute_start` (the lifetime of the progress lambda)
- `commands/urlrepl.cpp`: `render_test`
- `commands/registry.cpp`: `registry::check_responses`
- `bot.cpp`: `register_events` (slash commands, messages and panels),
  `on_component`, `on_trigger_component`, `on_url_component`,
  `attribute_later`

**Comments wrong because the code is wrong:** none were changed to match
the code. One step comment was drafted and then withdrawn: it described
`find_links` judging `<link>` on the trimmed end, which is BUG-001's
defect, and would have read as intended behaviour.

---

## 7. Appendix

### 7.1 Tooling results

Run at `1cd9fec` on 2026-09-25, Windows, MSVC 14.51 and clang-tidy from the
Visual Studio LLVM component.

| Tool | Result |
|---|---|
| `cmake --build build --config Debug` / `Release` | Both build. No compiler diagnostics; `/W4 /WX` is on |
| `cmake --build build-asan --config Debug` | Builds |
| `ctest --preset debug`, `release`, `asan` | 464 of 464 pass in each |
| `Invoke-ClangTidy.ps1` over `src/` (45 files) | Clean |
| `Invoke-ClangTidy.ps1 -IncludeTests` (95 files) | **Does not complete.** 36 `clang-diagnostic-missing-designated-field-initializers` diagnostics are errors under `/WX`, in 10 test files, so clang-tidy reports "failed to compile" (TEST-001). Beyond those, about 65 diagnostics, listed below |
| `Invoke-ClangFormat.ps1 -Check` | All 150 files formatted |
| `Update-TestCatalog.ps1 -OutputPath <scratch>` | Identical to `docs/testing/Test_Catalog.md` (465 cases, the one extra being a hidden benchmark) |
| Complexity scan at threshold 12 | 24 functions, listed below |
| Fuzzing, 60 s each, rebuilt at this commit | No crashes. `fuzz_text` 12.0M runs, `fuzz_url_scan` 361k, `fuzz_legacy_parser` 266k. `fuzz_text` stayed at 9 coverage features with a 3-byte corpus throughout (TEST-003) |
| cppcheck, Python | Not available on this machine |

**clang-tidy over the tests, by check**, excluding the missing-initializer
errors:

| Count | Check |
|---:|---|
| 16 | `misc-const-correctness` |
| 14 | `modernize-use-designated-initializers` |
| 11 | `readability-function-cognitive-complexity` (all Catch2 test bodies, and `check_components_fit` in `urlrepl_command_test.cpp`) |
| 6 | `bugprone-throwing-static-initialization` (namespace-scope `std::string`/`std::map` test constants) |
| 3 | `readability-convert-member-functions-to-static` |
| 2 each | `readability-redundant-member-init` (`mock_clock.hpp`), `readability-container-size-empty`, `performance-unnecessary-value-param`, `performance-inefficient-vector-operation`, `misc-unused-using-decls` (`ports_test.cpp`: `result`; `url_rules_test.cpp`: `planned_link`) |
| 1 each | `readability-use-anyofallof` (`capture_log.hpp`), `readability-named-parameter` (`quiet_log.cpp`), `modernize-use-integer-sign-comparison` (`migrations_test.cpp`), `bugprone-empty-catch` (`registry_test.cpp:86`, deliberate: the test only needs the throw to happen), and `bugprone-exception-escape` on `command_info`'s implicit move constructor (`src/core/commands/registry.hpp:65`). That one is harmless: MSVC's `std::map` move constructor is not `noexcept`, and only tests ever move a `command_info` |

**Functions with cognitive complexity of 12 or more** (the project's limit is
25). These are the comment pass's first candidates:

| Complexity | Function |
|---:|---|
| 24 | `bot::register_events` (`bot.cpp:272`) |
| 22 | `classify` (`legacy_replacements.cpp:100`) |
| 21 | `linkstats_command::autocomplete` (`linkstats.cpp:574`), `parse_legacy_rules` (`url_rules.cpp:255`) |
| 20 | `backfill_service::consider` (`backfill.cpp:294`) |
| 19 | `render_test` (`urlrepl.cpp:151`) |
| 18 | `embed_tracker::finish` (`embed_watch.cpp:268`), `attribute` (`legacy_replacements.cpp:182`) |
| 17 | `pipeline::run` (`message_pipeline.cpp:14`) |
| 16 | `bot::attribute_later` (`bot.cpp:518`), `bot::on_trigger_component` (`bot.cpp:701`) |
| 15 | `parse_emoji` (`reactions.cpp:121`), `code_spans` (`url_scan.cpp:33`) |
| 14 | `bot::on_url_component` (`bot.cpp:741`), `registry::check_responses` (`registry.cpp:224`), `backfill_service::reactors` (`backfill.cpp:368`), `embed_tracker::tick` (`embed_watch.cpp:218`), `render_failure` (`embed_watch.cpp:98`), `trigger_responder::operator()` (`triggers.cpp:256`), `carry_out_embed_actions` (`url_replacer.cpp:132`), `parse_dotenv` (`env.cpp:56`) |
| 13 | `decode_board` (`linkstats.cpp:282`), `linkstats_command::recompute_start` (`linkstats.cpp:746`), `export_system_certificates` (`ca_certificates.cpp:20`) |

### 7.2 Inventory

**`src/`**, about 15,100 lines. Each unit is a `.hpp` and `.cpp` pair unless
noted. "Used by" lists the modules that include it, not the tests.

| Module (lines) | Unit | What it holds | Used by |
|---|---|---|---|
| root (1,136) | `bot` | the shell: owns every store and port, registers commands, stages, events and timers, routes panels | `main.cpp` |
| | `version` | `version_string()` and the version constants | `bot` |
| | `main.cpp` | startup order (§7.3) | — |
| `commands` (4,711) | `registry` | `command`, `command_info`, `response_flags`, `registry`, `dispatch`, `subcommand_path`, `describe_invocation`, `user_label` | every command, `bot` |
| | `basic` | `/ping`, `/say`, `/status`, `/join`, `/leave`, `/shutdown`, `/goodbye`; `plan_say`, `plan_join` | `bot` |
| | `bots` | `/bots`, `render_allowed_bots` | `bot` |
| | `trigger` | `/trigger`, the trigger panel, `trigger_form`, `apply_form`, `toggle_for`, `parse_responses` | `bot` |
| | `nickname` | `/nickname`, `/nicknames`, `render_nickname_history` | `bot` |
| | `midnight` | `/midnight`, `render_midnight_list` | `bot` |
| | `urlrepl` | `/urlrepl`, `/urltoggle`, the URL rule panel, `build_rule`, `render_test`, `switch_url_replacement` | `bot` |
| | `linkstats` | `/linkstats`, boards, profile, `encode_board` / `decode_board`, `parse_day`, `recompute_support` | `bot` |
| | `message_options` | the `silent` and `previews` options | `trigger`, `midnight` |
| | `preflight` | `requirement`, `unmet`, `describe_permissions`, the passive requirements | `bot` |
| `config` (543) | `bootstrap` | `config.json`, `secrets`, `parse_snowflake`, the debug override | `main`, `bot` |
| | `guild_settings` | per-guild key/value settings | `bot`, `basic`, `goodbye`, `url_rules` |
| `db` (1,035) | `database`, `statement` | the connection, locking, binding, transactions | every store |
| | `migrations` | the nine schema steps | `bot` |
| | `backup` | online backup and rotation | `bot` |
| | `error` | `db_error` | `db` |
| `discord` (445) | `dpp_gateway` | `discord_gateway` over DPP | `bot` |
| | `dpp_http_client` | `http_client` over DPP (scaffolding, WIRE-003) | `bot` |
| | `raw_api` | endpoints DPP lacks (scaffolding) | `bot` |
| | `message_flags` | `channel_message_flags`, `apply_flags`, `describe_flags` | `registry`, `bot`, `message_pipeline`, stores |
| `events` (5,363) | `message_pipeline` | `incoming_message`, the `action` variant, `pipeline` | `bot`, the stages |
| | `goodbye` | the goodbye stage | `bot`, `basic` |
| | `triggers` | `trigger`, `trigger_store`, `trigger_responder`, matching | `bot`, `trigger` |
| | `bot_allowlist` | allowed bots per guild | `bot`, `bots` |
| | `nicknames`, `nickname_import` | the history store, `pending_nicknames`, attribution helpers, the Java import | `bot`, `nickname` |
| | `midnight` | `midnight_entry`, `midnight_store`, `midnight_scheduler`, timezone helpers | `bot`, `midnight` |
| | `url_rules` | `url_rule`, `url_rule_store`, `explain_links`, `plan_replacements`, the legacy rule import | `url_replacer`, `urlrepl`, `backfill`, `bot` |
| | `url_replacer` | the replacement stage, `post_replacement`, `plan_retry` | `bot` |
| | `embed_watch` | `embed_tracker`, rendering, `build_edit` | `url_replacer`, `bot` |
| | `replacements` | `replacement_store` | `url_replacer`, `embed_watch`, `backfill`, `bot` |
| | `reactions` | `reaction_store`, emoji helpers, statistics queries | `bot`, `linkstats`, `backfill` |
| | `legacy_replacements` | `classify`, `attribute`, snowflake time helpers | `backfill` |
| | `backfill` | `backfill_service`, `backfill_progress_store` | `bot`, `linkstats` |
| `ports` (279) | `clock`, `discord_gateway`, `http_client`, `tts_engine`, `result.hpp` | the ports (mocks in `tests/mocks/`) | everything that talks outward |
| `ui` (192) | `paginator` | `page_state`, `encode` / `decode`, paging controls | every renderer with buttons, `bot` |
| `util` (1,325) | `log`, `text`, `env`, `ca_certificates`, `url_scan` | logging, text helpers, `.env`, certificates, the link scanner | everywhere |

**`tests/`**, about 8,400 lines and 464 cases:
- `unit/`: 33 files;
- `db/`: 13 files;
- `mocks/`: clock, Discord, HTTP, TTS;
- `support/`: log capture, temporary directories, the quiet-log listener,
  `slash_event.hpp`;
- `fuzz/`: three targets.

There is no `live/` and no `fixtures/` (DOC-002).

**Build and tools:**
- `CMakeLists.txt`, `src/`, `tests/` and `tests/fuzz/CMakeLists.txt`;
- `cmake/warnings.cmake` (warnings and sanitizers) and `cmake/helpers.cmake`
  (runtime DLL and ASan copies);
- `CMakePresets.json`: `msvc`, `asan`, `fuzz`, `ninja-tidy`;
- `conanfile.py`: OpenSSL, zlib, opus, SQLite with FTS5, CTRE, Catch2;
- `tools/`: `Common.ps1`, `Invoke-ClangTidy.ps1`, `Invoke-ClangFormat.ps1`,
  `Update-TestCatalog.ps1`;
- `.github/workflows/ci.yml`: build and test Debug, check the catalog,
  gitleaks;
- `.vscode/`: tasks, launch, settings and IntelliSense;
- submodules `third_party/DPP` (10.1.6) and `third_party/dectalk` (not yet
  built).

**Docs:**
- `README.md`;
- `docs/features/README.md` and `Planned.md`;
- `docs/porting/`: `Porting_Plan_Final.md` (maintained), plus v1 to v4
  and `Porting_Initial_Analysis.md` (historical);
- `docs/testing/README.md` and `Test_Catalog.md` (generated);
- `docs/ideas/Self_Hosted_Embeds.md`;
- `docs/analysis/`: the guidance and this report.

**The Java bot, mapped** (`java-reference/LatiBot v1.1/src/main/java/latibot/`,
39 files):

| Java | C++ |
|---|---|
| `LatiBot.java` | `main.cpp`, `bot.cpp`; startup status: PORT-003 |
| `CommandListener`, `CommandRegistry`, `BaseCommand`, `Commands` | `commands/registry`, `bot::register_commands` |
| `MessageListener` | `events/message_pipeline`, `triggers`, `url_rules`, `url_replacer`, `embed_watch` (plan §5.4, §9) |
| `NicknameListener`, `ReadyListener` | `events/nicknames`, `nickname_import`, `bot`'s member and audit handlers (plan §8) |
| `ActionListener` (owner confirmation buttons) | dropped (plan §8.1) |
| `ReactionListener` (🔁 refresh on "riggbot") | dropped (initial analysis §9) |
| `utils/MidnightManager` | `events/midnight`, `commands/midnight` (plan §10) |
| `PingCmd`, `SayCmd`, `StatusCmd`, `JoinVoiceCmd`, `LeaveVoiceCmd`, `ShutdownCmd` | `commands/basic`; PORT-001, PORT-002 |
| `NicknameCmd`, `NicknamesCmd` | `commands/nickname` |
| `ReplaceUrlCmd`, `ToggleReplaceCmd` | `commands/urlrepl` (`/urlrepl`, `/urltoggle`) |
| `ToggleWebhooksCmd` | dropped: webhook mode is gone (plan §9.5) |
| `EmoteStatsCmd`, `GetEmotesCmd` | unscheduled (plan §16) |
| `audio/*` (`PlayCmd`, `QueueCmd`, … `SpeakCmd`), `DecTalkWrapper`, `TrackManager` | phase 4 and unscheduled (plan §12, §13, §15) |
| `chat/ChatTestCmd`, `ApiDriver` | phase 5 (plan §14) |

### 7.3 Reachability

Everything the running bot can reach, from `main`. Anything in `src/` not
reachable from here is listed under WIRE-003 to WIRE-005.

**`main.cpp`:**
1. `load_dotenv(".env")`
2. `apply_log_colors_from_environment`
3. `log_level_from_environment`
4. `bootstrap::load`, which also reads `LATIBOT_DEBUG_RECOMPUTE_BOT_ID`
5. `use_system_certificates`
6. `secrets::from_environment`
7. `bot::bot`, then `bot::run`

**The `bot` constructor:**
- `db::migrate`, then `import_nicknames_file` (`data/nicknames.json`,
  every start, idempotent).
- **`register_commands`**: 15 commands. `add_basic_commands` adds `/ping`,
  `/say`, `/status`, `/join`, `/leave`, `/shutdown` and `/goodbye`. Then
  `/trigger`, `/bots`, `/nickname`, `/nicknames`, `/midnight`, `/urlrepl`,
  `/urltoggle` and `/linkstats`. No command has aliases.
- **`register_stages`**: `goodbye`, then `url replacement`, then `triggers`.
- **`register_events`:**

  | DPP event | Goes to |
  |---|---|
  | `on_log` | the logger, tagged `[dpp]`, with the 4014 hint |
  | `on_slashcommand` | `registry::dispatch`, a coroutine |
  | `on_ready` | global command registration, once per run |
  | `on_guild_create` | `check_permissions`, `reconcile_nicknames`, `import_url_rules` (`data/UrlReplacements.txt`, once per guild), `seed_defaults` |
  | `on_message_create` | `describe`, then `pipeline::run`, then `carry_out`, which sends a message, detaches `post_replacement`, or starts the `stop_bot` thread |
  | `on_message_update` | `embed_tracker::on_embeds`, then `carry_out` |
  | `on_message_delete` | `embed_tracker::forget` |
  | the four reaction events | `reaction_store::add`, `remove`, `remove_emoji`, `remove_all` |
  | `on_guild_member_update`, `on_guild_audit_log_entry_create` (only with `track_nicknames`) | `on_member_update` (which calls `attribute_later`), and `on_audit_entry` |
  | `on_autocomplete` | `registry::offer_completions` |
  | `on_button_click`, `on_select_click` | `on_component`: nickname history, Retry, boards, then `on_trigger_component` and `on_url_component` |
  | `on_form_submit` | `on_form`, then `on_url_form` or the trigger form |

- **`register_timers`:**
  - the midnight tick (30 s);
  - the embed tracker tick (1 s);
  - backups (`backup_interval_minutes`), unless off;
  - one-shot `attribute_later` timers created per nickname change.

**Detached work** (`bot::detach`): `post_replacement`, and
`carry_out_embed_actions` through `bot::carry_out(vector<embed_action>)`.
The recompute runs inside its command's coroutine, not detached.

**String wiring, checked both ways:**
- **Panel views:** every view constant is both encoded and routed. The
  trigger toggles route through `toggle_for`, and the trigger form through
  `on_form`'s `!=` check.
- **Settings keys:** `goodbye_phrase`, `url_rules_imported` and
  `url_replacement_enabled` are each written and read with the same
  constant.
- **Subcommands and options:** every declared option name is read, and
  every name read is declared, in all seven command files.
- **Columns:** every store's column lists match migrations 1 to 9.
- **Test tags:** the component tags agree across the catalog script,
  `.vscode/settings.json` and `docs/testing/README.md`.
- **Build lists:** every `.cpp` under `src/`, `tests/` and `tests/fuzz/` is
  in its `CMakeLists.txt`.

---

## Progress log

| Pass | Covered |
|---|---|
| 1 | Skeleton; tooling baseline (§7.1). Read in full: `util`, `db`, `config`, `ports`, `discord`, `ui`, `events`, `commands`, `bot`, `main` |
| 2 | Reachability and string wiring (§7.3); tests; tools, build and CI; docs; fuzzing; Java parity; inventory (§7.2); executive summary, action plan and decisions; a check that every cited finding ID exists |

**The report is complete.** The comment pass (guidance §8) follows it and
is recorded in §6.

Leads that were checked and dropped, so they are not re-investigated:
- **Commands that declare Send Messages but only answer interactions.**
  Harmless: interaction replies do not need the permission, and the passive
  features require it anyway, so the startup warning is the same.
- **`dpp::snowflake` built from button arguments in `bot.cpp`.** DPP's
  string constructor is `noexcept` and yields 0 for junk.
- **The backfill's reactor paging over DPP's unordered map.** Safe, since
  it pages from the largest ID seen.
- **Migration upgrade tests beyond migrations 3 and 9.** Migrations 4 to 8
  only create tables, so there is no existing data to carry across.
