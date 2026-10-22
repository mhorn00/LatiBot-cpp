#pragma once

#include "speak_command.hpp"

#include <string>
#include <string_view>

namespace latibot::ports {
class http_client;
}

namespace latibot::commands {

/// The file /chat's voice message carries.
inline constexpr std::string_view voice_message_file = "voice-message.wav";

/// The arpabet phoneme input the Java bot's /chat turned on, so `[hxeh'low]`
/// is spoken as phonemes.
inline constexpr std::string_view chat_preamble = "[:phoneme arpabet speak on]";

/// Answers with the text spoken, as a voice message
/// (src/modules/dectalk/docs/Speech.md §4.4). Needs no voice channel.
class chat_command final : public command {
public:
    chat_command(speech_services services, ports::http_client& http);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;
    auto autocomplete(const dpp::autocomplete_t& event) const -> void override;

private:
    command_info info_;
    speech_services services_;
    ports::http_client* http_;
};

} // namespace latibot::commands
