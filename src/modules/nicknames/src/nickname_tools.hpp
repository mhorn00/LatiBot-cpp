#pragma once

namespace latibot::capabilities {
class model_tools;
}

namespace latibot::events {

class nickname_store;

/// `nickname_history`, the language model's `/nicknames`
/// (src/modules/nicknames/docs/Nicknames.md §2.4). The nicknames themselves
/// are hidden from the model, since a name never reaches it
/// (src/modules/llm/docs/Language_Model.md §3.8): it is handed markers it
/// can write, which are put back when the reply is posted.
auto add_nickname_tools(capabilities::model_tools& tools, const nickname_store& store) -> void;

} // namespace latibot::events
