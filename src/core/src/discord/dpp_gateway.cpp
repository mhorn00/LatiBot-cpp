#include "discord/dpp_gateway.hpp"

#include <dpp/cache.h>
#include <dpp/cluster.h>
#include <dpp/json.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <shared_mutex>
#include <string_view>
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

auto done(const dpp::confirmation_callback_t& confirmation) -> ports::result<void> {
    if (confirmation.is_error()) return std::unexpected(to_error(confirmation));
    return {};
}

auto seconds(double unix_time) -> std::chrono::sys_seconds {
    return std::chrono::sys_seconds{std::chrono::seconds{static_cast<std::int64_t>(unix_time)}};
}

/// Role names, highest first: by position, then by id, as Discord orders
/// two roles at one position.
auto role_names(const std::vector<dpp::snowflake>& ids) -> std::vector<std::string> {
    std::vector<const dpp::role*> found;
    for (const dpp::snowflake id : ids) {
        if (const dpp::role* role = dpp::find_role(id); role != nullptr) found.push_back(role);
    }
    std::ranges::sort(found, [](const dpp::role* left, const dpp::role* right) {
        return left->position != right->position ? left->position > right->position : left->id < right->id;
    });
    std::vector<std::string> names;
    names.reserve(found.size());
    for (const dpp::role* role : found) {
        names.push_back(role->name);
    }
    return names;
}

auto kind_of(dpp::channel_type type) -> std::string {
    switch (type) {
    case dpp::CHANNEL_TEXT:
        return "text";
    case dpp::CHANNEL_VOICE:
        return "voice";
    case dpp::CHANNEL_ANNOUNCEMENT:
        return "announcement";
    case dpp::CHANNEL_FORUM:
    case dpp::CHANNEL_MEDIA:
        return "forum";
    case dpp::CHANNEL_STAGE:
        return "stage";
    case dpp::CHANNEL_PUBLIC_THREAD:
    case dpp::CHANNEL_PRIVATE_THREAD:
    case dpp::CHANNEL_ANNOUNCEMENT_THREAD:
        return "thread";
    case dpp::CHANNEL_CATEGORY:
        return "category";
    default:
        return "other";
    }
}

auto describe_channel(const dpp::channel& channel) -> ports::channel_details {
    std::string parent;
    if (const dpp::channel* above = channel.parent_id.empty() ? nullptr : dpp::find_channel(channel.parent_id); above != nullptr) {
        parent = above->name;
    }
    return {.id = channel.id,
            .guild_id = channel.guild_id,
            .name = channel.name,
            .kind = kind_of(channel.get_type()),
            .topic = channel.topic,
            .age_restricted = channel.is_nsfw(),
            .parent = parent};
}

/// How many stickers a server's boosts allow.
auto sticker_slots(dpp::guild_premium_tier_t tier) -> std::uint32_t {
    switch (tier) {
    case dpp::tier_1:
        return 15;
    case dpp::tier_2:
        return 30;
    case dpp::tier_3:
        return 60;
    case dpp::tier_none:
        break;
    }
    return 5;
}

/// How many still, and how many animated, emoji a server's boosts allow.
auto emoji_slots(dpp::guild_premium_tier_t tier) -> std::uint32_t {
    switch (tier) {
    case dpp::tier_1:
        return 100;
    case dpp::tier_2:
        return 150;
    case dpp::tier_3:
        return 250;
    case dpp::tier_none:
        break;
    }
    return 50;
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

auto dpp_gateway::create_guild_emoji(dpp::snowflake guild_id, std::string name, std::string image, bool animated)
    -> dpp::task<ports::result<dpp::snowflake>> {
    dpp::emoji made(name);
    try {
        // DPP refuses an image over Discord's 256 KiB here, before sending.
        made.load_image(image, animated ? dpp::i_gif : dpp::i_png);
    } catch (const dpp::exception& error) {
        co_return std::unexpected(ports::api_error{.http_status = 0, .message = error.what()});
    }

    const auto confirmation = co_await cluster_->co_guild_emoji_create(guild_id, made);
    if (confirmation.is_error()) co_return std::unexpected(to_error(confirmation));
    co_return std::get<dpp::emoji>(confirmation.value).id;
}

auto dpp_gateway::edit_interaction_response(std::string token, dpp::message message) -> dpp::task<ports::result<void>> {
    const auto confirmation = co_await cluster_->co_interaction_response_edit(token, message);
    co_return done(confirmation);
}

auto dpp_gateway::create_guild_sticker(dpp::snowflake guild_id, std::string name, std::string tags, std::string image, bool gif)
    -> dpp::task<ports::result<dpp::snowflake>> {
    // By hand, since cluster::guild_sticker_create sends the fields as
    // payload_json, and Discord documents this endpoint as taking form fields.
    constexpr std::string_view boundary = "latibot-sticker-upload-boundary";
    const std::string_view format = gif ? "gif" : "png";
    const std::array<std::pair<std::string_view, std::string_view>, 3> fields{{{"name", name}, {"description", ""}, {"tags", tags}}};

    std::string body;
    for (const auto& [field, value] : fields) {
        body += std::format("--{}\r\nContent-Disposition: form-data; name=\"{}\"\r\n\r\n{}\r\n", boundary, field, value);
    }
    body += std::format("--{}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"sticker.{}\"\r\nContent-Type: image/{}\r\n\r\n",
                        boundary, format, format);
    body += image;
    body += std::format("\r\n--{}--\r\n", boundary);

    const dpp::http_request_completion_t answer = co_await cluster_->co_request(
        std::format("https://discord.com/api/v10/guilds/{}/stickers", guild_id), dpp::m_post, body,
        std::format("multipart/form-data; boundary={}", boundary), {{"Authorization", "Bot " + cluster_->token}});

    const nlohmann::json parsed = nlohmann::json::parse(answer.body, nullptr, false);
    const auto text = [&parsed](const char* key) {
        return parsed.is_object() && parsed.contains(key) && parsed[key].is_string() ? parsed[key].get<std::string>() : std::string{};
    };
    if (answer.status / 100 != 2) {
        const std::string reason = text("message");
        co_return std::unexpected(ports::api_error{.http_status = static_cast<int>(answer.status),
                                                   .message = reason.empty() ? std::format("status {}", answer.status) : reason});
    }
    co_return dpp::snowflake(text("id"));
}

auto dpp_gateway::guild_sticker_count(dpp::snowflake guild_id) -> dpp::task<ports::result<std::uint32_t>> {
    const auto confirmation = co_await cluster_->co_guild_stickers_get(guild_id);
    if (confirmation.is_error()) co_return std::unexpected(to_error(confirmation));
    co_return static_cast<std::uint32_t>(std::get<dpp::sticker_map>(confirmation.value).size());
}

auto dpp_gateway::emoji_servers() const -> std::vector<ports::emoji_server> {
    std::vector<ports::emoji_server> servers;

    dpp::cache<dpp::guild>* guilds = dpp::get_guild_cache();
    const std::shared_lock lock(guilds->get_mutex());
    for (const auto& [id, guild] : guilds->get_container()) {
        if (guild == nullptr) continue;

        ports::emoji_server server{.id = guild->id,
                                   .name = guild->name,
                                   .still_emoji = 0,
                                   .animated_emoji = 0,
                                   .slots = emoji_slots(guild->premium_tier),
                                   .sticker_slots = guild->has_more_stickers() ? 60 : sticker_slots(guild->premium_tier),
                                   .bot_can_add = true};
        for (const dpp::snowflake emoji_id : guild->emojis) {
            const dpp::emoji* emoji = dpp::find_emoji(emoji_id);
            if (emoji == nullptr) continue;
            ++(emoji->is_animated() ? server.animated_emoji : server.still_emoji);
        }
        // Without Server Members the bot's own member record may be absent.
        if (const auto self = guild->members.find(cluster_->me.id); self != guild->members.end()) {
            const dpp::permission granted = guild->base_permissions(self->second);
            server.bot_can_add = granted.can(dpp::p_manage_emojis_and_stickers) || granted.can(dpp::p_create_guild_expressions);
        }
        servers.push_back(std::move(server));
    }
    return servers;
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

auto dpp_gateway::add_reaction(dpp::snowflake channel_id, dpp::snowflake message_id, std::string emoji) -> dpp::task<ports::result<void>> {
    const auto confirmation = co_await cluster_->co_message_add_reaction(message_id, channel_id, emoji);
    co_return done(confirmation);
}

auto dpp_gateway::set_pinned(dpp::snowflake channel_id, dpp::snowflake message_id, bool pinned) -> dpp::task<ports::result<void>> {
    const auto confirmation =
        pinned ? co_await cluster_->co_message_pin(channel_id, message_id) : co_await cluster_->co_message_unpin(channel_id, message_id);
    co_return done(confirmation);
}

auto dpp_gateway::set_nickname(dpp::snowflake guild_id, dpp::snowflake user_id, std::optional<std::string> nickname)
    -> dpp::task<ports::result<void>> {
    dpp::guild_member member;
    member.guild_id = guild_id;
    member.user_id = user_id;
    member.set_nickname(nickname.value_or(std::string{}));
    const auto confirmation = co_await cluster_->co_guild_edit_member(member);
    co_return done(confirmation);
}

auto dpp_gateway::member_details(dpp::snowflake guild_id, dpp::snowflake user_id) const -> std::optional<ports::member_details> {
    const dpp::guild* guild = dpp::find_guild(guild_id);
    if (guild == nullptr) return std::nullopt;
    const auto member = guild->members.find(user_id);
    if (member == guild->members.end()) return std::nullopt;

    const dpp::user* user = dpp::find_user(user_id);
    ports::member_details details{.joined_at = std::nullopt,
                                  .created_at = seconds(user_id.get_creation_time()),
                                  .roles = role_names(member->second.get_roles()),
                                  .is_bot = user != nullptr && user->is_bot(),
                                  .owns_server = guild->owner_id == user_id,
                                  .boosting_since = std::nullopt};
    if (member->second.joined_at > 0) details.joined_at = seconds(static_cast<double>(member->second.joined_at));
    if (member->second.premium_since > 0) details.boosting_since = seconds(static_cast<double>(member->second.premium_since));
    return details;
}

auto dpp_gateway::server_details(dpp::snowflake guild_id) const -> std::optional<ports::server_details> {
    const dpp::guild* guild = dpp::find_guild(guild_id);
    if (guild == nullptr) return std::nullopt;

    ports::server_details details{.name = guild->name,
                                  .created_at = seconds(guild->get_creation_time()),
                                  .member_count = guild->member_count,
                                  .boost_tier = static_cast<int>(guild->premium_tier),
                                  .boosts = guild->premium_subscription_count,
                                  .channels = {},
                                  .roles = role_names(guild->roles),
                                  .emoji = {}};
    for (const dpp::snowflake id : guild->channels) {
        if (const dpp::channel* channel = dpp::find_channel(id); channel != nullptr) details.channels.push_back(describe_channel(*channel));
    }
    for (const dpp::snowflake id : guild->emojis) {
        if (const dpp::emoji* emoji = dpp::find_emoji(id); emoji != nullptr) details.emoji.push_back(std::format("{}:{}", emoji->name, id));
    }
    return details;
}

auto dpp_gateway::channel_details(dpp::snowflake channel_id) const -> std::optional<ports::channel_details> {
    const dpp::channel* channel = dpp::find_channel(channel_id);
    if (channel == nullptr) return std::nullopt;
    return describe_channel(*channel);
}

auto dpp_gateway::member_permissions(dpp::snowflake guild_id, dpp::snowflake channel_id, dpp::snowflake user_id) const
    -> std::optional<std::uint64_t> {
    const dpp::guild* guild = dpp::find_guild(guild_id);
    const dpp::channel* channel = dpp::find_channel(channel_id);
    if (guild == nullptr || channel == nullptr) return std::nullopt;
    const auto member = guild->members.find(user_id);
    if (member == guild->members.end()) return std::nullopt;
    return static_cast<std::uint64_t>(guild->permission_overwrites(member->second, *channel));
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
