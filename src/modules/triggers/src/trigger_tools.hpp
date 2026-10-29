#pragma once

namespace latibot::capabilities {
class model_replies;
class model_tools;
} // namespace latibot::capabilities

namespace latibot::events {

class trigger_store;

/// `list_triggers`, `add_trigger` and `remove_trigger`: the language model's
/// /trigger (src/modules/triggers/docs/Triggers.md §2.7). Adding and
/// removing need Manage Messages of whoever asked, as /trigger does;
/// `model` names the personas, and is null without it.
auto add_trigger_tools(capabilities::model_tools& tools, trigger_store& store, const capabilities::model_replies* model = nullptr) -> void;

} // namespace latibot::events
