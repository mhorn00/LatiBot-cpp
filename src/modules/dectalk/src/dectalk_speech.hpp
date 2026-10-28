#pragma once

#include "core/capabilities/speech.hpp"

namespace latibot::config {
class guild_settings;
}

namespace latibot::events {
class voice_sessions;
}

namespace latibot::ports {
class tts_engine;
}

namespace latibot::audio {

class speech_queue;
class voice_store;

/// The `speech` capability, said by DECtalk: what the language model needs
/// to speak its replies (src/modules/llm/docs/Language_Model.md §2.5).
///
/// The model's replies follow the rules `/speak` does: the sanitizer at the
/// model's trust level (src/modules/dectalk/docs/Speech.md §2.2), the server's limits
/// (§2.3), and the speech queue, under whoever asked, so they can
/// `/tts stop` it (§2.4).
class dectalk_speech final : public capabilities::speech {
public:
    /// Without `voices`, only the built-in voices can be used.
    dectalk_speech(ports::tts_engine& engine, speech_queue& queue, const events::voice_sessions& sessions,
                   const config::guild_settings& settings, const voice_store* voices = nullptr);

    [[nodiscard]] auto speaks_in(dpp::snowflake guild, dpp::snowflake text_channel) const -> bool override;
    [[nodiscard]] auto prepare_for_model(std::string_view text, dpp::snowflake guild) const -> std::string override;
    auto say(dpp::snowflake guild, dpp::snowflake for_user, std::string text, std::string_view voice) -> dpp::task<void> override;

    /// The built-in voices, Paul first, then the guild's custom ones.
    [[nodiscard]] auto voices(dpp::snowflake guild) const -> std::vector<std::string> override;
    [[nodiscard]] auto guide_for_model() const -> std::string override;

private:
    ports::tts_engine* engine_;
    speech_queue* queue_;
    const events::voice_sessions* sessions_;
    const config::guild_settings* settings_;
    const voice_store* voices_;
};

} // namespace latibot::audio
