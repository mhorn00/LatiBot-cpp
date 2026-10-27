#include "tool_switches.hpp"

#include "core/config/guild_settings.hpp"
#include "core/util/text.hpp"
#include "tools.hpp"

#include <algorithm>

namespace latibot::llm {

auto tools_off(const config::guild_settings& settings, dpp::snowflake guild) -> std::set<std::string, std::less<>> {
    std::set<std::string, std::less<>> off;
    const std::string stored = settings.get(guild, tools_off_key, "");
    std::string_view rest = stored;
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        const std::string_view name = util::trim(rest.substr(0, comma));
        if (!name.empty()) off.emplace(name);
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
    }
    return off;
}

auto switch_tool(config::guild_settings& settings, dpp::snowflake guild, std::string_view name, bool on) -> bool {
    std::set<std::string, std::less<>> off = tools_off(settings, guild);
    const bool changed = on ? off.erase(std::string(name)) > 0 : off.emplace(name).second;
    if (!changed) return false;

    std::string stored;
    for (const std::string& each : off) {
        if (!stored.empty()) stored += ',';
        stored += each;
    }
    if (stored.empty()) {
        settings.erase(guild, tools_off_key);
    } else {
        settings.set(guild, tools_off_key, stored);
    }
    return true;
}

auto allowed_tools(const tool_registry& tools, const config::guild_settings& settings, dpp::snowflake guild, std::int64_t persona)
    -> std::vector<std::string> {
    const auto off = tools_off(settings, guild);
    std::vector<std::string> allowed;
    for (std::string& name : tools.names()) {
        if (off.contains(name)) continue;
        if (persona != 0 && std::ranges::find(latibot_only_tools, name) != latibot_only_tools.end()) continue;
        allowed.push_back(std::move(name));
    }
    return allowed;
}

} // namespace latibot::llm
