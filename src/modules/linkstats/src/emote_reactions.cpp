#include "emote_reactions.hpp"

#include "core/util/log.hpp"
#include "links/replacements.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace latibot::events {
namespace {

constexpr char32_t variation_selector = 0xFE0F;
constexpr char32_t joiner = 0x200D;
constexpr char32_t keycap = 0x20E3;
constexpr char32_t tag_end = 0xE007F;

/// Code points that are emojis on their own, roughly Unicode's
/// Extended_Pictographic. Skin tones are in here too, and also follow one.
constexpr std::array<std::pair<char32_t, char32_t>, 28> pictographic{{
    {0x203C, 0x203C}, {0x2049, 0x2049}, {0x2122, 0x2122}, {0x2139, 0x2139}, {0x2194, 0x2199}, {0x21A9, 0x21AA},   {0x231A, 0x231B},
    {0x2328, 0x2328}, {0x23CF, 0x23CF}, {0x23E9, 0x23F3}, {0x23F8, 0x23FA}, {0x24C2, 0x24C2}, {0x25AA, 0x25AB},   {0x25B6, 0x25B6},
    {0x25C0, 0x25C0}, {0x25FB, 0x25FE}, {0x2600, 0x27BF}, {0x2934, 0x2935}, {0x2B05, 0x2B07}, {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},
    {0x2B55, 0x2B55}, {0x3030, 0x3030}, {0x303D, 0x303D}, {0x3297, 0x3297}, {0x3299, 0x3299}, {0x1F000, 0x1F1E5}, {0x1F200, 0x1FAFF},
}};

auto is_pictographic(char32_t point) -> bool {
    return std::ranges::any_of(pictographic, [&](const auto& range) { return point >= range.first && point <= range.second; });
}

auto is_regional_indicator(char32_t point) -> bool {
    return point >= 0x1F1E6 && point <= 0x1F1FF;
}

auto is_skin_tone(char32_t point) -> bool {
    return point >= 0x1F3FB && point <= 0x1F3FF;
}

auto is_tag(char32_t point) -> bool {
    return point >= 0xE0020 && point <= 0xE007E;
}

/// Reads UTF-8 text a code point at a time. Malformed text reads as nothing
/// more, which is no emoji.
class code_points {
public:
    explicit code_points(std::string_view text) : text_(text) {}

    [[nodiscard]] auto at() const -> std::size_t { return at_; }
    [[nodiscard]] auto done() const -> bool { return at_ >= text_.size(); }

    /// The next code point, without moving past it.
    [[nodiscard]] auto peek() const -> std::optional<char32_t> {
        std::size_t length = 0;
        return decode(length);
    }

    /// The next code point, moving past it.
    auto next() -> std::optional<char32_t> {
        std::size_t length = 0;
        const auto point = decode(length);
        if (point) at_ += length;
        return point;
    }

    /// Moves past the next code point when it is `wanted`.
    auto skip(char32_t wanted) -> bool {
        if (peek() != wanted) return false;
        next();
        return true;
    }

private:
    [[nodiscard]] auto decode(std::size_t& length) const -> std::optional<char32_t> {
        if (done()) return std::nullopt;
        const auto lead = static_cast<unsigned char>(text_[at_]);
        char32_t point = 0;
        if (lead < 0x80) {
            length = 1;
            point = lead;
        } else if ((lead & 0xE0U) == 0xC0) {
            length = 2;
            point = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0) {
            length = 3;
            point = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0) {
            length = 4;
            point = lead & 0x07U;
        } else {
            return std::nullopt;
        }
        if (at_ + length > text_.size()) return std::nullopt;
        for (std::size_t index = 1; index < length; ++index) {
            const auto byte = static_cast<unsigned char>(text_[at_ + index]);
            if ((byte & 0xC0U) != 0x80) return std::nullopt;
            point = (point << 6U) | (byte & 0x3FU);
        }
        return point;
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

/// Moves past one Unicode emoji, joined sequences and all: a flag, a keycap,
/// or emojis joined by U+200D, each with its variation selector, skin tone
/// and tags. False, wherever it stopped, when what is there is not one.
auto skip_unicode_emoji(code_points& text) -> bool {
    const auto first = text.next();
    if (!first) return false;

    if (is_regional_indicator(*first)) {
        const auto second = text.next();
        return second && is_regional_indicator(*second);
    }
    if ((*first >= '0' && *first <= '9') || *first == '#' || *first == '*') {
        text.skip(variation_selector);
        return text.skip(keycap);
    }
    if (!is_pictographic(*first)) return false;

    while (true) {
        text.skip(variation_selector);
        if (const auto tone = text.peek(); tone && is_skin_tone(*tone)) text.next();
        // A subdivision flag, England's say: tags, then their end.
        while (const auto tag = text.peek()) {
            if (!is_tag(*tag)) break;
            text.next();
        }
        text.skip(tag_end);

        if (!text.skip(joiner)) return true;
        const auto joined = text.next();
        if (!joined || !is_pictographic(*joined)) return false;
    }
}

auto is_space(char letter) -> bool {
    return letter == ' ' || letter == '\n' || letter == '\r' || letter == '\t';
}

} // namespace

auto message_emotes(std::string_view content) -> std::vector<emoji_ref> {
    std::vector<emoji_ref> found;
    std::set<std::string, std::less<>> seen;
    const auto keep = [&](emoji_ref emoji) {
        if (seen.insert(emoji.key).second) found.push_back(std::move(emoji));
    };

    std::size_t at = 0;
    while (at < content.size()) {
        if (is_space(content[at])) {
            ++at;
            continue;
        }

        // A custom emoji, <:name:id> or <a:name:id>.
        if (content[at] == '<') {
            const std::size_t close = content.find('>', at);
            if (close == std::string_view::npos) return {};
            auto custom = parse_emoji(content.substr(at, close - at + 1));
            if (!custom || !custom->key.starts_with("c:")) return {};
            keep(std::move(*custom));
            at = close + 1;
            continue;
        }

        code_points text(content.substr(at));
        if (!skip_unicode_emoji(text)) return {};
        keep(reaction_emoji({}, content.substr(at, text.at())));
        at += text.at();
    }
    return found;
}

auto as_emote_message(const history_message& message) -> emote_message {
    const bool from_person = !message.author_is_bot && message.webhook_id.empty() && !message.is_system;
    return {.id = message.id,
            .author_id = message.author_id,
            .from_person = from_person,
            .replied_to = message.replied_to,
            .emotes = from_person && !message.has_files ? message_emotes(message.content) : std::vector<emoji_ref>{}};
}

auto emote_reactions(dpp::snowflake post, std::span<const emote_message> after, std::span<const emote_message> replies)
    -> std::vector<emote_reaction> {
    std::vector<const emote_message*> counted;

    // Each person's first message after the post, and only that one: what
    // they said next may be about anything.
    std::set<std::uint64_t> spoken;
    for (const emote_message& message : after.first(std::min(after.size(), emote_window))) {
        if (!message.from_person || !spoken.insert(static_cast<std::uint64_t>(message.author_id)).second) continue;
        const bool elsewhere = !message.replied_to.empty() && message.replied_to != post;
        if (!elsewhere && !message.emotes.empty()) counted.push_back(&message);
    }
    for (const emote_message& reply : replies) {
        if (reply.from_person && reply.replied_to == post && !reply.emotes.empty()) counted.push_back(&reply);
    }

    // Oldest first, so an emoji somebody sent twice is counted from the
    // first message it was in.
    std::ranges::sort(counted, {}, [](const emote_message* message) { return static_cast<std::uint64_t>(message->id); });
    std::vector<emote_reaction> found;
    std::set<std::pair<std::uint64_t, std::string>> once;
    for (const emote_message* message : counted) {
        for (const emoji_ref& emoji : message->emotes) {
            if (!once.emplace(static_cast<std::uint64_t>(message->author_id), emoji.key).second) continue;
            found.push_back({.user_id = message->author_id, .emoji = emoji, .source_id = message->id});
        }
    }
    return found;
}

// --------------------------------------------------------------------------

emote_tracker::emote_tracker(replacement_store& posts, reaction_store& reactions) : posts_(&posts), reactions_(&reactions) {}

auto emote_tracker::post_before(const std::deque<emote_message>& messages) const -> std::optional<dpp::snowflake> {
    const emote_message& newest = messages.back();
    for (auto earlier = std::next(messages.rbegin()); earlier != messages.rend(); ++earlier) {
        if (posts_->contains(earlier->id)) return earlier->id;
        if (earlier->from_person && earlier->author_id == newest.author_id) return std::nullopt;
    }
    return std::nullopt;
}

auto emote_tracker::on_message(dpp::snowflake channel_id, emote_message message) -> std::size_t {
    if (!message.from_person) message.emotes.clear();

    const std::scoped_lock guard(mutex_);
    std::deque<emote_message>& messages = recent_[channel_id];
    messages.push_back(message);
    if (messages.size() > emote_window + 1) messages.pop_front();
    if (message.emotes.empty()) return 0;

    // A reply is about what it replies to, however far back that is, and
    // about nothing else.
    std::vector<emote_reaction> found;
    if (!message.replied_to.empty()) {
        if (!posts_->contains(message.replied_to)) return 0;
        found = emote_reactions(message.replied_to, {}, std::span(&message, 1));
        return reactions_->add_emotes(message.replied_to, found);
    }

    const auto post = post_before(messages);
    if (!post) return 0;
    found = emote_reactions(*post, std::span(&message, 1), {});
    const std::size_t added = reactions_->add_emotes(*post, found);
    if (added > 0) util::log().debug("{} sent {} emote(s) after post {}", message.author_id, added, *post);
    return added;
}

auto emote_tracker::on_post(dpp::snowflake channel_id, dpp::snowflake post) -> std::size_t {
    const std::scoped_lock guard(mutex_);
    const auto channel = recent_.find(channel_id);
    if (channel == recent_.end()) return 0;

    const std::deque<emote_message>& messages = channel->second;
    const auto found = std::ranges::find(messages, post, &emote_message::id);
    if (found == messages.end()) return 0;

    // Up to the next post, which has the messages after it.
    std::vector<emote_message> after;
    for (auto later = std::next(found); later != messages.end() && !posts_->contains(later->id); ++later) {
        after.push_back(*later);
    }
    return reactions_->add_emotes(post, emote_reactions(post, after, {}));
}

auto emote_tracker::on_delete(dpp::snowflake channel_id, dpp::snowflake message_id) -> void {
    {
        const std::scoped_lock guard(mutex_);
        if (const auto channel = recent_.find(channel_id); channel != recent_.end()) {
            std::erase_if(channel->second, [&](const emote_message& message) { return message.id == message_id; });
        }
    }
    if (const std::size_t gone = reactions_->remove_emotes_from(message_id); gone > 0) {
        util::log().debug("message {} was deleted, and the {} emote reaction(s) it was counted as with it", message_id, gone);
    }
}

} // namespace latibot::events
