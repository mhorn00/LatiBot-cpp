#include "core/discord/raw_api.hpp"

#include <dpp/cluster.h>
#include <dpp/discordclient.h>
#include <dpp/json.h>

#include <format>
#include <string>
#include <utility>
#include <vector>

namespace latibot::discord {
namespace {

auto to_dpp(ports::http_method method) -> dpp::http_method {
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

auto to_result(const rest_reply& reply) -> ports::result<nlohmann::json> {
    const auto& [body, http] = reply;

    if (http.error != dpp::h_success) {
        return std::unexpected(ports::api_error{.http_status = http.status,
                                                .message = "HTTP transport error " + std::to_string(static_cast<int>(http.error))});
    }

    if (http.status >= 400) {
        // Discord explains itself in the body; a bare status is rarely enough
        // to tell what went wrong with a hand-built request.
        return std::unexpected(ports::api_error{.http_status = http.status, .message = describe_refusal(http.status, body)});
    }

    return body;
}

/// Each field `errors` names, as `path: message`, depth first. Discord
/// nests them by field, and each field's own are in `_errors`. Recursive,
/// as deep as Discord's nesting, which is a handful of levels.
// NOLINTNEXTLINE(misc-no-recursion)
auto field_errors(const nlohmann::json& errors, const std::string& path, std::vector<std::string>& found) -> void {
    if (!errors.is_object()) return;
    for (const auto& [key, value] : errors.items()) {
        if (key == "_errors" && value.is_array()) {
            for (const nlohmann::json& each : value) {
                if (each.is_object() && each.contains("message") && each["message"].is_string()) {
                    found.push_back(std::format("{}: {}", path.empty() ? std::string("(body)") : path, each["message"].get<std::string>()));
                }
            }
            continue;
        }
        field_errors(value, path.empty() ? key : std::format("{}.{}", path, key), found);
    }
}

} // namespace

auto describe_refusal(int status, const nlohmann::json& body) -> std::string {
    std::string message = "Discord returned " + std::to_string(status);
    if (!body.is_object()) return message;
    if (body.contains("message") && body["message"].is_string()) message += ": " + body["message"].get<std::string>();
    if (const auto errors = body.find("errors"); errors != body.end()) {
        std::vector<std::string> found;
        field_errors(*errors, {}, found);
        for (const std::string& each : found) {
            message += "; " + each;
        }
    }
    return message;
}

auto build_endpoint(std::string_view path) -> std::string {
    std::string endpoint(path);

    if (!endpoint.starts_with("/api/")) {
        if (!endpoint.starts_with('/')) endpoint.insert(endpoint.begin(), '/');
        endpoint.insert(0, API_PATH);
    }

    // DPP appends "/" + parameters only when parameters are given, so a
    // trailing slash here would reach Discord as-is.
    while (endpoint.size() > 1 && endpoint.ends_with('/')) {
        endpoint.pop_back();
    }
    return endpoint;
}

auto raw_api::request(ports::http_method method, std::string path, std::string body) -> dpp::task<ports::result<nlohmann::json>> {
    const std::string endpoint = build_endpoint(path);

    const auto reply = co_await dpp::async<rest_reply>{[&](auto&& complete) {
        cluster_->post_rest(
            endpoint, "", "", to_dpp(method), body,
            [complete](nlohmann::json& parsed, const dpp::http_request_completion_t& http) mutable { complete(rest_reply{parsed, http}); });
    }};

    co_return to_result(reply);
}

auto raw_api::multipart(ports::http_method method, std::string path, std::string payload_json, std::vector<dpp::message_file_data> files)
    -> dpp::task<ports::result<nlohmann::json>> {
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
