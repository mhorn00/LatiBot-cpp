#pragma once

#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace latibot::llm {

/// Ask the model to answer a message (docs/features/Language_Model.md).
///
/// An action of its own because answering takes a model call, maybe several,
/// and the stage that decides to answer cannot wait for them: it only
/// decides, and the pipeline hands this to the responder as a background
/// task (`carried_out_by`).
struct ask_llm {
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;
    dpp::snowflake message_id;
    dpp::snowflake author_id;
    std::string author_name;
    std::string content;
    bool author_is_bot = false;

    /// Set when an advanced trigger fired rather than someone addressing the
    /// bot: which one, and what it asks the model to say.
    std::int64_t trigger_id = 0;
    std::string context_prompt;

    /// Also say the reply in the voice session this channel belongs to
    /// (docs/features/Language_Model.md §2.5).
    bool speak = false;

    /// How long to wait before answering: bot-to-bot pacing
    /// (docs/features/Language_Model.md §2.7).
    std::chrono::seconds wait{0};
};

} // namespace latibot::llm
