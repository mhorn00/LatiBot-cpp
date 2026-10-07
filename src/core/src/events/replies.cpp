#include "events/replies.hpp"

#include <nlohmann/json.hpp>

#include <format>

namespace latibot::events {
namespace {

/// Discord's message types for a slash command's response and a context
/// menu command's.
constexpr int chat_input_command = 20;
constexpr int context_menu_command = 23;

auto answers_a_command(const nlohmann::json& message) -> bool {
    // `interaction_metadata` is what Discord sends now; `interaction` is the
    // older field, still sent beside it.
    for (const char* field : {"interaction_metadata", "interaction"}) {
        const auto found = message.find(field);
        if (found != message.end() && found->is_object()) return true;
    }
    const auto type = message.find("type");
    return type != message.end() && type->is_number_integer() &&
           (type->get<int>() == chat_input_command || type->get<int>() == context_menu_command);
}

} // namespace

auto reply_target_in(const std::string& raw_event) -> reply_target {
    const auto frame = nlohmann::json::parse(raw_event, nullptr, /*allow_exceptions=*/false);
    if (frame.is_discarded() || !frame.is_object() || !frame.contains("d")) return {};

    const auto& payload = frame.at("d");
    if (!payload.is_object()) return {};
    const auto referenced = payload.find("referenced_message");
    if (referenced == payload.end() || !referenced->is_object()) return {};

    reply_target target{.author_id = {}, .command_output = answers_a_command(*referenced)};
    const auto author = referenced->find("author");
    if (author == referenced->end() || !author->is_object()) return target;
    const auto id = author->find("id");
    if (id == author->end() || !id->is_string()) return target;
    target.author_id = dpp::snowflake(id->get<std::string>());
    return target;
}

auto writes_mention(std::string_view content, dpp::snowflake who) -> bool {
    if (who.empty()) return false;
    return content.contains(std::format("<@{}>", who.str())) || content.contains(std::format("<@!{}>", who.str()));
}

} // namespace latibot::events
