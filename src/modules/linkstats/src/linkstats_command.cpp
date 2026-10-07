#include "linkstats_command.hpp"

#include "core/commands/options.hpp"
#include "core/config/guild_settings.hpp"
#include "core/discord/message_flags.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/ui/interaction.hpp"
#include "core/ui/paginator.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "media_posts.hpp"

#include <dpp/cluster.h>
#include <dpp/dispatcher.h>
#include <dpp/permissions.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <span>
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

auto date_option(const char* name, const char* description) -> dpp::command_option {
    return dpp::command_option(dpp::co_string, name, description, false).set_max_length(10);
}

auto domain_option() -> dpp::command_option {
    return dpp::command_option(dpp::co_string, "domain", "Only links to this site, like x.com.", false)
        .set_auto_complete(true)
        .set_max_length(domain_length_limit);
}

auto source_option() -> dpp::command_option {
    dpp::command_option source(dpp::co_string, "source", "What to count. Links and images when images are counted here.", false);
    source.add_choice(dpp::command_option_choice("Links and images", std::string("both")));
    source.add_choice(dpp::command_option_choice("Replaced links", std::string("links")));
    source.add_choice(dpp::command_option_choice("Images and videos", std::string("images")));
    return source;
}

auto emoji_option(const char* name, const char* description, bool required) -> dpp::command_option {
    return dpp::command_option(dpp::co_string, name, description, required).set_auto_complete(true).set_max_length(100);
}

auto page_size_option(const char* description) -> dpp::command_option {
    return dpp::command_option(dpp::co_integer, "per_page", description, false)
        .set_min_value(1)
        .set_max_value(static_cast<std::int64_t>(max_page_size));
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
    case board::emoji_given:
        return 'f';
    case board::received:
        break;
    }
    return 'r';
}

/// The board `board_letter` wrote as `letter`, or nothing for any other.
auto board_from_letter(char letter) -> std::optional<board> {
    switch (letter) {
    case 'r':
        return board::received;
    case 'g':
        return board::given;
    case 's':
        return board::self;
    case 'e':
        return board::emoji;
    case 'f':
        return board::emoji_given;
    default:
        return std::nullopt;
    }
}

/// Which side of a reaction a board counts. The emoji boards count what
/// people received or gave, leaving self-reactions out like every other
/// board.
auto kind_for(board which) -> events::stat_kind {
    switch (which) {
    case board::given:
    case board::emoji_given:
        return events::stat_kind::given;
    case board::self:
        return events::stat_kind::self;
    case board::received:
    case board::emoji:
        break;
    }
    return events::stat_kind::received;
}

auto is_emoji_board(board which) -> bool {
    return which == board::emoji || which == board::emoji_given;
}

auto source_letter(events::stat_source source) -> char {
    switch (source) {
    case events::stat_source::links:
        return 'l';
    case events::stat_source::images:
        return 'i';
    case events::stat_source::both:
        break;
    }
    return 'b';
}

/// The kind of post `source_letter` wrote as `letter`, or nothing for any
/// other.
auto source_from_letter(std::string_view letter) -> std::optional<events::stat_source> {
    if (letter == "l") return events::stat_source::links;
    if (letter == "i") return events::stat_source::images;
    if (letter == "b") return events::stat_source::both;
    return std::nullopt;
}

/// What a statistic is about, as its title names it: "replaced x.com links",
/// "images", "links and images".
auto posts_counted(const events::stat_query& query) -> std::string {
    // Images have no site, so a site means links alone.
    if (query.domain) return std::format("replaced {} links", *query.domain);
    switch (query.source) {
    case events::stat_source::links:
        return "replaced links";
    case events::stat_source::images:
        return "images";
    case events::stat_source::both:
        break;
    }
    return "links and images";
}

/// The same, for somebody's own: "their own links", "their own images".
auto own_posts(const events::stat_query& query) -> std::string {
    if (query.domain) return std::format("their own {} links", *query.domain);
    switch (query.source) {
    case events::stat_source::links:
        return "their own links";
    case events::stat_source::images:
        return "their own images";
    case events::stat_source::both:
        break;
    }
    return "their own links and images";
}

auto whole_number(std::string_view text) -> std::optional<std::int64_t> {
    std::int64_t value = 0;
    const auto [stop, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || stop != text.data() + text.size()) return std::nullopt;
    return value;
}

/// Discord's limit on a message, in characters.
constexpr std::size_t message_limit = 2000;

/// Kept for "…and 12 more that do not fit" when a page has outgrown that.
constexpr std::size_t cut_note_room = 60;

auto board_title(const events::reaction_store& store, board which, const events::stat_query& query) -> std::string {
    const std::string emoji = query.emoji_key ? events::display_emoji(store.describe(*query.emoji_key)) + " " : std::string("reactions ");
    const std::string posts = posts_counted(query);
    const std::string window = describe_window(query);

    // One person's reactions, by emoji.
    if (is_emoji_board(which) && query.user_id) {
        return std::format("Reactions <@{}> {} on {}{}", *query.user_id, which == board::emoji ? "received" : "gave", posts, window);
    }

    switch (which) {
    case board::given:
        return std::format("Most {}given on {}{}", emoji, posts, window);
    case board::self:
        return std::format("Most {}on {}{}", emoji, own_posts(query), window);
    case board::emoji:
        return std::format("Most used reactions received on {}{}", posts, window);
    case board::emoji_given:
        return std::format("Most used reactions given on {}{}", posts, window);
    case board::received:
        break;
    }
    return std::format("Most {}received on {}{}", emoji, posts, window);
}

/// How many people or emojis a board has, for paging it.
auto board_total(const events::reaction_store& store, dpp::snowflake guild_id, board which, const events::stat_query& query)
    -> std::size_t {
    return static_cast<std::size_t>(
        std::max<std::int64_t>(0, is_emoji_board(which) ? store.emojis(guild_id, query) : store.people(guild_id, query)));
}

/// What every page starts with: the title, and on an emoji board how many in
/// all, since that is the one number a list of emojis does not show.
auto board_header(const events::reaction_store& store, dpp::snowflake guild_id, board which, const events::stat_query& query,
                  std::size_t total) -> std::string {
    std::string text = std::format("**{}**\n", board_title(store, which, query));
    if (is_emoji_board(which) && total > 0) {
        const std::int64_t reactions = store.total(guild_id, query);
        text +=
            std::format("_{} reaction{} with {} different emoji{}_\n", reactions, reactions == 1 ? "" : "s", total, total == 1 ? "" : "s");
    }
    return text;
}

/// `count` places from `offset`, a line each, numbered from 1.
auto board_lines(const events::reaction_store& store, dpp::snowflake guild_id, board which, const events::stat_query& query,
                 std::size_t count, std::size_t offset) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::size_t place = offset;
    if (is_emoji_board(which)) {
        for (const events::emoji_tally& tally : store.emoji_breakdown(guild_id, query, count, offset)) {
            lines.push_back(std::format("{}. {} {}\n", ++place, events::display_emoji(tally.emoji), tally.count));
        }
    } else {
        for (const events::person_tally& tally : store.leaderboard(guild_id, query, count, offset)) {
            lines.push_back(std::format("{}. <@{}> {}\n", ++place, tally.user_id, tally.count));
        }
    }
    return lines;
}

/// "Page 2 of 7" under a board with more than one page.
auto board_footer(int page, std::size_t total, std::size_t per_page) -> std::string {
    return total > per_page ? std::format("\n_{}_", ui::page_label(page, total, per_page)) : std::string{};
}

/// A board's `per_page`, or its own when that is 0.
auto page_size_or_default(board which, std::size_t per_page) -> std::size_t {
    return per_page == 0 ? default_page_size(which) : per_page;
}

} // namespace

auto default_page_size(board which) -> std::size_t {
    return is_emoji_board(which) ? emoji_page_size : leaderboard_size;
}

auto encode_board(board which, const events::stat_query& query, std::size_t per_page) -> std::string {
    const auto day = [](const std::optional<std::chrono::sys_seconds>& when) {
        return when ? std::to_string(std::chrono::floor<std::chrono::days>(*when).time_since_epoch().count()) : std::string{};
    };
    return std::format("{1}{0}{2}{0}{3}{0}{4}{0}{5}{0}{6}{0}{7}{0}{8}", board_separator, board_letter(which),
                       query.emoji_key.value_or(std::string{}), query.domain.value_or(std::string{}), day(query.since), day(query.until),
                       query.user_id ? query.user_id->str() : std::string{}, source_letter(query.source),
                       per_page == 0 ? std::string{} : std::to_string(per_page));
}

auto decode_board(std::string_view argument) -> std::optional<board_spec> {
    // Split on ';' into the eight fields `encode_board` writes: board
    // letter, emoji key, site, since, until, person, kind of post and page
    // size. Five to seven are buttons sent before the last ones were added;
    // those before the kind of post only ever counted links. Anything else
    // was not made here and is refused whole.
    std::vector<std::string_view> fields;
    while (true) {
        const std::size_t cut = argument.find(board_separator);
        fields.push_back(argument.substr(0, cut));
        if (cut == std::string_view::npos) break;
        argument.remove_prefix(cut + 1);
    }
    if (fields.size() < 5 || fields.size() > 8 || fields[0].size() != 1) return std::nullopt;

    const std::optional<board> decoded = board_from_letter(fields[0].front());
    if (!decoded) return std::nullopt;
    const board which = *decoded;

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
    if (fields.size() >= 6 && !fields[5].empty()) {
        query.user_id = util::parse_snowflake(fields[5]);
        if (!query.user_id) return std::nullopt;
    }
    query.source = events::stat_source::links;
    if (fields.size() >= 7) {
        const auto source = source_from_letter(fields[6]);
        if (!source) return std::nullopt;
        query.source = *source;
    }

    std::size_t per_page = 0;
    if (fields.size() == 8 && !fields[7].empty()) {
        const auto size = whole_number(fields[7]);
        if (!size || *size < 1 || std::cmp_greater(*size, max_page_size)) return std::nullopt;
        per_page = static_cast<std::size_t>(*size);
    }
    return board_spec{.which = which, .query = query, .per_page = per_page};
}

auto render_board(const events::reaction_store& store, dpp::snowflake guild_id, board which, const events::stat_query& query, int page,
                  std::size_t per_page) -> dpp::message {
    const std::size_t size = page_size_or_default(which, per_page);
    const std::size_t total = board_total(store, guild_id, which, query);
    const int current = ui::clamp_page(page, total, size);
    const ui::page_range window = ui::range_for(current, total, size);

    std::string text = board_header(store, guild_id, which, query, total);
    const std::string footer = board_footer(current, total, size);

    // Every line, unless the board has grown since its page size was
    // checked (a longer name, a bigger number): then as many as fit beside
    // a note of how many did not.
    const std::vector<std::string> lines = board_lines(store, guild_id, which, query, size, window.begin);
    std::size_t used = util::character_count(text) + util::character_count(footer);
    std::size_t needed = used;
    for (const std::string& line : lines) {
        needed += util::character_count(line);
    }
    const std::size_t room = needed <= message_limit ? message_limit : message_limit - cut_note_room;
    std::size_t shown = 0;
    while (shown < lines.size() && used + util::character_count(lines[shown]) <= room) {
        used += util::character_count(lines[shown]);
        text += lines[shown++];
    }
    if (shown < lines.size()) text += std::format("_…and {} more that do not fit in a message_\n", lines.size() - shown);

    if (total == 0) {
        text +=
            "Nothing counted yet. Reactions are counted from when the bot first saw them; `/linkstats recompute` fills in "
            "older ones.";
    }
    text += footer;

    dpp::message reply(text);
    if (const auto row = ui::controls({.view = std::string(board_view), .page = current, .argument = encode_board(which, query, per_page)},
                                      total, size)) {
        reply.add_component(*row);
    }
    return reply;
}

auto largest_page(const events::reaction_store& store, dpp::snowflake guild_id, board which, const events::stat_query& query,
                  std::size_t wanted) -> std::size_t {
    const std::size_t total = board_total(store, guild_id, which, query);
    if (total == 0) return wanted;

    // Every line once, measured, then each page size tried from the one
    // asked for down: a page is its header, its lines and its footer.
    const std::size_t header = util::character_count(board_header(store, guild_id, which, query, total));
    std::vector<std::size_t> lengths;
    for (const std::string& line : board_lines(store, guild_id, which, query, total, 0)) {
        lengths.push_back(util::character_count(line));
    }

    for (std::size_t size = wanted; size > 0; --size) {
        bool fits = true;
        for (std::size_t begin = 0; fits && begin < lengths.size(); begin += size) {
            const int page = static_cast<int>(begin / size);
            std::size_t length = header + util::character_count(board_footer(page, total, size));
            for (std::size_t index = begin; index < std::min(begin + size, lengths.size()); ++index) {
                length += lengths[index];
            }
            fits = length <= message_limit;
        }
        if (fits) return size;
    }
    return 0;
}

namespace {

/// A select menu's text: Discord allows 100 characters.
constexpr std::size_t option_text_limit = 100;

/// A select menu holds at most 25 options.
constexpr std::size_t menu_limit = 25;

auto similar_line(std::size_t place, const events::emoji_tally& tally) -> std::string {
    return std::format("{}. {} `{}` {}", place, events::display_emoji(tally.emoji), tally.emoji.name, tally.count);
}

auto option_for(const events::emoji_tally& tally, std::size_t place) -> dpp::select_option {
    // Each option is its name and number, matching the list above it. A
    // custom emoji can only be drawn in a menu when the bot can use it, which
    // it can its own copy of it.
    dpp::select_option option(util::truncate(std::format("{}. {}", place, tally.emoji.name), option_text_limit), tally.emoji.key,
                              util::truncate(std::format("{} reaction{}", tally.count, tally.count == 1 ? "" : "s"), option_text_limit));
    if (tally.emoji.copy) option.set_emoji(tally.emoji.copy->name, tally.emoji.copy->id, tally.emoji.copy->animated);
    return option;
}

/// The menus that merge `group`: which to keep, and, once `keeper` is one
/// of them, which to merge into it.
auto merge_menus(std::span<const events::emoji_tally> group, std::string_view keeper, int page) -> std::vector<dpp::component> {
    std::vector<dpp::component> rows;
    const auto keep_id = ui::encode({.view = std::string(keep_view), .page = page, .argument = {}});
    if (!keep_id) return rows;

    // Which to keep. The page rides along so the group stays in view.
    const bool keeping = std::ranges::any_of(group, [&](const events::emoji_tally& tally) { return tally.emoji.key == keeper; });
    dpp::component keep_menu;
    keep_menu.set_type(dpp::cot_selectmenu).set_placeholder("Keep which one?").set_id(*keep_id);
    for (std::size_t index = 0; index < group.size(); ++index) {
        keep_menu.add_select_option(option_for(group[index], index + 1).set_default(keeping && group[index].emoji.key == keeper));
    }
    rows.push_back(dpp::component().set_type(dpp::cot_action_row).add_component(keep_menu));

    // What to merge into it, once there is one to keep. The keeper rides in
    // the id, since a menu only sends back what was picked in it.
    const auto merge_id = ui::encode({.view = std::string(merge_view), .page = page, .argument = std::string(keeper)});
    if (!keeping || !merge_id) return rows;

    const auto kept = std::ranges::find_if(group, [&](const events::emoji_tally& tally) { return tally.emoji.key == keeper; });
    dpp::component merge_menu;
    merge_menu.set_type(dpp::cot_selectmenu)
        .set_placeholder(util::truncate(std::format("Merge into {}…", kept->emoji.name), option_text_limit))
        .set_id(*merge_id);
    if (group.size() > 2) {
        merge_menu.add_select_option(
            dpp::select_option(std::format("All {} of the others", group.size() - 1), std::string(merge_everything), "Merge every one"));
    }
    for (std::size_t index = 0; index < group.size(); ++index) {
        if (group[index].emoji.key != keeper) merge_menu.add_select_option(option_for(group[index], index + 1));
    }
    rows.push_back(dpp::component().set_type(dpp::cot_action_row).add_component(merge_menu));
    return rows;
}

} // namespace

auto render_similar(const events::reaction_store& store, dpp::snowflake guild_id, int page, std::string_view keeper, bool can_merge,
                    std::string_view note) -> dpp::message {
    const auto groups = store.similar_emojis(guild_id);
    if (groups.empty()) {
        std::string text = note.empty() ? std::string{} : std::format("{}\n", note);
        text += "No two custom emojis here have names alike, so nothing looks duplicated.";
        return dpp::message(text);
    }

    const int current = ui::clamp_page(page, groups.size(), 1);
    const auto& group = groups[static_cast<std::size_t>(current)];
    const std::size_t shown = std::min(group.size(), menu_limit);

    std::string text = "**Custom emojis with names alike**\n";
    if (!note.empty()) text += std::format("{}\n", note);
    for (std::size_t index = 0; index < shown; ++index) {
        text += similar_line(index + 1, group[index]) + "\n";
    }
    if (group.size() > shown) text += std::format("…and {} more, which show once some of these are merged\n", group.size() - shown);

    const auto kept =
        std::ranges::find(group, keeper, [](const events::emoji_tally& tally) -> const std::string& { return tally.emoji.key; });
    const bool keeping = kept != group.end();

    if (!can_merge) {
        text += "\n_Somebody with Manage Server can merge these here, or with `/linkstats alias add`._";
    } else if (!keeping) {
        text +=
            "\n_Pick the one to keep, then which to merge into it. Merged emojis count as the one kept in every statistic, back "
            "to the start; `/linkstats alias remove` undoes one._";
    } else {
        text += std::format("\n_Keeping {}. Pick what to merge into it._", events::display_emoji(kept->emoji));
    }
    text += std::format("\n_Group {} of {}_", current + 1, groups.size());

    dpp::message reply(text);
    if (can_merge) {
        for (const dpp::component& row : merge_menus(std::span(group).first(shown), keeper, current)) {
            reply.add_component(row);
        }
    }

    if (const auto row = ui::controls({.view = std::string(similar_view), .page = current, .argument = {}}, groups.size(), 1)) {
        reply.add_component(*row);
    }
    return reply;
}

namespace {

/// Merges `chosen` — one key, or every other one in the group on `page` —
/// into `keeper`, and says what happened.
auto merge_similar(events::reaction_store& store, dpp::snowflake guild_id, int page, const std::string& keeper, const std::string& chosen,
                   const user_label& who) -> std::string {
    const auto groups = store.similar_emojis(guild_id);
    if (groups.empty()) return "Nothing is left to merge.";
    const auto& group = groups[static_cast<std::size_t>(ui::clamp_page(page, groups.size(), 1))];

    const auto in_group = [&](const std::string& key) {
        return std::ranges::any_of(group, [&](const events::emoji_tally& tally) { return tally.emoji.key == key; });
    };
    // The list may have changed under the menu: another merge, from here or
    // elsewhere. Only what is still in this group is merged.
    if (!in_group(keeper)) return "The list changed since that menu was drawn, so nothing was merged; here it is again.";

    std::vector<std::string> merging;
    if (chosen == merge_everything) {
        for (std::size_t index = 0; index < std::min(group.size(), menu_limit); ++index) {
            if (group[index].emoji.key != keeper) merging.push_back(group[index].emoji.key);
        }
    } else if (chosen != keeper && in_group(chosen)) {
        merging.push_back(chosen);
    }
    if (merging.empty()) return "The list changed since that menu was drawn, so nothing was merged; here it is again.";

    std::string merged;
    for (const std::string& key : merging) {
        if (const auto problem = store.set_alias(guild_id, key, keeper)) {
            util::log().info("could not merge emoji {} into {} in guild {}: {}", key, keeper, guild_id, *problem);
            continue;
        }
        util::log().info("emoji {} now counts as {} in guild {}, merged by {} from /linkstats duplicates", key, keeper, guild_id, who);
        merged += events::display_emoji(store.describe(key));
    }
    if (merged.empty()) return "Nothing could be merged; the log says why.";
    return std::format("Merged {} into {}.", merged, events::display_emoji(store.describe(keeper)));
}

} // namespace

auto on_linkstats_component(events::reaction_store& store, const dpp::interaction_create_t& event, const ui::page_state& state,
                            const std::string& chosen) -> bool {
    const dpp::snowflake guild = event.command.guild_id;

    if (state.view == board_view) {
        // The board's filters ride in the argument, so every page is the
        // same board as the first. Filters this build cannot read are as
        // stale as a view it does not know.
        const auto board = decode_board(state.argument);
        if (!board) return false;
        ui::update_panel(event, render_board(store, guild, board->which, board->query, state.page, board->per_page));
        return true;
    }

    if (state.view != similar_view && state.view != keep_view && state.view != merge_view) return false;

    // Whether this person may merge, now: the menus are only drawn for
    // somebody who could, but that may have changed since.
    const bool can_merge = invoker_permissions(event).can(dpp::p_manage_guild);

    if (state.view == similar_view) {
        ui::update_panel(event, render_similar(store, guild, state.page, state.argument, can_merge));
    } else if (!can_merge) {
        ui::answer_privately(event, "merging emojis needs Manage Server");
    } else if (state.view == keep_view) {
        ui::update_panel(event, render_similar(store, guild, state.page, chosen, can_merge));
    } else {
        const std::string note =
            merge_similar(store, guild, state.page, state.argument, chosen, describe_user(event.command.get_issuing_user()));
        // The keeper stays picked for whatever is left of the group; once
        // the group is gone the next one is in its place, with none picked.
        ui::update_panel(event, render_similar(store, guild, state.page, state.argument, can_merge, note));
    }
    return true;
}

auto render_aliases(const events::reaction_store& store, dpp::snowflake guild_id) -> std::string {
    const auto aliases = store.aliases(guild_id);
    if (aliases.empty()) return "No emoji aliases here. `/linkstats duplicates` lists likely candidates.";

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

namespace {

/// How many messages of each kind the report links to. The rest are in the
/// file the reply carries, and all of them in the log.
constexpr std::size_t report_links = 3;

/// "since 2021-01-01 until 2021-12-31".
auto describe_range(const events::backfill_request& request) -> std::string {
    std::string range = std::format("since {}", format_day(request.since));
    if (request.until) range += std::format(" until {}", format_day(*request.until - std::chrono::days{1}));
    return range;
}

/// The kinds of message a recompute lists for somebody to look at.
struct issue_list {
    std::string_view heading;
    const std::vector<events::message_place>* places;
};

auto issue_lists(const events::backfill_report& report) -> std::array<issue_list, 3> {
    return {{
        {.heading = "Not understood: in no replacement format known", .places = &report.unparsed},
        {.heading = "Not credited: after a link that is not the one it replaced", .places = &report.mismatched},
        {.heading = "Reactions not read: their old counts are kept", .places = &report.unread},
    }};
}

} // namespace

auto render_backfill(const events::backfill_report& report, const events::backfill_request& request, bool finished) -> std::string {
    const std::string range = describe_range(request);

    const std::string images =
        request.images ? std::format(", {} images and videos with {} reactions", report.images, report.image_reactions) : std::string{};

    if (!finished) {
        return std::format(
            "Recomputing link stats {}…\nChannel {} of {}: {} messages scanned, {} replacements found, {} reactions "
            "recorded{}.\n`/linkstats recompute cancel` stops it; running it again carries on from here.",
            range, std::min(report.channels_done + 1, report.channels_total), report.channels_total, report.scanned, report.replacements,
            report.reactions, images);
    }

    std::string text = std::format("**Link stats {} {}**\n", report.cancelled ? "recompute stopped" : "recomputed", range);
    text += std::format("Channels: {} of {}\n", report.channels_done, report.channels_total);
    text += std::format("Messages scanned: {}\n", report.scanned);
    text += std::format("Replacements found: {} ({} credited to whoever posted the link, {} not)\n", report.replacements, report.attributed,
                        report.unattributed);
    if (report.webhooks_skipped > 0) text += std::format("Webhook replacements skipped: {}\n", report.webhooks_skipped);
    text += std::format("Reactions recorded: {}\n", report.reactions);
    if (request.images) {
        text += std::format("Images and videos found: {}, with {} reactions\n", report.images, report.image_reactions);
    }
    text += std::format("Emotes sent as reactions: {}\n", report.emote_reactions);

    if (!report.learned_mirrors.empty()) {
        // Mirrors no rule remembered, found by what they answered.
        std::string hosts;
        std::size_t listed = 0;
        for (const auto& [host, site] : report.learned_mirrors) {
            if (listed++ == report_links) break;
            hosts += std::format("{}{} for {}", hosts.empty() ? "" : ", ", host, site);
        }
        const std::size_t more = report.learned_mirrors.size() - std::min(report.learned_mirrors.size(), report_links);
        text += std::format("Old mirrors recognised: {}{}\n", hosts, more > 0 ? std::format(" and {} more", more) : std::string{});
    }

    // Linked rather than guessed at (src/modules/linkstats/docs/Link_Stats.md §4.3), so a
    // click shows each one. The reply after this carries them all, and the
    // log has them too.
    for (const issue_list& list : issue_lists(report)) {
        if (list.places->empty()) continue;
        text += std::format("{}: {}\n", list.heading, list.places->size());
        for (std::size_t index = 0; index < std::min(list.places->size(), report_links); ++index) {
            text += std::format("- {}\n", events::jump_link(request.guild_id, (*list.places)[index]));
        }
    }

    for (std::size_t index = 0; index < std::min<std::size_t>(report.problems.size(), 3); ++index) {
        text += std::format("- {}\n", report.problems[index]);
    }

    if (report.cancelled) text += "Running it again with the same dates carries on from where it stopped.";

    // Everything above is sized to fit, but a message over Discord's limit
    // is refused whole, so this is the backstop.
    return util::truncate(text, 2000);
}

auto backfill_issues(const events::backfill_report& report, const events::backfill_request& request) -> std::string {
    std::string text;
    for (const issue_list& list : issue_lists(report)) {
        if (list.places->empty()) continue;
        text += std::format("{}{} ({})\n", text.empty() ? "" : "\n", list.heading, list.places->size());
        for (const events::message_place& place : *list.places) {
            text += events::jump_link(request.guild_id, place) + "\n";
        }
    }
    if (text.empty()) return text;
    return std::format("Link stats recompute {}\n\n{}", describe_range(request), text);
}

auto backfill_done_reply(const events::backfill_report& report, const events::backfill_request& request, dpp::snowflake channel_id,
                         dpp::snowflake progress_id, dpp::snowflake starter) -> dpp::message {
    std::size_t issues = 0;
    for (const issue_list& list : issue_lists(report)) {
        issues += list.places->size();
    }

    const std::string images =
        request.images ? std::format("; {} image{} and video{} with {} reaction{}", report.images, report.images == 1 ? "" : "s",
                                     report.images == 1 ? "" : "s", report.image_reactions, report.image_reactions == 1 ? "" : "s")
                       : std::string{};
    std::string text = std::format("<@{}> the link stats recompute {} {}: {} replacement{} found, {} reaction{} recorded{}.", starter,
                                   describe_range(request), report.cancelled ? "stopped" : "is done", report.replacements,
                                   report.replacements == 1 ? "" : "s", report.reactions, report.reactions == 1 ? "" : "s", images);
    if (issues > 0) text += std::format(" {} message{} to look at, linked in the file.", issues, issues == 1 ? "" : "s");

    dpp::message reply(channel_id, text);
    // A reply to the report, so it is one click away however far up it is.
    if (!progress_id.empty()) reply.set_reference(progress_id, request.guild_id, channel_id);
    // Only whoever started it is pinged.
    reply.set_allowed_mentions(false, false, false, false, {starter}, {});
    if (issues > 0) reply.add_file("recompute-issues.txt", backfill_issues(report, request), "text/plain");
    return reply;
}

namespace {

/// Shows a recompute's progress. A plain function rather than a capturing
/// lambda, since a coroutine lambda's captures die with the lambda.
auto show_progress(ports::discord_gateway& discord, dpp::message message) -> dpp::task<void> {
    const auto edited = co_await discord.edit_message(std::move(message));
    if (!edited.has_value()) util::log().debug("could not update the recompute progress message: {}", edited.error().message);
}

} // namespace

linkstats_command::linkstats_command(events::reaction_store& store, recompute_support recompute, config::guild_settings* settings)
    : info_{.name = "linkstats",
            .description = "Who gets the most reactions on the links the bot replaced.",
            .aliases = {},
            // History is only read by a recompute, but the permission check
            // should name it before somebody runs one and gets nothing.
            .required_bot_permissions = dpp::p_send_messages | dpp::p_read_message_history,
            .default_member_permissions = std::nullopt,
            .guild_only = true,
            // The boards are for the room. Tidying emojis, changing aliases
            // and running a recompute are answered privately, and the
            // recompute reports its progress in the channel, as a post.
            .responses = {.result = dpp::m_suppress_notifications, .refusal = dpp::m_ephemeral, .post = dpp::m_suppress_notifications},
            .subcommand_responses = {{"duplicates", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}},
                                     {"alias add", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}},
                                     {"alias remove", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}},
                                     {"recompute start", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}},
                                     {"recompute cancel", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}},
                                     {"images on", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}},
                                     {"images off", {.result = dpp::m_ephemeral, .refusal = std::nullopt, .post = std::nullopt}}}},
      store_(&store),
      recompute_(std::move(recompute)),
      settings_(settings) {}

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
    top.add_option(source_option());
    top.add_option(page_size_option("How many to a page: 10 people, or 20 emojis, if left out."));

    dpp::command_option side(dpp::co_string, "side", "Received if left out.", false);
    side.add_choice(dpp::command_option_choice("Reactions received", std::string("received")));
    side.add_choice(dpp::command_option_choice("Reactions given", std::string("given")));

    dpp::command_option reactions(dpp::co_sub_command, "reactions", "Every emoji reacted with and how often, for everyone or one person.");
    reactions.add_option(dpp::command_option(dpp::co_user, "user", "Only reactions this person received or gave.", false));
    reactions.add_option(side);
    reactions.add_option(date_option("since", "From this day, YYYY-MM-DD."));
    reactions.add_option(date_option("until", "Up to and including this day, YYYY-MM-DD."));
    reactions.add_option(domain_option());
    reactions.add_option(source_option());
    reactions.add_option(page_size_option("How many emojis to a page: 20 if left out."));

    const dpp::command_option duplicates(dpp::co_sub_command, "duplicates",
                                         "Custom emojis with the same or nearly the same name, to merge into one.");

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
    payload.add_option(reactions);
    payload.add_option(duplicates);
    dpp::command_option images(dpp::co_sub_command_group, "images", "Count reactions on the images and videos people post, too.");
    images.add_option(dpp::command_option(dpp::co_sub_command, "on", "Count them here. Needs Manage Server."));
    images.add_option(dpp::command_option(dpp::co_sub_command, "off", "Stop counting them here. Needs Manage Server."));

    payload.add_option(alias);
    payload.add_option(recompute);
    payload.add_option(images);
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
            if (offered < emoji_choices && site.contains(util::trim(filter))) {
                reply.add_autocomplete_choice(dpp::command_option_choice(site, site));
                ++offered;
            }
        }
    } else if (focused->name == "emoji" || focused->name == "as") {
        // Emojis: the ones used on replacements here, most used first. The
        // value sent back is the key, so the command never has to guess
        // which of two same-named emojis was meant. Removing an alias is
        // offered only aliases; anything else only what counts as itself,
        // since an emoji merged into another is counted as that one.
        const auto which = subcommand_path(event.command.get_command_interaction()) == "alias remove" ? events::emoji_listing::aliases
                                                                                                      : events::emoji_listing::counted;
        if (filter.size() > 2 && filter.starts_with(':') && filter.ends_with(':')) filter = filter.substr(1, filter.size() - 2);
        for (const events::emoji_tally& known : store_->known_emojis(event.command.guild_id, filter, emoji_choices, which)) {
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
    // people who run the server (src/modules/linkstats/docs/Link_Stats.md §1).
    if (subcommand.starts_with("recompute ")) return "recomputing link stats needs Manage Server";
    if (subcommand.starts_with("images ")) return "choosing whether images are counted needs Manage Server";
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
    } else if (subcommand.starts_with("images ")) {
        co_await this->images(event, subcommand == "images on");
    } else if (subcommand == "top") {
        co_await this->top(event);
    } else if (subcommand == "reactions") {
        co_await this->reactions(event);
    } else if (subcommand == "duplicates") {
        // The menus to merge them are only for those who could use them.
        const bool can_merge = invoker_permissions(event).can(dpp::p_manage_guild);
        co_await event.co_reply(result(event, render_similar(*store_, event.command.guild_id, 0, {}, can_merge)));
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

    query.source = source_for(event);

    const std::string typed = string_option(event, "emoji");
    if (!typed.empty()) {
        const auto emoji = resolve_emoji(*store_, guild, typed);
        if (!emoji) {
            co_await event.co_reply(refusal(event, std::format("nobody has reacted with \"{}\" on anything counted here", typed)));
            co_return;
        }
        query.emoji_key = emoji->key;
    }

    const board chosen = which.value_or(board::received);
    query.kind = kind_for(chosen);

    std::size_t per_page = 0;
    if (const auto problem = read_page_size(event, chosen, query, per_page)) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }
    co_await event.co_reply(result(event, render_board(*store_, guild, chosen, query, 0, per_page)));
}

auto linkstats_command::reactions(const dpp::slashcommand_t& event) -> dpp::task<void> {
    events::stat_query query;
    if (const auto problem = read_window(event, query)) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }

    const board chosen = string_option(event, "side") == "given" ? board::emoji_given : board::emoji;
    query.kind = kind_for(chosen);
    query.user_id = snowflake_option(event, "user");
    query.source = source_for(event);

    std::size_t per_page = 0;
    if (const auto problem = read_page_size(event, chosen, query, per_page)) {
        co_await event.co_reply(refusal(event, *problem));
        co_return;
    }
    co_await event.co_reply(result(event, render_board(*store_, event.command.guild_id, chosen, query, 0, per_page)));
}

auto linkstats_command::read_page_size(const dpp::slashcommand_t& event, board which, const events::stat_query& query,
                                       std::size_t& per_page) const -> std::optional<std::string> {
    const auto asked = int_option(event, "per_page");
    if (!asked) return std::nullopt;
    if (*asked < 1 || std::cmp_greater(*asked, max_page_size)) {
        return std::format("per_page can be 1 to {}", max_page_size);
    }

    // Every page is measured now, so none of them is refused by Discord
    // later for being too long.
    const auto wanted = static_cast<std::size_t>(*asked);
    const std::size_t fits = largest_page(*store_, event.command.guild_id, which, query, wanted);
    if (fits < wanted) {
        return std::format("{} to a page would make a page longer than Discord's 2,000 characters; {} here", wanted,
                           fits == 0 ? std::string("not even one fits") : std::format("{} is the most that fits", fits));
    }
    per_page = wanted;
    return std::nullopt;
}

auto linkstats_command::counts_images(dpp::snowflake guild_id) const -> bool {
    return settings_ != nullptr && events::images_enabled(*settings_, guild_id);
}

auto linkstats_command::source_for(const dpp::slashcommand_t& event) const -> events::stat_source {
    const std::string chosen = string_option(event, "source");
    if (chosen == "links") return events::stat_source::links;
    if (chosen == "images") return events::stat_source::images;
    if (chosen == "both") return events::stat_source::both;
    // Left out: what the server counts. One that never turned images on
    // has none, and its boards read as they always have.
    return counts_images(event.command.guild_id) ? events::stat_source::both : events::stat_source::links;
}

auto linkstats_command::images(const dpp::slashcommand_t& event, bool enabled) -> dpp::task<void> {
    // Manage Server has been checked by execute.
    if (settings_ == nullptr) {
        co_await event.co_reply(refusal(event, "counting images isn't available in this build"));
        co_return;
    }

    const dpp::snowflake guild = event.command.guild_id;
    const bool was = counts_images(guild);
    events::set_images_enabled(*settings_, guild, enabled);
    if (was != enabled) {
        util::log().info("reactions on images are {} counted in guild {}, set by {}", enabled ? "now" : "no longer", guild,
                         describe_user(event.command.get_issuing_user()));
    }

    if (enabled) {
        co_await event.co_reply(
            result(event, std::format("{}Reactions on the images and videos people post here are counted, credited to whoever posted "
                                      "them. `/linkstats recompute` counts the ones already posted.",
                                      was ? "Already on. " : "")));
    } else {
        co_await event.co_reply(
            result(event, std::format("{}Reactions on images and videos here are no longer counted. Those already counted are kept, and "
                                      "`source:Images and videos` still shows them.",
                                      was ? "" : "Already off. ")));
    }
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

    // Already merged into something: moving it quietly would undo that
    // merge, so it has to be taken back first, on purpose.
    if (const std::string current = store_->canonical(guild, emoji->key); current != emoji->key) {
        co_await event.co_reply(
            refusal(event, std::format("{} is already aliased to {}; `/linkstats alias remove` it first to change that",
                                       events::display_emoji(*emoji), events::display_emoji(store_->describe(current)))));
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
    request.images = counts_images(guild);

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
    const dpp::snowflake progress_id = posted.has_value() ? posted.value().id : dpp::snowflake{};

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
    dpp::snowflake report_id = progress_id;
    if (progress_id.empty()) {
        const auto sent = co_await discord.send_message(final_report);
        if (sent.has_value()) report_id = sent.value().id;
    } else {
        final_report.id = progress_id;
        co_await show_progress(discord, final_report);
    }

    // Then a reply to the report that pings whoever started it, since the
    // report is far up the channel by now. Posted as the others are, but
    // not silently: telling them is its whole point.
    dpp::message done = post(event, backfill_done_reply(report, request, channel, report_id, event.command.get_issuing_user().id));
    done.flags = static_cast<discord::message_flags>(done.flags & ~static_cast<discord::message_flags>(dpp::m_suppress_notifications));
    if (const auto sent = co_await discord.send_message(std::move(done)); !sent.has_value()) {
        util::log().warn("could not tell {} the link stats recompute in guild {} is over: {}",
                         describe_user(event.command.get_issuing_user()), guild, sent.error().message);
    }
}

} // namespace latibot::commands
