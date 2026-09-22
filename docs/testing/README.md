# Testing

How this project is tested, and the conventions that keep the suite readable
as it grows. The plan behind it is
[Porting_Plan_Stage2_v4.md §17](../porting/Porting_Plan_Stage2_v4.md); this
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
  support/     test helpers (temp directories, log capture)
  fuzz/        libFuzzer targets, built only by the fuzz preset
  live/        opt-in, needs a real Discord connection
  fixtures/    synthetic data files
```

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
| `[commands]` | `src/core/commands`: the registry and dispatch |
| `[discord]` | `src/core/discord`: raw API helper, gateway wrappers |
| `[ports]` | `src/core/ports` and the mocks that implement them |
| `[log]` | `src/core/util/log` |
| `[util]` | the remaining small helpers in `src/core/util`, version |

Add a new component tag when a new area appears (`[url]`, `[tts]`, `[llm]`
are expected), and register it in `tools/Update-TestCatalog.ps1` and in
`.vscode/settings.json` at the same time.

### Trait tags

| Tag | Meaning | Why it is worth filtering |
|---|---|---|
| `[coro]` | drives a coroutine | these are the ones that can hang; they all use `sync_wait_for` |
| `[threads]` | starts threads | the slowest and the most order-dependent |
| `[fs]` | writes real files | needs a writable temp directory |
| `[golden]` | compares against stored output | updated deliberately, never blindly |
| `[live]` | needs a real Discord connection | excluded from every preset; see below |

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
world mid-logic. Four ports exist — `clock`, `discord_gateway`, `http_client`,
`tts_engine` — each with a hand-written mock in `tests/mocks/`. There is no
mocking framework: a mock that fits on one screen is easier to trust.

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

**Golden tests** (from Phase 4) will compare DECtalk output against a stored
hash and sample count, and write the `.wav` next to it so a difference can be
listened to rather than guessed at.

**Fuzz targets** live in `tests/fuzz/` and are built by the `fuzz` preset.
They exist for the parsers that read untrusted text: the URL scanner, the
DECtalk sanitizer, the legacy message parser.

```powershell
cmake --preset fuzz; cmake --build build-fuzz --config Debug
.\build-fuzz\bin\Debug\fuzz_text.exe -max_total_time=60
```

**Live tests** are tagged `[live]`, excluded by every test preset, and need
`LATIBOT_TEST_TOKEN` plus a test server. They cover only what real Discord can
answer: modal behaviour, audit-log timing, whether embeds actually appear.

---

## Conventions

- **Name a test after the behaviour it pins down**, as a statement:
  "a failing migration rolls back and keeps the previous version". The name is
  what a failure prints, so it should say what is broken.
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
| clang-tidy | static analysis of our code only | `ninja-tidy` preset, then run clang-tidy over `src/` |
| OpenCppCoverage | line coverage report | optional, local |
| GitHub Actions | build and test on every push | `.github/workflows/ci.yml` |

CI has no Discord token and no audio device. That is deliberate: it is the
reason logic lives behind ports rather than inside event handlers.

---

## VS Code setup

`.vscode/settings.json` points the
[TestMate C++](https://marketplace.visualstudio.com/items?itemName=matepek.vscode-catch2-test-adapter)
extension at `build/bin/Debug/latibot_tests.exe` and groups it the way the
catalog is grouped:

```
LatiBot tests           build/bin/Debug/
  [db]
    tests/db/backup_test.cpp
      a backup is a complete, valid copy
      ...
```

One top-level node, so a single run button covers all 67 tests.

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
`ctest --preset asan`. Widen `pattern` to `build/bin/*/latibot_tests.exe` if
you would rather have them in the sidebar.

**A run looks instantaneous because it is.** All 67 tests take about 0.2 s.
To confirm a run really happened, uncomment `testMate.cpp.log.logpanel` in
`.vscode/settings.json`: the *C++ TestMate* output channel then logs every
command it spawns and the Catch2 XML it parses back.

---

## Known gaps

Worth being explicit about, so the catalog is not mistaken for coverage:

- **`bot` itself is untested.** It is the shell: it wires DPP events to the
  registry and ports. Testing it would mean mocking `dpp::cluster`, which is
  exactly what the ports exist to avoid. It stays thin instead.
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
