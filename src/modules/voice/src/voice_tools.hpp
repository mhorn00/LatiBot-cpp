#pragma once

#include <dpp/snowflake.h>

#include <functional>

namespace latibot::capabilities {
class model_tools;
}

namespace latibot::commands {

/// What `join_voice` and `leave_voice` need of Discord, apart so their tests
/// need no gateway.
struct voice_tool_services {
    /// The voice channel someone is in here; 0 for none.
    std::function<dpp::snowflake(dpp::snowflake guild, dpp::snowflake user)> channel_of{};

    /// The voice channel the bot is in here; 0 for none.
    std::function<dpp::snowflake(dpp::snowflake guild)> bot_channel{};

    /// Joins, or moves to, a channel; false when the gateway cannot be
    /// reached.
    std::function<bool(dpp::snowflake guild, dpp::snowflake channel)> connect{};

    /// Leaves; false when the gateway cannot be reached.
    std::function<bool(dpp::snowflake guild)> disconnect{};
};

/// `join_voice` and `leave_voice`: the language model's /join and /leave
/// (src/modules/voice/docs/Voice_Channels.md §2.4). Whoever asked needs
/// Speak, as the commands want.
auto add_voice_tools(capabilities::model_tools& tools, voice_tool_services services) -> void;

} // namespace latibot::commands
