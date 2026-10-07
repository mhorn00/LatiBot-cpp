#pragma once

#include <dpp/snowflake.h>

#include <string>
#include <string_view>

namespace latibot::events {

/// The message a reply replies to, as far as the reply's gateway frame says.
struct reply_target {
    /// Who wrote it; 0 when the frame does not say.
    dpp::snowflake author_id;

    /// It answered a slash command: a result or a refusal, sent as the
    /// command's response rather than posted in the channel. Replying to one
    /// does not address the bot (src/modules/llm/docs/Language_Model.md §2.1).
    bool command_output = false;
};

/// What a reply's gateway frame says about the message it replies to.
///
/// DPP reads the reference but not the message it points at, which Discord
/// sends whole in the same frame. Anything missing or malformed reads as
/// nothing known.
[[nodiscard]] auto reply_target_in(const std::string& raw_event) -> reply_target;

/// Whether `content` mentions `who` in so many words, as `<@id>` or `<@!id>`.
///
/// A reply pings the author of what it replies to unless that is turned off,
/// which puts them in the message's mentions without anyone writing one.
[[nodiscard]] auto writes_mention(std::string_view content, dpp::snowflake who) -> bool;

} // namespace latibot::events
