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
    struct reaction_added {
        dpp::snowflake channel_id;
        dpp::snowflake message_id;
        std::string emoji;
    };
    std::vector<reaction_added> reactions_added;
    std::deque<ports::result<void>> reaction_results;

    auto add_reaction(dpp::snowflake channel_id, dpp::snowflake message_id, std::string emoji) -> dpp::task<ports::result<void>> override {
        reactions_added.push_back({.channel_id = channel_id, .message_id = message_id, .emoji = emoji});
        co_return next_of(reaction_results);
    }

    struct pin_change {
        dpp::snowflake channel_id;
        dpp::snowflake message_id;
        bool pinned;
    };
    std::vector<pin_change> pins;
    std::deque<ports::result<void>> pin_results;

    auto set_pinned(dpp::snowflake channel_id, dpp::snowflake message_id, bool pinned) -> dpp::task<ports::result<void>> override {
        pins.push_back({.channel_id = channel_id, .message_id = message_id, .pinned = pinned});
        co_return next_of(pin_results);
    }

    struct nickname_set {
        dpp::snowflake guild_id;
        dpp::snowflake user_id;
        std::optional<std::string> nickname;
    };
    std::vector<nickname_set> nicknames_set;
    std::deque<ports::result<void>> nickname_results;

    auto set_nickname(dpp::snowflake guild_id, dpp::snowflake user_id, std::optional<std::string> nickname)
        -> dpp::task<ports::result<void>> override {
        nicknames_set.push_back({.guild_id = guild_id, .user_id = user_id, .nickname = nickname});
        co_return next_of(nickname_results);
    }

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

    /// Server emojis uploaded, in order.
    struct guild_emoji_upload {
        dpp::snowflake guild_id;
        std::string name;
        std::string image;
        bool animated = false;
    };
    std::vector<guild_emoji_upload> guild_emoji_uploads;
    std::deque<ports::result<dpp::snowflake>> guild_emoji_results;

    auto create_guild_emoji(dpp::snowflake guild_id, std::string name, std::string image, bool animated)
        -> dpp::task<ports::result<dpp::snowflake>> override {
        guild_emoji_uploads.push_back({.guild_id = guild_id, .name = std::move(name), .image = std::move(image), .animated = animated});
        if (!guild_emoji_results.empty()) {
            auto scripted = std::move(guild_emoji_results.front());
            guild_emoji_results.pop_front();
            co_return scripted;
        }
        co_return next_id();
    }

    /// What answered interactions were replaced with, and under which token.
    std::vector<std::pair<std::string, dpp::message>> interaction_edits;

    auto edit_interaction_response(std::string token, dpp::message message) -> dpp::task<ports::result<void>> override {
        interaction_edits.emplace_back(std::move(token), std::move(message));
        co_return ports::result<void>{};
    }

    /// Server stickers uploaded, in order.
    struct guild_sticker_upload {
        dpp::snowflake guild_id;
        std::string name;
        std::string tags;
        std::string image;
        bool gif = false;
    };
    std::vector<guild_sticker_upload> guild_sticker_uploads;
    std::deque<ports::result<dpp::snowflake>> guild_sticker_results;

    auto create_guild_sticker(dpp::snowflake guild_id, std::string name, std::string tags, std::string image, bool gif)
        -> dpp::task<ports::result<dpp::snowflake>> override {
        guild_sticker_uploads.push_back(
            {.guild_id = guild_id, .name = std::move(name), .tags = std::move(tags), .image = std::move(image), .gif = gif});
        if (!guild_sticker_results.empty()) {
            auto scripted = std::move(guild_sticker_results.front());
            guild_sticker_results.pop_front();
            co_return scripted;
        }
        co_return next_id();
    }

    /// How many stickers each server has, none when not listed. A server in
    /// `sticker_count_errors` fails to say.
    std::map<dpp::snowflake, std::uint32_t> sticker_counts;
    std::map<dpp::snowflake, ports::api_error> sticker_count_errors;

    auto guild_sticker_count(dpp::snowflake guild_id) -> dpp::task<ports::result<std::uint32_t>> override {
        if (const auto failing = sticker_count_errors.find(guild_id); failing != sticker_count_errors.end()) {
            co_return std::unexpected(failing->second);
        }
        const auto found = sticker_counts.find(guild_id);
        co_return found == sticker_counts.end() ? 0U : found->second;
    }

    /// The servers the bot is in.
    std::vector<ports::emoji_server> emoji_server_list;

    [[nodiscard]] auto emoji_servers() const -> std::vector<ports::emoji_server> override { return emoji_server_list; }

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
    std::map<std::pair<dpp::snowflake, dpp::snowflake>, ports::member_details> member_info;
    std::map<dpp::snowflake, ports::server_details> servers;
    std::map<dpp::snowflake, ports::channel_details> channel_info;
    /// A member's permissions in every channel; anyone not listed has none.
    std::map<std::pair<dpp::snowflake, dpp::snowflake>, std::uint64_t> permissions;

    [[nodiscard]] auto member_details(dpp::snowflake guild_id, dpp::snowflake user_id) const
        -> std::optional<ports::member_details> override {
        const auto found = member_info.find({guild_id, user_id});
        if (found == member_info.end()) return std::nullopt;
        return found->second;
    }

    [[nodiscard]] auto server_details(dpp::snowflake guild_id) const -> std::optional<ports::server_details> override {
        const auto found = servers.find(guild_id);
        if (found == servers.end()) return std::nullopt;
        return found->second;
    }

    [[nodiscard]] auto channel_details(dpp::snowflake channel_id) const -> std::optional<ports::channel_details> override {
        const auto found = channel_info.find(channel_id);
        if (found == channel_info.end()) return std::nullopt;
        return found->second;
    }

    [[nodiscard]] auto member_permissions(dpp::snowflake guild_id, dpp::snowflake /*channel_id*/, dpp::snowflake user_id) const
        -> std::optional<std::uint64_t> override {
        const auto found = permissions.find({guild_id, user_id});
        return found == permissions.end() ? std::uint64_t{0} : found->second;
    }

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

    /// The next scripted outcome, or success.
    static auto next_of(std::deque<ports::result<void>>& scripted) -> ports::result<void> {
        if (scripted.empty()) return {};
        auto first = std::move(scripted.front());
        scripted.pop_front();
        return first;
    }

    std::uint64_t last_id_ = 1000;
};

} // namespace latibot::testing
