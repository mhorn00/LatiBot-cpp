#pragma once

#include <dpp/snowflake.h>

namespace latibot::capabilities {

/// Which of the bot's messages are link replacements, for the language
/// model: a reply to one comments on the post, and is not talking to the bot
/// (src/modules/llm/docs/Language_Model.md §2.1). Offered by the links
/// module; without it, no message is one.
class link_replacements {
public:
    virtual ~link_replacements() = default;

    link_replacements() = default;
    link_replacements(const link_replacements&) = delete;
    auto operator=(const link_replacements&) -> link_replacements& = delete;

    /// Whether `message_id` is a replacement, or a post the links module
    /// counts as one. Reads one indexed row, so it is cheap enough for every
    /// reply. Safe from any thread.
    [[nodiscard]] virtual auto is_replacement(dpp::snowflake message_id) const -> bool = 0;
};

} // namespace latibot::capabilities
