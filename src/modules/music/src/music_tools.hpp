#pragma once

#include "music_command.hpp"

#include <dpp/snowflake.h>

#include <functional>

namespace latibot::capabilities {
class model_tools;
}

namespace latibot::commands {

/// What the language model's music tools need, Discord apart, so their
/// tests need no gateway.
struct music_tool_services {
    music_services music{};

    /// The voice channel someone is in here; 0 for none.
    std::function<dpp::snowflake(dpp::snowflake guild, dpp::snowflake user)> channel_of{};

    /// The voice channel the bot is in here; 0 for none.
    std::function<dpp::snowflake(dpp::snowflake guild)> bot_channel{};

    /// Joins a channel; false when the gateway cannot be reached.
    std::function<bool(dpp::snowflake guild, dpp::snowflake channel)> connect{};
};

/// `music_queue`, `play_music` and `music_control`: the language model's
/// /music (src/modules/music/docs/Music.md §3.6). Whoever asked needs Speak
/// to change anything, as /music wants.
auto add_music_tools(capabilities::model_tools& tools, music_tool_services services) -> void;

} // namespace latibot::commands
