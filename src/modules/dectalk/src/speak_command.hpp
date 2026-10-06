#pragma once

#include "core/commands/registry.hpp"
#include "dectalk_sanitizer.hpp"
#include "tts_engine.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace latibot::audio {
class speech_queue;
class voice_store;
} // namespace latibot::audio

namespace latibot::config {
class guild_settings;
struct bootstrap;
} // namespace latibot::config

namespace latibot::commands {

class voice_lab;

// --------------------------------------------------------------------------
// Decisions
// --------------------------------------------------------------------------

/// Per-guild limits on speech (docs/features/Speech.md §2.3).
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
/// the utterance playing now, an administrator, or a trusted user
/// (docs/features/Speech.md §2.4). With nothing playing, only the latter two.
[[nodiscard]] auto may_stop_speech(dpp::snowflake caller, std::optional<dpp::snowflake> speaking_for, bool trusted,
                                   bool administrator) noexcept -> bool;

/// What `/speak` and `/chat` say when the sanitizer left nothing to say.
inline constexpr std::string_view nothing_left_reply = "there's nothing left to say without the commands you can't use";

/// The voice named `wanted`: a built-in one, or one of the guild's custom
/// voices, which cannot share a built-in's name. Paul when `wanted` is
/// blank; nothing when there is no such voice.
[[nodiscard]] auto resolve_voice(const audio::voice_store* voices, dpp::snowflake guild, std::string_view wanted)
    -> std::optional<ports::voice_settings>;

/// How far to trust what whoever used `event` wants spoken
/// (docs/features/Speech.md §2.2).
[[nodiscard]] auto speech_trust_of(const config::bootstrap& bootstrap, const dpp::interaction_create_t& event) -> audio::speech_trust;

/// Logs, at debug, which inline commands the sanitizer took out.
auto log_removed(const audio::sanitized_speech& clean, std::string_view command, dpp::snowflake guild) -> void;

/// Offers the built-in voices, then the guild's custom ones, for a `voice`
/// option being typed into.
auto offer_voices(const dpp::autocomplete_t& event, const audio::voice_store* voices) -> void;

// --------------------------------------------------------------------------
// Commands
// --------------------------------------------------------------------------

/// Everything the voice commands share.
struct speech_services {
    ports::tts_engine* engine = nullptr;
    audio::speech_queue* queue = nullptr;
    config::guild_settings* settings = nullptr;
    const config::bootstrap* bootstrap = nullptr;

    /// The guild's custom voices, which /speak voice: also accepts.
    audio::voice_store* voices = nullptr;
};

/// Says something in the voice channel (docs/features/Speech.md §2.1).
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

/// Stops or skips speech, sets the limits on it, and keeps the guild's
/// custom voices (docs/features/Speech.md §2.4, §3).
class tts_command final : public command {
public:
    tts_command(speech_services services, voice_lab& lab);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;
    auto autocomplete(const dpp::autocomplete_t& event) const -> void override;

private:
    auto stop_or_skip(const dpp::slashcommand_t& event, bool skipping) -> dpp::task<void>;
    auto limits(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto open_lab(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto list_voices(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto delete_voice(const dpp::slashcommand_t& event) -> dpp::task<void>;

    command_info info_;
    speech_services services_;
    voice_lab* lab_;
};

} // namespace latibot::commands
