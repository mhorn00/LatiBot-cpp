# The triggers module

Replies to messages that match a pattern, with one of a trigger's responses
or, when the language model is built in, by asking it to answer the
trigger's prompt, at most once per cooldown in each channel. How it behaves, and why, is
[docs/Triggers.md](docs/Triggers.md); the
commands' replies are in [the user guide](../../../docs/User_Guide.md#triggers).

Built unless `LATIBOT_WITH_TRIGGERS` is off. Leaving it out leaves its tables
and their rows where they are, for when it is built again. Without the llm
module its triggers that ask the model are kept, listed, and never fire.

## What it owns

`tests/triggers_module_test.cpp` checks the rows marked ✓ against what the
module registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/trigger`; `/Fire a trigger`, a message's Apps menu command (Manage Messages): fires the trigger picked on that message | ✓ |
| Panels | `triggers` (the list's pages), `trigpanel`, `trigpick`, `trigedit`, `trigdel`, `trigyes`, `trigadd`, `trigaddmodel`, `trigform`, `trigcmdform` (the form the command's edit opens), `trigonoff`, `trigbots`, `trigsilent`, `trigprev`, `trigreply`, `trigfire` (the Apps menu's trigger menu) | ✓ |
| Message stages | `triggers`, at the reply position: after link replacement, before the language model, which a reply here keeps quiet. With the model it hears the bot's own messages too, for the triggers LatiBot and its personas set off for each other | ✓ |
| Discord events | none | ✓ |
| Timers | none | ✓ |
| Tables | `triggers`, `trigger_responses`, `trigger_imports` (which old tables were copied in: the model's advanced triggers, once, at startup) | ✓ |
| Model tools | `list_triggers`, `add_trigger`, `remove_trigger`, the language model's /trigger; adding and removing need Manage Messages of whoever asked; when the llm module is built | ✓ |
| Per-server settings | none | |
| Config section | none | ✓ |
| Environment | none | |
| Capabilities | offers none. Uses model_replies, when offered (llm): triggers that ask the model, answered by LatiBot or a persona, through every guard the model has. Uses model_tools, when offered (llm), for its tools | |
| Requires | the core | |

## Files

| File | |
|---|---|
| `include/triggers/module.hpp` | `make_module`, which the bot's module list calls |
| `src/module.cpp` | the module: its schema, command, panel and stage |
| `src/triggers.hpp`, `.cpp` | the store, and the stage that picks a reply |
| `src/trigger_command.hpp`, `.cpp` | `/trigger`, and its panel |
| `tests/` | `latibot_triggers_tests` |
