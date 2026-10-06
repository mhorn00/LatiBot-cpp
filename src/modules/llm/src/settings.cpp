#include "settings.hpp"

#include "core/config/guild_settings.hpp"
#include "core/util/text.hpp"
#include "models.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>

namespace latibot::llm {
namespace {

constexpr std::array<std::string_view, 3> groups{"context", "replies", "bots"};

// The defaults and ranges are the table in docs/features/Language_Model.md
// §2.9, chosen to keep a friendly server well inside the spend caps.
constexpr std::array<setting_spec, 10> specs{{
    {.key = "llm_context_messages", .label = "Recent messages it reads", .group = "context", .fallback = 15, .min = 0, .max = 50},
    {.key = "llm_context_tokens", .label = "Token budget for them", .group = "context", .fallback = 3000, .min = 200, .max = 20000},
    {.key = "llm_trigger_context", .label = "Messages an advanced trigger reads", .group = "context", .fallback = 5, .min = 0, .max = 20},
    {.key = "llm_max_output_tokens", .label = "Longest reply, in tokens", .group = "replies", .fallback = 1024, .min = 256, .max = 8192},
    {.key = "llm_user_per_minute", .label = "Replies per person per minute", .group = "replies", .fallback = 3, .min = 1, .max = 60},
    {.key = "llm_channel_per_minute", .label = "Replies per channel per minute", .group = "replies", .fallback = 8, .min = 1, .max = 120},
    {.key = "llm_bot_turns", .label = "Bot turns in a row (0: never)", .group = "bots", .fallback = 6, .min = 0, .max = 50},
    {.key = "llm_bot_delay_seconds", .label = "Seconds between bot turns", .group = "bots", .fallback = 5, .min = 0, .max = 300},
    {.key = "llm_bot_daily_cap", .label = "Replies to bots per day", .group = "bots", .fallback = 50, .min = 0, .max = 1000},
    {.key = "llm_bot_needs_human",
     .label = "Only after a person spoke (yes/no)",
     .group = "bots",
     .fallback = 0,
     .min = 0,
     .max = 1,
     .is_switch = true},
}};

auto clamped(const setting_spec& spec, std::int64_t value) -> std::int64_t {
    return std::clamp(value, spec.min, spec.max);
}

auto as_int(const config::guild_settings& settings, dpp::snowflake guild, std::string_view key) -> int {
    const setting_spec* spec = find_setting(key);
    return spec == nullptr ? 0 : static_cast<int>(setting_value(settings, guild, *spec));
}

} // namespace

auto setting_specs() noexcept -> std::span<const setting_spec> {
    return specs;
}

auto find_setting(std::string_view key) noexcept -> const setting_spec* {
    const auto found = std::ranges::find(specs, key, &setting_spec::key);
    return found == specs.end() ? nullptr : &*found;
}

auto setting_groups() noexcept -> std::span<const std::string_view> {
    return groups;
}

auto parse_setting(const setting_spec& spec, std::string_view typed, std::string& reason) -> std::optional<std::int64_t> {
    const std::string text = util::to_lower(util::trim(typed));

    if (spec.is_switch) {
        if (text == "yes" || text == "y" || text == "on" || text == "true" || text == "1") return 1;
        if (text == "no" || text == "n" || text == "off" || text == "false" || text == "0") return 0;
        reason = std::format("\"{}\" takes yes or no", spec.label);
        return std::nullopt;
    }

    std::int64_t value = 0;
    const auto [stop, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || stop != text.data() + text.size() || value < spec.min || value > spec.max) {
        reason = std::format("\"{}\" takes a whole number from {} to {}", spec.label, spec.min, spec.max);
        return std::nullopt;
    }
    return value;
}

auto describe_setting(const setting_spec& spec, std::int64_t value) -> std::string {
    if (spec.is_switch) return value != 0 ? "yes" : "no";
    return std::to_string(value);
}

auto setting_value(const config::guild_settings& settings, dpp::snowflake guild, const setting_spec& spec) -> std::int64_t {
    return clamped(spec, settings.get_int(guild, spec.key, spec.fallback));
}

auto load_llm_settings(const config::guild_settings& settings, dpp::snowflake guild, const llm_config& section) -> llm_settings {
    llm_settings loaded;
    loaded.enabled = settings.get_bool(guild, enabled_key, false);

    // A model since removed from the table, or a hand-edited typo, falls
    // back rather than failing every message with a 404 from the provider.
    loaded.model = settings.get(guild, model_key, section.model);
    if (find_model(loaded.model) == nullptr) loaded.model = section.model;

    loaded.context_messages = as_int(settings, guild, "llm_context_messages");
    loaded.context_tokens = as_int(settings, guild, "llm_context_tokens");
    loaded.trigger_context = as_int(settings, guild, "llm_trigger_context");
    loaded.max_output_tokens = as_int(settings, guild, "llm_max_output_tokens");
    loaded.user_per_minute = as_int(settings, guild, "llm_user_per_minute");
    loaded.channel_per_minute = as_int(settings, guild, "llm_channel_per_minute");
    loaded.pacing = {.turns = as_int(settings, guild, "llm_bot_turns"),
                     .delay = std::chrono::seconds{as_int(settings, guild, "llm_bot_delay_seconds")},
                     .daily_cap = as_int(settings, guild, "llm_bot_daily_cap"),
                     .needs_human = as_int(settings, guild, "llm_bot_needs_human") != 0};

    const auto role = util::parse_snowflake(settings.get(guild, personality_role_key, ""));
    loaded.personality_role = role.value_or(guild);
    return loaded;
}

} // namespace latibot::llm
