#pragma once

namespace latibot::capabilities {
class model_tools;
}

namespace latibot::events {

class midnight_store;

/// `list_midnight`, `add_midnight` and `edit_midnight`: the language model's
/// /midnight (src/modules/midnight/docs/Midnight.md §2.3). Adding and
/// editing need Manage Server of whoever asked, as /midnight does.
auto add_midnight_tools(capabilities::model_tools& tools, midnight_store& store) -> void;

} // namespace latibot::events
