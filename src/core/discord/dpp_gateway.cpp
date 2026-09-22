#include "core/discord/dpp_gateway.hpp"

#include <dpp/cluster.h>

#include <algorithm>
#include <utility>

namespace latibot::discord {
namespace {

api_error to_error(const dpp::confirmation_callback_t& confirmation) {
    return api_error{.http_status = confirmation.http_info.status, .message = confirmation.get_error().message};
}

/// Pulls the payload out of a DPP confirmation, or reports the failure.
template <typename T>
result<T> unwrap(const dpp::confirmation_callback_t& confirmation) {
    if (confirmation.is_error()) {
        return to_error(confirmation);
    }
    return std::get<T>(confirmation.value);
}

} // namespace

dpp::task<result<dpp::message>> dpp_gateway::send_message(dpp::message message) {
    const auto confirmation = co_await cluster_->co_message_create(message);
    co_return unwrap<dpp::message>(confirmation);
}

dpp::task<result<dpp::message>> dpp_gateway::edit_message(dpp::message message) {
    const auto confirmation = co_await cluster_->co_message_edit(message);
    co_return unwrap<dpp::message>(confirmation);
}

dpp::task<result<void>> dpp_gateway::delete_message(dpp::snowflake channel_id, dpp::snowflake message_id) {
    const auto confirmation = co_await cluster_->co_message_delete(message_id, channel_id);
    if (confirmation.is_error()) {
        co_return to_error(confirmation);
    }
    co_return result<void>{};
}

dpp::task<result<std::vector<dpp::message>>> dpp_gateway::get_messages(dpp::snowflake channel_id, dpp::snowflake before,
                                                                       std::uint64_t limit) {
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

dpp::task<result<std::vector<dpp::snowflake>>> dpp_gateway::get_reaction_users(dpp::snowflake channel_id,
                                                                               dpp::snowflake message_id,
                                                                               std::string emoji, dpp::snowflake after,
                                                                               std::uint64_t limit) {
    const auto confirmation =
        co_await cluster_->co_message_get_reactions(message_id, channel_id, emoji, /*before=*/0, after, limit);
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
