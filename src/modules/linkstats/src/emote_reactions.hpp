#pragma once

#include "legacy_replacements.hpp"
#include "reactions.hpp"

#include <dpp/snowflake.h>

#include <cstddef>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace latibot::events {

class replacement_store;

// Emotes sent as a message of their own just after a post, or as a reply to
// it: a reaction in all but name, and counted as one
// (src/modules/linkstats/docs/Link_Stats.md §12).

/// How many messages after a post can be a reaction to it. The next post
/// ends it sooner.
inline constexpr std::size_t emote_window = 25;

/// The emojis in a message that holds nothing else, each once, in the order
/// they first appear: custom emojis as `<:name:id>`, and Unicode emojis,
/// joined sequences, flags and keycaps included. Empty for any other message,
/// and for one with nothing in it.
[[nodiscard]] auto message_emotes(std::string_view content) -> std::vector<emoji_ref>;

/// A message as counting emotes sees it.
struct emote_message {
    dpp::snowflake id;
    dpp::snowflake author_id;

    /// Somebody, not a bot or a webhook.
    bool from_person = false;

    /// The message this one replies to, when it is a reply.
    dpp::snowflake replied_to;

    /// `message_emotes` of its text, when it is all emotes and carries no
    /// file or sticker; empty otherwise.
    std::vector<emoji_ref> emotes;
};

/// A message from history, or one just posted, as counting emotes sees it.
[[nodiscard]] auto as_emote_message(const history_message& message) -> emote_message;

/// The emote reactions to `post`.
///
/// `after` is the messages after it, oldest first, already cut at the next
/// post; only the first `emote_window` are looked at. Each person's first
/// message there counts when it is all emotes and is not a reply to
/// something else. `replies` are replies to the post from anywhere later,
/// and each counts when it is all emotes. Each emoji counts once per person,
/// however often they sent it.
[[nodiscard]] auto emote_reactions(dpp::snowflake post, std::span<const emote_message> after, std::span<const emote_message> replies)
    -> std::vector<emote_reaction>;

/// Counts emote reactions as messages arrive.
///
/// Keeps the last few messages of each channel, enough to see how far back
/// the nearest post is and whether someone has spoken since. After a restart
/// it knows none, so emotes after a post from before it are counted only as
/// replies, until a recompute.
class emote_tracker {
public:
    emote_tracker(replacement_store& posts, reaction_store& reactions);

    /// A message just posted in a server. Returns how many emote reactions
    /// it was counted as.
    auto on_message(dpp::snowflake channel_id, emote_message message) -> std::size_t;

    /// A message became a post after it arrived: a link Discord has just
    /// shown to be an image (§9.3). The messages after it are looked at
    /// again. Returns how many emote reactions that found.
    auto on_post(dpp::snowflake channel_id, dpp::snowflake post) -> std::size_t;

    /// A message was deleted: whatever it was counted as goes with it.
    auto on_delete(dpp::snowflake channel_id, dpp::snowflake message_id) -> void;

private:
    /// The post the newest of `messages` follows, when it is its author's
    /// first message since, at most `emote_window` messages after it.
    [[nodiscard]] auto post_before(const std::deque<emote_message>& messages) const -> std::optional<dpp::snowflake>;

    replacement_store* posts_;
    reaction_store* reactions_;

    std::mutex mutex_;

    /// The newest messages of each channel, oldest first: at most the
    /// window and the post before it.
    std::map<dpp::snowflake, std::deque<emote_message>> recent_;
};

} // namespace latibot::events
