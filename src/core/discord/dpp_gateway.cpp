#include "core/discord/dpp_gateway.hpp"

#include <dpp/cluster.h>

#include <algorithm>
#include <utility>

namespace latibot::discord {
namespace {

auto to_error(const dpp::confirmation_callback_t& confirmation) -> ports::api_error {
    return ports::api_error{.http_status = confirmation.http_info.status, .message = confirmation.get_error().message};
}

/// Pulls the payload out of a DPP confirmation, or reports the failure.
template <typename T>
auto unwrap(const dpp::confirmation_callback_t& confirmation) -> ports::result<T> {
    if (confirmation.is_error()) {
        return to_error(confirmation);
    }
    return std::get<T>(confirmation.value);
}

} // namespace

auto dpp_gateway::send_message(dpp::message message) -> dpp::task<ports::result<dpp::message>> {
    const auto confirmation = co_await cluster_->co_message_create(message);
    co_return unwrap<dpp::message>(confirmation);
}

auto dpp_gateway::edit_message(dpp::message message) -> dpp::task<ports::result<dpp::message>> {
    const auto confirmation = co_await cluster_->co_message_edit(message);
    co_return unwrap<dpp::message>(confirmation);
}

auto dpp_gateway::delete_message(dpp::snowflake channel_id, dpp::snowflake message_id) -> dpp::task<ports::result<void>> {
    const auto confirmation = co_await cluster_->co_message_delete(message_id, channel_id);
    if (confirmation.is_error()) {
        co_return to_error(confirmation);
    }
    co_return ports::result<void>{};
}

auto dpp_gateway::set_embeds_suppressed(dpp::snowflake channel_id, dpp::snowflake message_id, bool suppressed)
    -> dpp::task<ports::result<void>> {
    // A PATCH carrying only the flags, which is the one edit Discord allows
    // on somebody else's message. It ignores every flag but this one, so
    // sending 0 to turn previews back on clears nothing else.
    dpp::message target(channel_id, "");
    target.id = message_id;
    target.flags = suppressed ? dpp::m_suppress_embeds : 0;

    const auto confirmation = co_await cluster_->co_message_edit_flags(target);
    if (confirmation.is_error()) {
        co_return to_error(confirmation);
    }
    co_return ports::result<void>{};
}

auto dpp_gateway::get_message(dpp::snowflake channel_id, dpp::snowflake message_id) -> dpp::task<ports::result<dpp::message>> {
    const auto confirmation = co_await cluster_->co_message_get(message_id, channel_id);
    co_return unwrap<dpp::message>(confirmation);
}

auto dpp_gateway::get_messages(dpp::snowflake channel_id, dpp::snowflake before, std::uint64_t limit)
    -> dpp::task<ports::result<std::vector<dpp::message>>> {
    const auto confirmation = co_await cluster_->co_messages_get(channel_id, /*around=*/0, before, /*after=*/0, limit);
    if (confirmation.is_error()) {
        co_return to_error(confirmation);
    }

    // DPP hands back a map keyed by id; callers want them newest first, which
    // is the order Discord sends and the order the backfill walks.
    const auto& messages = std::get<dpp::message_map>(confirmation.value);
    std::vector<dpp::message> ordered;
    ordered.reserve(messages.size());
    for (const auto& [id, message] : messages) {
        ordered.push_back(message);
    }
    std::ranges::sort(ordered, [](const dpp::message& lhs, const dpp::message& rhs) { return lhs.id > rhs.id; });
    co_return ordered;
}

auto dpp_gateway::get_reaction_users(dpp::snowflake channel_id, dpp::snowflake message_id, std::string emoji, dpp::snowflake after,
                                     std::uint64_t limit) -> dpp::task<ports::result<std::vector<dpp::snowflake>>> {
    const auto confirmation = co_await cluster_->co_message_get_reactions(message_id, channel_id, emoji, /*before=*/0, after, limit);
    if (confirmation.is_error()) {
        co_return to_error(confirmation);
    }

    const auto& users = std::get<dpp::user_map>(confirmation.value);
    std::vector<dpp::snowflake> ids;
    ids.reserve(users.size());
    for (const auto& [id, user] : users) {
        ids.push_back(id);
    }
    co_return ids;
}

} // namespace latibot::discord
