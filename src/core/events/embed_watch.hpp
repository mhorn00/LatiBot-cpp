#pragma once

#include "core/events/replacements.hpp"
#include "core/events/url_rules.hpp"

#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace latibot::ports {
class clock;
}

namespace latibot::events {

/// How long one attempt gets to produce a preview before the next is tried
/// (plan §20, row 15). One value for every guild until somebody needs
/// another.
inline constexpr std::chrono::seconds embed_timeout{6};

/// Tries per mirror on a fresh replacement: `alt1, alt1, alt2, alt2, …`
/// (plan §9.3). A slow first fetch is common enough to be worth a second
/// chance before giving up on a mirror.
inline constexpr std::size_t attempts_per_mirror = 2;

/// Retry runs one pass, one try per mirror (plan §9.4).
inline constexpr std::size_t retry_attempts_per_mirror = 1;

/// The view name in a Retry button's custom_id; the argument is our
/// message's id, so the button still works after a restart.
inline constexpr std::string_view url_retry_view = "urlretry";

// --------------------------------------------------------------------------
// What the tracker asks for
// --------------------------------------------------------------------------

/// Rewrite our replacement message.
struct edit_replacement {
    dpp::snowflake channel_id;
    dpp::snowflake message_id;
    std::string content;

    /// The failure note: our own previews off, and a Retry button.
    bool failed = false;
};

/// Turn the original message's previews off, or back on.
struct set_original_embeds {
    dpp::snowflake channel_id;
    dpp::snowflake message_id;
    bool suppressed = true;
};

using embed_action = std::variant<edit_replacement, set_original_embeds>;

/// The message an `edit_replacement` becomes, Retry button included.
[[nodiscard]] dpp::message build_edit(const edit_replacement& edit);

// --------------------------------------------------------------------------
// Rendering
// --------------------------------------------------------------------------

enum class link_progress : std::uint8_t { waiting, embedded, failed };

/// One link being tried.
struct watched_link {
    planned_link link;

    /// Counts every try, both of each mirror's; the mirror is
    /// `attempt / attempts_per_mirror`.
    std::size_t attempt = 0;
    link_progress progress = link_progress::waiting;
};

/// The URL `link` is on at this attempt.
[[nodiscard]] std::string current_url(const watched_link& link, std::size_t per_mirror);

/// Our message: one `🔗 [_](link)` line per link, spoilered where the
/// original was (plan §9.2). The underscore is the whole visible text,
/// which is what makes the preview, not the link, the thing people see.
[[nodiscard]] std::string render_replacement(std::span<const watched_link> links, std::size_t per_mirror);

/// The note left when nothing embedded (plan §9.4). It names the mirrors
/// that were tried, which is the first thing anybody asks.
[[nodiscard]] std::string render_failure(std::span<const planned_link> links);

/// Which links a set of previews covers, by index.
///
/// A preview is matched to a link by path first, on any host, since mirrors
/// usually report the original site as their URL. Previews left over are then
/// given to the remaining links in order, which is the order Discord builds
/// them in. With one link, which is nearly always, any preview at all counts.
[[nodiscard]] std::vector<bool> embedded_links(std::span<const watched_link> links, std::span<const std::string> embed_urls);

// --------------------------------------------------------------------------
// The tracker
// --------------------------------------------------------------------------

/// What to start watching.
struct watch_request {
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;

    /// Ours.
    dpp::snowflake message_id;

    /// The message the links came from, in the same channel.
    dpp::snowflake original_message_id;

    std::vector<planned_link> links;
    std::size_t per_mirror = attempts_per_mirror;

    /// A Retry, which has to put the original's previews back off if it
    /// succeeds, since the failure turned them on.
    bool retry = false;
};

/// Follows replacement messages until each link has a preview or has run out
/// of mirrors (plan §9.3).
///
/// Event-driven rather than polled: a preview arriving on `on_embeds`
/// settles a link immediately, and `tick` only moves on links whose time is
/// up. The Java bot fetched the message five seconds later and compared a
/// count, so a slow network looked exactly like a failure.
///
/// Every method returns what should be done rather than doing it, and is
/// safe to call from DPP's event threads.
class embed_tracker {
public:
    embed_tracker(replacement_store& store, ports::clock& clock, std::chrono::milliseconds timeout = embed_timeout);

    /// Starts on a message that was just posted or retried. `embed_urls` is
    /// whatever previews it already had, which Discord sometimes includes in
    /// the reply to the post itself.
    std::vector<embed_action> watch(watch_request request, std::span<const std::string> embed_urls = {});

    /// A message's previews changed. Messages nobody is watching yet are
    /// remembered briefly: the update can overtake the reply to the post that
    /// created the message, and would otherwise be lost.
    std::vector<embed_action> on_embeds(dpp::snowflake message_id, std::span<const std::string> embed_urls);

    /// Moves every link whose time is up to its next attempt.
    std::vector<embed_action> tick();

    /// Ends a watch at once on the previews a message has now, trying nothing
    /// further: any preview makes it working, none makes it failed, with the
    /// note and Retry. For a replacement whose watch was lost to a restart,
    /// where starting over would edit a message that may have sat there for
    /// hours.
    std::vector<embed_action> settle(watch_request request, std::span<const std::string> embed_urls);

    /// Our message was deleted; there is nothing left to edit.
    void forget(dpp::snowflake message_id);

    [[nodiscard]] bool watching(dpp::snowflake message_id) const;

private:
    struct watch_state {
        watch_request request;
        std::vector<watched_link> links;
        std::chrono::steady_clock::time_point deadline;
    };

    struct early_update {
        std::vector<std::string> embed_urls;
        std::chrono::steady_clock::time_point seen;
    };

    /// Marks what `embed_urls` covers. True when nothing is left waiting.
    static bool absorb(watch_state& state, std::span<const std::string> embed_urls);

    /// The end of a watch: records the outcome and says what to change.
    std::vector<embed_action> finish(const watch_state& state);

    void prune_early(std::chrono::steady_clock::time_point now);

    replacement_store* store_;
    ports::clock* clock_;
    std::chrono::milliseconds timeout_;

    mutable std::mutex mutex_;
    std::map<dpp::snowflake, watch_state> watches_;
    std::map<dpp::snowflake, early_update> early_;
};

} // namespace latibot::events
