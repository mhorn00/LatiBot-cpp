#pragma once

#include "guards.hpp"
#include "llm_config.hpp"

#include <dpp/snowflake.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace latibot::config {
class guild_settings;
} // namespace latibot::config

namespace latibot::llm {

/// One number `/llm settings` edits (src/modules/llm/docs/Language_Model.md §3.7),
/// stored in `guild_settings` under `key`.
///
/// Values are checked against the range when they are set, and clamped into
/// it when they are read, so a hand-edited row cannot take the model past
/// what the panel would allow.
struct setting_spec {
    std::string_view key;

    /// As the panel shows it, at most 45 characters, which is Discord's
    /// limit on a modal field's label.
    std::string_view label;

    /// Which of the panel's forms it is on.
    std::string_view group;

    std::int64_t fallback = 0;
    std::int64_t min = 0;
    std::int64_t max = 0;

    /// Yes or no rather than a number: stored as 0 or 1.
    bool is_switch = false;
};

/// Every setting, in the order the panel shows them.
[[nodiscard]] auto setting_specs() noexcept -> std::span<const setting_spec>;

[[nodiscard]] auto find_setting(std::string_view key) noexcept -> const setting_spec*;

/// The panel's forms, in order, each at most five settings: a modal's limit.
[[nodiscard]] auto setting_groups() noexcept -> std::span<const std::string_view>;

/// Reads what was typed into a setting's field. Nothing, with the reason set,
/// when it is not a value the setting takes: out-of-range input is refused
/// with the range, rather than clamped, so nobody is surprised by a value
/// they did not type (src/modules/llm/docs/Language_Model.md §3.7).
[[nodiscard]] auto parse_setting(const setting_spec& spec, std::string_view typed, std::string& reason) -> std::optional<std::int64_t>;

/// A value as the panel shows it: "yes", "3000".
[[nodiscard]] auto describe_setting(const setting_spec& spec, std::int64_t value) -> std::string;

/// The stored value of one setting, clamped, or its default.
[[nodiscard]] auto setting_value(const config::guild_settings& settings, dpp::snowflake guild, const setting_spec& spec) -> std::int64_t;

// Settings that are not numbers, and so not on the panel's forms.
inline constexpr std::string_view enabled_key = "llm_enabled";
inline constexpr std::string_view model_key = "llm_model";
inline constexpr std::string_view personality_role_key = "llm_personality_role";

/// Everything a request reads, from one guild's settings
/// (src/modules/llm/docs/Language_Model.md §3.7).
struct llm_settings {
    /// Off until someone turns it on: every message it answers costs money.
    bool enabled = false;

    std::string model;

    /// The short-term window: how many recent messages, and roughly how many
    /// tokens of them.
    int context_messages = 15;
    int context_tokens = 3000;

    /// Thinking included.
    int max_output_tokens = 1024;

    int user_per_minute = 3;
    int channel_per_minute = 8;

    /// How many recent messages an advanced trigger sees
    /// (src/modules/llm/docs/Language_Model.md §2.9).
    int trigger_context = 5;

    pacing_rules pacing;

    /// Who may edit the personality: a role, or the guild's id for @everyone,
    /// which is the default (src/modules/llm/docs/Language_Model.md §3.5).
    dpp::snowflake personality_role;
};

/// The guild's settings, each clamped into its range. The model falls back
/// to `config.json`'s when the stored one is not a model the bot knows.
[[nodiscard]] auto load_llm_settings(const config::guild_settings& settings, dpp::snowflake guild, const llm_config& section)
    -> llm_settings;

} // namespace latibot::llm
