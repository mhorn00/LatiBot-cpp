#include "core/commands/voice.hpp"

#include "core/audio/voice_store.hpp"
#include "core/commands/basic.hpp"
#include "core/commands/options.hpp"
#include "core/config/guild_settings.hpp"
#include "core/discord/voice_state.hpp"
#include "core/events/voice_sessions.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/cluster.h>
#include <dpp/discordclient.h>
#include <dpp/dispatcher.h>

#include <algorithm>
#include <format>
#include <vector>

namespace latibot::commands {

auto voice_grace_for(const config::guild_settings& settings, dpp::snowflake guild) -> std::chrono::seconds {
    const std::int64_t seconds = settings.get_int(guild, events::voice_grace_key, events::default_voice_grace.count());
    return std::chrono::seconds{std::clamp<std::int64_t>(seconds, 0, max_voice_grace_seconds)};
}

namespace {

/// How many saved voices an autocomplete offers; Discord's own limit is 25.
constexpr std::size_t voice_choices = 25;

/// Private answers, for the subcommands that are nobody else's business.
constexpr response_overrides private_result{.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt};

} // namespace

voice_command::voice_command(events::voice_sessions& sessions, config::guild_settings& settings, audio::voice_store& voices, voice_lab& lab)
    : info_{.name = "voice",
            .description = "Voice sessions and custom voices.",
            .aliases = {},
            .required_bot_permissions = dpp::p_connect | dpp::p_speak,
            .default_member_permissions = dpp::permission(dpp::p_speak),
            .guild_only = true,
            // The room sees the bot arrive and go, as with /join (plan §6).
            .responses = {.result = dpp::m_suppress_notifications, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {{"grace", private_result},
                                     {"lab", private_result},
                                     {"list", private_result},
                                     {"delete", private_result}}},
      sessions_(&sessions),
      settings_(&settings),
      voices_(&voices),
      lab_(&lab) {}

auto voice_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_sub_command, "start", "Join your voice channel; /speak goes there from anywhere."));
    payload.add_option(dpp::command_option(dpp::co_sub_command, "stop", "End the voice session and leave."));

    dpp::command_option grace(dpp::co_sub_command, "grace", "Show or change how long I stay once everyone has left (Manage Server).");
    grace.add_option(dpp::command_option(dpp::co_integer, "seconds", "How long, in seconds.", false)
                         .set_min_value(0)
                         .set_max_value(max_voice_grace_seconds));
    payload.add_option(grace);

    dpp::command_option lab(dpp::co_sub_command, "lab", "Build a custom voice, or change one of this server's.");
    lab.add_option(dpp::command_option(dpp::co_string, "voice", "A saved voice to start from.", false).set_auto_complete(true));
    payload.add_option(lab);

    payload.add_option(dpp::command_option(dpp::co_sub_command, "list", "This server's custom voices."));

    dpp::command_option remove(dpp::co_sub_command, "delete", "Delete a custom voice (whoever made it, or an admin).");
    remove.add_option(dpp::command_option(dpp::co_string, "voice", "Which one.", true).set_auto_complete(true));
    payload.add_option(remove);
    return payload;
}

auto voice_command::autocomplete(const dpp::autocomplete_t& event) const -> void {
    const dpp::command_option* focused = focused_option(event.options);
    if (focused == nullptr || focused->name != "voice" || event.owner == nullptr) return;

    const auto* typed = std::get_if<std::string>(&focused->value);
    const std::string filter = audio::normalise_voice_name(typed == nullptr ? std::string_view{} : std::string_view(*typed));

    dpp::interaction_response reply(dpp::ir_autocomplete_reply);
    std::size_t offered = 0;
    for (const audio::saved_voice& saved : voices_->list(event.command.guild_id)) {
        if (offered == voice_choices || !saved.name.starts_with(filter)) continue;
        reply.add_autocomplete_choice(
            dpp::command_option_choice(std::format("{} (built on {})", saved.name, saved.voice.base), saved.name));
        ++offered;
    }
    event.owner->interaction_response_create(event.command.id, event.command.token, reply);
}

auto voice_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string subcommand = subcommand_path(event.command.get_command_interaction());
    if (subcommand == "start") {
        co_await start(event);
    } else if (subcommand == "stop") {
        co_await stop(event);
    } else if (subcommand == "grace") {
        co_await grace(event);
    } else if (subcommand == "lab") {
        const std::string from = string_option(event, "voice");
        auto panel = lab_->open(event.command.guild_id, event.command.get_issuing_user().id, from);
        if (!panel) {
            co_await event.co_reply(refusal(event, std::format("this server has no voice called \"{}\"", from)));
            co_return;
        }
        co_await event.co_reply(result(event, std::move(*panel)));
    } else if (subcommand == "list") {
        co_await list(event);
    } else if (subcommand == "delete") {
        co_await remove(event);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto voice_command::list(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::vector<audio::saved_voice> saved = voices_->list(event.command.guild_id);
    if (saved.empty()) {
        co_await event.co_reply(result(event, "this server has no custom voices yet; make one with /voice lab"));
        co_return;
    }

    std::string text = std::format("**{} custom voice{}**\n", saved.size(), saved.size() == 1 ? "" : "s");
    for (const audio::saved_voice& voice : saved) {
        const std::string edits = voice.voice.dv_parameters();
        text += std::format("`{}`: {}{}{}, by <@{}>\n", voice.name, voice.voice.base, edits.empty() ? "" : " with ", edits,
                            voice.created_by.str());
    }
    dpp::message listed(util::truncate(text, 2000));
    listed.set_allowed_mentions();
    co_await event.co_reply(result(event, std::move(listed)));
}

auto voice_command::remove(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const std::string name = audio::normalise_voice_name(string_option(event, "voice"));

    const auto saved = voices_->find(guild, name);
    if (!saved) {
        co_await event.co_reply(refusal(event, std::format("this server has no voice called \"{}\"", name)));
        co_return;
    }
    const bool administrator = invoker_permissions(event).can(dpp::p_administrator);
    if (const auto refused = voice_change_refusal(event.command.get_issuing_user().id, saved->created_by, administrator, name)) {
        co_await event.co_reply(refusal(event, *refused));
        co_return;
    }

    voices_->remove(guild, name);
    util::log().info("voice {} deleted from guild {} by {}", name, guild, describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(result(event, std::format("deleted `{}`", name)));
}

auto voice_command::start(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const dpp::snowflake caller = event.command.get_issuing_user().id;
    dpp::discord_client* shard = event.from();

    // The session goes where the caller is, moving the bot if it is
    // elsewhere in the guild: it is theirs.
    const dpp::snowflake target = discord::voice_channel_of(guild, caller);
    const join_decision decision = plan_join(target, discord::bot_voice_channel(shard, guild));
    if (decision.action == join_action::target_not_in_voice) {
        co_await event.co_reply(refusal(event, "you're not in a voice channel"));
        co_return;
    }
    if (decision.action != join_action::already_there) {
        if (shard == nullptr) {
            co_await event.co_reply(refusal(event, "i can't reach the gateway right now"));
            co_return;
        }
        shard->connect_voice(guild, decision.channel_id);
    }

    sessions_->start({.guild_id = guild, .voice_channel = target, .text_channel = event.command.channel_id, .started_by = caller});
    util::log().info("voice session started in guild {}, voice channel {}, by {}", guild, target,
                     describe_user(event.command.get_issuing_user()));

    dpp::message said(std::format("ok, voice session started in <#{}>; /speak goes there from anywhere in the server", target.str()));
    said.set_allowed_mentions();
    co_await event.co_reply(result(event, std::move(said)));
}

auto voice_command::stop(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    dpp::discord_client* shard = event.from();

    // Ending the session is the bot's own voice state changing, which the
    // shell sees and tidies up after; this only has to leave.
    const bool had_session = sessions_->find(guild).has_value();
    if (!had_session && discord::bot_voice_channel(shard, guild).empty()) {
        co_await event.co_reply(refusal(event, "there's no voice session to stop"));
        co_return;
    }
    if (shard != nullptr) shard->disconnect_voice(guild);
    sessions_->end(guild);

    util::log().info("voice session in guild {} stopped by {}", guild, describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(result(event, "ok bye"));
}

auto voice_command::grace(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const auto seconds = int_option(event, "seconds");

    if (!seconds) {
        co_await event.co_reply(
            result(event, std::format("once everyone has left, i stay {} before leaving", voice_grace_for(*settings_, guild))));
        co_return;
    }

    // Default member permissions are per command, so this checks for itself
    // (plan §21.13).
    if (!invoker_permissions(event).can(dpp::p_manage_guild)) {
        co_await event.co_reply(refusal(event, "changing that needs Manage Server"));
        co_return;
    }

    settings_->set_int(guild, events::voice_grace_key, *seconds);
    util::log().info("voice grace in guild {} set to {}s by {}", guild, *seconds, describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(
        result(event, std::format("once everyone has left, i'll now stay {} before leaving", voice_grace_for(*settings_, guild))));
}

} // namespace latibot::commands
