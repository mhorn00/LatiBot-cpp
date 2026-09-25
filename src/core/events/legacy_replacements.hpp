#pragma once

#include "core/events/url_rules.hpp"

#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::events {

/// A message from channel history, reduced to what recognising the bot's old
/// replacements needs (plan §9.7).
struct history_message {
    dpp::snowflake id;
    dpp::snowflake author_id;
    bool author_is_bot = false;

    /// Set for a message a webhook posted, which is how the Java bot's webhook
    /// mode spoke as somebody else.
    dpp::snowflake webhook_id;

    /// A reply to a slash command, or any message that is not ordinary text:
    /// never a replacement, even with a mirror link in it.
    bool is_system = false;

    /// The message this one replies to, when it is a reply.
    dpp::snowflake replied_to;

    std::string content;

    struct reaction_count {
        dpp::snowflake emoji_id;
        std::string emoji_name;
        std::uint32_t count = 0;
    };
    std::vector<reaction_count> reactions;
};

[[nodiscard]] history_message describe_history(const dpp::message& message);

/// When Discord made a message, from its id.
[[nodiscard]] std::chrono::sys_seconds created_at(dpp::snowflake id) noexcept;

/// The smallest id Discord could give a message made at `when`, which is
/// what paging "before a time" needs.
[[nodiscard]] dpp::snowflake first_id_at(std::chrono::sys_seconds when) noexcept;

/// The shapes the bot's replacements have had over the years (plan §9.7).
enum class legacy_format : std::uint8_t {
    /// The whole original text with the link replaced, posted as a reply.
    reply_copy = 1,
    /// Webhook mode: the original deleted and reposted as its author. Skipped:
    /// not the bot's own message, and the original is gone.
    webhook = 2,
    /// The whole original text with the link replaced, as a plain message.
    plain_copy = 3,
    /// `[.](link)`.
    dot = 4,
    /// `🔗 [.](link)`.
    link_dot = 5,
    /// `🔗 [_](link)`, which is still the format today.
    link_underscore = 6,
};

/// What a history message turned out to be.
struct legacy_match {
    enum class kind : std::uint8_t {
        /// Not a replacement: nobody's concern here.
        not_ours,
        /// A webhook replacement, counted and skipped.
        webhook,
        /// The bot's, with a mirror link, in one of the known formats.
        recognised,
        /// The bot's, with a mirror link, in no known format: reported by id
        /// rather than guessed at.
        unrecognised,
    };

    kind what = kind::not_ours;
    legacy_format format = legacy_format::link_underscore;

    /// The mirror links in it, in order.
    std::vector<std::string> mirror_urls;
};

/// Recognises the bot's old replacements (plan §9.7).
///
/// A message counts when the bot wrote it and it contains a link to a known
/// mirror, current or historical. That holds across every format, since each
/// one contains the replaced link; the format then says where to look for
/// the original.
[[nodiscard]] legacy_match classify(const history_message& message, dpp::snowflake bot_id, const mirror_map& mirrors);

/// Who a replacement was for.
struct attribution {
    std::optional<dpp::snowflake> original_message_id;
    std::optional<dpp::snowflake> author_id;

    /// A reply whose original is not in the history at hand: somebody has to
    /// fetch `original_message_id` to learn its author.
    bool needs_fetch = false;

    /// The nearest earlier link was not ours, and one further back was:
    /// worth a line in the log when a count looks off.
    bool skipped_a_link = false;

    /// Earlier links were there but none matched, so the message is left
    /// unattributed rather than credited to the wrong person. Reported.
    bool mismatched = false;
};

/// How many earlier messages with links are looked at before giving up.
/// The bot answers in a second or two; anything further back is not the
/// message it was answering.
inline constexpr std::size_t attribution_candidates = 10;

/// Finds the original a replacement answered.
///
/// A reply says so itself. Otherwise the original is the nearest earlier
/// message with a link in it, skipping bots and messages without links,
/// which covers somebody chatting in between. The link's path has to match
/// one of ours: a replacement for somebody else's link that happened to come
/// first would otherwise take the credit, so a mismatch keeps looking and,
/// failing that, leaves the message unattributed rather than guessed.
///
/// `older` is the channel before `message`, newest first.
[[nodiscard]] attribution attribute(const history_message& message, const legacy_match& match, std::span<const history_message> older,
                                    dpp::snowflake bot_id);

} // namespace latibot::events
