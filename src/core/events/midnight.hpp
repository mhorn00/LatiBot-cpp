#pragma once

#include "core/events/message_pipeline.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
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
/// yesterday (plan v4 §10).
inline constexpr std::chrono::seconds midnight_grace{5};

/// How often the scheduler looks at the clock.
///
/// Polling the wall clock is the whole fix: the Java version computed a delay
/// from the wall clock and then waited on a monotonic timer, so whenever the
/// machine slept the message arrived at whatever time it happened to wake up
/// (plan v4 §10).
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
[[nodiscard]] std::optional<local_reading> read_local(std::string_view timezone, std::chrono::system_clock::time_point now);

/// Whether an entry should post now.
///
/// Pure, so every daylight-saving case can be tested without waiting for one.
[[nodiscard]] bool due(const midnight_entry& entry, std::chrono::system_clock::time_point now);

/// The date a new entry should start out having "already posted" for.
///
/// Today, where it lives. Without this a message added at three in the
/// afternoon posts within thirty seconds, because the rule that fires it only
/// asks whether today's date differs from the last one posted for. Adding one
/// means "from the next midnight", which is what anybody typing it expects.
///
/// Empty when the zone is unknown, which the command refuses before it gets
/// this far.
[[nodiscard]] std::string already_posted_today(std::string_view timezone, std::chrono::system_clock::time_point now);

/// Whether `name` is a timezone this machine knows.
[[nodiscard]] bool is_known_timezone(std::string_view name);

/// Timezone names matching what somebody has typed, for autocomplete.
///
/// Matching is case-insensitive and anywhere in the name, so "chicago" finds
/// America/Chicago. Returns at most `limit`, which is Discord's ceiling on
/// autocomplete choices.
[[nodiscard]] std::vector<std::string> matching_timezones(std::string_view typed, std::size_t limit);

// --------------------------------------------------------------------------
// Storage
// --------------------------------------------------------------------------

/// Midnight messages, in SQLite.
class midnight_store {
public:
    explicit midnight_store(db::database& db) : db_(&db) {}

    [[nodiscard]] std::vector<midnight_entry> for_guild(dpp::snowflake guild_id) const;

    /// Every enabled entry across every guild, which is what a tick looks at.
    [[nodiscard]] std::vector<midnight_entry> enabled() const;

    [[nodiscard]] std::optional<midnight_entry> find(std::int64_t id, dpp::snowflake guild_id) const;

    std::int64_t add(const midnight_entry& entry);

    /// False when the entry does not exist, or belongs to another guild.
    bool update(const midnight_entry& entry);
    bool remove(std::int64_t id, dpp::snowflake guild_id);

    /// Writes down that an entry has posted for `date`.
    ///
    /// False when it had already posted for that date, which is what makes
    /// firing safe to attempt more than once.
    bool mark_fired(std::int64_t id, std::string_view date);

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
    [[nodiscard]] std::vector<action> tick();

private:
    midnight_store* store_;
    ports::clock* clock_;
};

} // namespace latibot::events
