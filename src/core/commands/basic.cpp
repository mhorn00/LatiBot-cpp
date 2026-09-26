#include "core/commands/basic.hpp"

#include "core/commands/options.hpp"
#include "core/config/guild_settings.hpp"
#include "core/events/goodbye.hpp"
#include "core/ports/clock.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/cache.h>
#include <dpp/cluster.h>
#include <dpp/discordclient.h>
#include <dpp/dispatcher.h>
#include <dpp/guild.h>
#include <dpp/message.h>
#include <dpp/permissions.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <utility>

namespace latibot::commands {
namespace {

/// The voice channel a member is in, or 0.
dpp::snowflake voice_channel_of(dpp::snowflake guild_id, dpp::snowflake user_id) {
    const dpp::guild* guild = dpp::find_guild(guild_id);
    if (guild == nullptr) {
        return {};
    }

    const auto found = guild->voice_members.find(user_id);
    return found == guild->voice_members.end() ? dpp::snowflake{} : found->second.channel_id;
}

/// The voice channel the bot is connected to in this guild, or 0.
dpp::snowflake bot_voice_channel(const dpp::slashcommand_t& event) {
    dpp::discord_client* shard = event.from();
    if (shard == nullptr) {
        return {};
    }

    const dpp::voiceconn* connection = shard->get_voice(event.command.guild_id);
    return connection == nullptr ? dpp::snowflake{} : connection->channel_id;
}

} // namespace

// --------------------------------------------------------------------------
// Decisions
// --------------------------------------------------------------------------

join_decision plan_join(dpp::snowflake target_channel, dpp::snowflake bot_channel) noexcept {
    if (target_channel.empty()) {
        return {.action = join_action::target_not_in_voice, .channel_id = {}};
    }
    if (bot_channel.empty()) {
        return {.action = join_action::connect, .channel_id = target_channel};
    }
    if (bot_channel == target_channel) {
        return {.action = join_action::already_there, .channel_id = target_channel};
    }
    return {.action = join_action::move, .channel_id = target_channel};
}

std::string describe_join(join_action action, dpp::snowflake followed) {
    return std::format("{} <@{}>", action == join_action::move ? "ok moving to" : "ok joining", followed.str());
}

say_decision plan_say(std::string_view message, std::string_view reply_to) {
    if (util::is_blank(message)) {
        return {.action = say_action::blank_message, .reply_to = {}};
    }

    const std::string_view id = util::trim(reply_to);
    if (id.empty()) {
        return {.action = say_action::send, .reply_to = {}};
    }

    // Snowflakes are decimal, so anything else is a paste of the wrong thing:
    // a message link, a mention, a channel name. Saying so beats letting the
    // API reject it with a less helpful message.
    const auto parsed = util::parse_snowflake(id);
    if (!parsed) {
        return {.action = say_action::bad_reply_id, .reply_to = {}};
    }

    return {.action = say_action::reply, .reply_to = *parsed};
}

dpp::activity_type parse_activity_type(std::string_view name) {
    const std::string key = util::to_lower(util::trim(name));

    if (key == "watching") {
        return dpp::at_watching;
    }
    if (key == "listening") {
        return dpp::at_listening;
    }
    if (key == "competing") {
        return dpp::at_competing;
    }
    if (key == "custom" || key == "custom_status") {
        return dpp::at_custom;
    }
    return dpp::at_game;
}

dpp::activity make_activity(dpp::activity_type type, const std::string& text) {
    if (type == dpp::at_custom) {
        return {type, "Custom Status", text, ""};
    }
    return {type, text, "", ""};
}

namespace {

constexpr std::string_view status_text_key = "status_text";
constexpr std::string_view status_type_key = "status_type";

} // namespace

void save_status(config::guild_settings& settings, const saved_status& status) {
    settings.set(config::bot_wide, status_text_key, status.text);
    settings.set(config::bot_wide, status_type_key, status.type);
}

std::optional<saved_status> load_status(const config::guild_settings& settings) {
    auto text = settings.find(config::bot_wide, status_text_key);
    if (!text || text->empty()) {
        return std::nullopt;
    }
    return saved_status{.text = std::move(*text), .type = settings.get(config::bot_wide, status_type_key, "")};
}

dpp::presence presence_for(const saved_status& status) {
    return {dpp::ps_online, make_activity(parse_activity_type(status.type), status.text)};
}

// --------------------------------------------------------------------------
// /ping
// --------------------------------------------------------------------------

ping_command::ping_command(ports::clock& clock)
    : info_{.name = "ping",
            .description = "Pong!",
            .aliases = {},
            .required_bot_permissions = 0,
            .default_member_permissions = std::nullopt,
            .guild_only = false,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      clock_(&clock) {}

dpp::task<void> ping_command::execute(const dpp::slashcommand_t& event) {
    const auto started = clock_->steady_now();
    co_await event.co_reply(result(event, "Pong!"));
    const auto round_trip = std::chrono::duration_cast<std::chrono::milliseconds>(clock_->steady_now() - started);

    // The websocket figure is DPP's own heartbeat measurement, which is
    // gateway latency rather than the REST round trip above. They answer
    // different questions, so report both.
    const dpp::discord_client* shard = event.from();
    const auto gateway = shard == nullptr ? 0 : static_cast<int>(shard->websocket_ping * 1000.0);

    co_await event.co_edit_original_response(
        result(event, std::format("Pong! ({} ms round trip, {} ms gateway)", round_trip.count(), gateway)));
}

// --------------------------------------------------------------------------
// /say
// --------------------------------------------------------------------------

say_command::say_command(dpp::cluster& cluster)
    : info_{.name = "say",
            .description = "Say something as the bot.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages | dpp::p_read_message_history,
            .default_member_permissions = dpp::permission(dpp::p_manage_messages),
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      cluster_(&cluster) {}

dpp::slashcommand say_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_string, "message", "The message to send.", true).set_min_length(1).set_max_length(2000));
    payload.add_option(dpp::command_option(dpp::co_string, "reply", "Optional message id to reply to.", false));
    return payload;
}

dpp::task<void> say_command::execute(const dpp::slashcommand_t& event) {
    const std::string message = string_option(event, "message");
    const std::string reply_to = string_option(event, "reply");
    const say_decision decision = plan_say(message, reply_to);

    switch (decision.action) {
    case say_action::blank_message:
        co_await event.co_reply(refusal(event, "that message is empty"));
        co_return;

    case say_action::bad_reply_id:
        util::log().debug("/say refused: \"{}\" is not a message id", reply_to);
        co_await event.co_reply(refusal(event, std::format("\"{}\" is not a message id", reply_to)));
        co_return;

    case say_action::send: {
        co_await event.co_reply(result(event, "ok"));
        co_await cluster_->co_message_create(post(event, dpp::message(event.command.channel_id, message)));
        util::log().info("said {} characters in channel {} for {}", message.size(), event.command.channel_id,
                         describe_user(event.command.get_issuing_user()));
        co_return;
    }

    case say_action::reply: {
        // Fetched first: replying to a message from another channel, or to one
        // that has been deleted, otherwise fails at the API with nothing to
        // show the caller. Fetching can take longer than the three seconds a
        // first response has, so the answer is deferred.
        co_await defer(event);
        const auto target = co_await cluster_->co_message_get(decision.reply_to, event.command.channel_id);
        if (target.is_error()) {
            util::log().debug("/say could not fetch message {} in channel {}", decision.reply_to, event.command.channel_id);
            co_await answer_deferred(event,
                                     refusal(event, std::format("couldn't find message {} in this channel", decision.reply_to.str())));
            co_return;
        }

        co_await answer_deferred(event, result(event, "ok"));
        dpp::message said(event.command.channel_id, message);
        said.set_reference(decision.reply_to);
        co_await cluster_->co_message_create(post(event, std::move(said)));
        util::log().info("said {} characters in channel {} replying to {} for {}", message.size(), event.command.channel_id,
                         decision.reply_to, describe_user(event.command.get_issuing_user()));
        co_return;
    }
    }
}

// --------------------------------------------------------------------------
// /status
// --------------------------------------------------------------------------

status_command::status_command(dpp::cluster& cluster, config::guild_settings& settings)
    : info_{.name = "status",
            .description = "Set the bot's status.",
            .aliases = {},
            .required_bot_permissions = 0,
            .default_member_permissions = dpp::permission(dpp::p_manage_nicknames),
            .guild_only = false,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      cluster_(&cluster),
      settings_(&settings) {}

dpp::slashcommand status_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_string, "status", "The status text.", true).set_min_length(1).set_max_length(128));

    dpp::command_option type(dpp::co_string, "type", "How the status reads.", false);
    type.add_choice(dpp::command_option_choice("Playing", std::string("playing")));
    type.add_choice(dpp::command_option_choice("Watching", std::string("watching")));
    type.add_choice(dpp::command_option_choice("Listening to", std::string("listening")));
    type.add_choice(dpp::command_option_choice("Competing in", std::string("competing")));
    type.add_choice(dpp::command_option_choice("Custom", std::string("custom")));
    payload.add_option(type);
    return payload;
}

dpp::task<void> status_command::execute(const dpp::slashcommand_t& event) {
    const saved_status status{.text = string_option(event, "status"), .type = string_option(event, "type")};

    // Kept as well as set, so the next start puts it back (plan §6).
    cluster_->set_presence(presence_for(status));
    save_status(*settings_, status);
    util::log().info("status set to {} \"{}\" by {}", status.type.empty() ? "playing" : status.type, status.text,
                     describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(result(event, std::format("status set to: {}", status.text)));
}

// --------------------------------------------------------------------------
// /join
// --------------------------------------------------------------------------

join_command::join_command()
    : info_{.name = "join",
            .description = "Join the voice channel you are in, or another user's.",
            .aliases = {},
            .required_bot_permissions = dpp::p_connect | dpp::p_speak,
            .default_member_permissions = dpp::permission(dpp::p_speak),
            .guild_only = true,
            // The room sees the bot come and go, so it sees why (plan §6).
            .responses = {.result = 0, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}} {}

dpp::slashcommand join_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_user, "user", "Whose channel to join.", false));
    return payload;
}

dpp::task<void> join_command::execute(const dpp::slashcommand_t& event) {
    const dpp::snowflake caller = event.command.get_issuing_user().id;
    const dpp::snowflake target = snowflake_option(event, "user").value_or(caller);
    const bool following_someone_else = target != caller;

    const join_decision decision = plan_join(voice_channel_of(event.command.guild_id, target), bot_voice_channel(event));

    dpp::discord_client* shard = event.from();
    switch (decision.action) {
    case join_action::target_not_in_voice:
        util::log().debug("/join: {} is not in a voice channel in guild {}", target, event.command.guild_id);
        co_await event.co_reply(
            refusal(event, following_someone_else ? "they're not in a voice channel" : "you're not in a voice channel"));
        co_return;

    case join_action::already_there:
        co_await event.co_reply(
            refusal(event, following_someone_else ? "i'm already in their voice channel" : "i'm already in your voice channel"));
        co_return;

    case join_action::connect:
    case join_action::move:
        if (shard == nullptr) {
            co_await event.co_reply(refusal(event, "i can't reach the gateway right now"));
            co_return;
        }
        shard->connect_voice(event.command.guild_id, decision.channel_id);
        util::log().info("{} voice channel {} in guild {} for {}", decision.action == join_action::move ? "moved to" : "joined",
                         decision.channel_id, event.command.guild_id, describe_user(event.command.get_issuing_user()));
        dpp::message said(describe_join(decision.action, target));
        said.set_allowed_mentions();
        co_await event.co_reply(result(event, std::move(said)));
        co_return;
    }
}

// --------------------------------------------------------------------------
// /leave
// --------------------------------------------------------------------------

leave_command::leave_command()
    : info_{.name = "leave",
            .description = "Leave the voice channel.",
            .aliases = {},
            .required_bot_permissions = dpp::p_connect,
            .default_member_permissions = dpp::permission(dpp::p_speak),
            .guild_only = true,
            // The room sees the bot come and go, so it sees why (plan §6).
            .responses = {.result = 0, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}} {}

dpp::task<void> leave_command::execute(const dpp::slashcommand_t& event) {
    dpp::discord_client* shard = event.from();
    if (shard == nullptr || bot_voice_channel(event).empty()) {
        co_await event.co_reply(refusal(event, "i'm not in a voice channel"));
        co_return;
    }

    shard->disconnect_voice(event.command.guild_id);
    util::log().info("left the voice channel in guild {} for {}", event.command.guild_id, describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(result(event, "ok bye"));
}

// --------------------------------------------------------------------------
// /shutdown
// --------------------------------------------------------------------------

shutdown_command::shutdown_command(std::function<void()> request_shutdown)
    : info_{.name = "shutdown",
            .description = "Shut the bot down.",
            .aliases = {},
            .required_bot_permissions = 0,
            .default_member_permissions = dpp::permission(dpp::p_administrator),
            .guild_only = true,
            // Everyone is about to lose the bot; they hear it go (plan §6).
            .responses = {.result = 0, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      request_shutdown_(std::move(request_shutdown)) {}

dpp::task<void> shutdown_command::execute(const dpp::slashcommand_t& event) {
    util::log().info("shutdown requested by {}", describe_user(event.command.get_issuing_user()));

    // Awaited, not queued: the process is about to stop, and an unanswered
    // interaction shows the caller an error instead of a goodbye.
    co_await event.co_reply(result(event, "ok bye bye!"));
    request_shutdown_();
}

// --------------------------------------------------------------------------

// --------------------------------------------------------------------------
// /goodbye
// --------------------------------------------------------------------------

goodbye_command::goodbye_command(config::guild_settings& settings)
    : info_{.name = "goodbye",
            .description = "Show or change the phrase that stops the bot.",
            .aliases = {},
            .required_bot_permissions = 0,
            .default_member_permissions = dpp::permission(dpp::p_administrator),
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      settings_(&settings) {}

dpp::slashcommand goodbye_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_string, "phrase", "The new phrase.", false).set_min_length(1).set_max_length(200));
    payload.add_option(dpp::command_option(dpp::co_boolean, "off", "Turn the phrase off entirely.", false));
    return payload;
}

dpp::task<void> goodbye_command::execute(const dpp::slashcommand_t& event) {
    const dpp::snowflake guild = event.command.guild_id;

    if (bool_option(event, "off").value_or(false)) {
        // Stored empty rather than erased, so the guild keeps saying "off"
        // instead of falling back to the default on the next restart.
        settings_->set(guild, events::goodbye_phrase_key, "");
        util::log().info("goodbye phrase turned off in guild {} by {}", guild, describe_user(event.command.get_issuing_user()));
        co_await event.co_reply(result(event, "the goodbye phrase is off; set one to turn it back on"));
        co_return;
    }

    const std::string wanted = std::string(util::trim(string_option(event, "phrase")));
    if (wanted.empty()) {
        const std::string current = settings_->get(guild, events::goodbye_phrase_key, events::default_goodbye_phrase);
        co_await event.co_reply(result(
            event, current.empty() ? "the goodbye phrase is off" : std::format("an administrator saying \"{}\" stops the bot", current)));
        co_return;
    }

    settings_->set(guild, events::goodbye_phrase_key, wanted);
    util::log().info("goodbye phrase in guild {} set to \"{}\" by {}", guild, wanted, describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(result(event, std::format("an administrator saying \"{}\" now stops the bot", wanted)));
}

// --------------------------------------------------------------------------

void add_basic_commands(registry& into, dpp::cluster& cluster, ports::clock& clock, config::guild_settings& settings,
                        std::function<void()> request_shutdown) {
    into.add(std::make_unique<ping_command>(clock));
    into.add(std::make_unique<say_command>(cluster));
    into.add(std::make_unique<status_command>(cluster, settings));
    into.add(std::make_unique<join_command>());
    into.add(std::make_unique<leave_command>());
    into.add(std::make_unique<shutdown_command>(std::move(request_shutdown)));
    into.add(std::make_unique<goodbye_command>(settings));
}

} // namespace latibot::commands
