#include "core/commands/speak.hpp"

#include "core/audio/dectalk_sanitizer.hpp"
#include "core/audio/pcm.hpp"
#include "core/audio/speech_queue.hpp"
#include "core/audio/voice_params.hpp"
#include "core/commands/options.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/discord/voice_state.hpp"
#include "core/ports/tts_engine.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/cluster.h>
#include <dpp/discordclient.h>
#include <dpp/dispatcher.h>

#include <algorithm>
#include <format>
#include <utility>

namespace latibot::commands {

// --------------------------------------------------------------------------
// Decisions
// --------------------------------------------------------------------------

auto plan_speak(dpp::snowflake bot_channel, dpp::snowflake caller_channel) noexcept -> speak_plan {
    if (!bot_channel.empty()) return {.route = speak_route::bot_channel, .channel = bot_channel};
    if (!caller_channel.empty()) return {.route = speak_route::join_caller, .channel = caller_channel};
    return {.route = speak_route::nowhere, .channel = {}};
}

auto speech_limits_for(const config::guild_settings& settings, dpp::snowflake guild) -> speech_limits {
    const speech_limits defaults;
    const std::int64_t characters = settings.get_int(guild, tts_max_characters_key, static_cast<std::int64_t>(defaults.max_characters));
    const std::int64_t seconds = settings.get_int(guild, tts_max_seconds_key, defaults.max_duration.count());
    return {.max_characters = static_cast<std::size_t>(std::clamp<std::int64_t>(characters, 1, max_characters_limit)),
            .max_duration = std::chrono::seconds{std::clamp<std::int64_t>(seconds, 1, max_seconds_limit)}};
}

auto speak_refusal(std::string_view text, const speech_limits& limits) -> std::optional<std::string> {
    if (util::is_blank(text)) return "there's nothing to say";
    const std::size_t length = util::character_count(text);
    if (length > limits.max_characters) {
        return std::format("that's {} characters; this server's limit is {}", length, limits.max_characters);
    }
    return std::nullopt;
}

auto may_stop_speech(dpp::snowflake caller, std::optional<dpp::snowflake> speaking_for, bool trusted, bool administrator) noexcept -> bool {
    return trusted || administrator || (speaking_for && *speaking_for == caller);
}

// --------------------------------------------------------------------------
// /speak
// --------------------------------------------------------------------------

namespace {

/// How many voices an autocomplete offers; Discord's own limit is 25.
constexpr std::size_t voice_choices = 25;

auto describe_speech_error(const std::string& message) -> std::string {
    return std::format("couldn't say that: {}", message);
}

} // namespace

speak_command::speak_command(speech_services services)
    : info_{.name = "speak",
            .description = "Say something in the voice channel.",
            .aliases = {},
            .required_bot_permissions = dpp::p_connect | dpp::p_speak,
            .default_member_permissions = dpp::permission(dpp::p_speak),
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      services_(services) {}

auto speak_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_string, "text", "What to say. [:inline commands] work.", true)
                           .set_min_length(1)
                           .set_max_length(max_characters_limit));
    payload.add_option(dpp::command_option(dpp::co_string, "voice", "Who says it.", false).set_auto_complete(true));
    payload.add_option(dpp::command_option(dpp::co_integer, "rate", "Words a minute, 75 to 600. 200 by default.", false)
                           .set_min_value(audio::min_rate)
                           .set_max_value(audio::max_rate));
    payload.add_option(
        dpp::command_option(dpp::co_integer, "volume", "Percent, 0 to 200. 100 by default.", false).set_min_value(0).set_max_value(200));
    return payload;
}

auto speak_command::autocomplete(const dpp::autocomplete_t& event) const -> void {
    const dpp::command_option* focused = focused_option(event.options);
    if (focused == nullptr || focused->name != "voice" || event.owner == nullptr) return;

    const auto* typed = std::get_if<std::string>(&focused->value);
    const std::string filter = util::to_lower(util::trim(typed == nullptr ? std::string_view{} : std::string_view(*typed)));

    dpp::interaction_response reply(dpp::ir_autocomplete_reply);
    std::size_t offered = 0;
    for (const audio::builtin_voice& voice : audio::builtin_voices()) {
        if (offered == voice_choices || !voice.name.starts_with(filter)) continue;
        reply.add_autocomplete_choice(
            dpp::command_option_choice(std::format("{} ({})", voice.name, voice.description), std::string(voice.name)));
        ++offered;
    }
    event.owner->interaction_response_create(event.command.id, event.command.token, reply);
}

auto speak_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const dpp::snowflake caller = event.command.get_issuing_user().id;
    const speech_limits limits = speech_limits_for(*services_.settings, guild);

    const std::string text = string_option(event, "text");
    if (const auto refused = speak_refusal(text, limits)) {
        co_await event.co_reply(refusal(event, *refused));
        co_return;
    }

    ports::voice_settings voice;
    if (const std::string wanted = string_option(event, "voice"); !wanted.empty()) {
        const audio::builtin_voice* found = audio::find_builtin_voice(wanted);
        if (found == nullptr) {
            co_await event.co_reply(refusal(event, std::format("i don't know a voice called \"{}\"", wanted)));
            co_return;
        }
        voice.voice = std::string(found->name);
    }
    voice.rate = static_cast<int>(int_option(event, "rate").value_or(audio::default_rate));
    voice.volume = static_cast<int>(int_option(event, "volume").value_or(100));

    dpp::discord_client* shard = event.from();
    const speak_plan plan = plan_speak(discord::bot_voice_channel(shard, guild), discord::voice_channel_of(guild, caller));
    if (plan.route == speak_route::nowhere) {
        co_await event.co_reply(refusal(event, "i'm not in a voice channel, and neither are you"));
        co_return;
    }

    const bool administrator = invoker_permissions(event).can(dpp::p_administrator);
    const auto trust =
        services_.bootstrap->is_trusted(guild, caller, administrator) ? audio::speech_trust::trusted : audio::speech_trust::user;
    audio::sanitized_speech clean = audio::sanitize_speech(text, trust);
    if (!clean.removed.empty()) {
        std::string removed;
        for (const std::string& name : clean.removed) {
            removed += (removed.empty() ? "" : ", ") + name;
        }
        util::log().debug("/speak in guild {}: removed {}", guild, removed);
    }
    if (util::is_blank(clean.text)) {
        co_await event.co_reply(refusal(event, "there's nothing left to say without the commands you can't use"));
        co_return;
    }

    if (plan.route == speak_route::join_caller) {
        if (shard == nullptr) {
            co_await event.co_reply(refusal(event, "i can't reach the gateway right now"));
            co_return;
        }
        shard->connect_voice(guild, plan.channel);
        util::log().info("joined voice channel {} in guild {} to speak for {}", plan.channel, guild,
                         describe_user(event.command.get_issuing_user()));
    }

    // Taken before synthesizing, so a /tts stop while this is being made
    // stops it too.
    const std::uint64_t ticket = services_.queue->ticket(guild);
    co_await defer(event);

    auto spoken = co_await services_.engine->synthesize(
        {.text = std::move(clean.text), .voice = std::move(voice), .max_duration = limits.max_duration});
    if (!spoken.ok()) {
        util::log().warn("/speak in guild {} failed: {}", guild, spoken.error().message);
        co_await answer_deferred(event, refusal(event, describe_speech_error(spoken.error().message)));
        co_return;
    }

    const ports::pcm_audio& pcm = spoken.value();
    const audio::speech_outcome outcome = services_.queue->enqueue(guild, caller, ticket, audio::to_discord(pcm.samples, pcm.sample_rate));
    util::log().info("speaking {} for {} in guild {}", pcm.duration(), describe_user(event.command.get_issuing_user()), guild);

    std::string reply = outcome == audio::speech_outcome::stopped ? "stopped before i got to it" : "ok";
    if (outcome != audio::speech_outcome::stopped && pcm.truncated) {
        reply += std::format(", but it's cut off at {} (this server's limit)", limits.max_duration);
    }
    co_await answer_deferred(event, result(event, reply));
}

// --------------------------------------------------------------------------
// /tts
// --------------------------------------------------------------------------

tts_command::tts_command(speech_services services)
    : info_{.name = "tts",
            .description = "Stop or skip what the bot is saying, or set limits on it.",
            .aliases = {},
            .required_bot_permissions = dpp::p_connect | dpp::p_speak,
            .default_member_permissions = dpp::permission(dpp::p_speak),
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      services_(services) {}

auto tts_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_sub_command, "stop", "Stop speaking, and drop everything waiting to be said."));
    payload.add_option(dpp::command_option(dpp::co_sub_command, "skip", "Skip what is being said now."));

    dpp::command_option limits(dpp::co_sub_command, "limits", "Show or change this server's limits on speech (Manage Server).");
    limits.add_option(dpp::command_option(dpp::co_integer, "characters", "The longest text /speak takes.", false)
                          .set_min_value(1)
                          .set_max_value(max_characters_limit));
    limits.add_option(dpp::command_option(dpp::co_integer, "seconds", "The longest one utterance may run.", false)
                          .set_min_value(1)
                          .set_max_value(max_seconds_limit));
    payload.add_option(limits);
    return payload;
}

auto tts_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string subcommand = subcommand_path(event.command.get_command_interaction());
    if (subcommand == "stop" || subcommand == "skip") {
        co_await stop_or_skip(event, subcommand == "skip");
    } else if (subcommand == "limits") {
        co_await limits(event);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto tts_command::stop_or_skip(const dpp::slashcommand_t& event, bool skipping) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const dpp::snowflake caller = event.command.get_issuing_user().id;

    if (services_.queue->size(guild) == 0) {
        co_await event.co_reply(refusal(event, "i'm not saying anything"));
        co_return;
    }

    const bool administrator = invoker_permissions(event).can(dpp::p_administrator);
    const bool trusted = services_.bootstrap->is_trusted(guild, caller, administrator);
    if (!may_stop_speech(caller, services_.queue->current_owner(guild), trusted, administrator)) {
        co_await event.co_reply(refusal(event, "only whoever asked for this, or an admin, can stop it"));
        co_return;
    }

    if (skipping) {
        services_.queue->skip(guild);
        util::log().info("speech skipped in guild {} by {}", guild, describe_user(event.command.get_issuing_user()));
        co_await event.co_reply(result(event, "skipped"));
        co_return;
    }

    const std::size_t dropped = services_.queue->stop(guild);
    util::log().info("speech stopped in guild {} by {} ({} dropped)", guild, describe_user(event.command.get_issuing_user()), dropped);
    co_await event.co_reply(result(event, "stopped"));
}

auto tts_command::limits(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const auto characters = int_option(event, "characters");
    const auto seconds = int_option(event, "seconds");

    if (!characters && !seconds) {
        const speech_limits current = speech_limits_for(*services_.settings, guild);
        co_await event.co_reply(result(
            event, std::format("/speak takes up to {} characters, and stops after {}", current.max_characters, current.max_duration)));
        co_return;
    }

    // Default member permissions are per command, not per subcommand, so
    // this one checks for itself (plan §21.13).
    if (!invoker_permissions(event).can(dpp::p_manage_guild)) {
        co_await event.co_reply(refusal(event, "changing the limits needs Manage Server"));
        co_return;
    }

    if (characters) services_.settings->set_int(guild, tts_max_characters_key, *characters);
    if (seconds) services_.settings->set_int(guild, tts_max_seconds_key, *seconds);
    const speech_limits now = speech_limits_for(*services_.settings, guild);
    util::log().info("speech limits in guild {} set to {} characters, {} by {}", guild, now.max_characters, now.max_duration,
                     describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(
        result(event, std::format("/speak now takes up to {} characters, and stops after {}", now.max_characters, now.max_duration)));
}

} // namespace latibot::commands
