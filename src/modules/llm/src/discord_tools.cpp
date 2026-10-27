#include "discord_tools.hpp"

#include "aliases.hpp"
#include "core/capabilities/bot_status.hpp"
#include "core/capabilities/nickname_changes.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "reminders.hpp"

#include <dpp/permissions.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <sstream>
#include <utility>
#include <vector>

namespace latibot::llm {
namespace {

using json = nlohmann::json;

/// How far back `search_messages` reads, a hundred at a time.
constexpr int search_pages = 5;
constexpr std::size_t search_results = 20;

/// How long one message, or one result, is shown.
constexpr std::size_t shown_content = 1500;
constexpr std::size_t shown_snippet = 200;

/// Discord's own limits.
constexpr std::size_t nickname_limit = 32;
constexpr std::size_t status_limit = 128;
constexpr std::size_t poll_question_limit = 300;
constexpr std::size_t poll_answer_limit = 55;
constexpr int poll_longest_hours = 768;

auto refuse(std::string reason) -> tool_outcome {
    return {.content = std::move(reason), .is_error = true};
}

auto ok(std::string content) -> tool_outcome {
    return {.content = std::move(content), .is_error = false};
}

auto text_in(const json& input, const char* key) -> std::optional<std::string> {
    if (!input.is_object()) return std::nullopt;
    const auto found = input.find(key);
    if (found == input.end() || !found->is_string()) return std::nullopt;
    const std::string_view trimmed = util::trim(found->get_ref<const std::string&>());
    if (trimmed.empty()) return std::nullopt;
    return std::string(trimmed);
}

auto number_in(const json& input, const char* key) -> std::optional<std::int64_t> {
    if (!input.is_object()) return std::nullopt;
    const auto found = input.find(key);
    if (found == input.end() || !found->is_number_integer()) return std::nullopt;
    return found->get<std::int64_t>();
}

auto flag_in(const json& input, const char* key) -> std::optional<bool> {
    if (!input.is_object()) return std::nullopt;
    const auto found = input.find(key);
    if (found == input.end() || !found->is_boolean()) return std::nullopt;
    return found->get<bool>();
}

auto when(std::chrono::sys_seconds at) -> std::string {
    return std::format("{:%Y-%m-%d %H:%M} UTC", at);
}

auto when(double unix_time) -> std::string {
    return when(std::chrono::sys_seconds{std::chrono::seconds{static_cast<std::int64_t>(unix_time)}});
}

/// Whom the alias the model passed stands for.
auto person(const json& input, const char* key, const tool_context& context) -> std::optional<dpp::snowflake> {
    const auto alias = text_in(input, key);
    if (!alias || context.cast == nullptr) return std::nullopt;
    return context.cast->user_for(*alias);
}

/// How a person is shown to the model: their alias, never their name.
auto shown_as(dpp::snowflake user, const tool_context& context) -> std::string {
    return context.cast == nullptr ? std::string("someone") : context.cast->meet(user);
}

/// What the model wrote, with names put back, as Discord should get it.
auto restored(std::string_view text, const tool_context& context) -> std::string {
    return context.cast == nullptr ? std::string(text) : context.cast->restore(text);
}

/// What people wrote, as the model may read it.
auto sanitized(std::string_view text, const tool_context& context) -> std::string {
    return context.cast == nullptr ? std::string(text) : context.cast->sanitize(text);
}

/// Who wrote a message, as the model is shown them: LatiBot, a persona by
/// name, or a person by alias.
auto author_of(const dpp::message& message, const tool_context& context, dpp::snowflake bot_id) -> std::string {
    if (message.author.id == bot_id) return "LatiBot (a bot)";
    if (!message.webhook_id.empty()) return std::format("{} (a bot)", message.author.username);
    if (context.cast != nullptr) {
        const std::string alias = context.cast->meet(message.author.id, message.author.global_name, message.author.username);
        return message.author.is_bot() ? alias + " (a bot)" : alias;
    }
    return "someone";
}

auto creation_of(dpp::snowflake id) -> std::string {
    return when(id.get_creation_time());
}

/// A changing tool refuses a bot: LatiBot and its personas included, so one
/// model cannot talk another into acting.
auto refuses_bots(const tool_context& context) -> std::optional<tool_outcome> {
    if (!context.asker_is_bot) return std::nullopt;
    return refuse("this message is a bot's: tools that change something only act for people");
}

/// A channel the model names, by name or id, in this server; the one being
/// answered in when it names none.
auto channel_named(const json& input, const tool_context& context, const ports::discord_gateway& discord)
    -> std::expected<ports::channel_details, std::string> {
    const auto named = text_in(input, "channel");
    if (!named) {
        if (auto here = discord.channel_details(context.channel_id)) return *here;
        return ports::channel_details{.id = context.channel_id,
                                      .guild_id = context.guild_id,
                                      .name = {},
                                      .kind = "text",
                                      .topic = {},
                                      .age_restricted = false,
                                      .parent = {}};
    }

    std::string_view wanted = *named;
    if (wanted.starts_with("<#") && wanted.ends_with('>')) wanted = wanted.substr(2, wanted.size() - 3);
    if (wanted.starts_with('#')) wanted.remove_prefix(1);
    if (const auto id = util::parse_snowflake(wanted)) {
        const auto found = discord.channel_details(*id);
        if (!found || found->guild_id != context.guild_id) return std::unexpected(std::string("there is no such channel in this server"));
        return *found;
    }
    if (const auto server = discord.server_details(context.guild_id)) {
        for (const ports::channel_details& channel : server->channels) {
            if (util::equals_ignoring_case(channel.name, wanted)) return channel;
        }
    }
    return std::unexpected(std::format("there is no channel called #{} here", wanted));
}

auto describe_message(const dpp::message& message, const tool_context& context, dpp::snowflake bot_id) -> std::string {
    std::string text = std::format("Message {} in #{} by {}, at {}", message.id, message.channel_id, author_of(message, context, bot_id),
                                   creation_of(message.id));
    if (!message.message_reference.message_id.empty())
        text += std::format(", replying to message {}", message.message_reference.message_id);
    text += ":\n";
    text += message.content.empty() ? std::string("(no text)") : util::truncate(sanitized(message.content, context), shown_content);
    if (!message.attachments.empty()) {
        text += "\nAttachments:";
        for (const dpp::attachment& attached : message.attachments) {
            text += std::format(" {} ({})", attached.filename, attached.content_type.empty() ? "file" : attached.content_type);
        }
    }
    if (!message.reactions.empty()) {
        text += "\nReactions:";
        for (const dpp::reaction& reaction : message.reactions) {
            text += std::format(" {} ×{}", reaction.emoji_name, reaction.count);
        }
    }
    if (message.pinned) text += "\n(pinned)";
    return text;
}

/// A date and time the model wrote, `2026-10-28T15:00` or with seconds,
/// read in UTC.
auto parse_utc(std::string_view text) -> std::optional<std::chrono::sys_seconds> {
    for (const char* format : {"%Y-%m-%dT%H:%M:%S", "%Y-%m-%dT%H:%M", "%Y-%m-%d %H:%M", "%Y-%m-%d"}) {
        std::istringstream stream{std::string(util::trim(text))};
        std::chrono::sys_seconds parsed;
        stream >> std::chrono::parse(format, parsed);
        if (!stream.fail()) return parsed;
    }
    return std::nullopt;
}

// --------------------------------------------------------------------------
// Looking things up
// --------------------------------------------------------------------------

auto current_time(const json& input, const tool_context& context) -> tool_outcome {
    const std::string zone_name = text_in(input, "timezone").value_or("UTC");
    try {
        const std::chrono::time_zone* zone = std::chrono::locate_zone(zone_name);
        const std::chrono::zoned_time local{zone, context.now};
        return ok(std::format("{:%A %d %B %Y, %H:%M} ({}, {:%Z})", local.get_local_time(), zone_name, local));
    } catch (const std::exception&) {
        return refuse(std::format("there is no time zone called \"{}\"; use a name like Europe/London or America/New_York", zone_name));
    }
}

auto get_message(const discord_tool_services& services, json input, tool_context context) -> dpp::task<tool_outcome> {
    const auto written = text_in(input, "message");
    if (!written) co_return refuse("get_message needs the message's id or link");
    const auto place = find_message_place(*written, context.guild_id, context.channel_id);
    if (!place) co_return refuse(place.error());

    const auto fetched = co_await services.discord->get_message(place->channel_id, place->message_id);
    if (!fetched.has_value()) co_return refuse(std::format("could not read that message: {}", fetched.error().message));
    co_return ok(describe_message(fetched.value(), context, services.bot_id()));
}

auto search_messages(const discord_tool_services& services, json input, tool_context context) -> dpp::task<tool_outcome> {
    const auto channel = channel_named(input, context, *services.discord);
    if (!channel) co_return refuse(channel.error());

    // A name the model wrote as a marker is looked for as people wrote it.
    const std::string query = util::to_lower(restored(text_in(input, "query").value_or(""), context));
    const std::optional<dpp::snowflake> author = person(input, "author", context);
    if (text_in(input, "author") && !author) co_return refuse("there is nobody here with that alias");
    std::optional<std::chrono::sys_seconds> after;
    std::optional<std::chrono::sys_seconds> before;
    if (const auto text = text_in(input, "after")) {
        after = parse_utc(*text);
        if (!after) co_return refuse("after must be a date, like 2026-10-28 or 2026-10-28T15:00");
    }
    if (const auto text = text_in(input, "before")) {
        before = parse_utc(*text);
        if (!before) co_return refuse("before must be a date, like 2026-10-28 or 2026-10-28T15:00");
    }
    const std::size_t wanted =
        static_cast<std::size_t>(std::clamp<std::int64_t>(number_in(input, "limit").value_or(10), 1, search_results));

    std::vector<std::string> found;
    dpp::snowflake from = channel->id == context.channel_id ? context.message_id : dpp::snowflake{};
    int read = 0;
    for (int page = 0; page < search_pages && found.size() < wanted; ++page) {
        const auto messages = co_await services.discord->get_messages(channel->id, from, 100);
        if (!messages.has_value()) co_return refuse(std::format("could not read #{}: {}", channel->name, messages.error().message));
        if (messages.value().empty()) break;
        for (const dpp::message& message : messages.value()) {
            ++read;
            from = message.id;
            const std::chrono::sys_seconds at{std::chrono::seconds{static_cast<std::int64_t>(message.id.get_creation_time())}};
            if (before && at >= *before) continue;
            if (after && at < *after) continue;
            if (author && message.author.id != *author) continue;
            if (!query.empty() && !util::to_lower(message.content).contains(query)) continue;
            found.push_back(std::format("- {} at {} by {}: {}", message.id, when(at), author_of(message, context, services.bot_id()),
                                        util::truncate(sanitized(message.content, context), shown_snippet)));
            if (found.size() >= wanted) break;
        }
        if (after && from.get_creation_time() < static_cast<double>(after->time_since_epoch().count())) break;
    }

    if (found.empty()) co_return ok(std::format("nothing matches in the last {} messages of #{}", read, channel->name));
    std::string text = std::format("{} match(es) in #{}, newest first, from the last {} messages:\n", found.size(), channel->name, read);
    for (const std::string& line : found) {
        text += line + '\n';
    }
    co_return ok(text);
}

auto user_info(const discord_tool_services& services, const json& input, const tool_context& context) -> tool_outcome {
    const std::optional<dpp::snowflake> named = person(input, "user", context);
    if (text_in(input, "user") && !named) return refuse("there is nobody here with that alias");
    const dpp::snowflake user = named.value_or(context.author_id);

    const auto details = services.discord->member_details(context.guild_id, user);
    if (!details) return refuse("they are not a member here, or not one the bot knows of yet");

    std::string text = std::format("{}: account made {}", shown_as(user, context), when(details->created_at));
    if (details->joined_at) text += std::format(", joined this server {}", when(*details->joined_at));
    if (details->is_bot) text += ", a bot";
    if (details->owns_server) text += ", owns this server";
    if (details->boosting_since) text += std::format(", boosting since {}", when(*details->boosting_since));
    text += ".\nRoles: ";
    if (details->roles.empty()) text += "none";
    for (std::size_t index = 0; index < details->roles.size(); ++index) {
        if (index > 0) text += ", ";
        text += details->roles[index];
    }
    return ok(text);
}

auto server_info(const discord_tool_services& services, const tool_context& context) -> tool_outcome {
    const auto server = services.discord->server_details(context.guild_id);
    if (!server) return refuse("the bot does not know this server yet");

    std::string text = std::format("{}: made {}, {} members, boost level {} ({} boosts).\n", server->name, when(server->created_at),
                                   server->member_count, server->boost_tier, server->boosts);
    text += "Channels:";
    for (const ports::channel_details& channel : server->channels) {
        if (channel.kind == "category" || channel.kind == "thread") continue;
        text += std::format(" #{} ({}{})", channel.name, channel.kind, channel.parent.empty() ? "" : ", in " + channel.parent);
    }
    text += "\nRoles:";
    for (std::size_t index = 0; index < server->roles.size() && index < 50; ++index) {
        text += std::format("{} {}", index == 0 ? "" : ",", server->roles[index]);
    }
    text += std::format("\nCustom emoji: {}", server->emoji.size());
    for (std::size_t index = 0; index < server->emoji.size() && index < 50; ++index) {
        const std::string& emoji = server->emoji[index];
        text += std::format("{} :{}:", index == 0 ? "" : ",", emoji.substr(0, emoji.find(':')));
    }
    return ok(util::truncate(text, 4000));
}

auto channel_info(const discord_tool_services& services, const json& input, const tool_context& context) -> tool_outcome {
    const auto channel = channel_named(input, context, *services.discord);
    if (!channel) return refuse(channel.error());
    std::string text = std::format("#{}: a {} channel", channel->name, channel->kind);
    if (!channel->parent.empty()) text += std::format(", in {}", channel->parent);
    if (channel->age_restricted) text += ", age-restricted";
    text += std::format(", made {}", creation_of(channel->id));
    text += channel->topic.empty() ? std::string(". No topic.") : std::format(".\nTopic: {}", sanitized(channel->topic, context));
    return ok(text);
}

// --------------------------------------------------------------------------
// Changing things
// --------------------------------------------------------------------------

auto set_nickname(const discord_tool_services& services, json input, tool_context context) -> dpp::task<tool_outcome> {
    if (auto refused = refuses_bots(context)) co_return *refused;
    const std::optional<dpp::snowflake> user = person(input, "user", context);
    if (!user) co_return refuse("set_nickname needs the alias of whose nickname to change");
    if (*user == services.bot_id()) co_return refuse("that is me; my nickname is not mine to change this way");

    std::optional<std::string> nickname;
    if (const auto written = text_in(input, "nickname")) nickname = restored(*written, context);
    if (nickname && util::character_count(*nickname) > nickname_limit) {
        co_return refuse(std::format("a nickname is at most {} characters", nickname_limit));
    }

    const auto changed = services.nicknames != nullptr
                             ? co_await services.nicknames->change(context.guild_id, *user, nickname, context.author_id)
                             : co_await services.discord->set_nickname(context.guild_id, *user, nickname);
    if (!changed.has_value()) {
        co_return refuse(
            std::format("Discord refused: {} (the bot needs Manage Nicknames and a role above theirs, and nobody can rename "
                        "the server's owner)",
                        changed.error().message));
    }
    util::log().info("the model set {}'s nickname in guild {} to {}, asked by {}", *user, context.guild_id,
                     nickname ? std::format("\"{}\"", *nickname) : std::string("nothing"), context.author_id);
    co_return ok(nickname ? std::format("{}'s nickname is now set", shown_as(*user, context))
                          : std::format("{}'s nickname is cleared", shown_as(*user, context)));
}

auto set_status(const discord_tool_services& services, const json& input, const tool_context& context) -> tool_outcome {
    if (auto refused = refuses_bots(context)) return *refused;
    if (services.status == nullptr) return refuse("the status cannot be set from here");
    if (!services.trusted || !services.trusted(context.guild_id)) {
        return refuse("only a trusted server may set the status, since every server sees it");
    }
    const auto text = text_in(input, "text");
    if (!text || util::character_count(*text) > status_limit) {
        return refuse(std::format("set_status needs the text, at most {} characters", status_limit));
    }
    const std::string type = text_in(input, "type").value_or("custom");
    constexpr std::array<std::string_view, 5> types{"playing", "watching", "listening", "competing", "custom"};
    if (std::ranges::find(types, type) == types.end()) return refuse("type is one of playing, watching, listening, competing or custom");

    services.status->set(type, restored(*text, context));
    util::log().info("the model set the status to {} \"{}\", asked by {} in guild {}", type, *text, context.author_id, context.guild_id);
    return ok("the status is set, in every server");
}

auto add_reaction(const discord_tool_services& services, json input, tool_context context) -> dpp::task<tool_outcome> {
    if (auto refused = refuses_bots(context)) co_return *refused;
    const auto written = text_in(input, "emoji");
    if (!written) co_return refuse("add_reaction needs an emoji");

    message_place place{.channel_id = context.channel_id, .message_id = context.message_id};
    if (const auto named = text_in(input, "message")) {
        const auto found = find_message_place(*named, context.guild_id, context.channel_id);
        if (!found) co_return refuse(found.error());
        place = *found;
    }
    const auto emoji = reaction_emoji(*written, services.discord->server_details(context.guild_id));
    if (!emoji) co_return refuse(std::format("this server has no emoji called {}", *written));

    const auto added = co_await services.discord->add_reaction(place.channel_id, place.message_id, *emoji);
    if (!added.has_value()) co_return refuse(std::format("Discord refused: {}", added.error().message));
    co_return ok("reacted");
}

auto pin_message(const discord_tool_services& services, json input, tool_context context) -> dpp::task<tool_outcome> {
    if (auto refused = refuses_bots(context)) co_return *refused;
    const auto written = text_in(input, "message");
    if (!written) co_return refuse("pin_message needs the message's id or link");
    const auto place = find_message_place(*written, context.guild_id, context.channel_id);
    if (!place) co_return refuse(place.error());

    // Pinning is the asker's to do, not just the bot's.
    const std::uint64_t theirs = services.discord->member_permissions(context.guild_id, place->channel_id, context.author_id).value_or(0);
    if ((theirs & (dpp::p_manage_messages | dpp::p_administrator)) == 0) {
        co_return refuse("whoever asked needs Manage Messages in that channel to have messages pinned");
    }

    const bool pin = flag_in(input, "pin").value_or(true);
    const auto changed = co_await services.discord->set_pinned(place->channel_id, place->message_id, pin);
    if (!changed.has_value()) co_return refuse(std::format("Discord refused: {}", changed.error().message));
    util::log().info("the model {} message {} in guild {}, asked by {}", pin ? "pinned" : "unpinned", place->message_id, context.guild_id,
                     context.author_id);
    co_return ok(pin ? "pinned" : "unpinned");
}

auto create_poll(const discord_tool_services& services, json input, tool_context context) -> dpp::task<tool_outcome> {
    if (auto refused = refuses_bots(context)) co_return *refused;
    const auto question = text_in(input, "question");
    if (!question || util::character_count(*question) > poll_question_limit) {
        co_return refuse(std::format("create_poll needs a question of at most {} characters", poll_question_limit));
    }
    const auto answers = input.find("answers");
    if (answers == input.end() || !answers->is_array() || answers->size() < 2 || answers->size() > 10) {
        co_return refuse("create_poll needs between 2 and 10 answers");
    }

    dpp::poll poll;
    poll.set_question(restored(*question, context));
    for (const json& answer : *answers) {
        if (!answer.is_string() || util::is_blank(answer.get<std::string>())) co_return refuse("each answer must be some text");
        const std::string text = restored(answer.get<std::string>(), context);
        if (util::character_count(text) > poll_answer_limit) {
            co_return refuse(std::format("an answer is at most {} characters", poll_answer_limit));
        }
        poll.add_answer(text);
    }
    poll.set_duration(static_cast<std::uint32_t>(std::clamp<std::int64_t>(number_in(input, "hours").value_or(24), 1, poll_longest_hours)));
    poll.set_allow_multiselect(flag_in(input, "multiple").value_or(false));

    dpp::message message(context.channel_id, "");
    message.set_poll(poll);
    message.set_allowed_mentions(false, false, false, false);
    const auto posted = co_await services.post(std::move(message), context.persona_id, context.guild_id);
    if (!posted.has_value()) co_return refuse(std::format("Discord refused: {}", posted.error().message));
    util::log().info("the model posted a poll in channel {}, asked by {}", context.channel_id, context.author_id);
    co_return ok(std::format("the poll is posted, as message {}", posted.value().id));
}

auto set_reminder(const discord_tool_services& services, const json& input, const tool_context& context) -> tool_outcome {
    if (auto refused = refuses_bots(context)) return *refused;
    if (services.reminders == nullptr) return refuse("reminders are not kept here");
    const auto text = text_in(input, "text");
    if (!text || util::character_count(*text) > reminder_length_limit) {
        return refuse(std::format("set_reminder needs the text, at most {} characters", reminder_length_limit));
    }

    std::optional<std::chrono::sys_seconds> due;
    if (const auto minutes = number_in(input, "in_minutes")) {
        if (*minutes < 1) return refuse("in_minutes must be at least 1");
        due = context.now + std::chrono::minutes{*minutes};
    } else if (const auto at = text_in(input, "at")) {
        due = parse_utc(*at);
        if (!due) return refuse("at must be a UTC date and time, like 2026-10-28T15:00");
    } else {
        return refuse("set_reminder needs in_minutes or at");
    }
    if (*due <= context.now) return refuse("that time has passed");
    if (*due > context.now + longest_reminder) return refuse("a reminder may be at most a year ahead");

    const std::optional<dpp::snowflake> named = person(input, "user", context);
    if (text_in(input, "user") && !named) return refuse("there is nobody here with that alias");
    if (services.reminders->count(context.guild_id) >= reminders_per_guild) {
        return refuse(std::format("this server already has {} reminders waiting, the most it may", reminders_per_guild));
    }

    const dpp::snowflake user = named.value_or(context.author_id);
    const std::int64_t id = services.reminders->add({.id = 0,
                                                     .guild_id = context.guild_id,
                                                     .channel_id = context.channel_id,
                                                     .user_id = user,
                                                     .set_by = context.author_id,
                                                     .persona_id = context.persona_id,
                                                     .due_at = *due,
                                                     .text = restored(*text, context)});
    util::log().info("the model set reminder {} in channel {} for {} at {}, asked by {}", id, context.channel_id, user, when(*due),
                     context.author_id);
    return ok(std::format("set: {} will be reminded here at {}", shown_as(user, context), when(*due)));
}

auto schema(json properties, std::vector<std::string> required = {}) -> json {
    return {{"type", "object"}, {"properties", std::move(properties)}, {"required", required}};
}

auto described(const char* description, const char* type = "string") -> json {
    return {{"type", type}, {"description", description}};
}

} // namespace

auto find_message_place(std::string_view text, dpp::snowflake guild, dpp::snowflake channel) -> std::expected<message_place, std::string> {
    const std::string_view trimmed = util::trim(text);
    if (const auto id = util::parse_snowflake(trimmed)) return message_place{.channel_id = channel, .message_id = *id};

    // https://discord.com/channels/<guild>/<channel>/<message>, from any of
    // Discord's hosts.
    const std::size_t at = trimmed.find("/channels/");
    if (at == std::string_view::npos) return std::unexpected(std::string("that is neither a message id nor a link to a message"));
    std::vector<std::string_view> parts;
    std::string_view rest = trimmed.substr(at + std::string_view("/channels/").size());
    while (!rest.empty()) {
        const std::size_t slash = rest.find('/');
        parts.push_back(rest.substr(0, slash));
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1);
    }
    if (parts.size() < 3) return std::unexpected(std::string("that link does not point at a message"));
    if (parts[0] == "@me") return std::unexpected(std::string("that message is in a DM, which the bot does not read"));
    const auto in_guild = util::parse_snowflake(parts[0]);
    const auto in_channel = util::parse_snowflake(parts[1]);
    const auto message = util::parse_snowflake(parts[2]);
    if (!in_guild || !in_channel || !message) return std::unexpected(std::string("that link does not point at a message"));
    if (*in_guild != guild) return std::unexpected(std::string("that message is in another server"));
    return message_place{.channel_id = *in_channel, .message_id = *message};
}

auto reaction_emoji(std::string_view written, const std::optional<ports::server_details>& server) -> std::optional<std::string> {
    std::string_view name = util::trim(written);
    if (name.starts_with("<") && name.ends_with(">")) {
        // <:name:id> or <a:name:id>: Discord takes name:id.
        name = name.substr(1, name.size() - 2);
        if (name.starts_with("a:")) name.remove_prefix(2);
        if (name.starts_with(':')) name.remove_prefix(1);
        return std::string(name);
    }
    const bool ascii = std::ranges::all_of(name, [](char letter) { return static_cast<unsigned char>(letter) < 0x80; });
    if (!ascii) return std::string(name);

    // Plain letters are a custom emoji's name, with or without its colons.
    if (name.starts_with(':') && name.ends_with(':') && name.size() > 2) name = name.substr(1, name.size() - 2);
    if (!server) return std::nullopt;
    for (const std::string& emoji : server->emoji) {
        if (util::equals_ignoring_case(std::string_view(emoji).substr(0, emoji.find(':')), name)) return emoji;
    }
    return std::nullopt;
}

auto add_discord_tools(tool_registry& tools, discord_tool_services services) -> void {
    // Each handler keeps its own copy of the services: they hold pointers
    // and functions, cheap to copy, and outlive every call.
    tools.add({.name = "current_time",
               .description = "The date and time now, in UTC or a time zone such as Europe/London or America/New_York.",
               .input_schema = schema({{"timezone", described("An IANA time zone name; UTC when left out.")}})},
              [](const json& input, const tool_context& context) { return current_time(input, context); });

    tools.add_async(
        {.name = "get_message",
         .description = "Read one message by its id, in this channel, or by a link to it, in this server: who wrote it, when, its text, "
                        "attachments and reactions.",
         .input_schema = schema({{"message", described("The message's id, or a link to it.")}}, {"message"})},
        [services](json input, tool_context context) { return get_message(services, std::move(input), std::move(context)); });

    tools.add_async(
        {.name = "search_messages",
         .description =
             std::format("Search a channel's recent history, the last {} messages at most, newest first, by text, author or date. "
                         "Discord gives bots no real search, so older messages cannot be found.",
                         search_pages * 100),
         .input_schema = schema({{"query", described("Text the message contains, any case. Write a person's name as their marker.")},
                                 {"author", described("The alias of whoever wrote it.")},
                                 {"channel", described("A channel's name or id; this one when left out.")},
                                 {"after", described("Only messages from this UTC date or time on, like 2026-10-28T15:00.")},
                                 {"before", described("Only messages before this UTC date or time.")},
                                 {"limit", described("How many to return, 1 to 20; 10 when left out.", "integer")}})},
        [services](json input, tool_context context) { return search_messages(services, std::move(input), std::move(context)); });

    tools.add({.name = "user_info",
               .description = "About a member: when they joined and made their account, their roles, and whether they are a bot, own the "
                              "server or boost it. Never their name.",
               .input_schema = schema({{"user", described("Their alias; whoever you are answering when left out.")}})},
              [services](const json& input, const tool_context& context) { return user_info(services, input, context); });

    tools.add({.name = "server_info",
               .description = "About this server: when it was made, how many members, its boost level, channels, roles and custom emoji.",
               .input_schema = schema(json::object())},
              [services](const json&, const tool_context& context) { return server_info(services, context); });

    tools.add({.name = "channel_info",
               .description = "About a channel: its kind, category, topic and whether it is age-restricted.",
               .input_schema = schema({{"channel", described("A channel's name or id; this one when left out.")}})},
              [services](const json& input, const tool_context& context) { return channel_info(services, input, context); });

    tools.add_async(
        {.name = "set_nickname",
         .description = std::format("Change someone's nickname in this server, or clear it, at most {} characters. Recorded in their "
                                    "nickname history as asked for by whoever you are answering.",
                                    nickname_limit),
         .input_schema = schema({{"user", described("The alias of whose nickname to change.")},
                                 {"nickname", described("The new nickname; leave it out to clear theirs.")}},
                                {"user"})},
        [services](json input, tool_context context) { return set_nickname(services, std::move(input), std::move(context)); });

    tools.add({.name = "set_status",
               .description = "Set your own status, which every server sees. Only a trusted server may.",
               .input_schema = schema({{"text", described("What it says, at most 128 characters.")},
                                       {"type",
                                        {{"type", "string"},
                                         {"enum", json::array({"playing", "watching", "listening", "competing", "custom"})},
                                         {"description", "How it reads; custom when left out."}}}},
                                      {"text"})},
              [services](const json& input, const tool_context& context) { return set_status(services, input, context); });

    tools.add_async({.name = "add_reaction",
                     .description = "React to a message with an emoji: a Unicode one, or one of this server's custom emoji by name.",
                     .input_schema = schema({{"emoji", described("The emoji, like 👍, or a custom one's name, like :partyparrot:.")},
                                             {"message", described("The message's id or link; the one you are answering when left out.")}},
                                            {"emoji"})},
                    [services](json input, tool_context context) { return add_reaction(services, std::move(input), std::move(context)); });

    tools.add_async({.name = "pin_message",
                     .description = "Pin a message, or unpin it. Whoever you are answering needs Manage Messages.",
                     .input_schema = schema({{"message", described("The message's id or link.")},
                                             {"pin", described("False to unpin; true when left out.", "boolean")}},
                                            {"message"})},
                    [services](json input, tool_context context) { return pin_message(services, std::move(input), std::move(context)); });

    tools.add_async(
        {.name = "create_poll",
         .description = "Post a Discord poll in this channel, as yourself.",
         .input_schema = schema(
             {{"question", described("The question, at most 300 characters.")},
              {"answers",
               {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "2 to 10 answers, each at most 55 characters."}}},
              {"hours", described("How long it is open, 1 to 768; 24 when left out.", "integer")},
              {"multiple", described("Whether people may pick more than one answer.", "boolean")}},
             {"question", "answers"})},
        [services](json input, tool_context context) { return create_poll(services, std::move(input), std::move(context)); });

    tools.add({.name = "set_reminder",
               .description = "Remind someone in this channel later: you post the reminder, mentioning them, when it is due. Give "
                              "in_minutes, or at as a UTC date and time.",
               .input_schema = schema({{"text", described("What to remind them of, written as you would say it then.")},
                                       {"in_minutes", described("How many minutes from now.", "integer")},
                                       {"at", described("When, in UTC, like 2026-10-28T15:00.")},
                                       {"user", described("The alias of whom to remind; whoever you are answering when left out.")}},
                                      {"text"})},
              [services](const json& input, const tool_context& context) { return set_reminder(services, input, context); });
}

auto deliver_reminders(const discord_tool_services& services, std::chrono::sys_seconds now) -> dpp::task<void> {
    if (services.reminders == nullptr) co_return;
    for (const reminder& due : services.reminders->due(now)) {
        // Forgotten first: one that fails to post is not posted twice.
        services.reminders->remove(due.id);

        dpp::message message(due.channel_id, std::format("<@{}> ⏰ {}", due.user_id, due.text));
        message.allowed_mentions.parse_users = false;
        message.allowed_mentions.users = {due.user_id};
        const auto posted = co_await services.post(std::move(message), due.persona_id, due.guild_id);
        if (posted.has_value()) {
            util::log().info("posted reminder {} in channel {} for {}", due.id, due.channel_id, due.user_id);
        } else {
            util::log().warn("could not post reminder {} in channel {}: {}", due.id, due.channel_id, posted.error().message);
        }
    }
}

} // namespace latibot::llm
