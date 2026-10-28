#pragma once

#include "core/ports/result.hpp"

#include <dpp/coro/task.h>
#include <dpp/message.h>
#include <dpp/snowflake.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::capabilities {

/// Whom the model is answering, which is whom a tool acts for
/// (src/modules/llm/docs/Language_Model.md §3.4).
struct model_tool_context {
    dpp::snowflake guild_id{};
    dpp::snowflake channel_id{};

    /// Who asked, and the message they asked in.
    dpp::snowflake author_id{};
    dpp::snowflake message_id{};

    /// What they may do in `channel_id`, as Discord's permission bits; a
    /// tool that does what a command does checks what the command checks.
    std::uint64_t author_permissions = 0;

    std::chrono::sys_seconds now{};

    /// The model knows people only by aliases, never their names or ids
    /// (src/modules/llm/docs/Language_Model.md §3.8). Whom an alias the model
    /// passed stands for; nothing for one it has not been shown.
    std::function<std::optional<dpp::snowflake>(std::string_view alias)> user_for{};

    /// How a person is shown to the model: their alias.
    std::function<std::string(dpp::snowflake user)> shown_as{};

    /// What the model wrote, with the names it wrote as markers put back,
    /// as Discord should get it.
    std::function<std::string(std::string_view text)> restore{};

    /// What people wrote, with their names made aliases, as the model may
    /// read it.
    std::function<std::string(std::string_view text)> sanitize{};

    /// A channel of this server the model named: `#name`, `name`, `<#id>`
    /// or an id; nothing when there is none here.
    std::function<std::optional<dpp::snowflake>(std::string_view named)> channel_for{};

    /// A marker standing for `text`, such as a past nickname, which the
    /// model may write in its reply and which is put back when it is posted.
    /// For names the model may use but must never read. Good for this reply
    /// only.
    std::function<std::string(std::string_view text)> hide{};

    /// Posts as whoever is answering, LatiBot or a persona through its
    /// webhook, in the channel the message names: this one, unless the tool
    /// chose another.
    std::function<dpp::task<ports::result<dpp::message>>(dpp::message message)> post{};
};

/// What a tool hands back to the model: text, and whether it failed. A
/// failure is for the model to read and recover from, so say why.
struct model_tool_result {
    std::string content{};
    bool is_error = false;
};

/// One tool, as the model sees it and as it runs.
struct model_tool {
    /// snake_case, unique across the bot.
    std::string name{};
    std::string description{};

    /// A JSON schema for its input: an object.
    nlohmann::json input_schema{};

    /// Whether it changes anything. One that does is refused when the
    /// message being answered is a bot's, LatiBot's and its personas'
    /// included, so one model cannot talk another into acting.
    bool changes = false;

    /// Handed copies: it may outlive the call that started it.
    std::function<dpp::task<model_tool_result>(nlohmann::json input, model_tool_context context)> run{};
};

// What every tool does with its input and output.

[[nodiscard]] inline auto tool_done(std::string content) -> model_tool_result {
    return {.content = std::move(content), .is_error = false};
}

[[nodiscard]] inline auto tool_refused(std::string reason) -> model_tool_result {
    return {.content = std::move(reason), .is_error = true};
}

/// An input schema: an object with these properties, of which `required`
/// must be given.
[[nodiscard]] inline auto tool_schema(nlohmann::json properties, std::vector<std::string> required = {}) -> nlohmann::json {
    return {{"type", "object"}, {"properties", std::move(properties)}, {"required", std::move(required)}};
}

/// One property of a schema.
[[nodiscard]] inline auto tool_property(std::string_view description, std::string_view type = "string") -> nlohmann::json {
    return {{"type", type}, {"description", description}};
}

/// One property that is one of `choices`.
[[nodiscard]] inline auto tool_choice(std::string_view description, std::vector<std::string> choices) -> nlohmann::json {
    return {{"type", "string"}, {"enum", std::move(choices)}, {"description", description}};
}

/// A string the model passed, without the spaces around it; nothing when
/// it passed none, or only spaces.
[[nodiscard]] inline auto tool_text(const nlohmann::json& input, std::string_view key) -> std::optional<std::string> {
    if (!input.is_object()) return std::nullopt;
    const auto found = input.find(key);
    if (found == input.end() || !found->is_string()) return std::nullopt;
    const std::string& text = found->get_ref<const std::string&>();
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::nullopt;
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

[[nodiscard]] inline auto tool_number(const nlohmann::json& input, std::string_view key) -> std::optional<std::int64_t> {
    if (!input.is_object()) return std::nullopt;
    const auto found = input.find(key);
    if (found == input.end() || !found->is_number_integer()) return std::nullopt;
    return found->get<std::int64_t>();
}

[[nodiscard]] inline auto tool_flag(const nlohmann::json& input, std::string_view key) -> std::optional<bool> {
    if (!input.is_object()) return std::nullopt;
    const auto found = input.find(key);
    if (found == input.end() || !found->is_boolean()) return std::nullopt;
    return found->get<bool>();
}

/// Where a module gives the language model a tool for one of its features
/// (docs/modules/Module_Plan_Final.md §5.2). Offered by the llm module in
/// `offer`; a module adds its tools in `start`, when it finds this. Without
/// llm, nobody offers it and no tool is made. A server turns any tool off
/// with `/llm tools off`, and it is logged when it runs.
class model_tools {
public:
    virtual ~model_tools() = default;

    model_tools() = default;
    model_tools(const model_tools&) = delete;
    auto operator=(const model_tools&) -> model_tools& = delete;

    /// Throws std::invalid_argument when the name is taken: a programming
    /// error, caught at startup.
    virtual auto add(model_tool tool) -> void = 0;
};

} // namespace latibot::capabilities
