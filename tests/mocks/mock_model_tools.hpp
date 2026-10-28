#pragma once

#include "core/capabilities/model_tools.hpp"

#include <dpp/permissions.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::testing {

/// The `model_tools` capability, for a module's tests: the tools it added,
/// and a way to run one as the language model would, for a person known by
/// an alias. The llm module's own tests cover what it adds around them.
class mock_model_tools final : public capabilities::model_tools {
public:
    std::map<std::string, capabilities::model_tool, std::less<>> tools;

    /// Each person a test names, by the alias the model knows them by.
    std::map<std::string, dpp::snowflake, std::less<>> aliases;

    /// The server's channels, by name.
    std::map<std::string, dpp::snowflake, std::less<>> channels;

    /// What tools posted through the context, as whoever was answering.
    std::vector<dpp::message> posted;

    auto add(capabilities::model_tool tool) -> void override {
        std::string name = tool.name;
        if (!tools.emplace(std::move(name), std::move(tool)).second) throw std::invalid_argument("a tool of that name already exists");
    }

    [[nodiscard]] auto names() const -> std::set<std::string> {
        std::set<std::string> all;
        for (const auto& [name, tool] : tools) {
            all.insert(name);
        }
        return all;
    }

    /// A request from `author` in `channel`, who may do what `permissions`
    /// says there.
    [[nodiscard]] auto context(dpp::snowflake guild, dpp::snowflake channel, dpp::snowflake author,
                               std::uint64_t permissions = dpp::p_send_messages) -> capabilities::model_tool_context {
        return {.guild_id = guild,
                .channel_id = channel,
                .author_id = author,
                .message_id = dpp::snowflake{999},
                .author_permissions = permissions,
                .now = std::chrono::sys_seconds{std::chrono::seconds{1'790'000'000}},
                .user_for = [this](std::string_view alias) -> std::optional<dpp::snowflake> {
                    const auto found = aliases.find(alias);
                    return found == aliases.end() ? std::nullopt : std::optional(found->second);
                },
                .shown_as =
                    [this](dpp::snowflake user) {
                        for (const auto& [alias, id] : aliases) {
                            if (id == user) return alias;
                        }
                        return std::string("someone");
                    },
                .restore = [](std::string_view text) { return std::string(text); },
                .sanitize = [](std::string_view text) { return std::string(text); },
                .channel_for = [this](std::string_view named) -> std::optional<dpp::snowflake> {
                    if (named.starts_with('#')) named.remove_prefix(1);
                    const auto found = channels.find(named);
                    return found == channels.end() ? std::nullopt : std::optional(found->second);
                },
                .hide = [](std::string_view text) { return "<hidden:" + std::string(text) + ">"; },
                .post = [this](dpp::message message) -> dpp::task<ports::result<dpp::message>> {
                    message.id = dpp::snowflake{7000 + posted.size()};
                    posted.push_back(message);
                    co_return message;
                }};
    }

    /// Runs `name` and waits for it.
    auto run(std::string_view name, nlohmann::json input, capabilities::model_tool_context with) -> capabilities::model_tool_result {
        const auto found = tools.find(name);
        if (found == tools.end()) throw std::invalid_argument("no tool of that name");
        auto result = found->second.run(std::move(input), std::move(with)).sync_wait_for(std::chrono::seconds{5});
        if (!result) throw std::runtime_error("the tool did not finish");
        return *result;
    }
};

} // namespace latibot::testing
