#pragma once

#include "core/capabilities/model_tools.hpp"
#include "tools.hpp"

#include <dpp/coro/task.h>
#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace latibot::ports {
class discord_gateway;
}

namespace latibot::llm {

/// The `model_tools` capability: the other modules' tools, added to the
/// model's own registry (src/modules/llm/docs/Language_Model.md §3.4). Each
/// call is handed what the module needs of the request, in its own terms,
/// and a tool that `changes` something refuses a bot's message here, so no
/// module can forget to.
class module_tools final : public capabilities::model_tools {
public:
    using post_fn = std::function<dpp::task<ports::result<dpp::message>>(dpp::message message, std::int64_t persona, dpp::snowflake guild)>;

    module_tools(tool_registry& tools, const ports::discord_gateway& discord, post_fn post);

    auto add(capabilities::model_tool tool) -> void override;

private:
    tool_registry* tools_;
    const ports::discord_gateway* discord_;
    post_fn post_;
};

/// The channel of `guild` that `named` names, as the model writes one.
[[nodiscard]] auto find_channel(std::string_view named, dpp::snowflake guild, const ports::discord_gateway& discord)
    -> std::optional<dpp::snowflake>;

/// What a module's tool is told of `context`: who asked, where, what they
/// may do there, and the request's people, by alias.
[[nodiscard]] auto module_context(const tool_context& context, const ports::discord_gateway& discord, const module_tools::post_fn& post)
    -> capabilities::model_tool_context;

} // namespace latibot::llm
