#pragma once

#include "core/commands/registry.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace latibot::ports {
class tts_engine;
}

namespace latibot::audio {
class speech_queue;
}

namespace latibot::config {
class guild_settings;
struct bootstrap;
} // namespace latibot::config

namespace latibot::commands {

// --------------------------------------------------------------------------
// Decisions
// --------------------------------------------------------------------------

/// Where `/speak` speaks.
enum class speak_route : std::uint8_t {
    /// Where the bot already is: a voice session's channel, or wherever
    /// `/join` put it.
    bot_channel,
    /// The bot is not in voice, so it joins whoever asked, as the Java bot
    /// did.
    join_caller,
    /// Neither of them is in voice.
    nowhere,
};

struct speak_plan {
    speak_route route = speak_route::nowhere;
    dpp::snowflake channel;
};

[[nodiscard]] auto plan_speak(dpp::snowflake bot_channel, dpp::snowflake caller_channel) noexcept -> speak_plan;

/// Per-guild limits on speech (plan §12.5, §12.7, §20).
struct speech_limits {
    std::size_t max_characters = 1000;
    std::chrono::seconds max_duration{60};
};

inline constexpr std::string_view tts_max_characters_key = "tts_max_characters";
inline constexpr std::string_view tts_max_seconds_key = "tts_max_seconds";

/// The bounds `/tts limits` accepts, and stored values are clamped to.
inline constexpr std::int64_t max_characters_limit = 4000;
inline constexpr std::int64_t max_seconds_limit = 600;

/// This guild's limits, with anything out of range clamped.
[[nodiscard]] auto speech_limits_for(const config::guild_settings& settings, dpp::snowflake guild) -> speech_limits;

/// Why `/speak` will not say `text`, or nothing when it will.
[[nodiscard]] auto speak_refusal(std::string_view text, const speech_limits& limits) -> std::optional<std::string>;

/// Whether `caller` may stop or skip what is being said: whoever asked for
/// the utterance playing now, an administrator, or a trusted user (plan
/// §12.7). With nothing playing, only the latter two.
[[nodiscard]] auto may_stop_speech(dpp::snowflake caller, std::optional<dpp::snowflake> speaking_for, bool trusted,
                                   bool administrator) noexcept -> bool;

// --------------------------------------------------------------------------
// Commands
// --------------------------------------------------------------------------

/// Everything the voice commands share.
struct speech_services {
    ports::tts_engine* engine = nullptr;
    audio::speech_queue* queue = nullptr;
    config::guild_settings* settings = nullptr;
    const config::bootstrap* bootstrap = nullptr;
};

/// Says something in the voice channel (plan §12.6).
class speak_command final : public command {
public:
    explicit speak_command(speech_services services);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;
    auto autocomplete(const dpp::autocomplete_t& event) const -> void override;

private:
    command_info info_;
    speech_services services_;
};

/// Stops or skips speech, and sets the limits on it (plan §12.7).
class tts_command final : public command {
public:
    explicit tts_command(speech_services services);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    auto stop_or_skip(const dpp::slashcommand_t& event, bool skipping) -> dpp::task<void>;
    auto limits(const dpp::slashcommand_t& event) -> dpp::task<void>;

    command_info info_;
    speech_services services_;
};

} // namespace latibot::commands
