#include "core/discord/dpp_http_client.hpp"

#include <dpp/cluster.h>

#include <map>
#include <string>
#include <utility>

namespace latibot::discord {
namespace {

dpp::http_method to_dpp(ports::http_method method) {
    switch (method) {
    case ports::http_method::get:
        return dpp::m_get;
    case ports::http_method::post:
        return dpp::m_post;
    case ports::http_method::put:
        return dpp::m_put;
    case ports::http_method::patch:
        return dpp::m_patch;
    case ports::http_method::del:
        return dpp::m_delete;
    }
    return dpp::m_get;
}

} // namespace

dpp::task<result<ports::http_response>> dpp_http_client::send(ports::http_request request) {
    // DPP takes headers as a multimap; ours are an ordered list (see the port).
    std::multimap<std::string, std::string> headers;
    for (const auto& [name, value] : request.headers) {
        headers.emplace(name, value);
    }

    const auto completion = co_await cluster_->co_request(
        request.url, to_dpp(request.method), request.body, request.content_type, headers);

    // A non-2xx reply is a response, not a failure: providers report rate
    // limits and refusals that way and callers need the body. Only a
    // transport-level problem becomes an error.
    if (completion.error != dpp::h_success) {
        co_return api_error{.http_status = completion.status,
                            .message = "HTTP transport error " +
                                       std::to_string(static_cast<int>(completion.error))};
    }

    co_return ports::http_response{.status = completion.status, .body = completion.body};
}

} // namespace latibot::discord
