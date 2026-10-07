#include "anthropic.hpp"

#include "json_read.hpp"
#include "models.hpp"

#include <format>
#include <utility>

namespace latibot::llm {
namespace {

using json = nlohmann::json;

/// What chat replies are asked to spend. Thinking stays on for the models
/// that think, which is what they are tuned for, but short
/// (src/modules/llm/docs/Language_Model.md §3.2).
constexpr std::string_view chat_effort = "low";

auto text_block(std::string_view text) -> json {
    return {{"type", "text"}, {"text", text}};
}

/// A turn as the Messages API spells it. An assistant turn the API wrote is
/// sent back exactly as it came, thinking blocks and all.
auto message_of(const turn& entry) -> json {
    if (entry.from == speaker::assistant) {
        if (entry.raw.is_array()) return {{"role", "assistant"}, {"content", entry.raw}};

        json content = json::array();
        if (!entry.text.empty()) content.push_back(text_block(entry.text));
        for (const tool_call& call : entry.calls) {
            content.push_back({{"type", "tool_use"}, {"id", call.id}, {"name", call.name}, {"input", call.input}});
        }
        return {{"role", "assistant"}, {"content", content}};
    }

    // Tool results first: the API wants them before anything else in the
    // turn that answers the calls.
    json content = json::array();
    for (const tool_result& result : entry.results) {
        content.push_back(
            {{"type", "tool_result"}, {"tool_use_id", result.call_id}, {"content", result.content}, {"is_error", result.is_error}});
    }
    if (!entry.text.empty()) content.push_back(text_block(entry.text));
    return {{"role", "user"}, {"content", content}};
}

auto stop_of(std::string_view reason) -> stop_reason {
    if (reason == "end_turn" || reason == "stop_sequence") return stop_reason::finished;
    if (reason == "tool_use") return stop_reason::tool_use;
    if (reason == "max_tokens") return stop_reason::max_tokens;
    if (reason == "refusal") return stop_reason::refusal;
    return stop_reason::other;
}

/// The API's own description of an error, which says far more than the
/// status: "prompt is too long", "model not found", "overloaded".
auto error_of(int status, std::string_view body) -> ports::api_error {
    const json parsed = json::parse(body, nullptr, /*allow_exceptions=*/false);
    std::string message;
    if (parsed.is_object() && parsed.contains("error") && parsed["error"].is_object()) {
        const json& error = parsed["error"];
        message = std::format("{}: {}", text_at(error, "type"), text_at(error, "message"));
    } else {
        message = std::string(body.substr(0, 200));
    }
    return {.http_status = status, .message = std::format("Anthropic answered {}: {}", status, message)};
}

} // namespace

auto anthropic_body(const request& call) -> nlohmann::json {
    json body{{"model", call.model}, {"max_tokens", call.max_output_tokens}};

    json system = json::array();
    if (!call.stable_system.empty()) {
        json block = text_block(call.stable_system);
        block["cache_control"] = {{"type", "ephemeral"}};
        system.push_back(std::move(block));
    }
    if (!call.varying_system.empty()) system.push_back(text_block(call.varying_system));
    if (!system.empty()) body["system"] = std::move(system);

    json messages = json::array();
    for (const turn& entry : call.conversation) {
        messages.push_back(message_of(entry));
    }
    body["messages"] = std::move(messages);

    if (!call.tools.empty()) {
        json tools = json::array();
        for (const tool_definition& tool : call.tools) {
            tools.push_back({{"name", tool.name}, {"description", tool.description}, {"input_schema", tool.input_schema}});
        }
        body["tools"] = std::move(tools);
        if (!call.allow_tools) body["tool_choice"] = {{"type", "none"}};
    }

    const model_info* model = find_model(call.model);
    if (model != nullptr && model->takes_effort) body["output_config"] = {{"effort", chat_effort}};

    return body;
}

auto read_anthropic_reply(int status, std::string_view body) -> ports::result<response> {
    if (status < 200 || status >= 300) return std::unexpected(error_of(status, body));

    const json parsed = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_object() || !parsed.contains("content") || !parsed["content"].is_array()) {
        return std::unexpected(ports::api_error{.http_status = status, .message = "Anthropic sent a reply with no content"});
    }

    response reply;
    reply.reply.from = speaker::assistant;
    reply.reply.raw = parsed["content"];

    // Thinking blocks stay in `raw` and nowhere else: their text is not for
    // the channel, and with the default display it is empty anyway.
    for (const json& block : parsed["content"]) {
        const std::string type = text_at(block, "type");
        if (type == "text") {
            if (!reply.reply.text.empty()) reply.reply.text += "\n";
            reply.reply.text += text_at(block, "text");
        } else if (type == "tool_use") {
            reply.reply.calls.push_back({.id = text_at(block, "id"),
                                         .name = text_at(block, "name"),
                                         .input = block.contains("input") ? block.at("input") : json::object()});
        }
    }

    reply.stop = stop_of(text_at(parsed, "stop_reason"));
    if (parsed.contains("usage") && parsed["usage"].is_object()) {
        const json& used = parsed["usage"];
        reply.used = {.input_tokens = count_at(used, "input_tokens"),
                      .output_tokens = count_at(used, "output_tokens"),
                      .cache_write_tokens = count_at(used, "cache_creation_input_tokens"),
                      .cache_read_tokens = count_at(used, "cache_read_input_tokens")};
    }
    return reply;
}

anthropic_provider::anthropic_provider(ports::http_client& http, std::string api_key) : http_(&http), api_key_(std::move(api_key)) {}

auto anthropic_provider::complete(request call) -> dpp::task<ports::result<response>> {
    // Bytes that are not UTF-8 are replaced rather than thrown on: one odd
    // character in somebody's message should not cost the reply.
    ports::http_request outgoing{.url = std::string(anthropic_url),
                                 .method = ports::http_method::post,
                                 .body = anthropic_body(call).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                                 .content_type = "application/json",
                                 .headers = {{"x-api-key", api_key_}, {"anthropic-version", std::string(anthropic_version)}}};

    const auto sent = co_await http_->send(std::move(outgoing));
    if (!sent.has_value()) co_return std::unexpected(sent.error());
    co_return read_anthropic_reply(sent.value().status, sent.value().body);
}

} // namespace latibot::llm
