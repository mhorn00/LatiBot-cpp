#include "core/commands/linkstats.hpp"

#include "core/commands/options.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/ui/paginator.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/cluster.h>
#include <dpp/dispatcher.h>
#include <dpp/permissions.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <utility>
#include <vector>

namespace latibot::commands {
namespace {

/// Autocomplete shows at most this many choices; Discord's limit is 25.
constexpr std::size_t emoji_choices = 25;

auto is_word(std::string_view text) -> bool {
    return !text.empty() && std::ranges::all_of(text, [](unsigned char letter) { return std::isalnum(letter) != 0 || letter == '_'; });
}

auto format_day(std::chrono::sys_seconds when) -> std::string {
    return std::format("{:%Y-%m-%d}", std::chrono::floor<std::chrono::days>(when));
}

/// "since 2024-01-01", "until 2024-06-30", both, or nothing for all time.
auto describe_window(const events::stat_query& query) -> std::string {
    std::string text;
    if (query.since) text += " since " + format_day(*query.since);
    if (query.until) {
        // The bound is exclusive, and the day before it is the one typed.
        text += " until " + format_day(*query.until - std::chrono::days{1});
    }
    return text;
}

/// The since, until and domain options, or what was wrong with them.
auto read_window(const dpp::slashcommand_t& event, events::stat_query& query) -> std::optional<std::string> {
    const std::string since = string_option(event, "since");
    const std::string until = string_option(event, "until");

    if (!since.empty()) {
        const auto day = parse_day(since);
        if (!day) return std::format("\"{}\" isn't a date i can read; use YYYY-MM-DD", since);
        query.since = std::chrono::sys_seconds(*day);
    }
    if (!until.empty()) {
        const auto day = parse_day(until);
        if (!day) return std::format("\"{}\" isn't a date i can read; use YYYY-MM-DD", until);
        // Inclusive as typed, so the whole of that day counts.
        query.until = std::chrono::sys_seconds(*day + std::chrono::days{1});
    }
    if (query.since && query.until && *query.since >= *query.until) return std::string("that range ends before it starts");

    // The site, reduced the way rules are, so "https://www.X.com" and
    // "x.com" ask for the same thing.
    if (const std::string site = string_option(event, "domain"); !site.empty()) {
        query.domain = events::normalise_domain(site);
        if (!query.domain) return std::format("\"{}\" doesn't look like a site; try something like x.com", site);
    }
    return std::nullopt;
}

auto emoji_list(std::span<const events::emoji_tally> tallies) -> std::string {
    std::string text;
    for (const events::emoji_tally& tally : tallies) {
        if (!text.empty()) text += ", ";
        text += std::format("{} {}", events::display_emoji(tally.emoji), tally.count);
    }
    return text;
}

auto date_option(const char* name, const char* description) -> dpp::command_option {
    return dpp::command_option(dpp::co_string, name, description, false).set_max_length(10);
}

auto domain_option() -> dpp::command_option {
    return dpp::command_option(dpp::co_string, "domain", "Only links to this site, like x.com.", false)
        .set_auto_complete(true)
        .set_max_length(domain_length_limit);
}

auto emoji_option(const char* name, const char* description, bool required) -> dpp::command_option {
    return dpp::command_option(dpp::co_string, name, description, required).set_auto_complete(true).set_max_length(100);
}

} // namespace

auto parse_day(std::string_view text) -> std::optional<std::chrono::sys_days> {
    text = util::trim(text);
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return std::nullopt;

    const auto number = [&](std::size_t at, std::size_t length) -> std::optional<int> {
        int value = 0;
        const char* begin = text.data() + at;
        const auto [stop, error] = std::from_chars(begin, begin + length, value);
        if (error != std::errc{} || stop != begin + length) return std::nullopt;
        return value;
    };

    const auto year = number(0, 4);
    const auto month = number(5, 2);
    const auto day = number(8, 2);
    if (!year || !month || !day) return std::nullopt;

    const std::chrono::year_month_day date{std::chrono::year{*year}, std::chrono::month{static_cast<unsigned>(*month)},
                                           std::chrono::day{static_cast<unsigned>(*day)}};
    if (!date.ok()) return std::nullopt;
    return std::chrono::sys_days(date);
}

auto board_from_string(std::string_view name) -> std::optional<board> {
    if (name.empty() || name == "received") return board::received;
    if (name == "given") return board::given;
    if (name == "self") return board::self;
    if (name == "emoji") return board::emoji;
    return std::nullopt;
}

auto resolve_emoji(const events::reaction_store& store, dpp::snowflake guild_id, std::string_view typed)
    -> std::optional<events::emoji_ref> {
    std::string_view text = util::trim(typed);
    if (text.size() > 2 && text.starts_with(':') && text.ends_with(':')) text = text.substr(1, text.size() - 2);

    auto parsed = events::parse_emoji(text);
    if (!parsed) return std::nullopt;

    // A name rather than an emoji: find the one this guild has used.
    if (parsed->key.starts_with("u:") && is_word(parsed->name)) {
        for (const events::emoji_tally& known : store.known_emojis(guild_id, parsed->name, emoji_choices)) {
            if (util::equals_ignoring_case(known.emoji.name, parsed->name)) return known.emoji;
        }
        return std::nullopt;
    }

    // A key from autocomplete carries no name; fill it in for showing.
    if (parsed->name.empty()) return store.describe(parsed->key);
    return parsed;
}

namespace {

constexpr char board_separator = ';';

auto board_letter(board which) -> char {
    switch (which) {
    case board::given:
        return 'g';
    case board::self:
        return 's';
    case board::emoji:
        return 'e';
    case board::received:
        break;
    }
    return 'r';
}

/// Which side of a reaction a board counts. The emoji board counts what
/// people received, leaving self-reactions out like every other board.
auto kind_for(board which) -> events::stat_kind {
    switch (which) {
    case board::given:
        return events::stat_kind::given;
    case board::self:
        return events::stat_kind::self;
    case board::received:
    case board::emoji:
        break;
    }
    return events::stat_kind::received;
}

auto whole_number(std::string_view text) -> std::optional<std::int64_t> {
    std::int64_t value = 0;
    const auto [stop, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || stop != text.data() + text.size()) return std::nullopt;
    return value;
}

auto board_title(const events::reaction_store& store, board which, const events::stat_query& query) -> std::string {
    const std::string emoji = query.emoji_key ? events::display_emoji(store.describe(*query.emoji_key)) + " " : std::string("reactions ");
    const std::string site = query.domain ? *query.domain + " " : std::string{};
    const std::string window = describe_window(query);

    switch (which) {
    case board::given:
        return std::format("Most {}given on replaced {}links{}", emoji, site, window);
    case board::self:
        return std::format("Most {}on their own {}links{}", emoji, site, window);
    case board::emoji:
        return std::format("Most used reactions on replaced {}links{}", site, window);
    case board::received:
        break;
    }
    return std::format("Most {}received on replaced {}links{}", emoji, site, window);
}

} // namespace

auto encode_board(board which, const events::stat_query& query) -> std::string {
    const auto day = [](const std::optional<std::chrono::sys_seconds>& when) {
        return when ? std::to_string(std::chrono::floor<std::chrono::days>(*when).time_since_epoch().count()) : std::string{};
    };
    return std::format("{1}{0}{2}{0}{3}{0}{4}{0}{5}", board_separator, board_letter(which), query.emoji_key.value_or(std::string{}),
                       query.domain.value_or(std::string{}), day(query.since), day(query.until));
}

auto decode_board(std::string_view argument) -> std::optional<std::pair<board, events::stat_query>> {
    // Split on ';' into exactly the five fields `encode_board` writes: board
    // letter, emoji key, site, since and until. Anything else was not made
    // here and is refused whole.
    std::vector<std::string_view> fields;
    while (true) {
        const std::size_t cut = argument.find(board_separator);
        fields.push_back(argument.substr(0, cut));
        if (cut == std::string_view::npos) break;
        argument.remove_prefix(cut + 1);
    }
    if (fields.size() != 5 || fields[0].size() != 1) return std::nullopt;

    board which = board::received;
    switch (fields[0].front()) {
    case 'r':
        break;
    case 'g':
        which = board::given;
        break;
    case 's':
        which = board::self;
        break;
    case 'e':
        which = board::emoji;
        break;
    default:
        return std::nullopt;
    }

    events::stat_query query;
    query.kind = kind_for(which);
    if (!fields[1].empty()) query.emoji_key = std::string(fields[1]);
    if (!fields[2].empty()) query.domain = std::string(fields[2]);
    for (const auto& [text, bound] : {std::pair{fields[3], &query.since}, std::pair{fields[4], &query.until}}) {
        if (text.empty()) continue;
        const auto days = whole_number(text);
        if (!days) return std::nullopt;
        *bound = std::chrono::sys_seconds(std::chrono::sys_days(std::chrono::days(*days)));
    }
    return std::pair{which, query};
}

auto render_board(const events::reaction_store& store, dpp::snowflake guild_id, board which, const events::stat_query& query, int page)
    -> dpp::message {
    const bool by_emoji = which == board::emoji;
    const auto total =
        static_cast<std::size_t>(std::max<std::int64_t>(0, by_emoji ? store.emojis(guild_id, query) : store.people(guild_id, query)));
    const int current = ui::clamp_page(page, total, leaderboard_size);
    const ui::page_range window = ui::range_for(current, total, leaderboard_size);

    std::string text = std::format("**{}**\n", board_title(store, which, query));
    std::size_t place = window.begin;

    if (by_emoji) {
        for (const events::emoji_tally& tally : store.emoji_breakdown(guild_id, query, leaderboard_size, window.begin)) {
            text += std::format("{}. {} {}\n", ++place, events::display_emoji(tally.emoji), tally.count);
        }
    } else {
        for (const events::person_tally& tally : store.leaderboard(guild_id, query, leaderboard_size, window.begin)) {
            text += std::format("{}. <@{}> {}\n", ++place, tally.user_id, tally.count);
        }
    }

    if (total == 0) {
        text +=
            "Nothing counted yet. Reactions are counted from when the bot first saw them; `/linkstats recompute` fills in "
            "older ones.";
    } else if (total > leaderboard_size) {
        text += std::format("\n_{}_", ui::page_label(current, total, leaderboard_size));
    }

    dpp::message reply(text);
    if (const auto row = ui::controls({.view = std::string(board_view), .page = current, .argument = encode_board(which, query)}, total,
                                      leaderboard_size)) {
        reply.add_component(*row);
    }
    return reply;
}

auto render_profile(const events::reaction_store& store, dpp::snowflake guild_id, dpp::snowflake user_id, const events::stat_query& window)
    -> std::string {
    events::stat_query query = window;
    query.user_id = user_id;

    query.kind = events::stat_kind::received;
    const std::int64_t received = store.total(guild_id, query);
    const auto received_top = store.emoji_breakdown(guild_id, query, profile_emojis);

    query.kind = events::stat_kind::given;
    const std::int64_t given = store.total(guild_id, query);
    const auto given_top = store.emoji_breakdown(guild_id, query, profile_emojis);

    query.kind = events::stat_kind::self;
    const std::int64_t self = store.total(guild_id, query);

    const std::string site = window.domain ? std::format(" on {} links", *window.domain) : std::string{};
    std::string text = std::format("**Link stats for <@{}>{}{}**\n", user_id, site, describe_window(window));
    text += std::format("Reactions received: {}{}\n", received, received_top.empty() ? "" : " (" + emoji_list(received_top) + ")");
    text += std::format("Reactions given: {}{}\n", given, given_top.empty() ? "" : " (" + emoji_list(given_top) + ")");
    text += std::format("Reacted to their own links: {} time{}\n", self, self == 1 ? "" : "s");
    return text;
}

auto render_duplicates(const events::reaction_store& store, dpp::snowflake guild_id) -> std::string {
    const auto groups = store.likely_duplicates(guild_id);
    if (groups.empty()) return "No two custom emojis here share a name, so nothing looks duplicated.";

    std::string text = "**Custom emojis that share a name**\nMerge one into another with `/linkstats alias add`.\n";
    for (const auto& group : groups) {
        text += std::format("- **{}**: {}\n", group.front().emoji.name, emoji_list(group));
        if (text.size() > 1800) {
            text += "…and more\n";
            break;
        }
    }
    return text;
}

auto render_aliases(const events::reaction_store& store, dpp::snowflake guild_id) -> std::string {
    const auto aliases = store.aliases(guild_id);
    if (aliases.empty()) return "No emoji aliases here. `/linkstats emojis` lists likely candidates.";

    std::string text = "**Emoji aliases**\n";
    for (const events::emoji_alias& alias : aliases) {
        text += std::format("- {} counts as {}\n", events::display_emoji(alias.emoji), events::display_emoji(alias.canonical));
        if (text.size() > 1800) {
            text += "…and more\n";
            break;
        }
    }
    return text;
}

// --------------------------------------------------------------------------

auto render_backfill(const events::backfill_report& report, const events::backfill_request& request, bool finished) -> std::string {
    std::string range = std::format("since {}", format_day(request.since));
    if (request.until) range += std::format(" until {}", format_day(*request.until - std::chrono::days{1}));

    if (!finished) {
        return std::format(
            "Recomputing link stats {}…\nChannel {} of {}: {} messages scanned, {} replacements found, {} reactions "
            "recorded.\n`/linkstats recompute cancel` stops it; running it again carries on from here.",
            range, std::min(report.channels_done + 1, report.channels_total), report.channels_total, report.scanned, report.replacements,
            report.reactions);
    }

    std::string text = std::format("**Link stats {} {}**\n", report.cancelled ? "recompute stopped" : "recomputed", range);
    text += std::format("Channels: {} of {}\n", report.channels_done, report.channels_total);
    text += std::format("Messages scanned: {}\n", report.scanned);
    text += std::format("Replacements found: {} ({} credited to whoever posted the link, {} not)\n", report.replacements, report.attributed,
                        report.unattributed);
    if (report.mismatched > 0) {
        // Reported rather than accepted (plan §9.7); the log has each id.
        text += std::format("Of those not credited, {} followed a link that wasn't the one replaced\n", report.mismatched);
    }
    if (report.webhooks_skipped > 0) text += std::format("Webhook replacements skipped: {}\n", report.webhooks_skipped);
    text += std::format("Reactions recorded: {}\n", report.reactions);

    // Listed by id rather than guessed at (plan §9.7). The log has all of
    // them; a message has room for some.
    if (!report.unparsed.empty()) {
        text += std::format("Not understood: {}", report.unparsed.size());
        std::string ids;
        for (std::size_t index = 0; index < std::min<std::size_t>(report.unparsed.size(), 15); ++index) {
            ids += std::format(" `{}`", report.unparsed[index]);
        }
        text += ids;
        text += report.unparsed.size() > 15 ? " and more, all in the log\n" : "\n";
    }

    for (std::size_t index = 0; index < std::min<std::size_t>(report.problems.size(), 5); ++index) {
        text += std::format("- {}\n", report.problems[index]);
    }

    if (report.cancelled) text += "Running it again with the same dates carries on from where it stopped.";
    return text;
}

namespace {

/// Shows a recompute's progress. A plain function rather than a capturing
/// lambda, since a coroutine lambda's captures die with the lambda.
auto show_progress(ports::discord_gateway& discord, dpp::message message) -> dpp::task<void> {
    const auto edited = co_await discord.edit_message(std::move(message));
    if (!edited.ok()) util::log().debug("could not update the recompute progress message: {}", edited.error().message);
}

} // namespace

linkstats_command::linkstats_command(events::reaction_store& store, recompute_support recompute)
    : info_{.name = "linkstats",
            .description = "Who gets the most reactions on the links the bot replaced.",
            .aliases = {},
            // History is only read by a recompute, but the permission check
            // should name it before somebody runs one and gets nothing.
            .required_bot_permissions = dpp::p_send_messages | dpp::p_read_message_history,
            .default_member_permissions = std::nullopt,
            .guild_only = true,
            // The boards are for the room. Changing aliases and running a
            // recompute are answered privately, and the recompute reports its
            // progress in the channel, as a post.
            .responses = {.result = dpp::m_suppress_notifications, .refusal = dpp::m_ephemeral, .post = dpp::m_suppress_notifications},
            .subcommand_responses = {{"alias add", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}},
                                     {"alias remove", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}},
                                     {"recompute start", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}},
                                     {"recompute cancel", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}}}},
      store_(&store),
      recompute_(std::move(recompute)) {}

auto linkstats_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);

    dpp::command_option by(dpp::co_string, "by", "What to rank. Received if left out.", false);
    by.add_choice(dpp::command_option_choice("Reactions received", std::string("received")));
    by.add_choice(dpp::command_option_choice("Reactions given", std::string("given")));
    by.add_choice(dpp::command_option_choice("Reactions to your own links", std::string("self")));
    by.add_choice(dpp::command_option_choice("Most used emojis", std::string("emoji")));

    dpp::command_option top(dpp::co_sub_command, "top", "A leaderboard.");
    top.add_option(by);
    top.add_option(emoji_option("emoji", "Only this emoji.", false));
    top.add_option(date_option("since", "From this day, YYYY-MM-DD."));
    top.add_option(date_option("until", "Up to and including this day, YYYY-MM-DD."));
    top.add_option(domain_option());

    dpp::command_option user(dpp::co_sub_command, "user", "One person's reactions, received and given.");
    user.add_option(dpp::command_option(dpp::co_user, "user", "You, if left out.", false));
    user.add_option(date_option("since", "From this day, YYYY-MM-DD."));
    user.add_option(date_option("until", "Up to and including this day, YYYY-MM-DD."));
    user.add_option(domain_option());

    const dpp::command_option emojis(dpp::co_sub_command, "emojis", "Custom emojis that share a name, likely the same emote twice.");

    dpp::command_option alias_add(dpp::co_sub_command, "add", "Count one emoji as another, in all history.");
    alias_add.add_option(emoji_option("emoji", "The one to merge away.", true));
    alias_add.add_option(emoji_option("as", "The one it counts as.", true));

    dpp::command_option alias_remove(dpp::co_sub_command, "remove", "Count an emoji as itself again.");
    alias_remove.add_option(emoji_option("emoji", "The alias to remove.", true));

    const dpp::command_option alias_list(dpp::co_sub_command, "list", "Show this server's emoji aliases.");

    dpp::command_option alias(dpp::co_sub_command_group, "alias", "Emojis that should count as one.");
    alias.add_option(alias_add);
    alias.add_option(alias_remove);
    alias.add_option(alias_list);

    dpp::command_option start(dpp::co_sub_command, "start", "Rebuild the reaction counts from channel history. Needs Manage Server.");
    start.add_option(dpp::command_option(dpp::co_string, "since", "How far back, YYYY-MM-DD.", true).set_max_length(10));
    start.add_option(date_option("until", "Up to and including this day, YYYY-MM-DD. Today if left out."));
    start.add_option(dpp::command_option(dpp::co_channel, "channel", "Only this channel. Every text channel if left out.", false)
                         .add_channel_type(dpp::CHANNEL_TEXT)
                         .add_channel_type(dpp::CHANNEL_ANNOUNCEMENT));
    start.add_option(dpp::command_option(dpp::co_boolean, "fresh", "Start every channel over instead of carrying on.", false));

    const dpp::command_option cancel(dpp::co_sub_command, "cancel", "Stop a recompute that is running.");

    dpp::command_option recompute(dpp::co_sub_command_group, "recompute", "Rebuild reaction counts from history.");
    recompute.add_option(start);
    recompute.add_option(cancel);

    payload.add_option(top);
    payload.add_option(user);
    payload.add_option(emojis);
    payload.add_option(alias);
    payload.add_option(recompute);
    return payload;
}

auto linkstats_command::autocomplete(const dpp::autocomplete_t& event) const -> void {
    const dpp::command_option* focused = focused_option(event.options);
    if (focused == nullptr || event.owner == nullptr) return;

    // What has been typed so far into whichever option is focused. The same
    // handler serves `domain` in two subcommands and `emoji` / `as` in three.
    const auto* typed = std::get_if<std::string>(&focused->value);
    std::string_view filter = typed == nullptr ? std::string_view{} : std::string_view(*typed);
    dpp::interaction_response reply(dpp::ir_autocomplete_reply);

    // Sites: the ones this guild has replacements for, matched anywhere.
    if (focused->name == "domain") {
        std::size_t offered = 0;
        for (const std::string& site : store_->known_domains(event.command.guild_id)) {
            if (offered < emoji_choices && site.find(util::trim(filter)) != std::string::npos) {
                reply.add_autocomplete_choice(dpp::command_option_choice(site, site));
                ++offered;
            }
        }
    } else if (focused->name == "emoji" || focused->name == "as") {
        // Emojis: the ones used on replacements here, most used first. The
        // value sent back is the key, so the command never has to guess
        // which of two same-named emojis was meant.
        if (filter.size() > 2 && filter.starts_with(':') && filter.ends_with(':')) filter = filter.substr(1, filter.size() - 2);
        for (const events::emoji_tally& known : store_->known_emojis(event.command.guild_id, filter, emoji_choices)) {
            // A custom emoji cannot be drawn in a choice, so it shows by name.
            const std::string label = known.emoji.key.starts_with("c:") ? std::format(":{}: ({})", known.emoji.name, known.count)
                                                                        : std::format("{} ({})", known.emoji.name, known.count);
            reply.add_autocomplete_choice(dpp::command_option_choice(label.substr(0, 100), known.emoji.key));
        }
    } else {
        return;
    }

    event.owner->interaction_response_create(event.command.id, event.command.token, reply);
}

auto linkstats_refusal(std::string_view subcommand, dpp::permission invoker) -> std::optional<std::string> {
    if (invoker.can(dpp::p_manage_guild)) return std::nullopt;
    if (subcommand.starts_with("alias ") && subcommand != "alias list") return "changing emoji aliases needs Manage Server";
    // Reading years of history is a lot of API calls; this one is for the
    // people who run the server (plan §9.7).
    if (subcommand.starts_with("recompute ")) return "recomputing link stats needs Manage Server";
    return std::nullopt;
}

auto linkstats_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string subcommand = subcommand_path(event.command.get_command_interaction());

    if (const auto refused = linkstats_refusal(subcommand, invoker_permissions(event))) {
        co_await event.co_reply(refusal(event, *refused));
        co_return;
    }

    if (subcommand.starts_with("alias ")) {
        co_await this->alias(event, subcommand);
    } else if (subcommand.starts_with("recompute ")) {
        co_await this->recompute(event, subcommand);
    } else if (subcommand == "top") {
        co_await this->top(event);
    } else if (subcommand == "user") {
        co_await this->user(event);
    } else if (subcommand == "emojis") {
        co_await event.co_reply(result(event, render_duplicates(*store_, event.command.guild_id)));
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto linkstats_command::top(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const auto which = board_from_string(string_option(event, "by"));

    events::stat_query query;
    if (const auto problem = read_window(event, query)) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }

    const std::string typed = string_option(event, "emoji");
    if (!typed.empty()) {
        const auto emoji = resolve_emoji(*store_, guild, typed);
        if (!emoji) {
            co_await event.co_reply(refusal(event, std::format("nobody has reacted with \"{}\" on a replaced link here", typed)));
            co_return;
        }
        query.emoji_key = emoji->key;
    }

    const board chosen = which.value_or(board::received);
    query.kind = kind_for(chosen);
    co_await event.co_reply(result(event, render_board(*store_, guild, chosen, query)));
}

auto linkstats_command::user(const dpp::slashcommand_t& event) -> dpp::task<void> {
    events::stat_query window;
    if (const auto problem = read_window(event, window)) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }

    const dpp::snowflake subject = snowflake_option(event, "user").value_or(event.command.get_issuing_user().id);

    co_await event.co_reply(result(event, render_profile(*store_, event.command.guild_id, subject, window)));
}

auto linkstats_command::alias(const dpp::slashcommand_t& event, std::string_view subcommand) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;

    if (subcommand == "alias list") {
        co_await event.co_reply(result(event, render_aliases(*store_, guild)));
        co_return;
    }

    // Changing them needs Manage Server, which execute has checked.
    const auto emoji = resolve_emoji(*store_, guild, string_option(event, "emoji"));
    if (!emoji) {
        co_await event.co_reply(refusal(event, "i don't know that emoji; pick one from the list as you type"));
        co_return;
    }

    const user_label who = describe_user(event.command.get_issuing_user());

    if (subcommand == "alias remove") {
        if (!store_->remove_alias(guild, emoji->key)) {
            co_await event.co_reply(refusal(event, std::format("{} isn't an alias", events::display_emoji(*emoji))));
            co_return;
        }
        util::log().info("emoji alias for {} removed in guild {} by {}", emoji->key, guild, who);
        co_await event.co_reply(result(event, std::format("{} counts as itself again.", events::display_emoji(*emoji))));
        co_return;
    }

    const auto canonical = resolve_emoji(*store_, guild, string_option(event, "as"));
    if (!canonical) {
        co_await event.co_reply(refusal(event, "i don't know the emoji to count it as; pick one from the list as you type"));
        co_return;
    }

    // Whichever way they were typed, remember what they look like so the
    // alias can be shown.
    store_->remember(*emoji);
    store_->remember(*canonical);

    if (const auto problem = store_->set_alias(guild, emoji->key, canonical->key)) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }

    util::log().info("emoji {} now counts as {} in guild {}, set by {}", emoji->key, canonical->key, guild, who);
    co_await event.co_reply(
        result(event, std::format("{} counts as {} now, in every statistic back to the start.", events::display_emoji(*emoji),
                                  events::display_emoji(store_->describe(store_->canonical(guild, emoji->key))))));
}

auto linkstats_command::recompute(const dpp::slashcommand_t& event, std::string_view subcommand) -> dpp::task<void> {
    // Manage Server has been checked by execute.
    if (recompute_.service == nullptr || recompute_.discord == nullptr) {
        co_await event.co_reply(refusal(event, "recomputing isn't available in this build"));
        co_return;
    }

    if (subcommand == "recompute cancel") {
        const bool stopping = recompute_.service->cancel(event.command.guild_id);
        if (stopping) {
            util::log().info("link stats recompute in guild {} cancelled by {}", event.command.guild_id,
                             describe_user(event.command.get_issuing_user()));
        }
        co_await event.co_reply(result(event, stopping ? "Stopping at the next page of history." : "Nothing is being recomputed here."));
    } else if (subcommand == "recompute start") {
        co_await recompute_start(event);
    } else {
        co_await event.co_reply(refusal(event, "i don't know that subcommand"));
    }
}

auto linkstats_command::recompute_start(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;

    events::stat_query window;
    if (const auto problem = read_window(event, window)) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }

    events::backfill_request request{.guild_id = guild,
                                     .channel_ids = {},
                                     .since = window.since.value_or(std::chrono::sys_seconds{}),
                                     .until = window.until,
                                     .bot_id = recompute_.bot_id ? recompute_.bot_id() : dpp::snowflake{},
                                     .fresh = false};

    request.fresh = bool_option(event, "fresh").value_or(false);

    if (const auto channel = snowflake_option(event, "channel")) {
        request.channel_ids.push_back(*channel);
    } else if (recompute_.channels_of) {
        request.channel_ids = recompute_.channels_of(guild);
    }

    if (request.channel_ids.empty() || request.bot_id.empty()) {
        co_await event.co_reply(refusal(event, "there are no channels here i can look through"));
        co_return;
    }

    if (!recompute_.service->begin(guild)) {
        co_await event.co_reply(refusal(event, "a recompute is already running here; `/linkstats recompute cancel` stops it"));
        co_return;
    }

    util::log().info("link stats recompute started in guild {} by {}: {} channel(s) since {}, reading replacements posted by {}", guild,
                     describe_user(event.command.get_issuing_user()), request.channel_ids.size(), format_day(request.since),
                     request.bot_id);

    // The interaction's token lasts fifteen minutes and a recompute can take
    // hours, so progress goes in an ordinary message instead.
    co_await event.co_reply(result(event, "Started. Progress goes in this channel."));

    ports::discord_gateway& discord = *recompute_.discord;
    const dpp::snowflake channel = event.command.channel_id;
    const auto posted = co_await discord.send_message(post(event, dpp::message(channel, render_backfill({}, request, false))));
    const dpp::snowflake progress_id = posted.ok() ? posted.value().id : dpp::snowflake{};

    // Edits the progress message as the run goes. It captures this frame's
    // locals by reference, which is safe only because `run` is awaited just
    // below and the lambda is never kept past it. If that message could not
    // be posted, the run gets no callback, and the report is posted fresh at
    // the end instead.
    const auto progress = [this, &event, &discord, &request, channel,
                           progress_id](const events::backfill_report& report) -> dpp::task<void> {
        dpp::message update = post(event, dpp::message(channel, render_backfill(report, request, false)));
        update.id = progress_id;
        return show_progress(discord, std::move(update));
    };

    events::backfill_report report;
    try {
        report = co_await recompute_.service->run(request, progress_id.empty() ? events::backfill_service::progress_fn{} : progress);
    } catch (...) {
        // Whatever went wrong, the guild must not stay claimed, or nobody
        // could start another until a restart.
        recompute_.service->end(guild);
        throw;
    }
    recompute_.service->end(guild);

    dpp::message final_report = post(event, dpp::message(channel, render_backfill(report, request, true)));
    if (progress_id.empty()) {
        co_await discord.send_message(final_report);
    } else {
        final_report.id = progress_id;
        co_await show_progress(discord, final_report);
    }
}

} // namespace latibot::commands
