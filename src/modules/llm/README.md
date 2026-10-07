# The language model module

Answers when someone addresses the bot, or when an advanced trigger fires,
with a model from Anthropic or OpenAI, within a spend cap; remembers what it
chooses to; and knows people only by aliases, never by their Discord ids or
names. How it behaves, and why, is [docs/Language_Model.md](docs/Language_Model.md);
the commands' replies are in [the user guide](../../../docs/User_Guide.md#llm).

Built unless `LATIBOT_WITH_LLM` is off. Leaving it out leaves its tables and
their rows where they are, for when it is built again.

## What it owns

`tests/llm_module_test.cpp` checks the rows marked ✓ against what the module
registers, so this table cannot fall behind the code.

| What | | Checked |
|---|---|---|
| Commands | `/llm`, `/memory` | ✓ |
| Panels | `llmset`, `llmsetpick`, `llmsetform`, `llmswitch`, `llmdoc`, `memlist` | ✓ |
| Message stages | `language model`, at the model position, last: it consumes what it answers | ✓ |
| Discord events | `llm: who is typing` (typing starts), for conversation mode, which waits for people to finish; needs the typing intent, which is not privileged | ✓ |
| Timers | none | ✓ |
| Tables | `llm_usage`, `llm_documents`, `llm_memory`, `llm_memory_search` (full-text, with its three triggers), `llm_blacklist`, `llm_triggers`, `llm_aliases` | ✓ |
| Per-server settings | the llm_ keys in `src/settings.hpp`, each with its default and range | |
| Config section | `llm.provider`, `llm.model`, `llm.check_model`, `llm.spend_cap_daily_usd`, `llm.spend_cap_monthly_usd`, `llm.tool_rounds`; a model the bot cannot price stops startup | ✓ |
| Environment | ANTHROPIC_API_KEY, OPENAI_API_KEY; a provider exists only with its key, and the log channel masks both | |
| Capabilities | uses speech, when offered (dectalk): replies in a voice session's channel are spoken. Without it the model never speaks. Uses link_replacements, when offered (links): a reply to a replacement is not taken as talking to the bot. | |
| Requires | the core | |

## Files

| File | |
|---|---|
| `include/llm/module.hpp` | `make_module` and `config_defaults`, which the bot's module list calls |
| `src/module.cpp` | the module: its schema, keys, providers, commands, panels and stage |
| `src/llm_config.hpp`, `src/config_check.*` | its config section, and refusing a model it cannot price |
| `src/stage.*` | deciding whether a message is answered |
| `src/conversation.*` | conversation mode: its windows, who is typing, and hearing its name |
| `src/responder.*`, `src/prompt.*` | answering: the prompt, the tool loop, posting and speaking |
| `src/anthropic.*`, `src/openai.*`, `src/provider.hpp`, `src/models.*` | the providers and their prices |
| `src/memory.*`, `src/memory_tools.*`, `src/tools.*` | what the model remembers, as tools it calls |
| `src/aliases.*` | who people are to the model |
| `src/documents.*`, `src/guards.*`, `src/spend.*`, `src/settings.*`, `src/advanced_triggers.*` | its documents, blacklist, spending, per-server settings and advanced triggers |
| `src/llm_command.*` | `/llm` and `/memory`, and their panels |
| `tests/` | `latibot_llm_tests`; `mock_llm.hpp` stands in for a provider |
