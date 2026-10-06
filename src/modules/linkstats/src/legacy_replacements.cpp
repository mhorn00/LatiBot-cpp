#include "legacy_replacements.hpp"

#include "core/util/url_scan.hpp"
#include "media_posts.hpp"

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

/// Which of the masked formats `content` is, with every link in it masked by
/// `masked`. Nothing when it is none of them.
auto masked_format(std::string_view content, const std::vector<masked_link>& masked) -> std::optional<legacy_format> {
    // Everything outside the masked links has to be decoration; anything
    // else is a shape nobody wrote down.
    std::string rest;
    std::size_t last = 0;
    for (const masked_link& link : masked) {
        rest += content.substr(last, link.begin - last);
        last = link.end;
    }
    rest += content.substr(last);

    const bool marked = rest.contains("🔗") || rest.contains(":link:");
    if (!only_decoration(rest)) return std::nullopt;

    // The label inside the brackets tells the formats apart: "." with or
    // without the 🔗, and "_", which only ever came with it.
    const auto labelled = [&](std::string_view label) {
        return std::ranges::all_of(masked, [&](const masked_link& link) { return link.label == label; });
    };
    if (labelled(".")) return marked ? legacy_format::link_dot : legacy_format::dot;
    if (labelled("_") && marked) return legacy_format::link_underscore;
    return std::nullopt;
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
    described.has_media = has_media(message);
    described.has_files = !message.attachments.empty() || !message.stickers.empty();

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

    // Step 1: a replacement always has a link in it. A message with none
    // cannot be one, whoever wrote it, and that settles most messages.
    const std::vector<util::found_link> links = util::find_links(message.content);
    if (links.empty()) return {};

    std::vector<util::found_link> mirror_links;
    for (const util::found_link& found : links) {
        const auto parts = util::split_url(found.url);
        if (parts && mirrors.contains(util::rule_host(parts->authority))) mirror_links.push_back(found);
    }
    const bool known = !mirror_links.empty();

    // Webhook mode posted as the member and deleted the original, so there
    // is nothing to attribute from and nothing of the bot's to count. Only a
    // known mirror says a webhook's message was one: it is otherwise just
    // somebody's message.
    if (!message.webhook_id.empty()) {
        if (!known) return {};
        match.what = legacy_match::kind::webhook;
        match.format = legacy_format::webhook;
        return match;
    }

    // Step 2: from here on it must be the bot's own ordinary message.
    // Somebody else posting a mirror link is just a link.
    if (message.author_id != bot_id || message.is_system) return {};

    // Without a known mirror, any of the links may be one: the mirror may be
    // older than every rule (see the header).
    const std::vector<util::found_link>& candidates = known ? mirror_links : links;
    for (const util::found_link& found : candidates) {
        match.mirror_urls.emplace_back(found.url);
    }
    const auto copy_kind = known ? legacy_match::kind::recognised : legacy_match::kind::unconfirmed;

    // Step 3: the formats, from the easiest to tell apart. A reply is always
    // format 1, whatever its text looks like.
    if (!message.replied_to.empty()) {
        match.what = copy_kind;
        match.format = legacy_format::reply_copy;
        return match;
    }

    std::vector<masked_link> masked;
    for (const util::found_link& link : candidates) {
        if (const auto around = masked_around(message.content, link)) masked.push_back(*around);
    }

    // Bare links in the text: the full copy of the original.
    if (masked.empty()) {
        match.what = copy_kind;
        match.format = legacy_format::plain_copy;
        return match;
    }

    // Some links masked and some bare is no format the bot ever wrote, so
    // from here every way out that is not a known format says so. Without a
    // known mirror nothing says it was a replacement at all, so it is left
    // alone rather than reported.
    match.what = known ? legacy_match::kind::unrecognised : legacy_match::kind::not_ours;
    if (masked.size() != candidates.size()) return match;

    if (const auto format = masked_format(message.content, masked)) {
        match.what = legacy_match::kind::recognised;
        match.format = *format;
    }
    return match;
}

auto replaced_links(const legacy_match& match, std::string_view original) -> std::vector<replaced_link> {
    const std::vector<util::found_link> theirs = util::find_links(original);

    std::vector<replaced_link> replaced;
    for (const std::string& mirror : match.mirror_urls) {
        const auto ours = util::split_url(mirror);
        if (!ours) continue;
        const std::string_view path = util::comparable_path(ours->path);
        if (path.empty()) continue;

        // A replacement always moves a link to another host; the same link on
        // the same host is the bot repeating it, not replacing it.
        const std::string host = util::rule_host(ours->authority);
        const auto answered = std::ranges::find_if(theirs, [&](const util::found_link& link) {
            const auto parts = util::split_url(link.url);
            return parts && util::comparable_path(parts->path) == path && util::rule_host(parts->authority) != host;
        });
        if (answered != theirs.end()) replaced.push_back({.mirror_url = mirror, .original_url = std::string(answered->url)});
    }
    return replaced;
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
            found.replaced = replaced_links(match, original->content);
        }
        return found;
    }

    // Every other format has to be matched to the message it answered: the
    // paths of the mirror links, which a mirror keeps from the original. A
    // link to a site's front page matches every other one; it proves nothing
    // about which message was answered.
    if (std::ranges::all_of(match.mirror_urls, [](const std::string& url) { return path_of(url).empty(); })) return found;

    // Walk back through older messages, newest first. Only a person's message
    // with links counts as a candidate, and only the first few candidates are
    // tried: a match further back than that is more likely a coincidence.
    std::size_t candidates = 0;
    for (const history_message& earlier : older) {
        if (earlier.author_is_bot || earlier.author_id == bot_id || !earlier.webhook_id.empty()) continue;

        if (util::find_links(earlier.content).empty()) continue;
        if (candidates++ == attribution_candidates) break;

        if (auto replaced = replaced_links(match, earlier.content); !replaced.empty()) {
            found.original_message_id = earlier.id;
            found.author_id = earlier.author_id;
            found.skipped_a_link = candidates > 1;
            found.replaced = std::move(replaced);
            return found;
        }
    }

    // Nothing matched. If there were candidates, the nearest link answered
    // something else, which is reported rather than credited
    // (docs/features/Link_Stats.md §4.2).
    found.mismatched = candidates > 0;
    return found;
}

} // namespace latibot::events
