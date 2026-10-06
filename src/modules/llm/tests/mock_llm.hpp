#pragma once

#include "provider.hpp"

#include <deque>
#include <string>
#include <vector>

namespace latibot::testing {

/// A model that answers from a script, and remembers what it was asked.
///
/// Below the providers' own JSON: those are tested against recorded replies
/// through `mock_http`, so what uses a provider can be tested on the
/// conversation alone.
class mock_llm final : public llm::provider {
public:
    std::vector<llm::request> requests;
    std::deque<ports::result<llm::response>> replies;

    /// A reply that says `text` and stops.
    auto answer(std::string text,
                llm::usage used = {.input_tokens = 100, .output_tokens = 10, .cache_write_tokens = 0, .cache_read_tokens = 0}) -> void {
        replies.emplace_back(
            llm::response{.reply = {.from = llm::speaker::assistant, .text = std::move(text), .calls = {}, .results = {}, .raw = {}},
                          .stop = llm::stop_reason::finished,
                          .used = used});
    }

    /// A reply that asks for one tool.
    auto call_tool(std::string name, nlohmann::json input, std::string id = "call_1") -> void {
        replies.emplace_back(
            llm::response{.reply = {.from = llm::speaker::assistant,
                                    .text = {},
                                    .calls = {{.id = std::move(id), .name = std::move(name), .input = std::move(input)}},
                                    .results = {},
                                    .raw = {}},
                          .stop = llm::stop_reason::tool_use,
                          .used = {.input_tokens = 100, .output_tokens = 10, .cache_write_tokens = 0, .cache_read_tokens = 0}});
    }

    auto fail(std::string message, int status = 500) -> void {
        replies.emplace_back(std::unexpected(ports::api_error{.http_status = status, .message = std::move(message)}));
    }

    [[nodiscard]] auto name() const -> std::string_view override { return "mock"; }

    auto complete(llm::request call) -> dpp::task<ports::result<llm::response>> override {
        requests.push_back(std::move(call));
        if (replies.empty()) {
            co_return std::unexpected(ports::api_error{.http_status = 0, .message = "mock_llm: no reply scripted for this request"});
        }
        auto next = std::move(replies.front());
        replies.pop_front();
        co_return next;
    }
};

} // namespace latibot::testing
