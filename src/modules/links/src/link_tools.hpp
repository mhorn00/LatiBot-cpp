#pragma once

namespace latibot::capabilities {
class model_tools;
}

namespace latibot::events {

class url_rule_store;

/// `fix_link`: the language model's way to give a link on the site this
/// server's rules send it to (src/modules/links/docs/Url_Replacement.md §2.8).
auto add_link_tools(capabilities::model_tools& tools, const url_rule_store& rules) -> void;

} // namespace latibot::events
