#include "core/commands/chat.hpp"

#include "core/audio/dectalk_sanitizer.hpp"
#include "core/audio/voice_params.hpp"
#include "core/audio/wav.hpp"
#include "core/commands/options.hpp"
#include "core/config/guild_settings.hpp"
#include "core/discord/raw_api.hpp"
#include "core/ports/tts_engine.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/cluster.h>
#include <dpp/dispatcher.h>
#include <dpp/json.h>

#include <format>
#include <utility>
#include <vector>

namespace latibot::commands {

auto voice_message_response(std::uint64_t flags, std::chrono::milliseconds duration, std::string_view waveform) -> std::string {
    constexpr int channel_message_with_source = 4;
    const nlohmann::json payload{
        {"type", channel_message_with_source},
        {"data",
         {{"flags", flags | voice_message_flag},
          {"attachments", nlohmann::json::array({{{"id", 0},
                                                  {"filename", voice_message_file},
                                                  {"duration_secs", static_cast<double>(duration.count()) / 1000.0},
                                                  {"waveform", waveform}}})}}}};
    return payload.dump();
}

chat_command::chat_command(speech_services services, discord::raw_api& raw)
    : info_{.name = "chat",
            .description = "Say something as a voice message.",
            .aliases = {},
            .required_bot_permissions = 0,
            .default_member_permissions = dpp::permission(dpp::p_speak),
            .guild_only = true,
            // The voice message is for the room, as the Java bot's was; it
            // arrives silently.
            .responses = {.result = dpp::m_suppress_notifications, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      services_(services),
      raw_(&raw) {}

auto chat_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_string, "text", "What to say. [:inline commands] and [phonemes] work.", true)
                           .set_min_length(1)
                           .set_max_length(max_characters_limit));
    payload.add_option(dpp::command_option(dpp::co_string, "voice", "Who says it.", false).set_auto_complete(true));
    payload.add_option(dpp::command_option(dpp::co_integer, "rate", "Words a minute, 75 to 600. 200 by default.", false)
                           .set_min_value(audio::min_rate)
                           .set_max_value(audio::max_rate));
    return payload;
}

auto chat_command::autocomplete(const dpp::autocomplete_t& event) const -> void {
    offer_voices(event, services_.voices);
}

auto chat_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const speech_limits limits = speech_limits_for(*services_.settings, guild);

    const std::string text = string_option(event, "text");
    if (const auto refused = speak_refusal(text, limits)) {
        co_await event.co_reply(refusal(event, *refused));
        co_return;
    }

    const std::string wanted = string_option(event, "voice");
    auto voice = resolve_voice(services_.voices, guild, wanted);
    if (!voice) {
        co_await event.co_reply(refusal(event, std::format("i don't know a voice called \"{}\"", wanted)));
        co_return;
    }
    voice->rate = static_cast<int>(int_option(event, "rate").value_or(audio::default_rate));

    const audio::sanitized_speech clean = audio::sanitize_speech(text, speech_trust_of(*services_.bootstrap, event));
    log_removed(clean, "/chat", guild);
    if (util::is_blank(clean.text)) {
        co_await event.co_reply(refusal(event, std::string(nothing_left_reply)));
        co_return;
    }

    // Answered directly rather than deferred, as the Java bot did, so the
    // voice message is the reply itself. That has to happen within three
    // seconds, and synthesis takes milliseconds.
    auto spoken = co_await services_.engine->synthesize(
        {.text = std::string(chat_preamble) + clean.text, .voice = std::move(*voice), .max_duration = limits.max_duration});
    if (!spoken.ok()) {
        util::log().warn("/chat in guild {} failed: {}", guild, spoken.error().message);
        co_await event.co_reply(refusal(event, std::format("couldn't say that: {}", spoken.error().message)));
        co_return;
    }

    const ports::pcm_audio& pcm = spoken.value();
    std::vector<dpp::message_file_data> files{{.name = std::string(voice_message_file),
                                               .content = audio::wav_file(pcm.samples, pcm.sample_rate, pcm.channels),
                                               .mimetype = "audio/wav"}};
    const auto flags = static_cast<std::uint64_t>(responses_for(event).result);
    const auto sent = co_await raw_->multipart(
        ports::http_method::post, std::format("/interactions/{}/{}/callback", event.command.id.str(), event.command.token),
        voice_message_response(flags, pcm.duration(), audio::waveform_base64(pcm.samples)), std::move(files));
    if (!sent.ok()) {
        // Nothing answered the interaction, so a refusal still can.
        util::log().error("/chat in guild {} could not send its voice message: {}", guild, sent.error().message);
        co_await event.co_reply(refusal(event, "couldn't send the voice message; it's in the log"));
        co_return;
    }
    util::log().info("sent a voice message of {} for {} in guild {}", pcm.duration(), describe_user(event.command.get_issuing_user()),
                     guild);
}

} // namespace latibot::commands
