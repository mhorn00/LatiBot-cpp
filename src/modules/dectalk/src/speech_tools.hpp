#pragma once

#include "core/discord/voice_message.hpp"
#include "core/ports/result.hpp"
#include "speak_command.hpp"

#include <dpp/coro/task.h>
#include <dpp/snowflake.h>

#include <functional>

namespace latibot::capabilities {
class model_tools;
}

namespace latibot::commands {

/// What the language model's speech tools need, Discord apart, so their
/// tests need no gateway.
struct speech_tool_services {
    speech_services speech{};

    /// The voice channel someone is in here; 0 for none.
    std::function<dpp::snowflake(dpp::snowflake guild, dpp::snowflake user)> channel_of{};

    /// The voice channel the bot is in here; 0 for none.
    std::function<dpp::snowflake(dpp::snowflake guild)> bot_channel{};

    /// Joins a channel; false when the gateway cannot be reached.
    std::function<bool(dpp::snowflake guild, dpp::snowflake channel)> connect{};

    /// Posts a voice message in a channel as the bot.
    std::function<dpp::task<ports::result<dpp::snowflake>>(dpp::snowflake channel, discord::voice_message message)> post_voice{};
};

/// `speak`, `voice_message` and `stop_speech`: the language model's /speak,
/// /chat and /tts stop and skip (src/modules/dectalk/docs/Speech.md §2.6).
/// What it writes is sanitized at its own trust level, whoever asked.
auto add_speech_tools(capabilities::model_tools& tools, speech_tool_services services) -> void;

} // namespace latibot::commands
