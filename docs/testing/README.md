# Testing

How this project is tested, and the conventions that keep the suite readable
as it grows. The plan behind it is
[Porting_Plan_Final.md §17](../porting/Porting_Plan_Final.md); this
document is the living version, kept in step with the code.

For the current list of what is tested, see
[Test_Catalog.md](Test_Catalog.md), which is generated from the sources.

---

## Running the tests

```powershell
ctest --preset debug       # everything, Debug
ctest --preset release     # everything, Release
ctest --preset asan        # everything, under AddressSanitizer
```

Or run the binary directly to use Catch2's own filtering:

```powershell
.\build\bin\Debug\latibot_tests.exe                 # all
.\build\bin\Debug\latibot_tests.exe "[db]"          # one component
.\build\bin\Debug\latibot_tests.exe "[config]~[fs]" # config, skipping file I/O
.\build\bin\Debug\latibot_tests.exe --list-tests    # names and tags
```

In VS Code, the **Testing** sidebar groups tests by component, then by source
file (see [VS Code setup](#vs-code-setup)).

---

## How the suite is organised

```
tests/
  unit/        pure logic: no database, no network, no files
  db/          anything that opens a database
  mocks/       hand-written stand-ins for the ports
  support/     test helpers (temp directories, log capture, Discord's limits)
  fuzz/        libFuzzer targets, built only by the fuzz preset; corpus/ holds
               their seed inputs
```

Two more are planned and do not exist yet: `live/`, for tests that need a real
Discord connection, and `fixtures/`, for synthetic data files.

The bot's code lives in a static library, `latibot_core`, which both
`LatiBot.exe` and `latibot_tests.exe` link. Tests exercise exactly the code
that ships.

---

## Tags

Every test carries **exactly one component tag**, and any number of trait
tags. The component tag is the axis the catalog and the VS Code tree are
grouped by; the traits are for filtering.

### Component tags

| Tag | Covers |
|---|---|
| `[db]` | `src/core/db`: connection, statements, migrations, backups |
| `[config]` | `src/core/config`: `config.json`, per-guild settings |
| `[commands]` | `src/core/commands`: the registry, dispatch and the commands |
| `[events]` | `src/core/events`: the message pipeline and its stages (goodbye, triggers, URL replacement), the embed tracker, reactions, nicknames, midnight and the backfill |
| `[ui]` | `src/core/ui`: paging and panel primitives, and the routes panels claim their views in |
| `[module]` | `src/core/module`: capabilities, the order modules offer and start in, the host; `tests/support/test_host.hpp` stands in for the bot |
| `[discord]` | `src/core/discord`: raw API helper, gateway wrappers |
| `[audio]` | `src/core/audio`: the DECtalk engine, the sanitizer, voices, PCM and WAV, the mixer |
| `[music]` | `src/core/music` and `/music`: the queue, the player, links, yt-dlp and ffmpeg |
| `[llm]` | `src/core/llm`: the providers, the tool loop, memory, documents, spending, guards, the prompt, the responder and its pipeline stage |
| `[ports]` | `src/core/ports` and the mocks that implement them |
| `[log]` | `src/core/util/log` |
| `[util]` | the remaining small helpers in `src/core/util`, version |

Add a new component tag when a new area appears, and register it in
`tools/Update-TestCatalog.ps1` and in `.vscode/settings.json` at the same
time.

### Trait tags

| Tag | Meaning | Why it is worth filtering |
|---|---|---|
| `[coro]` | drives a coroutine | these are the ones that can hang; they all use `sync_wait_for` |
| `[threads]` | starts threads | the slowest and the most order-dependent |
| `[fs]` | writes real files | needs a writable temp directory |
| `[golden]` | compares against stored output | updated deliberately, never blindly |
| `[live]` | needs something real: Discord, yt-dlp, ffmpeg | hidden as well, with `[.]`; see below |

`tools/Update-TestCatalog.ps1` fails if a test has no component tag, two
component tags, or a tag nobody recognises. That is the guard against the
catalog quietly drifting.

---

## What gets tested where

**Pure logic** is the default and should stay the bulk of the suite. Features
are written as functions from plain data to a decision, with the Discord event
handling kept to a thin shell around them. Anything shaped that way needs no
mock at all.

**The database layer** is tested against a real SQLite database opened at
`":memory:"`, not a mock. It is fast (milliseconds), and it tests the actual
SQL, which is the part most likely to be wrong.

**Ports and mocks** cover the cases where a feature has to talk to the outside
world mid-logic. Five ports exist — `clock`, `discord_gateway`, `http_client`,
`tts_engine`, `voice_output` — each with a hand-written mock in `tests/mocks/`.
There is no mocking framework: a mock that fits on one screen is easier to
trust.

The language model has two levels of stand-in. The providers turn a
conversation into a provider's JSON and back, and are tested against recorded
replies through `mock_http`, so what is sent to Anthropic and OpenAI is pinned
down byte for byte. Everything above them (the tool loop, the responder) talks
to `llm::provider`, which `mock_llm` implements from a script of replies:
"call `remember` with this", then "answer with that". No test needs an API
key, and none calls out.

A feature test then looks like this:

```cpp
TEST_CASE("a coroutine feature runs against the Discord mock", "[ports][coro]") {
    latibot::testing::mock_discord discord;

    const auto outcome = post_then_edit(discord).sync_wait_for(2s);

    REQUIRE(outcome.has_value());   // no value means it timed out
    CHECK(discord.sent.size() == 1);
}
```

**Always drive coroutines with `sync_wait_for`**, never `sync_wait`. A bug
that deadlocks a coroutine then fails the test in two seconds instead of
hanging the suite forever.

**The suite runs with logging off.** A Debug build defaults to the `debug`
level, which would otherwise put every migration, stage decision and trigger
match into the test output. `tests/support/quiet_log.cpp` registers a Catch2
listener that silences the logger for the run; a test that wants to assert on
log output uses `testing::capture_log`, which sets its own level and restores
this one afterwards.

**Golden tests** compare DECtalk's output for a few fixed phrases against a
sample count and hash in `tests/golden/dectalk.txt`. Every utterance runs on
a fresh engine, so the samples are the same on every run and in Debug and
Release alike (plan §21.16). On a mismatch the test writes the audio it got
to `tests/golden/<name>.wav` (gitignored), so the difference can be listened
to rather than guessed at. After a deliberate change, such as a DECtalk
update, rewrite the file and listen to the `.wav` files before committing it:

```powershell
$env:LATIBOT_UPDATE_GOLDEN = '1'; .\build\bin\Debug\latibot_tests.exe "[golden]"; Remove-Item Env:LATIBOT_UPDATE_GOLDEN
```

**DECtalk itself runs in the suite.** The `[audio]` engine tests start the
real engine rather than a mock: what they check is how DECtalk behaves, which
the design rests on. It needs no audio device, so they run in CI too.

**Fuzz targets** live in `tests/fuzz/` and are built by the `fuzz` preset.
They exist for the parsers that read untrusted text: `fuzz_text` for the text
helpers, `fuzz_url_scan` for the link scanner and replacement planning,
`fuzz_legacy_parser` for recognising the bot's old replacements, and
`fuzz_dectalk_sanitizer` for what reaches the speech engine. Each checks
invariants rather than just
"did not crash" — the scanner's links are in order, inside the text and
exactly what their offsets say, and spoilered exactly when an odd number of
markers outside code precede them; trimmed text has whitespace only on either side, and
text cut to a limit stays within it without splitting a character; and the
sanitizer's output, read the way DECtalk reads it, runs no command the
speaker may not, and sanitizing it again changes nothing.

libFuzzer steers by coverage, so the targets link `latibot_fuzz_core`: the
code they exercise, built again with coverage instrumentation, since
`latibot_core` cannot carry it (`tests/fuzz/CMakeLists.txt`). Each target has
a few seed inputs in `tests/fuzz/corpus/<target>/`. Pass a directory for new
inputs first, which is where libFuzzer writes, and the seeds after it:

```powershell
cmake --preset fuzz; cmake --build --preset fuzz
New-Item -ItemType Directory -Force build\build-fuzz\corpus\fuzz_url_scan
.\build\build-fuzz\bin\Debug\fuzz_url_scan.exe build\build-fuzz\corpus\fuzz_url_scan tests\fuzz\corpus\fuzz_url_scan -max_total_time=60
```

**Benchmarks** are hidden tests tagged `[!benchmark]` and `[.]`, so neither
CTest nor a plain run of the binary includes them. Run one by name or tag:

```powershell
.\build\bin\Release\latibot_tests.exe "[!benchmark]"
```

**Live tests** are tagged `[live]` **and** `[.]`. The presets filter by test
*name*, and ctest's names do not include tags, so `[live]` alone would not
keep them out; Catch2 never runs or lists a test tagged `[.]` unless asked
by name or tag. Run them with `latibot_tests.exe "[live]"`.

The first are music's (`tests/unit/yt_dlp_live_test.cpp`): real yt-dlp and
ffmpeg on a stable, freely licensed file, skipped when either program is
not installed. Discord ones will need `LATIBOT_TEST_TOKEN` plus a test
server, and cover only what real Discord can answer: modal behaviour,
audit-log timing, whether embeds actually appear.

---

## Conventions

- **Name a test after the behaviour it pins down**, as a statement:
  "a failing migration rolls back and keeps the previous version". The name is
  what a failure prints, so it should say what is broken. Keep it plain ASCII
  and do not start it with `/`: CTest passes the name on the command line,
  where Catch2 on Windows reads a leading `/` as an option and the console
  mangles anything else, so the test passes when run directly and fails under
  `ctest`. The catalog script refuses both.
- **One behaviour per test.** Use `SECTION` for variations that share setup.
- **Say why, not what, in comments.** The code shows what is asserted; a
  comment earns its place by explaining why the behaviour matters, especially
  where it encodes a decision from the porting plan.
- **A bug fix starts with a failing test.** This applies to the four URL bugs
  carried over from the Java bot: each gets a test that reproduces it first.
- **Synthetic data only.** The repository is public. Never put real Discord
  IDs, names or message contents in a fixture.
- **Tests are part of done.** A phase is not finished until its tests pass in
  CI.

---

## Tooling

| Tool | What it adds | How |
|---|---|---|
| CTest | runs the suite, integrates with the IDE | `ctest --preset debug` |
| AddressSanitizer | use-after-free, overruns, leaks | `ctest --preset asan` |
| libFuzzer | random input against the parsers | `fuzz` preset |
| clang-tidy | static analysis of our code only | `tools/Invoke-ClangTidy.ps1` over `src/`, or `src/` and `tests/` with `-IncludeTests`; `tests/.clang-tidy` adjusts it for test code |
| OpenCppCoverage | line coverage report | optional, local |
| GitHub Actions | build and test on every push | `.github/workflows/ci.yml` |

**What CI checks, and what is left to you.** On every push and pull request,
CI builds Debug, Release and AddressSanitizer and runs their test presets,
checks the test catalog is current and the formatting is clean, and scans for
secrets. The AddressSanitizer job first checks the runner's Visual Studio has
the ASan runtime, and fails saying so if an image ever drops it. clang-tidy
and fuzzing are local only, as they are slow. So before calling a change
done, run `tools/Invoke-ClangTidy.ps1` yourself, and the asan workflow too
rather than waiting for CI; the VS Code tasks do both.

CI has no Discord token and no audio device. That is deliberate: it is the
reason logic lives behind ports rather than inside event handlers.

---

## VS Code setup

`.vscode/settings.json` points the
[TestMate C++](https://marketplace.visualstudio.com/items?itemName=matepek.vscode-catch2-test-adapter)
extension at `build/build/bin/Debug/latibot_tests.exe` and groups it the way the
catalog is grouped:

```
LatiBot tests           build/build/bin/Debug/
  [db]
    tests/db/backup_test.cpp
      a backup is a complete, valid copy
      ...
```

One top-level node, so a single run button covers the whole suite.

Three things are worth knowing before editing that file:

- **`tags` is an array of tag *combinations*.** Each entry is itself an array,
  and the tags are written **without brackets** — `["db"]`, not `"[db]"`.
  TestMate's setting has no schema, so a flat list of bracketed strings is
  accepted by the editor, ignored by the extension, and the only symptom is
  that tag grouping silently does not happen. `tagFormat` puts the brackets
  back for display.
- **`groupUngroupedTo` belongs inside `groupByTags`**, not beside it. A test
  with no recognised component tag then lands under *other (missing a
  component tag)*, the visible counterpart of the check in
  `tools/Update-TestCatalog.ps1`.
- **A new component tag means editing three things**: the test, the `tags`
  array in `.vscode/settings.json`, and the table above plus the generator.

**Running a test builds it first.** `runTask.before` runs the *Build tests
(Debug)* task from `.vscode/tasks.json`, which costs about a second when
nothing has changed. Without it the sidebar re-runs whichever binary happened
to be built last, and edited code appears to have no effect.

**Only the Debug build is in the tree.** TestMate runs a binary; it does not
pick a configuration. Listing both would put two copies of every test in the
tree, whose tags and results drift apart as soon as one config is rebuilt and
the other is not. Release and ASan go through `ctest --preset release` and
`ctest --preset asan`. Widen `pattern` to `build/build/bin/*/latibot_tests.exe` if
you would rather have them in the sidebar.

**A run looks instantaneous because it is.** The whole suite takes well under
a second. To confirm a run really happened, uncomment `testMate.cpp.log.logpanel` in
`.vscode/settings.json`: the *C++ TestMate* output channel then logs every
command it spawns and the Catch2 XML it parses back.

---

## Known gaps

Worth being explicit about, so the catalog is not mistaken for coverage:

- **`bot` itself is untested.** It is the shell: it wires DPP events to the
  registry and ports. Testing it would mean mocking `dpp::cluster`, which is
  exactly what the ports exist to avoid. It stays thin instead.

  Three parts of it are no longer as thin as that claim implies, and are worth
  watching:

  - `on_component` and `on_form` hand a panel's buttons, select menus and
    modal submissions to the panel whose view name it is. The panels route
    their own, from their modules (`trigger_panel`, `url_panel`,
    `voice_lab`, `llm_panels`), and `tests/unit/panels_test.cpp` uses each
    one end to end through `support/panel_harness.hpp`: every press, choice
    and form is the JSON Discord sends, read by DPP's own interaction
    handler, and answered through DPP's own `reply` and `dialog`, which on
    DPP's webhook path hand the answer back instead of sending it. No
    connection, no token. That is what caught every form arriving empty
    (plan §21.21), which tests of hand-built events could not.
    Still untested in the shell: which panel it hands a view to (a one-line
    chain of `||`), the nickname history's and the link board's paging, the
    Retry button on a replacement, and the refusal of a form with no fields.
  - The nickname handlers. `is_new_nickname`, `describes`, `may_attribute` and
    `audit_nickname` are pure and tested; that they are hooked to the right
    events, that the guild is read correctly out of an audit entry's raw
    frame, and that the delayed audit-log fallback fires, are not.
  - The timers. That the midnight tick, the embed tracker's one-second tick,
    the log channel's two-second tick and the backup schedule are started at
    all, and at the intervals configured. The log channel's buffering,
    posting and backoff are tested against `mock_discord`; that the bot
    starts it from the stored setting at startup is not, nor that `/logs set`
    really posts, which needs an interaction.
  - The URL replacement wiring. The tracker, the reaction store and the
    backfill are tested against the mocks; that `on_message_update`,
    `on_message_delete` and the four reaction events reach them, that
    `recompute` is handed the right text channels from DPP's cache, and that
    each guild's first `guild_create` hands `settle_stranded` the replacements
    the last run left unfinished, is not.
    Whether a given mirror actually produces a preview is a question only
    real Discord answers, which is what `[live]` tests are for.
  - The voice wiring. The speech queue, voice sessions and auto-leave are
    tested against `mock_voice` and `mock_clock`, and DECtalk itself runs in
    the suite; that DPP's voice-ready, track-marker and voice-state events
    reach them, that `dpp_voice_output` queues audio DPP will actually play,
    and `/chat`'s upload being accepted as a voice message, are not. The voice
    lab's panel is tested end to end, its ▶ Test as far as the synthesizer. Nor is how the cache tells a bot from a person when
    counting who is left in a channel.
  - The language model's wiring. The stage, the responder, the memory tools
    and the stores are tested against the mocks, and the providers' JSON
    against recorded replies; that `describe` sees a mention or a reply to
    the bot (the second is read from the raw gateway frame), that the shell
    waits out the pacing, are not. The `/llm settings` panel and the
    document forms are tested end to end. Nor is
    whether the providers' real APIs still accept what is sent, which only a
    call with a key can answer; the request shapes were checked against their
    documentation in September 2026.
- **No command's `execute()` is tested.** Replying needs `event.co_reply`,
  and that needs a `dpp::cluster`, so which branch of a handler answers with
  what, and whether as a result or a refusal, is unchecked; so is that
  `/links set` actually stores the rule. What the handlers decide is pulled
  out where it matters and tested: the renderers, `plan_say` and `plan_join`,
  and the permission checks Discord cannot make for us, `urltoggle_refusal`
  and `linkstats_refusal`. That each handler calls them is not. A seam for
  replies, which `execute()` would receive, would close this.
- **A failed command's apology is only sent with a cluster behind it.** What
  it says and the flags it carries come from `command::refusal`, which is
  tested; the reply, and the follow-up when the command had already replied,
  need a real interaction.
- **`dpp_gateway`, `dpp_http_client` and `raw_api` are only partly tested.**
  Their pure parts (endpoint building) have tests; the parts that call DPP do
  not, because there is no cluster to call. They are deliberately thin
  wrappers for that reason. `[live]` tests will cover them.
- **No coverage measurement runs regularly.** OpenCppCoverage is documented
  but not wired into CI.
- **Nothing tests `main`.** It reads config, builds the bot, runs.

---

## Adding a test

1. Put it in `tests/unit/` unless it needs a database (`tests/db/`).
2. Give it one component tag and any traits that apply.
3. Name it after the behaviour.
4. Run `pwsh tools/Update-TestCatalog.ps1` and commit the regenerated catalog.
   `Invoke-ClangFormat.ps1` does this for you, since reformatting moves the
   line numbers the catalog links to.
