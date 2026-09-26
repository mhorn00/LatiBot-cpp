#pragma once

#include "core/commands/speak.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace latibot::discord {
class raw_api;
}

namespace latibot::commands {

/// Discord's IS_VOICE_MESSAGE message flag. DPP models it on messages it
/// receives but has no way to send one (plan §2.1).
inline constexpr std::uint64_t voice_message_flag = 1U << 13U;

/// The file a voice message carries.
inline constexpr std::string_view voice_message_file = "voice-message.wav";

/// The `payload_json` that answers an interaction with a voice message:
/// response type 4 carrying the voice message flag, `flags` besides, and the
/// one attachment's duration and waveform, which Discord shows in place of a
/// file (plan §12.8).
[[nodiscard]] auto voice_message_response(std::uint64_t flags, std::chrono::milliseconds duration, std::string_view waveform)
    -> std::string;

/// The arpabet phoneme input the Java bot's /chat turned on, so `[hxeh'low]`
/// is spoken as phonemes.
inline constexpr std::string_view chat_preamble = "[:phoneme arpabet speak on]";

/// Answers with the text spoken, as a voice message (plan §12.8). Needs no
/// voice channel.
class chat_command final : public command {
public:
    chat_command(speech_services services, discord::raw_api& raw);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;
    auto autocomplete(const dpp::autocomplete_t& event) const -> void override;

private:
    command_info info_;
    speech_services services_;
    discord::raw_api* raw_;
};

} // namespace latibot::commands
