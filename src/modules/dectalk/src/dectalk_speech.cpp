#include "dectalk_speech.hpp"

#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "dectalk_sanitizer.hpp"
#include "speak_command.hpp"
#include "speech_queue.hpp"
#include "tts_engine.hpp"
#include "voice/pcm.hpp"
#include "voice/voice_sessions.hpp"
#include "voice_params.hpp"

#include <cstdint>
#include <format>
#include <utility>

namespace latibot::audio {

dectalk_speech::dectalk_speech(ports::tts_engine& engine, speech_queue& queue, const events::voice_sessions& sessions,
                               const config::guild_settings& settings)
    : engine_(&engine), queue_(&queue), sessions_(&sessions), settings_(&settings) {}

auto dectalk_speech::speaks_in(dpp::snowflake guild, dpp::snowflake text_channel) const -> bool {
    const auto session = sessions_->find(guild);
    return session && session->text_channel == text_channel;
}

auto dectalk_speech::prepare_for_model(std::string_view text, dpp::snowflake guild) const -> std::string {
    sanitized_speech clean = sanitize_speech(text, speech_trust::llm);
    commands::log_removed(clean, "the model's reply", guild);
    return std::move(clean.text);
}

auto dectalk_speech::say(dpp::snowflake guild, dpp::snowflake for_user, std::string text) -> dpp::task<void> {
    const commands::speech_limits limits = commands::speech_limits_for(*settings_, guild);
    // Cut to the guild's limit on /speak, which exists for the same reason:
    // nobody wants a minute of a paragraph read out.
    text = util::truncate(text, limits.max_characters);
    // The ellipsis is for reading; DECtalk would read it as noise.
    if (text.ends_with("…")) text.resize(text.size() - std::string_view("…").size());

    const std::uint64_t ticket = queue_->ticket(guild);
    auto spoken = co_await engine_->synthesize({.text = std::move(text), .voice = {}, .max_duration = limits.max_duration});
    if (!spoken.has_value()) {
        util::log().warn("could not speak the model's reply in guild {}: {}", guild, spoken.error().message);
        co_return;
    }

    const ports::pcm_audio& pcm = spoken.value();
    // Queued under whoever asked, so they can /tts stop it
    // (src/modules/dectalk/docs/Speech.md §2.4).
    queue_->enqueue(guild, for_user, ticket, to_discord(pcm.samples, pcm.sample_rate));
    util::log().info("speaking the model's reply of {} in guild {}", pcm.duration(), guild);
}

auto dectalk_speech::guide_for_model() const -> std::string {
    std::string voices;
    for (const builtin_voice& voice : builtin_voices()) {
        if (!voices.empty()) voices += ", ";
        voices += std::format("{} {}", voice.command, voice.name);
    }

    return std::format(R"(## Speaking
Your reply will also be spoken aloud by DECtalk in the server's voice channel. Write plain spoken text: no markdown, emoji, links or lists, and keep it to a few sentences. You may use a few DECtalk inline commands, which are kept in the posted message too:
- [:rate 120] to [:rate 350] sets the speed in words per minute.
- A voice: {}.
- [:dv ap 180] sets the average pitch in Hz.
- [:tone 440 300] plays a tone, frequency then milliseconds.
Anything else in square brackets is removed before speaking.)",
                       voices);
}

} // namespace latibot::audio
