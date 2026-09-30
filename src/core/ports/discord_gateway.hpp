#pragma once

#include "core/ports/result.hpp"

#include <dpp/coro/task.h>
#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <cstdint>
#include <string>
#include <vector>

namespace latibot::ports {

/// The Discord calls our features make.
///
/// Deliberately small: it covers what features need today and grows as they
/// land, rather than mirroring `dpp::cluster`. Keeping it narrow is what lets
/// a mock stand in for Discord in tests (plan §17.3).
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
    /// somebody else wrote, which needs Manage Messages (plan §9.2). Only
    /// the flag changes; the message is otherwise untouched.
    virtual auto set_embeds_suppressed(dpp::snowflake channel_id, dpp::snowflake message_id, bool suppressed)
        -> dpp::task<result<void>> = 0;

    virtual auto get_message(dpp::snowflake channel_id, dpp::snowflake message_id) -> dpp::task<result<dpp::message>> = 0;

    /// Newest first, as Discord returns them. `before` of 0 starts at the
    /// most recent message.
    virtual auto get_messages(dpp::snowflake channel_id, dpp::snowflake before, std::uint64_t limit)
        -> dpp::task<result<std::vector<dpp::message>>> = 0;

    /// Who reacted with one emoji. Discord pages this 100 at a time and never
    /// reports *when* a reaction was added (plan §9.7).
    virtual auto get_reaction_users(dpp::snowflake channel_id, dpp::snowflake message_id, std::string emoji, dpp::snowflake after,
                                    std::uint64_t limit) -> dpp::task<result<std::vector<dpp::snowflake>>> = 0;

    /// Shows "LatiBot is typing…" in a channel for ten seconds, or until the
    /// bot posts there (plan §14.7).
    virtual auto start_typing(dpp::snowflake channel_id) -> dpp::task<result<void>> = 0;

    /// Uploads an emoji the bot's application owns, which the bot can use in
    /// any server (docs/features/Link_Stats.md 10). `image` is a PNG or,
    /// when `animated`, a GIF, of at most 256 KiB. Gives the new emoji's id.
    virtual auto create_application_emoji(std::string name, std::string image, bool animated) -> dpp::task<result<dpp::snowflake>> = 0;

    virtual auto delete_application_emoji(dpp::snowflake emoji_id) -> dpp::task<result<void>> = 0;
};

} // namespace latibot::ports
