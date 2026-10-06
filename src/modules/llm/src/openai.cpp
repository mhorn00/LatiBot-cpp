#include "core/llm/openai.hpp"

#include "core/llm/json_read.hpp"

#include <format>
#include <utility>

namespace latibot::llm {
namespace {

using json = nlohmann::json;

/// A turn as Chat Completions spells it, which may be several messages: each
/// tool result is a message of its own.
auto append_messages(json& messages, const turn& entry) -> void {
    if (entry.from == speaker::assistant) {
        if (entry.raw.is_object()) {
            messages.push_back(entry.raw);
            return;
        }

        json message{{"role", "assistant"}, {"content", entry.text}};
        if (!entry.calls.empty()) {
            json calls = json::array();
            for (const tool_call& call : entry.calls) {
                calls.push_back(
                    {{"id", call.id}, {"type", "function"}, {"function", {{"name", call.name}, {"arguments", call.input.dump()}}}});
            }
            message["tool_calls"] = std::move(calls);
        }
        messages.push_back(std::move(message));
        return;
    }

    for (const tool_result& result : entry.results) {
        // Chat Completions has no error flag on a tool result, so an error
        // says so in its text.
        messages.push_back({{"role", "tool"},
                            {"tool_call_id", result.call_id},
                            {"content", result.is_error ? "error: " + result.content : result.content}});
    }
    if (!entry.text.empty()) messages.push_back({{"role", "user"}, {"content", entry.text}});
}

auto stop_of(std::string_view reason) -> stop_reason {
    if (reason == "stop") return stop_reason::finished;
    if (reason == "tool_calls") return stop_reason::tool_use;
    if (reason == "length") return stop_reason::max_tokens;
    if (reason == "content_filter") return stop_reason::refusal;
    return stop_reason::other;
}

auto error_of(int status, std::string_view body) -> ports::api_error {
    const json parsed = json::parse(body, nullptr, /*allow_exceptions=*/false);
    std::string message;
    if (parsed.is_object() && parsed.contains("error") && parsed.at("error").is_object()) {
        message = text_at(parsed.at("error"), "message");
    } else {
        message = std::string(body.substr(0, 200));
    }
    return {.http_status = status, .message = std::format("OpenAI answered {}: {}", status, message)};
}

/// A call's arguments, which arrive as JSON inside a string. Text that is not
/// JSON becomes an empty object, which the tool then refuses as missing
/// what it needs.
auto arguments_of(const json& function) -> json {
    const json parsed = json::parse(text_at(function, "arguments"), nullptr, /*allow_exceptions=*/false);
    return parsed.is_object() ? parsed : json::object();
}

} // namespace

auto openai_body(const request& call) -> nlohmann::json {
    json messages = json::array();

    std::string system = call.stable_system;
    if (!call.varying_system.empty()) {
        if (!system.empty()) system += "\n\n";
        system += call.varying_system;
    }
    if (!system.empty()) messages.push_back({{"role", "system"}, {"content", system}});

    for (const turn& entry : call.conversation) {
        append_messages(messages, entry);
    }

    json body{{"model", call.model},
              {"max_completion_tokens", call.max_output_tokens},
              {"messages", std::move(messages)},
              {"reasoning_effort", "none"}};

    if (!call.tools.empty()) {
        json tools = json::array();
        for (const tool_definition& tool : call.tools) {
            tools.push_back({{"type", "function"},
                             {"function", {{"name", tool.name}, {"description", tool.description}, {"parameters", tool.input_schema}}}});
        }
        body["tools"] = std::move(tools);
        if (!call.allow_tools) body["tool_choice"] = "none";
    }

    return body;
}

auto read_openai_reply(int status, std::string_view body) -> ports::result<response> {
    if (status < 200 || status >= 300) return std::unexpected(error_of(status, body));

    const json parsed = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_object() || !parsed.contains("choices") || !parsed.at("choices").is_array() || parsed.at("choices").empty()) {
        return std::unexpected(ports::api_error{.http_status = status, .message = "OpenAI sent a reply with no choices"});
    }

    const json& choice = parsed.at("choices").at(0);
    const json message = choice.is_object() && choice.contains("message") ? choice.at("message") : json::object();

    response reply;
    reply.reply.from = speaker::assistant;
    reply.reply.raw = message;
    reply.reply.text = text_at(message, "content");

    if (message.contains("tool_calls") && message.at("tool_calls").is_array()) {
        for (const json& call : message.at("tool_calls")) {
            const json function = call.is_object() && call.contains("function") ? call.at("function") : json::object();
            reply.reply.calls.push_back({.id = text_at(call, "id"), .name = text_at(function, "name"), .input = arguments_of(function)});
        }
    }

    reply.stop = stop_of(text_at(choice, "finish_reason"));
    if (const std::string refused = text_at(message, "refusal"); !refused.empty()) {
        reply.stop = stop_reason::refusal;
        reply.reply.text = refused;
    }

    if (parsed.contains("usage") && parsed.at("usage").is_object()) {
        const json& used = parsed.at("usage");
        const std::int64_t cached =
            used.contains("prompt_tokens_details") ? count_at(used.at("prompt_tokens_details"), "cached_tokens") : 0;
        reply.used = {.input_tokens = count_at(used, "prompt_tokens") - cached,
                      .output_tokens = count_at(used, "completion_tokens"),
                      .cache_write_tokens = 0,
                      .cache_read_tokens = cached};
    }
    return reply;
}

openai_provider::openai_provider(ports::http_client& http, std::string api_key) : http_(&http), api_key_(std::move(api_key)) {}

auto openai_provider::complete(request call) -> dpp::task<ports::result<response>> {
    // Bytes that are not UTF-8 are replaced rather than thrown on: one odd
    // character in somebody's message should not cost the reply.
    ports::http_request outgoing{.url = std::string(openai_url),
                                 .method = ports::http_method::post,
                                 .body = openai_body(call).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                                 .content_type = "application/json",
                                 .headers = {{"Authorization", "Bearer " + api_key_}}};

    const auto sent = co_await http_->send(std::move(outgoing));
    if (!sent.has_value()) co_return std::unexpected(sent.error());
    co_return read_openai_reply(sent.value().status, sent.value().body);
}

} // namespace latibot::llm
