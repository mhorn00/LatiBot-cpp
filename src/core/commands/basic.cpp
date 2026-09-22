#include "core/commands/basic.hpp"

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
#include <cctype>
#include <charconv>
#include <chrono>
#include <format>
#include <memory>
#include <utility>

namespace latibot::commands {
namespace {

/// An ephemeral reply. Acknowledgements are for whoever ran the command; the
/// visible effect is the message, the presence change or the bot moving.
dpp::message ack(std::string_view text) {
    dpp::message reply(text);
    reply.set_flags(dpp::m_ephemeral);
    return reply;
}

/// A string option, or an empty string when it was not supplied.
std::string string_option(const dpp::slashcommand_t& event, const char* name) {
    const dpp::command_value value = event.get_parameter(name);
    const auto* text = std::get_if<std::string>(&value);
    return text == nullptr ? std::string{} : *text;
}

/// A user option, or 0 when it was not supplied.
dpp::snowflake user_option(const dpp::slashcommand_t& event, const char* name) {
    const dpp::command_value value = event.get_parameter(name);
    const auto* id = std::get_if<dpp::snowflake>(&value);
    return id == nullptr ? dpp::snowflake{} : *id;
}

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

std::string lowercased(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(),
                           [](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
    return result;
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
    std::uint64_t parsed = 0;
    const char* first = id.data();
    const char* last = first + id.size();
    // from_chars takes an explicit end pointer, so the view needs no null
    // terminator; the check cannot tell the two conventions apart.
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    const auto [stopped, error] = std::from_chars(first, last, parsed);
    if (error != std::errc{} || stopped != last) {
        return {.action = say_action::bad_reply_id, .reply_to = {}};
    }

    return {.action = say_action::reply, .reply_to = dpp::snowflake{parsed}};
}

dpp::activity_type parse_activity_type(std::string_view name) {
    const std::string key = lowercased(util::trim(name));

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

// --------------------------------------------------------------------------
// /ping
// --------------------------------------------------------------------------

ping_command::ping_command(ports::clock& clock)
    : info_{.name = "ping",
            .description = "Pong!",
            .aliases = {},
            .required_bot_permissions = 0,
            .default_member_permissions = std::nullopt,
            .guild_only = false},
      clock_(&clock) {}

dpp::task<void> ping_command::execute(const dpp::slashcommand_t& event) {
    const auto started = clock_->steady_now();
    co_await event.co_reply(ack("Pong!"));
    const auto round_trip = std::chrono::duration_cast<std::chrono::milliseconds>(clock_->steady_now() - started);

    // The websocket figure is DPP's own heartbeat measurement, which is
    // gateway latency rather than the REST round trip above. They answer
    // different questions, so report both.
    const dpp::discord_client* shard = event.from();
    const auto gateway = shard == nullptr ? 0 : static_cast<int>(shard->websocket_ping * 1000.0);

    co_await event.co_edit_original_response(
        ack(std::format("Pong! ({} ms round trip, {} ms gateway)", round_trip.count(), gateway)));
}

// --------------------------------------------------------------------------
// /say
// --------------------------------------------------------------------------

say_command::say_command(dpp::cluster& cluster)
    : info_{.name = "say",
            .description = "Say something as the bot.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages | dpp::p_read_message_history,
            .default_member_permissions = dpp::permission(dpp::p_manage_roles),
            .guild_only = true},
      cluster_(&cluster) {}

dpp::slashcommand say_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_string, "message", "The message to send.", true)
                           .set_min_length(1)
                           .set_max_length(2000));
    payload.add_option(dpp::command_option(dpp::co_string, "reply", "Optional message id to reply to.", false));
    return payload;
}

dpp::task<void> say_command::execute(const dpp::slashcommand_t& event) {
    const std::string message = string_option(event, "message");
    const std::string reply_to = string_option(event, "reply");
    const say_decision decision = plan_say(message, reply_to);

    switch (decision.action) {
    case say_action::blank_message:
        co_await event.co_reply(ack("that message is empty"));
        co_return;

    case say_action::bad_reply_id:
        co_await event.co_reply(ack(std::format("\"{}\" is not a message id", reply_to)));
        co_return;

    case say_action::send: {
        co_await event.co_reply(ack("ok"));
        const dpp::message post(event.command.channel_id, message);
        co_await cluster_->co_message_create(post);
        co_return;
    }

    case say_action::reply: {
        // Fetched first: replying to a message from another channel, or to one
        // that has been deleted, otherwise fails at the API with nothing to
        // show the caller.
        const auto target = co_await cluster_->co_message_get(decision.reply_to, event.command.channel_id);
        if (target.is_error()) {
            co_await event.co_reply(
                ack(std::format("couldn't find message {} in this channel", decision.reply_to.str())));
            co_return;
        }

        co_await event.co_reply(ack("ok"));
        dpp::message post(event.command.channel_id, message);
        post.set_reference(decision.reply_to);
        co_await cluster_->co_message_create(post);
        co_return;
    }
    }
}

// --------------------------------------------------------------------------
// /status
// --------------------------------------------------------------------------

status_command::status_command(dpp::cluster& cluster)
    : info_{.name = "status",
            .description = "Set the bot's status.",
            .aliases = {},
            .required_bot_permissions = 0,
            .default_member_permissions = dpp::permission(dpp::p_manage_nicknames),
            .guild_only = false},
      cluster_(&cluster) {}

dpp::slashcommand status_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(
        dpp::command_option(dpp::co_string, "status", "The status text.", true).set_min_length(1).set_max_length(128));

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
    const std::string text = string_option(event, "status");
    const dpp::activity_type type = parse_activity_type(string_option(event, "type"));

    cluster_->set_presence(dpp::presence(dpp::ps_online, make_activity(type, text)));
    co_await event.co_reply(ack(std::format("status set to: {}", text)));
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
            .guild_only = true} {}

dpp::slashcommand join_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_user, "user", "Whose channel to join.", false));
    return payload;
}

dpp::task<void> join_command::execute(const dpp::slashcommand_t& event) {
    const dpp::snowflake requested = user_option(event, "user");
    const dpp::snowflake caller = event.command.get_issuing_user().id;
    const dpp::snowflake target = requested.empty() ? caller : requested;
    const bool following_someone_else = target != caller;

    const join_decision decision =
        plan_join(voice_channel_of(event.command.guild_id, target), bot_voice_channel(event));

    dpp::discord_client* shard = event.from();
    switch (decision.action) {
    case join_action::target_not_in_voice:
        co_await event.co_reply(
            ack(following_someone_else ? "they're not in a voice channel" : "you're not in a voice channel"));
        co_return;

    case join_action::already_there:
        co_await event.co_reply(
            ack(following_someone_else ? "i'm already in their voice channel" : "i'm already in your voice channel"));
        co_return;

    case join_action::connect:
    case join_action::move:
        if (shard == nullptr) {
            co_await event.co_reply(ack("i can't reach the gateway right now"));
            co_return;
        }
        shard->connect_voice(event.command.guild_id, decision.channel_id);
        co_await event.co_reply(ack(decision.action == join_action::move ? "ok moving" : "ok joining"));
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
            .guild_only = true} {}

dpp::task<void> leave_command::execute(const dpp::slashcommand_t& event) {
    dpp::discord_client* shard = event.from();
    if (shard == nullptr || bot_voice_channel(event).empty()) {
        co_await event.co_reply(ack("i'm not in a voice channel"));
        co_return;
    }

    shard->disconnect_voice(event.command.guild_id);
    co_await event.co_reply(ack("ok bye"));
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
            .guild_only = true},
      request_shutdown_(std::move(request_shutdown)) {}

dpp::task<void> shutdown_command::execute(const dpp::slashcommand_t& event) {
    util::log().info("shutdown requested by {}", event.command.get_issuing_user().username);

    // Awaited, not queued: the process is about to stop, and an unanswered
    // interaction shows the caller an error instead of a goodbye.
    co_await event.co_reply(ack("ok bye bye!"));
    request_shutdown_();
}

// --------------------------------------------------------------------------

void add_basic_commands(registry& into, dpp::cluster& cluster, ports::clock& clock,
                        std::function<void()> request_shutdown) {
    into.add(std::make_unique<ping_command>(clock));
    into.add(std::make_unique<say_command>(cluster));
    into.add(std::make_unique<status_command>(cluster));
    into.add(std::make_unique<join_command>());
    into.add(std::make_unique<leave_command>());
    into.add(std::make_unique<shutdown_command>(std::move(request_shutdown)));
}

} // namespace latibot::commands
