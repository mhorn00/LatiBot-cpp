#pragma once

#include "core/ports/discord_gateway.hpp"

#include <deque>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace latibot::testing {

/// Stands in for Discord: records what a feature did, and hands back whatever
/// the test scripted.
///
/// By default a send succeeds and echoes the message back with a fresh id, so
/// a test only has to script the cases it cares about (a failure, a page of
/// history, a set of reactors).
class mock_discord final : public ports::discord_gateway {
public:
    std::vector<dpp::message> sent;
    std::vector<dpp::message> edited;
    std::vector<std::pair<dpp::snowflake, dpp::snowflake>> deleted;

    struct history_request {
        dpp::snowflake channel_id;
        dpp::snowflake before;
        std::uint64_t limit;
    };
    std::vector<history_request> history_requests;

    struct suppression {
        dpp::snowflake channel_id;
        dpp::snowflake message_id;
        bool suppressed;
    };
    std::vector<suppression> suppressions;

    struct reaction_request {
        dpp::snowflake message_id;
        std::string emoji;
        dpp::snowflake after;
    };
    std::vector<reaction_request> reaction_requests;

    /// Scripted outcomes. When a queue is empty a sensible default is used.
    std::deque<ports::result<dpp::message>> send_results;
    std::deque<ports::result<dpp::message>> edit_results;
    std::deque<ports::result<void>> delete_results;
    std::deque<ports::result<void>> suppress_results;
    std::deque<ports::result<std::vector<dpp::message>>> message_pages;
    std::deque<ports::result<std::vector<dpp::snowflake>>> reaction_pages;

    /// What `get_message` finds, by id. Anything else is a 404, as it would
    /// be for a message that has been deleted, unless `message_errors` says
    /// what else to fail with.
    std::map<dpp::snowflake, dpp::message> stored_messages;
    std::map<dpp::snowflake, ports::api_error> message_errors;

    auto send_message(dpp::message message) -> dpp::task<ports::result<dpp::message>> override {
        sent.push_back(message);
        if (!send_results.empty()) {
            auto scripted = std::move(send_results.front());
            send_results.pop_front();
            co_return scripted;
        }
        message.id = next_id();
        co_return message;
    }

    auto edit_message(dpp::message message) -> dpp::task<ports::result<dpp::message>> override {
        edited.push_back(message);
        if (!edit_results.empty()) {
            auto scripted = std::move(edit_results.front());
            edit_results.pop_front();
            co_return scripted;
        }
        co_return message;
    }

    auto delete_message(dpp::snowflake channel_id, dpp::snowflake message_id) -> dpp::task<ports::result<void>> override {
        deleted.emplace_back(channel_id, message_id);
        if (!delete_results.empty()) {
            auto scripted = std::move(delete_results.front());
            delete_results.pop_front();
            co_return scripted;
        }
        co_return ports::result<void>{};
    }

    auto set_embeds_suppressed(dpp::snowflake channel_id, dpp::snowflake message_id, bool suppressed)
        -> dpp::task<ports::result<void>> override {
        suppressions.push_back({.channel_id = channel_id, .message_id = message_id, .suppressed = suppressed});
        if (!suppress_results.empty()) {
            auto scripted = std::move(suppress_results.front());
            suppress_results.pop_front();
            co_return scripted;
        }
        co_return ports::result<void>{};
    }

    auto get_message(dpp::snowflake /*channel_id*/, dpp::snowflake message_id) -> dpp::task<ports::result<dpp::message>> override {
        if (const auto failing = message_errors.find(message_id); failing != message_errors.end()) {
            co_return std::unexpected(failing->second);
        }
        const auto found = stored_messages.find(message_id);
        if (found == stored_messages.end()) co_return std::unexpected(ports::api_error{.http_status = 404, .message = "Unknown Message"});
        co_return found->second;
    }

    auto get_messages(dpp::snowflake channel_id, dpp::snowflake before, std::uint64_t limit)
        -> dpp::task<ports::result<std::vector<dpp::message>>> override {
        history_requests.push_back({.channel_id = channel_id, .before = before, .limit = limit});
        if (!message_pages.empty()) {
            auto scripted = std::move(message_pages.front());
            message_pages.pop_front();
            co_return scripted;
        }
        // An empty page means "no more history", which is how a backfill
        // knows to stop.
        co_return std::vector<dpp::message>{};
    }

    auto get_reaction_users(dpp::snowflake /*channel_id*/, dpp::snowflake message_id, std::string emoji, dpp::snowflake after,
                            std::uint64_t /*limit*/) -> dpp::task<ports::result<std::vector<dpp::snowflake>>> override {
        reaction_requests.push_back({.message_id = message_id, .emoji = emoji, .after = after});
        if (!reaction_pages.empty()) {
            auto scripted = std::move(reaction_pages.front());
            reaction_pages.pop_front();
            co_return scripted;
        }
        co_return std::vector<dpp::snowflake>{};
    }

    /// Channels a typing indicator was started in, in order.
    std::vector<dpp::snowflake> typing;

    auto start_typing(dpp::snowflake channel_id) -> dpp::task<ports::result<void>> override {
        typing.push_back(channel_id);
        co_return ports::result<void>{};
    }

    /// Application emojis uploaded, and deleted, in order.
    struct emoji_upload {
        std::string name;
        std::string image;
        bool animated = false;
    };
    std::vector<emoji_upload> emoji_uploads;
    std::vector<dpp::snowflake> emoji_deletes;
    std::deque<ports::result<dpp::snowflake>> emoji_upload_results;
    std::deque<ports::result<void>> emoji_delete_results;

    auto create_application_emoji(std::string name, std::string image, bool animated) -> dpp::task<ports::result<dpp::snowflake>> override {
        emoji_uploads.push_back({.name = std::move(name), .image = std::move(image), .animated = animated});
        if (!emoji_upload_results.empty()) {
            auto scripted = std::move(emoji_upload_results.front());
            emoji_upload_results.pop_front();
            co_return scripted;
        }
        co_return next_id();
    }

    auto delete_application_emoji(dpp::snowflake emoji_id) -> dpp::task<ports::result<void>> override {
        emoji_deletes.push_back(emoji_id);
        if (!emoji_delete_results.empty()) {
            auto scripted = std::move(emoji_delete_results.front());
            emoji_delete_results.pop_front();
            co_return scripted;
        }
        co_return ports::result<void>{};
    }

    /// Each channel's webhooks, as `channel_webhooks` lists them and
    /// `create_webhook` adds to them; a channel in `webhook_errors` fails
    /// both with that error.
    std::map<dpp::snowflake, std::vector<ports::webhook>> webhooks;
    std::map<dpp::snowflake, ports::api_error> webhook_errors;
    std::vector<std::pair<dpp::snowflake, std::string>> webhooks_created;

    /// Every message posted through a webhook, in order, with whom it was
    /// posted as.
    struct webhook_post {
        ports::webhook hook;
        dpp::message message;
        std::string username;
        std::string avatar_url;
        dpp::snowflake thread;
    };
    std::vector<webhook_post> webhook_posts;
    std::deque<ports::result<dpp::message>> webhook_results;

    /// The user the mock's webhooks are made by: the bot, as a test sets it.
    dpp::snowflake webhook_creator{1};

    auto channel_webhooks(dpp::snowflake channel_id) -> dpp::task<ports::result<std::vector<ports::webhook>>> override {
        if (const auto failing = webhook_errors.find(channel_id); failing != webhook_errors.end())
            co_return std::unexpected(failing->second);
        const auto found = webhooks.find(channel_id);
        co_return found == webhooks.end() ? std::vector<ports::webhook>{} : found->second;
    }

    auto create_webhook(dpp::snowflake channel_id, std::string name) -> dpp::task<ports::result<ports::webhook>> override {
        webhooks_created.emplace_back(channel_id, name);
        if (const auto failing = webhook_errors.find(channel_id); failing != webhook_errors.end())
            co_return std::unexpected(failing->second);
        const ports::webhook made{.id = next_id(),
                                  .channel_id = channel_id,
                                  .name = std::move(name),
                                  .token = std::format("token-{}", last_id_),
                                  .created_by = webhook_creator};
        webhooks[channel_id].push_back(made);
        co_return made;
    }

    auto execute_webhook(const ports::webhook& hook, dpp::message message, std::string username, std::string avatar_url,
                         dpp::snowflake thread) -> dpp::task<ports::result<dpp::message>> override {
        webhook_posts.push_back(
            {.hook = hook, .message = message, .username = username, .avatar_url = std::move(avatar_url), .thread = thread});
        if (!webhook_results.empty()) {
            auto scripted = std::move(webhook_results.front());
            webhook_results.pop_front();
            co_return scripted;
        }
        message.id = next_id();
        message.webhook_id = hook.id;
        message.author.id = hook.id;
        message.author.username = std::move(username);
        co_return message;
    }

    /// Threads and the channel each belongs to.
    std::map<dpp::snowflake, dpp::snowflake> thread_parents;

    [[nodiscard]] auto thread_parent(dpp::snowflake channel_id) const -> std::optional<dpp::snowflake> override {
        const auto found = thread_parents.find(channel_id);
        if (found == thread_parents.end()) return std::nullopt;
        return found->second;
    }

    /// What the cache knows: members by (guild, user), roles and channels.
    std::map<std::pair<dpp::snowflake, dpp::snowflake>, ports::member_names> members;
    std::map<dpp::snowflake, std::string> roles;
    std::map<dpp::snowflake, std::string> channels;

    [[nodiscard]] auto member_names(dpp::snowflake guild_id, dpp::snowflake user_id) const -> std::optional<ports::member_names> override {
        const auto found = members.find({guild_id, user_id});
        if (found == members.end()) return std::nullopt;
        return found->second;
    }

    [[nodiscard]] auto role_name(dpp::snowflake role_id) const -> std::optional<std::string> override {
        const auto found = roles.find(role_id);
        if (found == roles.end()) return std::nullopt;
        return found->second;
    }

    [[nodiscard]] auto channel_name(dpp::snowflake channel_id) const -> std::optional<std::string> override {
        const auto found = channels.find(channel_id);
        if (found == channels.end()) return std::nullopt;
        return found->second;
    }

private:
    auto next_id() -> dpp::snowflake { return dpp::snowflake{++last_id_}; }

    std::uint64_t last_id_ = 1000;
};

} // namespace latibot::testing
