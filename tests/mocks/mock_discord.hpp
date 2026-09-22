#pragma once

#include "core/ports/discord_gateway.hpp"

#include <deque>
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

    /// Scripted outcomes. When a queue is empty a sensible default is used.
    std::deque<result<dpp::message>> send_results;
    std::deque<result<dpp::message>> edit_results;
    std::deque<result<void>> delete_results;
    std::deque<result<std::vector<dpp::message>>> message_pages;
    std::deque<result<std::vector<dpp::snowflake>>> reaction_pages;

    dpp::task<result<dpp::message>> send_message(dpp::message message) override {
        sent.push_back(message);
        if (!send_results.empty()) {
            auto scripted = std::move(send_results.front());
            send_results.pop_front();
            co_return scripted;
        }
        message.id = next_id();
        co_return message;
    }

    dpp::task<result<dpp::message>> edit_message(dpp::message message) override {
        edited.push_back(message);
        if (!edit_results.empty()) {
            auto scripted = std::move(edit_results.front());
            edit_results.pop_front();
            co_return scripted;
        }
        co_return message;
    }

    dpp::task<result<void>> delete_message(dpp::snowflake channel_id, dpp::snowflake message_id) override {
        deleted.emplace_back(channel_id, message_id);
        if (!delete_results.empty()) {
            auto scripted = std::move(delete_results.front());
            delete_results.pop_front();
            co_return scripted;
        }
        co_return result<void>{};
    }

    dpp::task<result<std::vector<dpp::message>>> get_messages(dpp::snowflake channel_id, dpp::snowflake before,
                                                              std::uint64_t limit) override {
        history_requests.push_back({channel_id, before, limit});
        if (!message_pages.empty()) {
            auto scripted = std::move(message_pages.front());
            message_pages.pop_front();
            co_return scripted;
        }
        // An empty page means "no more history", which is how a backfill
        // knows to stop.
        co_return std::vector<dpp::message>{};
    }

    dpp::task<result<std::vector<dpp::snowflake>>> get_reaction_users(dpp::snowflake /*channel_id*/,
                                                                      dpp::snowflake /*message_id*/,
                                                                      std::string /*emoji*/, dpp::snowflake /*after*/,
                                                                      std::uint64_t /*limit*/) override {
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
