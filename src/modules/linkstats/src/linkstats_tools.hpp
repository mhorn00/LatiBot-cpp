#pragma once

namespace latibot::capabilities {
class model_tools;
}

namespace latibot::config {
class guild_settings;
}

namespace latibot::events {

class reaction_store;

/// `link_stats`, the language model's `/linkstats`
/// (src/modules/linkstats/docs/Link_Stats.md §1.1): the boards, or one
/// person's count, with the same filters. People are shown to it by alias.
auto add_linkstats_tools(capabilities::model_tools& tools, const reaction_store& store, const config::guild_settings* settings) -> void;

} // namespace latibot::events
