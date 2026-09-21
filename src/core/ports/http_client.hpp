#pragma once

#include "core/ports/result.hpp"

#include <dpp/coro/task.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace latibot::ports {

enum class http_method : std::uint8_t { get, post, put, patch, del };

struct http_request {
    std::string url;
    http_method method = http_method::post;
    std::string body;
    std::string content_type = "application/json";

    /// A list rather than a map: header order is preserved, duplicates are
    /// allowed, and moving the request stays noexcept (MSVC's std::multimap
    /// allocates when moved, which makes the whole struct throw on move).
    std::vector<std::pair<std::string, std::string>> headers;
};

struct http_response {
    int status = 0;
    std::string body;
};

/// Outbound HTTP that is not part of the Discord API: the LLM providers
/// today, anything else later.
///
/// Tests replay recorded provider responses through a mock instead of calling
/// out (plan v4 §17.3), which also keeps CI free of API keys.
class http_client {
public:
    virtual ~http_client() = default;

    http_client() = default;
    http_client(const http_client&) = delete;
    http_client& operator=(const http_client&) = delete;

    /// A non-2xx status is still a successful call and arrives as a response;
    /// only transport failures produce an `api_error`.
    virtual dpp::task<result<http_response>> send(http_request request) = 0;
};

} // namespace latibot::ports
