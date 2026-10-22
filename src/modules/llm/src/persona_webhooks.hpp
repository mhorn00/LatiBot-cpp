#pragma once

#include "core/ports/discord_gateway.hpp"

#include <dpp/coro/task.h>
#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace latibot::llm {

/// What the bot calls the webhook it makes in a channel for its personas.
inline constexpr std::string_view persona_webhook_name = "LatiBot personas";

/// Posts as a persona: through the bot's own webhook in the channel, found
/// or made the first time, and kept in memory, so its token is never written
/// down (src/modules/llm/docs/Language_Model.md §3.9). A thread posts
/// through its parent channel's. Thread-safe.
class persona_webhooks {
public:
    /// `bot_id` says which webhooks are the bot's own; `secret` is handed
    /// each token, so the log channel masks it.
    persona_webhooks(ports::discord_gateway& discord, std::function<dpp::snowflake()> bot_id, std::function<void(std::string)> secret);

    /// Posts `message` in `channel` as `username`, with the image at
    /// `avatar_url`. A webhook someone deleted is found or made again, once.
    auto post(dpp::snowflake channel, dpp::message message, std::string username, std::string avatar_url)
        -> dpp::task<ports::result<dpp::message>>;

private:
    /// The bot's webhook in `channel`, from the cache, the channel's list,
    /// or made.
    auto hook_for(dpp::snowflake channel) -> dpp::task<ports::result<ports::webhook>>;

    [[nodiscard]] auto cached(dpp::snowflake channel) -> std::optional<ports::webhook>;
    auto keep(dpp::snowflake channel, const ports::webhook& hook) -> void;
    auto forget(dpp::snowflake channel) -> void;

    ports::discord_gateway* discord_;
    std::function<dpp::snowflake()> bot_id_;
    std::function<void(std::string)> secret_;

    std::mutex mutex_;
    std::map<dpp::snowflake, ports::webhook> hooks_;
};

} // namespace latibot::llm
