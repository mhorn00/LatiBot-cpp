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
    discord_gateway& operator=(const discord_gateway&) = delete;

    virtual dpp::task<result<dpp::message>> send_message(dpp::message message) = 0;

    virtual dpp::task<result<dpp::message>> edit_message(dpp::message message) = 0;

    virtual dpp::task<result<void>> delete_message(dpp::snowflake channel_id, dpp::snowflake message_id) = 0;

    /// Turns a message's link previews off or back on, including on messages
    /// somebody else wrote, which needs Manage Messages (plan §9.2). Only
    /// the flag changes; the message is otherwise untouched.
    virtual dpp::task<result<void>> set_embeds_suppressed(dpp::snowflake channel_id, dpp::snowflake message_id, bool suppressed) = 0;

    virtual dpp::task<result<dpp::message>> get_message(dpp::snowflake channel_id, dpp::snowflake message_id) = 0;

    /// Newest first, as Discord returns them. `before` of 0 starts at the
    /// most recent message.
    virtual dpp::task<result<std::vector<dpp::message>>> get_messages(dpp::snowflake channel_id, dpp::snowflake before,
                                                                      std::uint64_t limit) = 0;

    /// Who reacted with one emoji. Discord pages this 100 at a time and never
    /// reports *when* a reaction was added (plan §9.7).
    virtual dpp::task<result<std::vector<dpp::snowflake>>> get_reaction_users(dpp::snowflake channel_id, dpp::snowflake message_id,
                                                                              std::string emoji, dpp::snowflake after,
                                                                              std::uint64_t limit) = 0;
};

} // namespace latibot::ports
