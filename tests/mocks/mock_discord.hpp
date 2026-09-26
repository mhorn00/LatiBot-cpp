#pragma once

#include "core/ports/discord_gateway.hpp"

#include <deque>
#include <map>
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

    dpp::task<ports::result<dpp::message>> send_message(dpp::message message) override {
        sent.push_back(message);
        if (!send_results.empty()) {
            auto scripted = std::move(send_results.front());
            send_results.pop_front();
            co_return scripted;
        }
        message.id = next_id();
        co_return message;
    }

    dpp::task<ports::result<dpp::message>> edit_message(dpp::message message) override {
        edited.push_back(message);
        if (!edit_results.empty()) {
            auto scripted = std::move(edit_results.front());
            edit_results.pop_front();
            co_return scripted;
        }
        co_return message;
    }

    dpp::task<ports::result<void>> delete_message(dpp::snowflake channel_id, dpp::snowflake message_id) override {
        deleted.emplace_back(channel_id, message_id);
        if (!delete_results.empty()) {
            auto scripted = std::move(delete_results.front());
            delete_results.pop_front();
            co_return scripted;
        }
        co_return ports::result<void>{};
    }

    dpp::task<ports::result<void>> set_embeds_suppressed(dpp::snowflake channel_id, dpp::snowflake message_id, bool suppressed) override {
        suppressions.push_back({.channel_id = channel_id, .message_id = message_id, .suppressed = suppressed});
        if (!suppress_results.empty()) {
            auto scripted = std::move(suppress_results.front());
            suppress_results.pop_front();
            co_return scripted;
        }
        co_return ports::result<void>{};
    }

    dpp::task<ports::result<dpp::message>> get_message(dpp::snowflake /*channel_id*/, dpp::snowflake message_id) override {
        if (const auto failing = message_errors.find(message_id); failing != message_errors.end()) {
            co_return failing->second;
        }
        const auto found = stored_messages.find(message_id);
        if (found == stored_messages.end()) {
            co_return ports::api_error{.http_status = 404, .message = "Unknown Message"};
        }
        co_return found->second;
    }

    dpp::task<ports::result<std::vector<dpp::message>>> get_messages(dpp::snowflake channel_id, dpp::snowflake before,
                                                                     std::uint64_t limit) override {
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

    dpp::task<ports::result<std::vector<dpp::snowflake>>> get_reaction_users(dpp::snowflake /*channel_id*/, dpp::snowflake message_id,
                                                                             std::string emoji, dpp::snowflake after,
                                                                             std::uint64_t /*limit*/) override {
        reaction_requests.push_back({.message_id = message_id, .emoji = emoji, .after = after});
        if (!reaction_pages.empty()) {
            auto scripted = std::move(reaction_pages.front());
            reaction_pages.pop_front();
            co_return scripted;
        }
        co_return std::vector<dpp::snowflake>{};
    }

private:
    dpp::snowflake next_id() { return dpp::snowflake{++last_id_}; }

    std::uint64_t last_id_ = 1000;
};

} // namespace latibot::testing
