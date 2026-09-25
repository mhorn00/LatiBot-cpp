#pragma once

#include "core/events/legacy_replacements.hpp"
#include "core/events/reactions.hpp"
#include "core/events/replacements.hpp"
#include "core/events/url_rules.hpp"

#include <dpp/coro/task.h>
#include <dpp/snowflake.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::ports {
class clock;
class discord_gateway;
} // namespace latibot::ports

namespace latibot::events {

/// What to recompute (plan §9.7).
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
};

/// What a backfill found. Also its progress while it runs.
struct backfill_report {
    std::int64_t scanned = 0;
    std::int64_t replacements = 0;
    std::int64_t attributed = 0;
    std::int64_t unattributed = 0;

    /// Of the unattributed: an earlier link was there, but its path did not
    /// match, so it was reported rather than accepted (plan §9.7).
    std::int64_t mismatched = 0;
    std::int64_t webhooks_skipped = 0;
    std::int64_t reactions = 0;

    /// Messages that look like the bot's replacements but match no known
    /// format, by id: counted, never guessed at.
    std::vector<dpp::snowflake> unparsed;

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
    [[nodiscard]] std::optional<channel_progress> find(dpp::snowflake guild_id, dpp::snowflake channel_id, std::chrono::sys_seconds since,
                                                       std::optional<std::chrono::sys_seconds> until) const;

    void save(dpp::snowflake guild_id, dpp::snowflake channel_id, std::chrono::sys_seconds since,
              std::optional<std::chrono::sys_seconds> until, const channel_progress& progress, std::chrono::sys_seconds now);

private:
    db::database* db_;
};

/// Messages per history page: Discord's maximum.
inline constexpr std::uint64_t history_page_size = 100;

/// Reactors per page of one reaction: Discord's maximum.
inline constexpr std::uint64_t reactor_page_size = 100;

/// How often, in messages scanned, a running backfill reports progress.
inline constexpr std::int64_t progress_interval = 500;

/// Recovers years of reactions on the bot's old replacements (plan §9.7).
///
/// Walks each channel backwards a page at a time, recognises the bot's
/// replacements in any of their six historical formats, works out whose link
/// each one replaced, and rebuilds its reactions from what Discord shows now.
/// Re-running is safe: every message's rows are rebuilt, not added to.
///
/// One runs per guild at a time. `begin` claims the guild, `cancel` asks a
/// running one to stop at the next page, and `end` releases it.
class backfill_service {
public:
    backfill_service(ports::discord_gateway& discord, const url_rule_store& rules, replacement_store& replacements,
                     reaction_store& reactions, backfill_progress_store& progress, ports::clock& clock);

    /// Called every `progress_interval` messages with the report so far.
    using progress_fn = std::function<dpp::task<void>(const backfill_report&)>;

    dpp::task<backfill_report> run(backfill_request request, progress_fn progress = {});

    /// False when one is already running for this guild.
    bool begin(dpp::snowflake guild_id);
    void end(dpp::snowflake guild_id);

    /// False when none is running.
    bool cancel(dpp::snowflake guild_id);

    [[nodiscard]] bool running(dpp::snowflake guild_id) const;

private:
    using flag = std::shared_ptr<std::atomic<bool>>;

    struct channel_scan {
        const backfill_request* request;
        dpp::snowflake channel_id;
        const mirror_map* mirrors;
        flag cancelled;
    };

    dpp::task<void> scan_channel(channel_scan scan, backfill_report& report, const progress_fn& progress, std::int64_t& next_progress);

    /// Where a channel's walk starts: the end of the range, or where an
    /// earlier run over the same range stopped. Nothing when that run
    /// finished the channel.
    [[nodiscard]] std::optional<dpp::snowflake> starting_point(const channel_scan& scan) const;

    void save_progress(const channel_scan& scan, channel_progress progress);

    /// Looks at one page and saves how far it got. Returns the next page,
    /// empty when the range or the channel has run out, or nothing when a
    /// page could not be read.
    dpp::task<std::optional<std::vector<history_message>>> scan_page(const channel_scan& scan, std::vector<history_message> page,
                                                                     backfill_report& report);

    /// One page of history, newest first, or nothing with the reason noted.
    dpp::task<std::optional<std::vector<history_message>>> page_before(dpp::snowflake channel_id, dpp::snowflake before,
                                                                       backfill_report& report);

    dpp::task<void> consider(const channel_scan& scan, const history_message& message, std::span<const history_message> older,
                             backfill_report& report);

    /// The reactions on one replacement, as Discord shows them now. Nothing
    /// when a page could not be read, so a failed call never erases rows.
    dpp::task<std::optional<std::vector<reaction_store::observed>>> reactors(dpp::snowflake channel_id, const history_message& message);

    [[nodiscard]] flag cancel_flag(dpp::snowflake guild_id) const;

    ports::discord_gateway* discord_;
    const url_rule_store* rules_;
    replacement_store* replacements_;
    reaction_store* reactions_;
    backfill_progress_store* progress_;
    ports::clock* clock_;

    mutable std::mutex mutex_;
    std::map<dpp::snowflake, flag> jobs_;
};

} // namespace latibot::events
