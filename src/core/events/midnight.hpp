#pragma once

#include "core/events/message_pipeline.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::ports {
class clock;
}

namespace latibot::events {

/// How long after local midnight an entry is allowed to fire.
///
/// A few seconds of slack, so a tick landing a moment early does not post for
/// yesterday (plan §10).
inline constexpr std::chrono::seconds midnight_grace{5};

/// How long after local midnight an entry may still post.
///
/// Past this the day is skipped rather than posted late. A bot that was not
/// running at midnight should wait for the next one, not announce yesterday's
/// midnight over breakfast.
///
/// Comfortably wider than the tick, so ordinary scheduling jitter and a quick
/// restart still post. Changing how late is too late is this one line.
inline constexpr std::chrono::minutes midnight_window{5};

/// How often the scheduler looks at the clock.
///
/// Polling the wall clock is the whole fix: the Java version computed a delay
/// from the wall clock and then waited on a monotonic timer, so whenever the
/// machine slept the message arrived at whatever time it happened to wake up
/// (plan §10).
inline constexpr std::chrono::seconds midnight_tick{30};

/// A message to post at midnight, in one timezone, in one channel.
struct midnight_entry {
    std::int64_t id = 0;
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;

    /// An IANA name, e.g. "America/Chicago".
    std::string timezone;

    std::string message;
    bool enabled = true;

    /// How it is posted: silent, and whether with link previews. Only
    /// `discord::channel_message_flags` are kept. Silent by default, as every
    /// midnight message was before this could be chosen.
    discord::message_flags message_flags = dpp::m_suppress_notifications;

    /// The local date this last posted, as YYYY-MM-DD, or empty for never.
    ///
    /// Saved with the post rather than counted from it, which is what makes a
    /// restart at 00:00:30 not post a second time.
    std::string last_fired_date;
};

// --------------------------------------------------------------------------
// Decisions
// --------------------------------------------------------------------------

/// What the clock says where an entry lives.
struct local_reading {
    /// YYYY-MM-DD.
    std::string date;
    std::chrono::seconds since_midnight{};
};

/// Reads `now` in `timezone`, or nothing when the zone is not a real one.
[[nodiscard]] auto read_local(std::string_view timezone, std::chrono::system_clock::time_point now) -> std::optional<local_reading>;

/// What a tick should do about one entry.
enum class midnight_verdict : std::uint8_t {
    /// Nothing: still the day it last posted for, not yet past midnight, or
    /// switched off.
    wait,
    /// Post it.
    post,
    /// A new local day, but midnight was long enough ago that the bot cannot
    /// have been running for it. Skipped, not posted late.
    missed,
};

/// What to do about an entry now.
///
/// Pure, so every daylight-saving case can be tested without waiting for one.
[[nodiscard]] auto verdict_for(const midnight_entry& entry, std::chrono::system_clock::time_point now) -> midnight_verdict;

/// The date a new entry should start out having "already posted" for.
///
/// Today, where it lives. Without this a message added at three in the
/// afternoon posts within thirty seconds, because the rule that fires it only
/// asks whether today's date differs from the last one posted for. Adding one
/// means "from the next midnight", which is what anybody typing it expects.
///
/// Empty when the zone is unknown, which the command refuses before it gets
/// this far.
[[nodiscard]] auto already_posted_today(std::string_view timezone, std::chrono::system_clock::time_point now) -> std::string;

/// Whether `name` is a timezone this machine knows.
[[nodiscard]] auto is_known_timezone(std::string_view name) -> bool;

/// Timezone names matching what somebody has typed, for autocomplete.
///
/// Matching is case-insensitive and anywhere in the name, so "chicago" finds
/// America/Chicago. Returns at most `limit`, which is Discord's ceiling on
/// autocomplete choices.
[[nodiscard]] auto matching_timezones(std::string_view typed, std::size_t limit) -> std::vector<std::string>;

// --------------------------------------------------------------------------
// Storage
// --------------------------------------------------------------------------

/// Midnight messages, in SQLite.
class midnight_store {
public:
    explicit midnight_store(db::database& db) : db_(&db) {}

    [[nodiscard]] auto for_guild(dpp::snowflake guild_id) const -> std::vector<midnight_entry>;

    /// Every enabled entry across every guild, which is what a tick looks at.
    [[nodiscard]] auto enabled() const -> std::vector<midnight_entry>;

    [[nodiscard]] auto find(std::int64_t id, dpp::snowflake guild_id) const -> std::optional<midnight_entry>;

    auto add(const midnight_entry& entry) -> std::int64_t;

    /// False when the entry does not exist, or belongs to another guild.
    auto update(const midnight_entry& entry) -> bool;
    auto remove(std::int64_t id, dpp::snowflake guild_id) -> bool;

    /// Writes down that an entry has posted for `date`.
    ///
    /// False when it had already posted for that date, which is what makes
    /// firing safe to attempt more than once.
    auto mark_fired(std::int64_t id, std::string_view date) -> bool;

private:
    db::database* db_;
};

// --------------------------------------------------------------------------
// The scheduler
// --------------------------------------------------------------------------

/// Posts each entry once per local day.
class midnight_scheduler {
public:
    midnight_scheduler(midnight_store& store, ports::clock& clock) : store_(&store), clock_(&clock) {}

    /// Everything to post now. Each entry is marked as fired before it is
    /// returned, so a crash between deciding and posting costs one message
    /// rather than repeating it every thirty seconds.
    [[nodiscard]] auto tick() -> std::vector<action>;

private:
    /// Says once that a day went by without the bot being there for it.
    auto note_missed(const midnight_entry& entry, std::chrono::system_clock::time_point now) -> void;

    midnight_store* store_;
    ports::clock* clock_;

    /// The local date each entry was last reported as having missed, so a day
    /// the bot slept through is mentioned once rather than every thirty
    /// seconds. A missed day is deliberately not written to the database:
    /// `last_fired_date` means "posted", and it would be a lie there.
    std::map<std::int64_t, std::string> reported_misses_;
};

} // namespace latibot::events
