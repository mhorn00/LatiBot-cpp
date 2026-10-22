#include "discord/dpp_gateway.hpp"

#include <dpp/cache.h>
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
    if (confirmation.is_error()) return std::unexpected(to_error(confirmation));
    return std::get<T>(confirmation.value);
}

auto to_webhook(const dpp::webhook& hook) -> ports::webhook {
    return {.id = hook.id, .channel_id = hook.channel_id, .name = hook.name, .token = hook.token, .created_by = hook.user_obj.id};
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
    if (confirmation.is_error()) co_return std::unexpected(to_error(confirmation));
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
    if (confirmation.is_error()) co_return std::unexpected(to_error(confirmation));
    co_return ports::result<void>{};
}

auto dpp_gateway::get_message(dpp::snowflake channel_id, dpp::snowflake message_id) -> dpp::task<ports::result<dpp::message>> {
    const auto confirmation = co_await cluster_->co_message_get(message_id, channel_id);
    co_return unwrap<dpp::message>(confirmation);
}

auto dpp_gateway::get_messages(dpp::snowflake channel_id, dpp::snowflake before, std::uint64_t limit)
    -> dpp::task<ports::result<std::vector<dpp::message>>> {
    const auto confirmation = co_await cluster_->co_messages_get(channel_id, /*around=*/0, before, /*after=*/0, limit);
    if (confirmation.is_error()) co_return std::unexpected(to_error(confirmation));

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
    if (confirmation.is_error()) co_return std::unexpected(to_error(confirmation));

    const auto& users = std::get<dpp::user_map>(confirmation.value);
    std::vector<dpp::snowflake> ids;
    ids.reserve(users.size());
    for (const auto& [id, user] : users) {
        ids.push_back(id);
    }
    co_return ids;
}

auto dpp_gateway::start_typing(dpp::snowflake channel_id) -> dpp::task<ports::result<void>> {
    const auto confirmation = co_await cluster_->co_channel_typing(channel_id);
    if (confirmation.is_error()) co_return std::unexpected(to_error(confirmation));
    co_return ports::result<void>{};
}

auto dpp_gateway::create_application_emoji(std::string name, std::string image, bool animated) -> dpp::task<ports::result<dpp::snowflake>> {
    dpp::emoji made(name);
    try {
        // DPP refuses an image over Discord's 256 KiB here, before sending.
        made.load_image(image, animated ? dpp::i_gif : dpp::i_png);
    } catch (const dpp::exception& error) {
        co_return std::unexpected(ports::api_error{.http_status = 0, .message = error.what()});
    }

    const auto confirmation = co_await cluster_->co_application_emoji_create(made);
    if (confirmation.is_error()) co_return std::unexpected(to_error(confirmation));
    co_return std::get<dpp::emoji>(confirmation.value).id;
}

auto dpp_gateway::delete_application_emoji(dpp::snowflake emoji_id) -> dpp::task<ports::result<void>> {
    const auto confirmation = co_await cluster_->co_application_emoji_delete(emoji_id);
    if (confirmation.is_error()) co_return std::unexpected(to_error(confirmation));
    co_return ports::result<void>{};
}

auto dpp_gateway::channel_webhooks(dpp::snowflake channel_id) -> dpp::task<ports::result<std::vector<ports::webhook>>> {
    const auto confirmation = co_await cluster_->co_get_channel_webhooks(channel_id);
    if (confirmation.is_error()) co_return std::unexpected(to_error(confirmation));

    std::vector<ports::webhook> found;
    for (const auto& [id, hook] : std::get<dpp::webhook_map>(confirmation.value)) {
        found.push_back(to_webhook(hook));
    }
    co_return found;
}

auto dpp_gateway::create_webhook(dpp::snowflake channel_id, std::string name) -> dpp::task<ports::result<ports::webhook>> {
    dpp::webhook wanted;
    wanted.channel_id = channel_id;
    wanted.name = std::move(name);
    const auto confirmation = co_await cluster_->co_create_webhook(wanted);
    if (confirmation.is_error()) co_return std::unexpected(to_error(confirmation));
    co_return to_webhook(std::get<dpp::webhook>(confirmation.value));
}

auto dpp_gateway::execute_webhook(const ports::webhook& hook, dpp::message message, std::string username, std::string avatar_url,
                                  dpp::snowflake thread) -> dpp::task<ports::result<dpp::message>> {
    // DPP sends the webhook's name and avatar URL as this message's username
    // and avatar, so one webhook serves every name.
    dpp::webhook through(hook.id, hook.token);
    through.name = std::move(username);
    through.avatar_url = std::move(avatar_url);

    // Waited on, so Discord sends the message back, and its id with it.
    const auto confirmation = co_await cluster_->co_execute_webhook(through, message, /*wait=*/true, thread);
    co_return unwrap<dpp::message>(confirmation);
}

auto dpp_gateway::member_names(dpp::snowflake guild_id, dpp::snowflake user_id) const -> std::optional<ports::member_names> {
    ports::member_names names;
    if (const dpp::user* user = dpp::find_user(user_id); user != nullptr) {
        names.display_name = user->global_name;
        names.username = user->username;
    }
    if (const dpp::guild* guild = dpp::find_guild(guild_id); guild != nullptr) {
        if (const auto member = guild->members.find(user_id); member != guild->members.end()) {
            names.nickname = member->second.get_nickname();
        }
    }
    if (names.nickname.empty() && names.display_name.empty() && names.username.empty()) return std::nullopt;
    return names;
}

auto dpp_gateway::role_name(dpp::snowflake role_id) const -> std::optional<std::string> {
    const dpp::role* role = dpp::find_role(role_id);
    if (role == nullptr) return std::nullopt;
    return role->name;
}

auto dpp_gateway::channel_name(dpp::snowflake channel_id) const -> std::optional<std::string> {
    const dpp::channel* channel = dpp::find_channel(channel_id);
    if (channel == nullptr) return std::nullopt;
    return channel->name;
}

auto dpp_gateway::thread_parent(dpp::snowflake channel_id) const -> std::optional<dpp::snowflake> {
    const dpp::channel* channel = dpp::find_channel(channel_id);
    if (channel == nullptr) return std::nullopt;
    const dpp::channel_type type = channel->get_type();
    if (type != dpp::CHANNEL_PUBLIC_THREAD && type != dpp::CHANNEL_PRIVATE_THREAD && type != dpp::CHANNEL_ANNOUNCEMENT_THREAD) {
        return std::nullopt;
    }
    return channel->parent_id;
}

} // namespace latibot::discord
