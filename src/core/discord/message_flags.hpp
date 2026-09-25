#pragma once

#include <dpp/message.h>

#include <cstdint>
#include <string>

namespace latibot::discord {

/// Message flags, as DPP stores them.
using message_flags = std::uint16_t;

/// The flags worth choosing for a message the bot posts in a channel: no
/// notification, which is what Discord's @silent sets, and no link previews.
/// The rest are either Discord's to set, or only mean something on a reply to
/// a command.
inline constexpr message_flags channel_message_flags = dpp::m_suppress_notifications | dpp::m_suppress_embeds;

/// Those, plus ephemeral: what a reply to a command may carry. Ephemeral only
/// exists for replies; an ordinary message cannot be seen by one person.
inline constexpr message_flags reply_flags = channel_message_flags | dpp::m_ephemeral;

/// `flags` narrowed to `channel_message_flags`, which is how a stored message
/// setting is read and written: a value from a hand edit, or from a later
/// build that allowed more, cannot bring in a flag this one does not expect.
[[nodiscard]] constexpr message_flags channel_flags(std::int64_t flags) noexcept {
    return static_cast<message_flags>(flags & channel_message_flags);
}

/// Replaces the flags in `choosable` with `wanted`, leaving every other flag
/// as it was. `wanted` outside `choosable` is dropped rather than applied.
dpp::message& apply_flags(dpp::message& message, message_flags wanted, message_flags choosable = reply_flags);

/// "ephemeral, silent, no previews", or "none": the flags this bot chooses,
/// named for the log and for lists. Anything else is written in hex.
[[nodiscard]] std::string describe_flags(message_flags flags);

} // namespace latibot::discord
