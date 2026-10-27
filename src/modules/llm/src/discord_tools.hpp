#pragma once

#include "core/ports/discord_gateway.hpp"
#include "tools.hpp"

#include <dpp/coro/task.h>
#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace latibot::capabilities {
class bot_status;
class nickname_changes;
} // namespace latibot::capabilities

namespace latibot::llm {

class reminder_store;

/// What the Discord tools work with (src/modules/llm/docs/Language_Model.md §3.4).
struct discord_tool_services {
    ports::discord_gateway* discord = nullptr;

    /// Nickname changes on the record, when the nicknames module is built
    /// in; null otherwise, and then they are made but not recorded.
    capabilities::nickname_changes* nicknames = nullptr;

    /// The bot's status; null leaves `set_status` refusing.
    capabilities::bot_status* status = nullptr;

    reminder_store* reminders = nullptr;

    /// Whether a server is in `trusted_guilds`: only those may set the
    /// status, which every server sees.
    std::function<bool(dpp::snowflake guild)> trusted;

    std::function<dpp::snowflake()> bot_id;

    /// Posts a message in its channel as LatiBot, `persona` 0, or as a
    /// persona, through its webhook.
    std::function<dpp::task<ports::result<dpp::message>>(dpp::message message, std::int64_t persona, dpp::snowflake guild)> post;
};

/// A message the model names: its id alone, in the channel being answered
/// in, or a link to one in this server.
struct message_place {
    dpp::snowflake channel_id;
    dpp::snowflake message_id;
};

/// Reads `text` as a message id or a link, refusing a link to another
/// server or to a DM.
[[nodiscard]] auto find_message_place(std::string_view text, dpp::snowflake guild, dpp::snowflake channel)
    -> std::expected<message_place, std::string>;

/// An emoji as a reaction takes it: Unicode as it is, and a custom one,
/// written `:name:`, `name` or `<:name:id>`, as `name:id` when this server
/// has it. Nothing for a custom one it does not.
[[nodiscard]] auto reaction_emoji(std::string_view written, const std::optional<ports::server_details>& server)
    -> std::optional<std::string>;

/// `get_message`, `search_messages`, `user_info`, `server_info`,
/// `channel_info` and `current_time`, which look things up; and
/// `set_nickname`, `set_status`, `add_reaction`, `pin_message`,
/// `create_poll` and `set_reminder`, which change something, and refuse when
/// the message being answered is a bot's.
auto add_discord_tools(tool_registry& tools, discord_tool_services services) -> void;

/// Posts the reminders that are due, by whoever set them, then forgets them.
auto deliver_reminders(const discord_tool_services& services, std::chrono::sys_seconds now) -> dpp::task<void>;

} // namespace latibot::llm
