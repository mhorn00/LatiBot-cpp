#pragma once

#include <dpp/snowflake.h>

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::config {
class guild_settings;
}

namespace latibot::llm {

class tool_registry;

/// Which of the model's tools a server has turned off with `/llm tools off`
/// (src/modules/llm/docs/Language_Model.md §3.4): a comma-separated list, so a
/// tool added later starts on everywhere.
inline constexpr std::string_view tools_off_key = "llm_tools_off";

/// Tools only LatiBot has: a persona posts through a webhook, which has no
/// status of its own.
inline constexpr std::array<std::string_view, 1> latibot_only_tools{"set_status"};

/// The tools a server has turned off.
[[nodiscard]] auto tools_off(const config::guild_settings& settings, dpp::snowflake guild) -> std::set<std::string, std::less<>>;

/// Turns a tool on or off in a server. False when it already was.
auto switch_tool(config::guild_settings& settings, dpp::snowflake guild, std::string_view name, bool on) -> bool;

/// The tools LatiBot, `persona` 0, or a persona may call in a server: every
/// registered one the server has not turned off, and for a persona none of
/// LatiBot's own.
[[nodiscard]] auto allowed_tools(const tool_registry& tools, const config::guild_settings& settings, dpp::snowflake guild,
                                 std::int64_t persona) -> std::vector<std::string>;

} // namespace latibot::llm
