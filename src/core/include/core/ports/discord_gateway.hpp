#pragma once

#include "core/ports/result.hpp"

#include <dpp/coro/task.h>
#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace latibot::ports {

/// What a server member is called. Any of them may be empty.
struct member_names {
    /// Their nickname in this server.
    std::string nickname;
    /// Their display name, the same in every server.
    std::string display_name;
    /// Their account's username.
    std::string username;
};

/// A channel's webhook, which posts under any name and avatar it is given
/// (src/modules/llm/docs/Language_Model.md §3.9).
struct webhook {
    dpp::snowflake id;
    dpp::snowflake channel_id;
    std::string name;

    /// What lets it post. Discord gives it only to whoever may manage the
    /// channel's webhooks; empty otherwise.
    std::string token;

    /// The user who made it: the bot, for the bot's own.
    dpp::snowflake created_by;
};

/// The Discord calls our features make.
///
/// Deliberately small: it covers what features need today and grows as they
/// land, rather than mirroring `dpp::cluster`. Keeping it narrow is what lets
/// a mock stand in for Discord in tests.
class discord_gateway {
public:
    virtual ~discord_gateway() = default;

    discord_gateway() = default;
    discord_gateway(const discord_gateway&) = delete;
    auto operator=(const discord_gateway&) -> discord_gateway& = delete;

    virtual auto send_message(dpp::message message) -> dpp::task<result<dpp::message>> = 0;

    virtual auto edit_message(dpp::message message) -> dpp::task<result<dpp::message>> = 0;

    virtual auto delete_message(dpp::snowflake channel_id, dpp::snowflake message_id) -> dpp::task<result<void>> = 0;

    /// Turns a message's link previews off or back on, including on messages
    /// somebody else wrote, which needs Manage Messages
    /// (src/modules/links/docs/Url_Replacement.md §3.2). Only the flag changes; the
    /// message is otherwise untouched.
    virtual auto set_embeds_suppressed(dpp::snowflake channel_id, dpp::snowflake message_id, bool suppressed)
        -> dpp::task<result<void>> = 0;

    virtual auto get_message(dpp::snowflake channel_id, dpp::snowflake message_id) -> dpp::task<result<dpp::message>> = 0;

    /// Newest first, as Discord returns them. `before` of 0 starts at the
    /// most recent message.
    virtual auto get_messages(dpp::snowflake channel_id, dpp::snowflake before, std::uint64_t limit)
        -> dpp::task<result<std::vector<dpp::message>>> = 0;

    /// Who reacted with one emoji. Discord pages this 100 at a time and never
    /// reports *when* a reaction was added (src/modules/linkstats/docs/Link_Stats.md §2).
    virtual auto get_reaction_users(dpp::snowflake channel_id, dpp::snowflake message_id, std::string emoji, dpp::snowflake after,
                                    std::uint64_t limit) -> dpp::task<result<std::vector<dpp::snowflake>>> = 0;

    /// Shows "LatiBot is typing…" in a channel for ten seconds, or until the
    /// bot posts there (src/modules/llm/docs/Language_Model.md §2.3).
    virtual auto start_typing(dpp::snowflake channel_id) -> dpp::task<result<void>> = 0;

    /// Uploads an emoji the bot's application owns, which the bot can use in
    /// any server (src/modules/linkstats/docs/Link_Stats.md §10). `image` is a PNG or,
    /// when `animated`, a GIF, of at most 256 KiB. Gives the new emoji's id.
    virtual auto create_application_emoji(std::string name, std::string image, bool animated) -> dpp::task<result<dpp::snowflake>> = 0;

    virtual auto delete_application_emoji(dpp::snowflake emoji_id) -> dpp::task<result<void>> = 0;

    /// A channel's webhooks. Needs Manage Webhooks.
    virtual auto channel_webhooks(dpp::snowflake channel_id) -> dpp::task<result<std::vector<webhook>>> = 0;

    /// Makes a webhook in a channel, which a thread cannot have: its parent
    /// channel's serves it. Needs Manage Webhooks.
    virtual auto create_webhook(dpp::snowflake channel_id, std::string name) -> dpp::task<result<webhook>> = 0;

    /// Posts `message` through `hook`, shown as `username` with the image at
    /// `avatar_url`, or the webhook's own when that is empty. `thread` is the
    /// thread to post in, 0 for the webhook's channel. Gives back the posted
    /// message. A webhook message cannot be a reply.
    virtual auto execute_webhook(const webhook& hook, dpp::message message, std::string username, std::string avatar_url,
                                 dpp::snowflake thread) -> dpp::task<result<dpp::message>> = 0;

    /// What the bot's cache says a member is called, without asking
    /// Discord. Nothing when the cache has not seen them.
    [[nodiscard]] virtual auto member_names(dpp::snowflake guild_id, dpp::snowflake user_id) const
        -> std::optional<ports::member_names> = 0;

    /// A role's name, from the cache. Nothing when it is not there.
    [[nodiscard]] virtual auto role_name(dpp::snowflake role_id) const -> std::optional<std::string> = 0;

    /// A channel's name, from the cache. Nothing when it is not there.
    [[nodiscard]] virtual auto channel_name(dpp::snowflake channel_id) const -> std::optional<std::string> = 0;

    /// The channel a thread belongs to, from the cache. Nothing for a
    /// channel that is not a thread, or that the cache does not know.
    [[nodiscard]] virtual auto thread_parent(dpp::snowflake channel_id) const -> std::optional<dpp::snowflake> = 0;
};

} // namespace latibot::ports
