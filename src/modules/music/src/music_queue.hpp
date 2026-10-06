#pragma once

#include "media.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <vector>

namespace latibot::music {

// A server's queue, as plain data with the rules as functions
// (docs/features/Music.md §3.1, §3.2). Each of the Java bot's queue bugs
// (§2) has a test against these.

/// The most tracks a server's queue holds, the one playing included.
inline constexpr std::size_t max_queue = 500;

/// The most tracks one playlist adds.
inline constexpr std::size_t max_playlist = 100;

/// One track in a queue.
struct track {
    /// Unique among everything ever queued, so a queue entry is told apart
    /// from another of the same song.
    std::uint64_t id = 0;

    ports::media_item media;
    dpp::snowflake requested_by;

    /// Where it was queued: a note about it failing goes there.
    dpp::snowflake channel;
};

enum class repeat_mode : std::uint8_t {
    off,
    /// The current track, again and again.
    track,
    /// The whole queue: a finished track goes to the back.
    queue,
};

[[nodiscard]] auto to_string(repeat_mode mode) noexcept -> std::string_view;
[[nodiscard]] auto repeat_mode_from_string(std::string_view name) -> std::optional<repeat_mode>;

/// off, then track, then queue, then off.
[[nodiscard]] auto next_repeat_mode(repeat_mode mode) noexcept -> repeat_mode;

/// Where `/music play` puts what it adds.
enum class queue_position : std::uint8_t {
    /// At the back.
    end,
    /// Straight after the current track, in its own order.
    next,
    /// Straight away: the current track goes after it, to start over.
    now,
};

[[nodiscard]] auto queue_position_from_string(std::string_view name) -> std::optional<queue_position>;

/// A server's queue.
// Moving one is only as noexcept as moving a std::deque, which allocates on
// MSVC; nothing relies on it not throwing.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct guild_queue {
    std::optional<track> current;
    std::deque<track> upcoming;
    repeat_mode repeat = repeat_mode::off;
};

struct add_result {
    std::size_t added = 0;

    /// Left out because the queue was full.
    std::size_t over_limit = 0;

    /// The current track was put back into the queue for these to play now:
    /// the caller has to start the next one.
    bool replaces_current = false;
};

/// Adds tracks where asked, as many as fit under `limit`. For `now`, the
/// current track goes back into the queue straight after the new ones, to
/// play again from its start, and `current` is left empty.
auto add(guild_queue& queue, std::vector<track> tracks, queue_position where, std::size_t limit = max_queue) -> add_result;

/// Why the current track ended.
enum class track_end : std::uint8_t {
    /// It played to the end: repeat applies.
    finished,
    /// Somebody skipped it. Repeating one track does not stop a skip;
    /// repeating the queue keeps the track in it.
    skipped,
    /// It could not be played. Never repeated, whatever repeat says.
    failed,
};

/// Moves on from the current track, and returns what plays now, or nullptr
/// when nothing does.
auto advance(guild_queue& queue, track_end reason) -> const track*;

/// What `advance` would play next, without moving: for fetching it ahead.
[[nodiscard]] auto peek_next(const guild_queue& queue, track_end reason) -> const track*;

/// Makes the front of the queue current, with no repeat: after `add` with
/// `now`, or when nothing was playing.
auto start_next(guild_queue& queue) -> const track*;

/// Shuffles what is queued, never the current track. `seed` makes it
/// repeatable in tests. Returns how many were shuffled.
auto shuffle(guild_queue& queue, std::uint64_t seed) -> std::size_t;

/// Takes the track at `position` out of the queue, counting from 1 as
/// `/music queue` shows them. Nothing when there is none there.
auto remove(guild_queue& queue, std::size_t position) -> std::optional<track>;

/// Empties the queue, leaving the current track playing. Returns how many
/// were removed.
auto clear(guild_queue& queue) -> std::size_t;

/// How long a list of tracks runs.
struct running_time {
    std::chrono::seconds known{0};

    /// Tracks whose length is not known, live streams included.
    std::size_t unknown = 0;
};

[[nodiscard]] auto total_time(const std::deque<track>& tracks) -> running_time;

} // namespace latibot::music
