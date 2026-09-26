#include "core/events/legacy_replacements.hpp"

#include "core/util/url_scan.hpp"

#include <algorithm>
#include <array>

namespace latibot::events {
namespace {

/// Discord's epoch, the first second of 2015, in milliseconds.
constexpr std::uint64_t discord_epoch_ms = 1'420'070'400'000;

/// What the replacement formats put around their links, and nothing else.
constexpr std::array<std::string_view, 3> decorations{"🔗", ":link:", "||"};

/// A `[label](link)` around one of the links.
struct masked_link {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string_view label;
};

/// The masked link around `link`, if it is inside one. `(<link>)` counts as
/// well as `(link)`.
auto masked_around(std::string_view content, const util::found_link& link) -> std::optional<masked_link> {
    std::size_t open = link.begin;
    if (open > 0 && content[open - 1] == '<') --open;
    if (open < 2 || content.substr(open - 2, 2) != "](") return std::nullopt;

    const std::size_t label_end = open - 2;
    const std::size_t label_begin = content.rfind('[', label_end);
    if (label_begin == std::string_view::npos) return std::nullopt;

    std::size_t close = link.end;
    if (close < content.size() && content[close] == '>') ++close;
    if (close >= content.size() || content[close] != ')') return std::nullopt;

    return masked_link{.begin = label_begin, .end = close + 1, .label = content.substr(label_begin + 1, label_end - label_begin - 1)};
}

/// Whether `rest` is nothing but the link emoji, spoiler bars and spaces.
auto only_decoration(std::string rest) -> bool {
    for (const std::string_view decoration : decorations) {
        for (std::size_t at = rest.find(decoration); at != std::string::npos; at = rest.find(decoration)) {
            rest.erase(at, decoration.size());
        }
    }
    return std::ranges::all_of(rest,
                               [](unsigned char letter) { return letter == ' ' || letter == '\n' || letter == '\r' || letter == '\t'; });
}

auto path_of(std::string_view url) -> std::string_view {
    const auto parts = util::split_url(url);
    return parts ? util::comparable_path(parts->path) : std::string_view{};
}

} // namespace

auto describe_history(const dpp::message& message) -> history_message {
    history_message described;
    described.id = message.id;
    described.author_id = message.author.id;
    described.author_is_bot = message.author.is_bot();
    described.webhook_id = message.webhook_id;
    described.is_system = message.type != dpp::mt_default && message.type != dpp::mt_reply;
    described.replied_to = message.type == dpp::mt_reply ? message.message_reference.message_id : dpp::snowflake{};
    described.content = message.content;

    for (const dpp::reaction& reaction : message.reactions) {
        described.reactions.push_back({.emoji_id = reaction.emoji_id, .emoji_name = reaction.emoji_name, .count = reaction.count});
    }
    return described;
}

auto created_at(dpp::snowflake id) noexcept -> std::chrono::sys_seconds {
    const std::uint64_t ms = (static_cast<std::uint64_t>(id) >> 22) + discord_epoch_ms;
    return std::chrono::sys_seconds(std::chrono::seconds(static_cast<std::int64_t>(ms / 1000)));
}

auto first_id_at(std::chrono::sys_seconds when) noexcept -> dpp::snowflake {
    const auto seconds = when.time_since_epoch().count();
    if (seconds <= 0) return {};
    const auto ms = static_cast<std::uint64_t>(seconds) * 1000;
    return ms <= discord_epoch_ms ? dpp::snowflake{} : dpp::snowflake((ms - discord_epoch_ms) << 22);
}

auto classify(const history_message& message, dpp::snowflake bot_id, const mirror_map& mirrors) -> legacy_match {
    legacy_match match;

    // Step 1: a replacement always links to a known mirror. A message with
    // none cannot be one, whoever wrote it, and that settles most messages.
    std::vector<util::found_link> mirror_links;
    for (const util::found_link& found : util::find_links(message.content)) {
        const auto parts = util::split_url(found.url);
        if (parts && mirrors.contains(util::rule_host(parts->authority))) {
            mirror_links.push_back(found);
            match.mirror_urls.emplace_back(found.url);
        }
    }

    if (mirror_links.empty()) return {};

    // Webhook mode posted as the member and deleted the original, so there
    // is nothing to attribute from and nothing of the bot's to count.
    if (!message.webhook_id.empty()) {
        match.what = legacy_match::kind::webhook;
        match.format = legacy_format::webhook;
        return match;
    }

    // Step 2: from here on it must be the bot's own ordinary message.
    // Somebody else posting a mirror link is just a link.
    if (message.author_id != bot_id || message.is_system) return {};

    // Step 3: the formats, from the easiest to tell apart. A reply is always
    // format 1, whatever its text looks like.
    if (!message.replied_to.empty()) {
        match.what = legacy_match::kind::recognised;
        match.format = legacy_format::reply_copy;
        return match;
    }

    std::vector<masked_link> masked;
    for (const util::found_link& link : mirror_links) {
        if (const auto around = masked_around(message.content, link)) masked.push_back(*around);
    }

    // Bare links in the text: the full copy of the original.
    if (masked.empty()) {
        match.what = legacy_match::kind::recognised;
        match.format = legacy_format::plain_copy;
        return match;
    }

    // Some links masked and some bare is no format the bot ever wrote, so
    // from here every way out that is not a known format says so.
    match.what = legacy_match::kind::unrecognised;
    if (masked.size() != mirror_links.size()) return match;

    // Everything outside the masked links has to be decoration; anything
    // else is a shape nobody wrote down.
    std::string rest;
    std::size_t last = 0;
    for (const masked_link& link : masked) {
        rest += message.content.substr(last, link.begin - last);
        last = link.end;
    }
    rest += message.content.substr(last);

    const bool marked = rest.find("🔗") != std::string::npos || rest.find(":link:") != std::string::npos;
    if (!only_decoration(rest)) return match;

    // The label inside the brackets tells the remaining formats apart: "."
    // with or without the 🔗, and "_", which only ever came with it.
    const auto labelled = [&](std::string_view label) {
        return std::ranges::all_of(masked, [&](const masked_link& link) { return link.label == label; });
    };

    if (labelled(".")) {
        match.what = legacy_match::kind::recognised;
        match.format = marked ? legacy_format::link_dot : legacy_format::dot;
    } else if (labelled("_") && marked) {
        match.what = legacy_match::kind::recognised;
        match.format = legacy_format::link_underscore;
    }
    return match;
}

auto attribute(const history_message& message, const legacy_match& match, std::span<const history_message> older, dpp::snowflake bot_id)
    -> attribution {
    attribution found;

    // A reply names its original outright. It may be older than the page in
    // hand, in which case the caller fetches it.
    if (match.format == legacy_format::reply_copy) {
        found.original_message_id = message.replied_to;
        const auto original = std::ranges::find(older, message.replied_to, &history_message::id);
        if (original == older.end()) {
            found.needs_fetch = true;
        } else {
            found.author_id = original->author_id;
        }
        return found;
    }

    // Every other format has to be matched to the message it answered: the
    // paths of the mirror links, which a mirror keeps from the original.
    std::vector<std::string_view> ours;
    for (const std::string& url : match.mirror_urls) {
        // A link to a site's front page matches every other one; it proves
        // nothing about which message was answered.
        if (const std::string_view path = path_of(url); !path.empty()) ours.push_back(path);
    }
    if (ours.empty()) return found;

    // Walk back through older messages, newest first. Only a person's message
    // with links counts as a candidate, and only the first few candidates are
    // tried: a match further back than that is more likely a coincidence.
    std::size_t candidates = 0;
    for (const history_message& earlier : older) {
        if (earlier.author_is_bot || earlier.author_id == bot_id || !earlier.webhook_id.empty()) continue;

        const std::vector<util::found_link> links = util::find_links(earlier.content);
        if (links.empty()) continue;
        if (candidates++ == attribution_candidates) break;

        const bool matches = std::ranges::any_of(
            links, [&](const util::found_link& link) { return std::ranges::find(ours, path_of(link.url)) != ours.end(); });
        if (matches) {
            found.original_message_id = earlier.id;
            found.author_id = earlier.author_id;
            found.skipped_a_link = candidates > 1;
            return found;
        }
    }

    // Nothing matched. If there were candidates, the nearest link answered
    // something else, which is reported rather than credited (plan §9.7).
    found.mismatched = candidates > 0;
    return found;
}

} // namespace latibot::events
