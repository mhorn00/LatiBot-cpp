#pragma once

#include "core/audio/voice_mixer.hpp"
#include "core/music/music_queue.hpp"
#include "core/ports/media.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::music {

/// What the player needs from the rest of the bot, as functions so the tests
/// can answer them.
struct player_options {
    /// The server's music volume, 0–200 %.
    std::function<int(dpp::snowflake)> volume_percent;

    /// The longest a track may play, or nothing for no limit. Live streams
    /// have none either way.
    std::function<std::optional<std::chrono::seconds>(dpp::snowflake)> track_limit;

    /// Posts a short note in a channel: a track that failed, or was cut off.
    std::function<void(dpp::snowflake channel, std::string text)> notify;
};

/// What `/music nowplaying` and `/music queue` show.
struct music_status {
    std::optional<track> current;

    /// How far into the current track playback is.
    std::chrono::seconds elapsed{0};

    std::vector<track> upcoming;
    repeat_mode repeat = repeat_mode::off;
    bool paused = false;
};

/// Plays each server's queue through the mixer (docs/features/Music.md §4.3).
///
/// The queue's rules are `music_queue`'s; this adds the streams, the
/// markers, and moving on. A track ends when its end marker has been heard,
/// not when it has finished decoding, which is seconds earlier; the next
/// track is fetched from then, so it is ready when the marker comes.
///
/// Thread-safe. It is the mixer's music source, so the mixer calls `read`
/// with its lock held; the player never holds its own lock while calling
/// the mixer.
class music_player final : public audio::music_source {
public:
    music_player(ports::stream_opener& opener, audio::voice_mixer& mixer, player_options options);

    /// Queues tracks, and starts playing if nothing was. They are given ids
    /// here.
    auto add(dpp::snowflake guild, std::vector<track> tracks, queue_position where) -> add_result;

    /// Moves on from the current track. Returns it, or nothing when nothing
    /// was playing.
    auto skip(dpp::snowflake guild) -> std::optional<track>;

    /// Pauses, or resumes. Returns whether it is paused now, or nothing when
    /// nothing is playing.
    auto toggle_pause(dpp::snowflake guild) -> std::optional<bool>;

    /// Stops the music and empties the queue. Returns how many tracks went,
    /// the one playing included.
    auto stop(dpp::snowflake guild) -> std::size_t;

    /// Empties the queue, leaving the current track playing.
    auto clear(dpp::snowflake guild) -> std::size_t;

    auto shuffle(dpp::snowflake guild) -> std::size_t;

    /// Counting from 1, as `/music queue` shows them.
    auto remove(dpp::snowflake guild, std::size_t position) -> std::optional<track>;

    /// Sets how the queue repeats, or with nothing, moves to the next mode.
    auto set_repeat(dpp::snowflake guild, std::optional<repeat_mode> mode) -> repeat_mode;

    /// The server's volume changed: applies from now on.
    auto volume_changed(dpp::snowflake guild) -> void;

    [[nodiscard]] auto status(dpp::snowflake guild) -> music_status;

    /// The bot left the voice channel: the queue goes with it.
    auto forget(dpp::snowflake guild) -> void;

    // The mixer's music source -------------------------------------------

    auto read(dpp::snowflake guild, std::span<std::int16_t> into) -> chunk override;
    auto on_marker(dpp::snowflake guild, std::string_view marker) -> void override;

private:
    struct guild_player {
        guild_queue queue;

        /// The current track's audio.
        std::unique_ptr<ports::pcm_stream> stream;

        /// Which playing of which track this is: the end marker names it, so
        /// a marker from a track since skipped is known to be stale.
        std::uint64_t playback = 0;

        /// Samples of it handed to the mixer.
        std::size_t fed = 0;

        /// Its end marker has been handed over; waiting for it to be heard.
        bool end_sent = false;

        /// It failed: never repeated.
        bool failed = false;

        bool paused = false;

        /// Between a skip's two halves, while the mixer drops what it had.
        bool switching = false;

        int volume = 100;
        std::optional<std::size_t> limit_samples;

        /// The next track, fetched ahead: which track, and its stream.
        std::uint64_t prefetched_track = 0;
        std::unique_ptr<ports::pcm_stream> prefetched;
    };

    /// Starts the queue's current track, or tidies up when there is none.
    auto start_playback(dpp::snowflake guild, guild_player& player) -> void;

    /// Starts fetching what plays next, once the current track has
    /// finished decoding.
    auto prefetch(guild_player& player) -> void;

    auto end_of_track(guild_player& player) -> std::string;

    ports::stream_opener* opener_;
    audio::voice_mixer* mixer_;
    player_options options_;

    std::mutex mutex_;
    std::map<dpp::snowflake, guild_player> guilds_;
    std::uint64_t next_track_id_ = 1;
    std::uint64_t next_playback_ = 1;
    std::uint64_t shuffles_ = 0;
};

/// The marker that ends playback `playback`.
[[nodiscard]] auto music_marker(std::uint64_t playback) -> std::string;

/// The playback a marker ends, or nothing when it is not a music marker.
[[nodiscard]] auto playback_in(std::string_view marker) -> std::optional<std::uint64_t>;

} // namespace latibot::music
