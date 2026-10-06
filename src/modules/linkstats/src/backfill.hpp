#pragma once

#include "core/events/emote_reactions.hpp"
#include "core/events/legacy_replacements.hpp"
#include "core/events/reactions.hpp"
#include "core/events/replacements.hpp"
#include "core/events/url_rules.hpp"

#include <dpp/coro/task.h>
#include <dpp/snowflake.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::ports {
class clock;
class discord_gateway;
} // namespace latibot::ports

namespace latibot::events {

/// What to recompute (docs/features/Link_Stats.md §4).
struct backfill_request {
    dpp::snowflake guild_id;
    std::vector<dpp::snowflake> channel_ids;

    /// Half-open, like every other date range in the statistics.
    std::chrono::sys_seconds since;
    std::optional<std::chrono::sys_seconds> until;

    /// The bot's own user, which is who wrote every replacement.
    dpp::snowflake bot_id;

    /// Ignore saved progress and scan every channel from the top.
    bool fresh = false;

    /// Also count people's own image and video posts, as a guild that has
    /// turned that on does (docs/features/Link_Stats.md §9).
    bool images = false;
};

/// A message and the channel it is in: enough to link to it.
struct message_place {
    dpp::snowflake channel_id;
    dpp::snowflake message_id;

    friend auto operator==(const message_place&, const message_place&) -> bool = default;
};

/// The link Discord's "Copy Message Link" gives, which opens the message in
/// its channel.
[[nodiscard]] auto jump_link(dpp::snowflake guild_id, const message_place& place) -> std::string;

/// What a backfill found. Also its progress while it runs.
struct backfill_report {
    std::int64_t scanned = 0;
    std::int64_t replacements = 0;
    std::int64_t attributed = 0;
    std::int64_t unattributed = 0;

    /// Of the unattributed: an earlier link was there, but its path did not
    /// match, so it was reported rather than accepted
    /// (docs/features/Link_Stats.md §4.2).
    std::vector<message_place> mismatched;
    std::int64_t webhooks_skipped = 0;

    /// On replacements.
    std::int64_t reactions = 0;

    /// People's image and video posts found, and the reactions on them.
    std::int64_t images = 0;
    std::int64_t image_reactions = 0;

    /// Emojis sent as messages of their own after a post, or as replies to
    /// it, counted as reactions to it (docs/features/Link_Stats.md §12).
    std::int64_t emote_reactions = 0;

    /// Messages that look like the bot's replacements but match no known
    /// format: counted, never guessed at.
    std::vector<message_place> unparsed;

    /// Replacements whose reactions could not be read, which keep the
    /// counts they had.
    std::vector<message_place> unread;

    /// Mirror hosts learned from the replacements found, host then site, in
    /// the order they were learned: ones no rule remembered, recognised by
    /// what they answered. Remembered for the next run too.
    std::vector<std::pair<std::string, std::string>> learned_mirrors;

    std::size_t channels_done = 0;
    std::size_t channels_total = 0;

    /// Channels or calls that failed, in words.
    std::vector<std::string> problems;

    bool cancelled = false;
};

/// How far a backfill got in one channel.
struct channel_progress {
    std::optional<dpp::snowflake> oldest_scanned;
    bool complete = false;
};

/// `backfill_progress`.
class backfill_progress_store {
public:
    explicit backfill_progress_store(db::database& db) : db_(&db) {}

    /// Progress for this channel over exactly this range; nothing for any
    /// other range, which has to start again.
    [[nodiscard]] auto find(dpp::snowflake guild_id, dpp::snowflake channel_id, std::chrono::sys_seconds since,
                            std::optional<std::chrono::sys_seconds> until) const -> std::optional<channel_progress>;

    auto save(dpp::snowflake guild_id, dpp::snowflake channel_id, std::chrono::sys_seconds since,
              std::optional<std::chrono::sys_seconds> until, const channel_progress& progress, std::chrono::sys_seconds now) -> void;

private:
    db::database* db_;
};

/// Messages per history page: Discord's maximum.
inline constexpr std::uint64_t history_page_size = 100;

/// Reactors per page of one reaction: Discord's maximum.
inline constexpr std::uint64_t reactor_page_size = 100;

/// How often, in messages scanned, a running backfill reports progress.
inline constexpr std::int64_t progress_interval = 500;

/// Recovers years of reactions on the bot's old replacements
/// (docs/features/Link_Stats.md §4).
///
/// Walks each channel backwards a page at a time, recognises the bot's
/// replacements in any of their six historical formats, works out whose link
/// each one replaced, and rebuilds its reactions from what Discord shows now,
/// and the emotes sent as reactions from the messages after it (§12).
/// Re-running is safe: every message's rows are rebuilt, not added to.
///
/// Recognising them does not depend on today's URL rules: a mirror that
/// broke years ago is in none of them. See `classify`.
///
/// One runs per guild at a time. `begin` claims the guild, `cancel` asks a
/// running one to stop at the next page, and `end` releases it.
class backfill_service {
public:
    backfill_service(ports::discord_gateway& discord, url_rule_store& rules, replacement_store& replacements, reaction_store& reactions,
                     backfill_progress_store& progress, ports::clock& clock);

    /// Called every `progress_interval` messages with the report so far.
    using progress_fn = std::function<dpp::task<void>(const backfill_report&)>;

    auto run(backfill_request request, progress_fn progress = {}) -> dpp::task<backfill_report>;

    /// False when one is already running for this guild.
    auto begin(dpp::snowflake guild_id) -> bool;
    auto end(dpp::snowflake guild_id) -> void;

    /// False when none is running.
    auto cancel(dpp::snowflake guild_id) -> bool;

    [[nodiscard]] auto running(dpp::snowflake guild_id) const -> bool;

private:
    using flag = std::shared_ptr<std::atomic<bool>>;

    /// What counting emote reactions needs from the messages a walk has
    /// already passed in one channel (docs/features/Link_Stats.md §12).
    struct emote_scan {
        /// The messages just after where the walk is, nearest first, each
        /// with whether it is a post: at most `emote_window`.
        std::deque<std::pair<emote_message, bool>> newer;

        /// Replies of nothing but emotes, by the message they answer, until
        /// the walk reaches it.
        std::map<dpp::snowflake, std::vector<emote_message>> replies;

        /// The first message this run does not read, where the walk
        /// started; nothing when it started at the newest. Emotes sent from
        /// there on are kept as they were.
        std::optional<dpp::snowflake> unseen_from;
    };

    struct channel_scan {
        const backfill_request* request;
        dpp::snowflake channel_id;

        /// Every mirror known, including the ones this run has learned.
        mirror_map* mirrors;
        flag cancelled;
        emote_scan* emotes;
    };

    /// Remembers the hosts in `replaced` that no rule knew, with the site
    /// each stood in for.
    auto learn_mirrors(const channel_scan& scan, const std::vector<replaced_link>& replaced, backfill_report& report) -> void;

    auto scan_channel(channel_scan scan, backfill_report& report, const progress_fn& progress, std::int64_t& next_progress)
        -> dpp::task<void>;

    /// Where a channel's walk starts: the end of the range, or where an
    /// earlier run over the same range stopped. Nothing when that run
    /// finished the channel.
    [[nodiscard]] auto starting_point(const channel_scan& scan) const -> std::optional<dpp::snowflake>;

    auto save_progress(const channel_scan& scan, channel_progress progress) -> void;

    /// Looks at one page and saves how far it got. Returns the next page,
    /// empty when the range or the channel has run out, or nothing when a
    /// page could not be read.
    auto scan_page(const channel_scan& scan, std::vector<history_message> page, backfill_report& report)
        -> dpp::task<std::optional<std::vector<history_message>>>;

    /// One page of history, newest first, or nothing with the reason noted.
    auto page_before(dpp::snowflake channel_id, dpp::snowflake before, backfill_report& report)
        -> dpp::task<std::optional<std::vector<history_message>>>;

    auto consider(const channel_scan& scan, const history_message& message, std::span<const history_message> older, backfill_report& report)
        -> dpp::task<void>;

    /// Rebuilds a post's emote reactions from the messages after it, then
    /// remembers `message` for the posts before it.
    auto count_emotes(const channel_scan& scan, const history_message& message, backfill_report& report) -> void;

    /// Records an image or video post and rebuilds its reactions.
    auto consider_image(const channel_scan& scan, const history_message& message, backfill_report& report) -> dpp::task<void>;

    /// Rebuilds one message's reactions to match what Discord shows now, and
    /// says how many it has. Nothing when they could not be read, which is
    /// noted in the report and leaves the old counts alone.
    auto rebuild_reactions(const channel_scan& scan, const history_message& message, backfill_report& report)
        -> dpp::task<std::optional<std::int64_t>>;

    /// The reactions on one message, as Discord shows them now. Nothing
    /// when a page could not be read, so a failed call never erases rows.
    auto reactors(dpp::snowflake channel_id, const history_message& message)
        -> dpp::task<std::optional<std::vector<reaction_store::observed>>>;

    [[nodiscard]] auto cancel_flag(dpp::snowflake guild_id) const -> flag;

    ports::discord_gateway* discord_;
    url_rule_store* rules_;
    replacement_store* replacements_;
    reaction_store* reactions_;
    backfill_progress_store* progress_;
    ports::clock* clock_;

    mutable std::mutex mutex_;
    std::map<dpp::snowflake, flag> jobs_;
};

} // namespace latibot::events
