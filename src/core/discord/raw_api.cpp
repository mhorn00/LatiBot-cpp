#include "core/discord/raw_api.hpp"

#include <dpp/cluster.h>
#include <dpp/discordclient.h>
#include <dpp/json.h>

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

/// What DPP hands back: the parsed body plus the transport result.
using rest_reply = std::pair<nlohmann::json, dpp::http_request_completion_t>;

ports::result<nlohmann::json> to_result(const rest_reply& reply) {
    const auto& [body, http] = reply;

    if (http.error != dpp::h_success) {
        return ports::api_error{.http_status = http.status,
                                .message = "HTTP transport error " + std::to_string(static_cast<int>(http.error))};
    }

    if (http.status >= 400) {
        // Discord explains itself in the body; a bare status is rarely enough
        // to tell what went wrong with a hand-built request.
        std::string message = "Discord returned " + std::to_string(http.status);
        if (body.contains("message") && body["message"].is_string()) {
            message += ": " + body["message"].get<std::string>();
        }
        return ports::api_error{.http_status = http.status, .message = message};
    }

    return body;
}

} // namespace

std::string build_endpoint(std::string_view path) {
    std::string endpoint(path);

    if (!endpoint.starts_with("/api/")) {
        if (!endpoint.starts_with('/')) {
            endpoint.insert(endpoint.begin(), '/');
        }
        endpoint.insert(0, API_PATH);
    }

    // DPP appends "/" + parameters only when parameters are given, so a
    // trailing slash here would reach Discord as-is.
    while (endpoint.size() > 1 && endpoint.ends_with('/')) {
        endpoint.pop_back();
    }
    return endpoint;
}

dpp::task<ports::result<nlohmann::json>> raw_api::request(ports::http_method method, std::string path, std::string body) {
    const std::string endpoint = build_endpoint(path);

    const auto reply = co_await dpp::async<rest_reply>{[&](auto&& complete) {
        cluster_->post_rest(
            endpoint, "", "", to_dpp(method), body,
            [complete](nlohmann::json& parsed, const dpp::http_request_completion_t& http) mutable { complete(rest_reply{parsed, http}); });
    }};

    co_return to_result(reply);
}

dpp::task<ports::result<nlohmann::json>> raw_api::multipart(ports::http_method method, std::string path, std::string payload_json,
                                                            std::vector<dpp::message_file_data> files) {
    const std::string endpoint = build_endpoint(path);

    const auto reply = co_await dpp::async<rest_reply>{[&](auto&& complete) {
        cluster_->post_rest_multipart(
            endpoint, "", "", to_dpp(method), payload_json,
            [complete](nlohmann::json& parsed, const dpp::http_request_completion_t& http) mutable { complete(rest_reply{parsed, http}); },
            files);
    }};

    co_return to_result(reply);
}

} // namespace latibot::discord
