#pragma once

namespace latibot::capabilities {
class model_tools;
}

namespace latibot::events {

class trigger_store;

/// `list_triggers`, `add_trigger` and `remove_trigger`: the language model's
/// /trigger (src/modules/triggers/docs/Triggers.md §2.6). Adding and
/// removing need Manage Messages of whoever asked, as /trigger does.
auto add_trigger_tools(capabilities::model_tools& tools, trigger_store& store) -> void;

} // namespace latibot::events
