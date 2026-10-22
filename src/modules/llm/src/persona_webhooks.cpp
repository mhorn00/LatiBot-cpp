#include "persona_webhooks.hpp"

#include "core/util/log.hpp"

#include <algorithm>
#include <utility>

namespace latibot::llm {
namespace {

/// What Discord answers for a webhook that was deleted, or whose token no
/// longer works: worth finding or making it again.
auto webhook_gone(const ports::api_error& error) -> bool {
    return error.http_status == 404 || error.http_status == 401;
}

} // namespace

persona_webhooks::persona_webhooks(ports::discord_gateway& discord, std::function<dpp::snowflake()> bot_id,
                                   std::function<void(std::string)> secret)
    : discord_(&discord), bot_id_(std::move(bot_id)), secret_(std::move(secret)) {}

auto persona_webhooks::post(dpp::snowflake channel, dpp::message message, std::string username, std::string avatar_url)
    -> dpp::task<ports::result<dpp::message>> {
    const std::optional<dpp::snowflake> parent = discord_->thread_parent(channel);
    const dpp::snowflake owner = parent.value_or(channel);
    const dpp::snowflake thread = parent ? channel : dpp::snowflake{};

    for (int attempt = 0;; ++attempt) {
        const auto hook = co_await hook_for(owner);
        if (!hook.has_value()) co_return std::unexpected(hook.error());

        auto posted = co_await discord_->execute_webhook(hook.value(), message, username, avatar_url, thread);
        if (posted.has_value() || attempt > 0 || !webhook_gone(posted.error())) co_return posted;

        util::log().info("the persona webhook in channel {} is gone ({}); finding or making it again", owner, posted.error().message);
        forget(owner);
    }
}

auto persona_webhooks::hook_for(dpp::snowflake channel) -> dpp::task<ports::result<ports::webhook>> {
    if (auto known = cached(channel)) co_return *known;

    const auto listed = co_await discord_->channel_webhooks(channel);
    if (!listed.has_value()) co_return std::unexpected(listed.error());

    const dpp::snowflake me = bot_id_();
    const auto own = std::ranges::find_if(listed.value(), [&](const ports::webhook& hook) {
        return hook.created_by == me && !hook.token.empty() && hook.name == persona_webhook_name;
    });
    if (own != listed.value().end()) {
        keep(channel, *own);
        co_return *own;
    }

    auto made = co_await discord_->create_webhook(channel, std::string(persona_webhook_name));
    if (!made.has_value()) co_return std::unexpected(made.error());
    util::log().info("made the persona webhook in channel {}", channel);
    keep(channel, made.value());
    co_return made.value();
}

auto persona_webhooks::cached(dpp::snowflake channel) -> std::optional<ports::webhook> {
    const std::scoped_lock lock(mutex_);
    const auto found = hooks_.find(channel);
    if (found == hooks_.end()) return std::nullopt;
    return found->second;
}

auto persona_webhooks::keep(dpp::snowflake channel, const ports::webhook& hook) -> void {
    // Never logged; masked in case anything ever does.
    if (secret_) secret_(hook.token);
    const std::scoped_lock lock(mutex_);
    hooks_.insert_or_assign(channel, hook);
}

auto persona_webhooks::forget(dpp::snowflake channel) -> void {
    const std::scoped_lock lock(mutex_);
    hooks_.erase(channel);
}

} // namespace latibot::llm
