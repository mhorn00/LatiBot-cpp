#pragma once

#include "core/ports/result.hpp"

#include <dpp/coro/task.h>
#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dpp {
struct slashcommand_t;
}
namespace latibot::ports {
class http_client;
}

namespace latibot::discord {

// Voice messages, which DPP can read but has no way to send: `/chat`'s
// speech (src/modules/dectalk/docs/Speech.md §4.4) and `/soundboard`'s
// sounds (src/modules/soundboard/docs/Soundboard.md §3).

/// Discord's IS_VOICE_MESSAGE message flag.
inline constexpr std::uint64_t voice_message_flag = 1U << 13U;

/// How many bars Discord draws for a voice message.
inline constexpr std::size_t waveform_buckets = 256;

/// The shape Discord shows for a voice message: the audio cut into `buckets`
/// equal stretches, each the loudest sample in it, scaled so the loudest of
/// all is 255. Silence is all zeros. Fewer samples than buckets gives one
/// bucket per sample.
///
/// The Java bot averaged the signed bytes of the whole file, header and all,
/// which comes out near zero whatever the audio is
/// (src/modules/dectalk/docs/Speech.md §4.4).
[[nodiscard]] auto waveform(std::span<const std::int16_t> samples, std::size_t buckets = waveform_buckets) -> std::vector<std::uint8_t>;

/// `waveform`, base64-encoded, as the voice message payload carries it.
[[nodiscard]] auto waveform_base64(std::span<const std::int16_t> samples) -> std::string;

/// A voice message's one file, and what Discord shows in place of it.
struct voice_message {
    /// The whole file: WAV, or Ogg Opus as Discord's own apps record.
    std::string file;
    /// Its name, which nobody sees, and its type, which must begin with
    /// `audio/` for Discord to read the duration and the waveform.
    std::string filename;
    std::string mimetype;

    std::chrono::milliseconds duration{};
    /// `waveform_base64` of its samples.
    std::string waveform;
};

/// The `payload_json` that answers an interaction with a voice message:
/// response type 4 carrying the voice message flag, `flags` besides, and the
/// one attachment's name, duration and waveform. It has no content, which a
/// voice message cannot carry.
[[nodiscard]] auto voice_message_response(std::uint64_t flags, std::chrono::milliseconds duration, std::string_view waveform,
                                          std::string_view filename) -> std::string;

/// The `payload_json` that posts a voice message in a channel: the voice
/// message flag, `flags` besides, and the one attachment, as
/// `voice_message_response` has it.
[[nodiscard]] auto voice_message_post(std::uint64_t flags, std::chrono::milliseconds duration, std::string_view waveform,
                                      std::string_view filename) -> std::string;

/// A multipart form: its body, and the Content-Type naming its boundary.
struct form {
    std::string body;
    std::string content_type;
};

/// The form that carries `payload_json` and the voice message's file, as
/// `files[0]`, the part Discord matches attachment 0 to.
///
/// Built here rather than by DPP: with a single file DPP names its part
/// `file`, which attachment 0 does not match, and Discord refuses the
/// voice message as an invalid form body.
[[nodiscard]] auto voice_message_form(std::string_view payload_json, const voice_message& message) -> form;

/// Answers `event` with `message` as its first response, not after a
/// `defer`, so the voice message is the reply itself; that has to happen
/// within three seconds of the command. On failure nothing has answered it,
/// so a refusal still can.
///
/// Sent through `http` rather than DPP's REST queue, which would rebuild
/// the form (`voice_message_form`). Answering an interaction takes its
/// token, not the bot's.
auto answer_with_voice_message(ports::http_client& http, const dpp::slashcommand_t& event, std::uint64_t flags, voice_message message)
    -> dpp::task<ports::result<void>>;

/// Posts `message` in `channel` as the bot, for a voice message that answers
/// no interaction, such as one the language model asked for. Sent through
/// `http` with the bot's token, for the reason `answer_with_voice_message`
/// is. Gives the message's id.
auto post_voice_message(ports::http_client& http, std::string_view bot_token, dpp::snowflake channel, std::uint64_t flags,
                        voice_message message) -> dpp::task<ports::result<dpp::snowflake>>;

} // namespace latibot::discord
