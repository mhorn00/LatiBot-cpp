#include "speech_tools.hpp"

#include "chat_command.hpp"
#include "core/capabilities/model_tools.hpp"
#include "core/config/bootstrap.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "dectalk_sanitizer.hpp"
#include "speech_queue.hpp"
#include "voice/pcm.hpp"
#include "voice/voice_state.hpp"
#include "voice_params.hpp"
#include "wav.hpp"

#include <dpp/permissions.h>

#include <algorithm>
#include <format>
#include <memory>
#include <utility>

namespace latibot::commands {
namespace {

using capabilities::model_tool_context;
using capabilities::model_tool_result;
using capabilities::tool_done;
using capabilities::tool_refused;

/// The text, voice and rate the model asked for, made ready to synthesize:
/// within the server's limits, sanitized at the model's trust level.
struct prepared_speech {
    std::string text;
    ports::voice_settings voice;
    speech_limits limits;
};

auto prepare(const speech_tool_services& services, const nlohmann::json& input, const model_tool_context& context)
    -> std::expected<prepared_speech, std::string> {
    const auto written = capabilities::tool_text(input, "text");
    if (!written) return std::unexpected(std::string("it needs the text to say"));
    const std::string text = context.restore(*written);
    const speech_limits limits = speech_limits_for(*services.speech.settings, context.guild_id);
    if (const auto refused = speak_refusal(text, limits)) return std::unexpected(*refused);

    const std::string wanted = capabilities::tool_text(input, "voice").value_or("");
    auto voice = resolve_voice(services.speech.voices, context.guild_id, wanted);
    if (!voice) return std::unexpected(std::format("there is no voice called \"{}\"", wanted));
    voice->rate = static_cast<int>(
        std::clamp<std::int64_t>(capabilities::tool_number(input, "rate").value_or(audio::default_rate), audio::min_rate, audio::max_rate));

    // Whoever asked, never more than the model's own trust: the host
    // commands are never the model's to use.
    audio::sanitized_speech clean = audio::sanitize_speech(text, audio::speech_trust::llm);
    log_removed(clean, "the model's speech", context.guild_id);
    if (util::is_blank(clean.text)) return std::unexpected(std::string(nothing_left_reply));
    return prepared_speech{.text = std::move(clean.text), .voice = std::move(*voice), .limits = limits};
}

auto speak(speech_tool_services services, nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
    auto prepared = prepare(services, input, context);
    if (!prepared) co_return tool_refused(prepared.error());

    const discord::voice_plan plan =
        discord::plan_voice(services.bot_channel(context.guild_id), services.channel_of(context.guild_id, context.author_id));
    if (plan.route == discord::voice_route::nowhere) co_return tool_refused("you are not in a voice channel, and neither is whoever asked");
    if (plan.route == discord::voice_route::join_caller) {
        if (!services.connect(context.guild_id, plan.channel)) co_return tool_refused("the gateway cannot be reached right now");
    }

    // Taken before synthesizing, so a /tts stop meanwhile stops it too.
    const std::uint64_t ticket = services.speech.queue->ticket(context.guild_id);
    auto spoken = co_await services.speech.engine->synthesize(
        {.text = std::move(prepared->text), .voice = std::move(prepared->voice), .max_duration = prepared->limits.max_duration});
    if (!spoken.has_value()) co_return tool_refused(std::format("DECtalk could not say it: {}", spoken.error().message));

    const ports::pcm_audio& pcm = spoken.value();
    const audio::speech_outcome outcome =
        services.speech.queue->enqueue(context.guild_id, context.author_id, ticket, audio::to_discord(pcm.samples, pcm.sample_rate));
    if (outcome == audio::speech_outcome::stopped) co_return tool_done("it was stopped before it was said");
    co_return tool_done(
        std::format("saying it, {} long{}", pcm.duration(),
                    pcm.truncated ? std::format(", cut off at this server's limit of {}", prepared->limits.max_duration) : std::string{}));
}

auto voice_message(speech_tool_services services, nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
    auto prepared = prepare(services, input, context);
    if (!prepared) co_return tool_refused(prepared.error());

    auto spoken = co_await services.speech.engine->synthesize({.text = std::string(chat_preamble) + prepared->text,
                                                               .voice = std::move(prepared->voice),
                                                               .max_duration = prepared->limits.max_duration});
    if (!spoken.has_value()) co_return tool_refused(std::format("DECtalk could not say it: {}", spoken.error().message));

    const ports::pcm_audio& pcm = spoken.value();
    const auto sent = co_await services.post_voice(context.channel_id, {.file = audio::wav_file(pcm.samples, pcm.sample_rate, pcm.channels),
                                                                        .filename = std::string(voice_message_file),
                                                                        .mimetype = "audio/wav",
                                                                        .duration = pcm.duration(),
                                                                        .waveform = discord::waveform_base64(pcm.samples)});
    if (!sent.has_value()) co_return tool_refused(std::format("Discord refused the voice message: {}", sent.error().message));
    co_return tool_done(std::format("posted a voice message of {}, as LatiBot", pcm.duration()));
}

auto stop_speech(const speech_tool_services& services, const nlohmann::json& input, const model_tool_context& context)
    -> model_tool_result {
    if (services.speech.queue->size(context.guild_id) == 0) return tool_refused("nothing is being said");
    const bool administrator = (context.author_permissions & dpp::p_administrator) != 0;
    const bool trusted =
        services.speech.bootstrap != nullptr && services.speech.bootstrap->is_trusted(context.guild_id, context.author_id, administrator);
    if (!may_stop_speech(context.author_id, services.speech.queue->current_owner(context.guild_id), trusted, administrator)) {
        return tool_refused("only whoever asked for what is being said, or an admin, may stop it");
    }
    if (capabilities::tool_flag(input, "skip").value_or(false)) {
        services.speech.queue->skip(context.guild_id);
        return tool_done("skipped what was being said");
    }
    const std::size_t dropped = services.speech.queue->stop(context.guild_id);
    return tool_done(std::format("stopped, and dropped {} waiting", dropped));
}

auto speech_schema() -> nlohmann::json {
    return capabilities::tool_schema(
        {{"text", capabilities::tool_property("What to say, within this server's /speak limit. Plain spoken text; DECtalk inline "
                                              "commands such as [:rate 250] work, as in your spoken replies.")},
         {"voice", capabilities::tool_property("A built-in voice, such as paul, betty or harry, or one of this server's custom voices; "
                                               "paul when left out.")},
         {"rate", capabilities::tool_property("Words a minute, 75 to 600; 200 when left out.", "integer")}},
        {"text"});
}

} // namespace

auto add_speech_tools(capabilities::model_tools& tools, speech_tool_services services) -> void {
    const auto shared = std::make_shared<const speech_tool_services>(std::move(services));

    tools.add({.name = "speak",
               .description = "Say something aloud in the voice channel with DECtalk, as /speak does: where you are in voice, or else "
                              "joining whoever you are answering. Whoever asked can stop it with /tts stop.",
               .input_schema = speech_schema(),
               .changes = true,
               .run = [shared](nlohmann::json input, model_tool_context context) {
                   return speak(*shared, std::move(input), std::move(context));
               }});

    tools.add({.name = "voice_message",
               .description = "Post a voice message in this channel, said by DECtalk, as /chat does. It is posted as LatiBot, even by a "
                              "persona.",
               .input_schema = speech_schema(),
               .changes = true,
               .run = [shared](nlohmann::json input, model_tool_context context) {
                   return voice_message(*shared, std::move(input), std::move(context));
               }});

    tools.add({.name = "stop_speech",
               .description = "Stop what DECtalk is saying in voice and drop what waits, or skip only what is being said now. Only for "
                              "whoever asked for it, or an admin.",
               .input_schema = capabilities::tool_schema(
                   {{"skip", capabilities::tool_property("True to skip only what is being said now.", "boolean")}}),
               .changes = true,
               .run = [shared](nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return stop_speech(*shared, input, context);
               }});
}

} // namespace latibot::commands
