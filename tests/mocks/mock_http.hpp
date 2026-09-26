#pragma once

#include "core/ports/http_client.hpp"

#include <deque>
#include <string>
#include <vector>

namespace latibot::testing {

/// Replays canned HTTP responses and records what was sent.
///
/// LLM tests run against recorded provider replies (plan v4 §17.5), so they
/// need no network and no API key.
class mock_http final : public ports::http_client {
public:
    /// Everything that was sent, in order.
    std::vector<ports::http_request> requests;

    /// Handed out in order, one per call.
    std::deque<ports::result<ports::http_response>> responses;

    void queue(int status, std::string body) { responses.emplace_back(ports::http_response{.status = status, .body = std::move(body)}); }

    void queue_error(std::string message) { responses.emplace_back(ports::api_error{.http_status = 0, .message = std::move(message)}); }

    dpp::task<ports::result<ports::http_response>> send(ports::http_request request) override {
        requests.push_back(std::move(request));

        if (responses.empty()) {
            // Failing loudly beats returning an empty 200 that a test then
            // misreads as a real answer.
            co_return ports::api_error{.http_status = 0, .message = "mock_http: no response queued for this request"};
        }

        auto next = std::move(responses.front());
        responses.pop_front();
        co_return next;
    }
};

} // namespace latibot::testing
