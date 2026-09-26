#include "core/discord/message_flags.hpp"

#include <array>
#include <format>
#include <string_view>
#include <utility>

namespace latibot::discord {

auto apply_flags(dpp::message& message, message_flags wanted, message_flags choosable) -> dpp::message& {
    message.flags = static_cast<message_flags>((message.flags & ~choosable) | (wanted & choosable));
    return message;
}

auto describe_flags(message_flags flags) -> std::string {
    static constexpr std::array<std::pair<message_flags, std::string_view>, 3> names{{
        {dpp::m_ephemeral, "ephemeral"},
        {dpp::m_suppress_notifications, "silent"},
        {dpp::m_suppress_embeds, "no previews"},
    }};

    std::string text;
    message_flags remaining = flags;
    for (const auto& [bit, name] : names) {
        if ((remaining & bit) == 0) {
            continue;
        }
        if (!text.empty()) {
            text += ", ";
        }
        text += name;
        remaining = static_cast<message_flags>(remaining & ~bit);
    }

    if (remaining != 0) {
        if (!text.empty()) {
            text += ", ";
        }
        text += std::format("0x{:x}", remaining);
    }
    return text.empty() ? std::string("none") : text;
}

} // namespace latibot::discord
