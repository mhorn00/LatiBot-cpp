#pragma once

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <functional>

namespace latibot::capabilities {
class model_tools;
}

namespace latibot::ports {
class discord_gateway;
}

namespace latibot::llm {

class advanced_trigger_store;
class persona_store;

/// What the language model's own feature tools work with.
struct bot_tool_services {
    advanced_trigger_store* triggers = nullptr;
    const persona_store* personas = nullptr;
    const ports::discord_gateway* discord = nullptr;

    /// The gateway's latency, and how many servers the bot is in.
    std::function<std::chrono::milliseconds()> gateway_ping{};
    std::function<std::size_t()> server_count{};

    /// When the bot started, for its uptime.
    std::chrono::sys_seconds started_at{};
};

/// `list_advanced_triggers` and `add_advanced_trigger`, the model's /llm
/// trigger; `post_in_channel`, its /say; and `bot_info`, its /version and
/// /ping (src/modules/llm/docs/Language_Model.md §3.4). Added through the
/// model_tools capability, as every module's are.
auto add_bot_tools(capabilities::model_tools& tools, bot_tool_services services) -> void;

} // namespace latibot::llm
