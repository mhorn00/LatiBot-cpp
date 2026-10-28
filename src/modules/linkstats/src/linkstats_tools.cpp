#include "linkstats_tools.hpp"

#include "core/capabilities/model_tools.hpp"
#include "links/url_rules.hpp"
#include "linkstats_command.hpp"
#include "media_posts.hpp"
#include "reactions.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::events {
namespace {

using capabilities::tool_done;
using capabilities::tool_refused;
using capabilities::tool_text;

constexpr std::int64_t shown_by_default = 10;
constexpr std::int64_t shown_at_most = 25;

/// An emoji as the model is shown it: a Unicode one itself, a custom one by
/// name.
auto shown_emoji(const emoji_ref& emoji) -> std::string {
    return emoji.key.starts_with("c:") ? std::format(":{}:", emoji.name) : emoji.name;
}

/// The emoji the model named: `<:name:id>`, a Unicode emoji, or a custom
/// one's name, with or without colons, among those used here.
auto named_emoji(const reaction_store& store, dpp::snowflake guild, std::string_view written) -> std::optional<std::string> {
    std::string_view name = written;
    if (name.starts_with(':') && name.ends_with(':') && name.size() > 2) name = name.substr(1, name.size() - 2);
    const bool plain = std::ranges::all_of(name, [](char letter) {
        return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == '_';
    });
    if (plain) {
        for (const emoji_tally& known : store.known_emojis(guild, name, 25)) {
            if (known.emoji.name == name) return known.emoji.key;
        }
        return std::nullopt;
    }
    const auto parsed = parse_emoji(written);
    if (!parsed) return std::nullopt;
    return parsed->key;
}

auto link_stats(const reaction_store& store, const config::guild_settings* settings, const nlohmann::json& input,
                const capabilities::model_tool_context& context) -> capabilities::model_tool_result {
    const std::string by = tool_text(input, "by").value_or("received");
    stat_query query;
    const bool emojis = by == "emojis";
    if (!emojis) {
        const auto kind = stat_kind_from_string(by);
        if (!kind) return tool_refused("by is received, given, self or emojis");
        query.kind = *kind;
    }

    if (const auto alias = tool_text(input, "user")) {
        query.user_id = context.user_for(*alias);
        if (!query.user_id) return tool_refused("there is nobody here with that alias");
    }
    if (const auto written = tool_text(input, "emoji")) {
        query.emoji_key = named_emoji(store, context.guild_id, *written);
        if (!query.emoji_key) return tool_refused(std::format("nobody here has reacted with {} on a counted post", *written));
    }
    if (const auto since = tool_text(input, "since")) {
        const auto day = commands::parse_day(*since);
        if (!day) return tool_refused("since is a date, YYYY-MM-DD");
        query.since = std::chrono::sys_seconds(*day);
    }
    if (const auto until = tool_text(input, "until")) {
        const auto day = commands::parse_day(*until);
        if (!day) return tool_refused("until is a date, YYYY-MM-DD");
        query.until = std::chrono::sys_seconds(*day + std::chrono::days{1});
    }
    if (query.since && query.until && *query.since >= *query.until) return tool_refused("that range ends before it starts");
    if (const auto site = tool_text(input, "domain")) {
        query.domain = normalise_domain(*site);
        if (!query.domain) return tool_refused(std::format("\"{}\" does not look like a site, such as x.com", *site));
    }
    // What /linkstats counts when it is not told.
    query.source = settings != nullptr && images_enabled(*settings, context.guild_id) ? stat_source::both : stat_source::links;

    const auto limit = static_cast<std::size_t>(
        std::clamp(capabilities::tool_number(input, "limit").value_or(shown_by_default), std::int64_t{1}, shown_at_most));
    const std::string what = query.kind == stat_kind::given ? "given" : query.kind == stat_kind::self ? "on their own links" : "received";

    if (query.user_id && !emojis) {
        return tool_done(
            std::format("{}: {} reaction(s) {}", context.shown_as(*query.user_id), store.total(context.guild_id, query), what));
    }

    std::string text;
    if (emojis) {
        const auto board = store.emoji_breakdown(context.guild_id, query, limit);
        if (board.empty()) return tool_done("no reactions match");
        text = std::format("The most used emojis{}, of {}:\n",
                           query.user_id ? std::format(" by {}", context.shown_as(*query.user_id)) : std::string{},
                           store.emojis(context.guild_id, query));
        for (std::size_t index = 0; index < board.size(); ++index) {
            text += std::format("{}. {} ×{}\n", index + 1, shown_emoji(board[index].emoji), board[index].count);
        }
    } else {
        const auto board = store.leaderboard(context.guild_id, query, limit);
        if (board.empty()) return tool_done("no reactions match");
        text = std::format("Reactions {}, most first, of {} people and {} reactions:\n", what, store.people(context.guild_id, query),
                           store.total(context.guild_id, query));
        for (std::size_t index = 0; index < board.size(); ++index) {
            text += std::format("{}. {}: {}\n", index + 1, context.shown_as(board[index].user_id), board[index].count);
        }
    }
    return tool_done(text);
}

} // namespace

auto add_linkstats_tools(capabilities::model_tools& tools, const reaction_store& store, const config::guild_settings* settings) -> void {
    tools.add({.name = "link_stats",
               .description = "The reaction statistics of /linkstats: who got the most reactions on their replaced links (and image posts, "
                              "where this server counts them), who gave the most, or the most used emojis. Or one person's count.",
               .input_schema = capabilities::tool_schema(
                   {{"by", capabilities::tool_choice("What to rank; received when left out.", {"received", "given", "self", "emojis"})},
                    {"user", capabilities::tool_property("An alias: that person's count alone, or the emojis they used.")},
                    {"emoji", capabilities::tool_property("Only this emoji: a Unicode one, or a custom one's name.")},
                    {"since", capabilities::tool_property("From this day on, YYYY-MM-DD, UTC.")},
                    {"until", capabilities::tool_property("Up to and including this day, YYYY-MM-DD.")},
                    {"domain", capabilities::tool_property("Only links to this site, like x.com.")},
                    {"limit", capabilities::tool_property("How many places, 1 to 25; 10 when left out.", "integer")}}),
               .changes = false,
               .run = [&store, settings](nlohmann::json input, capabilities::model_tool_context context)
                   -> dpp::task<capabilities::model_tool_result> { co_return link_stats(store, settings, input, context); }});
}

} // namespace latibot::events
