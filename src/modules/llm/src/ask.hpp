#pragma once

#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace latibot::llm {

/// How a message came to the model (src/modules/llm/docs/Language_Model.md
/// §2.1, §2.6, §2.10).
enum class approach : std::uint8_t {
    /// Somebody mentioned it, replied to it, or started with its name.
    addressed,
    /// Somebody named it elsewhere in the message: answered like being
    /// addressed, once a check agrees it was talking to the bot, or at once
    /// during a conversation.
    named,
    /// During a conversation, a message nobody addressed it in, which a
    /// check thought was for it. It may still choose to say nothing.
    joined_in,
    /// An advanced trigger fired.
    trigger,
};

/// Ask the model to answer a message (src/modules/llm/docs/Language_Model.md).
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

    /// The message it replies to, shown to the model beside it; 0 when it is
    /// not a reply (src/modules/llm/docs/Language_Model.md §2.3).
    dpp::snowflake reply_to;

    approach how = approach::addressed;

    /// Whether the cheap check has to agree before the model answers
    /// (src/modules/llm/docs/Language_Model.md §2.10). The rate limits are
    /// taken only once it has.
    bool check_first = false;

    /// For an advanced trigger: which one, and what it asks the model to say.
    std::int64_t trigger_id = 0;
    std::string context_prompt;

    /// Also say the reply in the voice session this channel belongs to
    /// (src/modules/llm/docs/Language_Model.md §2.5).
    bool speak = false;

    /// How long to wait before answering: bot-to-bot pacing
    /// (src/modules/llm/docs/Language_Model.md §2.7).
    std::chrono::seconds wait{0};
};

/// Whether whoever wrote it was talking to the bot: they are notified of
/// the reply, and told if the model fails.
[[nodiscard]] constexpr auto spoke_to_bot(const ask_llm& ask) noexcept -> bool {
    return ask.how == approach::addressed || ask.how == approach::named;
}

} // namespace latibot::llm
