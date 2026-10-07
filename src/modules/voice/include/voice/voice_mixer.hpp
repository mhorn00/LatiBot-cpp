#pragma once

#include "voice/voice_output.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::audio {

/// Where the mixer reads music from: the music player.
class music_source {
public:
    virtual ~music_source() = default;
    music_source() = default;
    music_source(const music_source&) = delete;
    auto operator=(const music_source&) -> music_source& = delete;

    struct chunk {
        /// Samples written to the front of `into`.
        std::size_t samples = 0;

        /// Set when those samples end a track: the marker to queue after
        /// them, reported back to `on_marker` when it has been heard.
        std::string end_marker;

        /// Nothing to play at all, now or soon: the mixer stops visiting
        /// the guild until it is woken again.
        bool idle = false;
    };

    /// The next music for `guild`, at most `into.size()` samples. Called
    /// with the mixer's lock held, so it must not call back into the mixer.
    virtual auto read(dpp::snowflake guild, std::span<std::int16_t> into) -> chunk = 0;

    /// A marker from `read` has been played past. Called without the
    /// mixer's lock, so it may call back into the mixer.
    virtual auto on_marker(dpp::snowflake guild, std::string_view marker) -> void = 0;
};

/// The only thing that writes to a guild's voice connection: speech from the
/// speech queue, and music from a `music_source` (src/modules/music/docs/Music.md
/// §4.2).
///
/// DPP keeps one queue of audio per connection, so pausing it or clearing it
/// would pause or clear speech and music alike. So music is never queued
/// far ahead: the mixer keeps `lookahead` of it queued, topped up by `tick`,
/// and remembers what it sent. When speech arrives, the music not yet heard
/// is taken back, the connection is cleared, the speech plays, and the music
/// resumes from exactly where it was. The connection only ever holds one of
/// the two.
///
/// Towards the speech queue it is a `voice_output`, so speech needs no
/// change. Thread-safe.
class voice_mixer final : public ports::voice_output {
public:
    /// Markers music writes start with this; speech's do not.
    static constexpr std::string_view music_marker_prefix = "music:";

    /// Enough to ride out a timer that ticks once a second, and no more:
    /// the rewind means it costs speech nothing.
    static constexpr std::chrono::milliseconds default_lookahead{3000};

    /// Samples in one 20 ms packet of 48 kHz stereo, the unit DPP queues in.
    static constexpr std::size_t packet_samples = 1920;

    explicit voice_mixer(ports::voice_output& connection, std::chrono::milliseconds lookahead = default_lookahead);

    /// The music player, which is built after the mixer.
    auto set_music(music_source* source) -> void;

    // Speech, as a voice_output --------------------------------------------

    [[nodiscard]] auto ready(dpp::snowflake guild) -> bool override;

    /// Takes back whatever music is queued, then queues the speech.
    auto play(dpp::snowflake guild, std::span<const std::int16_t> audio, const std::string& marker) -> bool override;

    /// Drops the utterance playing now. Music resumes when speech is over.
    auto skip(dpp::snowflake guild) -> void override;

    /// Drops all speech; music resumes. Leaves music alone when no speech is
    /// queued, since `/tts stop` with nothing to stop must not stop music.
    auto stop(dpp::snowflake guild) -> void override;

    [[nodiscard]] auto remaining(dpp::snowflake guild) -> std::chrono::milliseconds override;

    // Music -------------------------------------------------------------------

    /// There is music to play: start feeding it.
    auto wake(dpp::snowflake guild) -> void;

    /// Forgets all music, queued and taken back: a skip or a stop. The
    /// connection is cleared only when it holds music, never speech.
    auto drop_music(dpp::snowflake guild) -> void;

    /// Pauses music at once, keeping what was not heard yet.
    auto hold_music(dpp::snowflake guild) -> void;

    /// Resumes music from where it was held.
    auto release_music(dpp::snowflake guild) -> void;

    /// Music read from the source and not heard yet, in samples: queued in
    /// the connection, taken back, or waiting to fill a packet.
    [[nodiscard]] auto music_unplayed(dpp::snowflake guild) -> std::size_t;

    // Events -------------------------------------------------------------------

    /// Tops up every guild that has music playing. On a timer.
    auto tick() -> void;

    /// Playback passed `marker`. Music markers go to the music source;
    /// speech markers end speech, and when the last one passes, music
    /// resumes. The speech queue is told about its own separately.
    auto on_marker(dpp::snowflake guild, std::string_view marker) -> void;

    /// A connection became ready: a new one after a join or a move, so
    /// nothing queued on an old one will play or report its markers.
    auto on_ready(dpp::snowflake guild) -> void;

    /// The bot left the guild's voice channel.
    auto forget(dpp::snowflake guild) -> void;

private:
    /// Music sent, or taken back to send again: audio, or a marker.
    struct entry {
        std::vector<std::int16_t> samples;
        std::string marker;
    };

    struct guild_mix {
        /// Speech markers queued in the connection, oldest first. While
        /// there are any, the connection holds speech and no music.
        std::deque<std::string> speech;

        /// Music sent since the connection last held no music, oldest
        /// first, trimmed to what could still be unplayed.
        std::deque<entry> history;
        std::size_t history_samples = 0;

        /// Music taken back, to send before reading anything new.
        std::deque<entry> replay;

        /// Samples read that do not yet fill a packet.
        std::vector<std::int16_t> carry;

        /// Music markers sent and not yet reported.
        std::set<std::string, std::less<>> pending_markers;

        bool held = false;
        bool active = false;
    };

    auto feed(dpp::snowflake guild, guild_mix& mix) -> void;

    /// Sends what is staged: whole packets, or all of it before a marker.
    auto flush(dpp::snowflake guild, guild_mix& mix, std::vector<std::int16_t>& staged, const std::string& marker) -> void;

    /// Stages music taken back, up to `wanted` samples; returns how many.
    auto stage_replay(dpp::snowflake guild, guild_mix& mix, std::vector<std::int16_t>& staged, std::size_t wanted) -> std::size_t;

    /// Stages what did not fill a packet last time, then reads the source,
    /// up to `wanted` samples in all.
    auto stage_source(dpp::snowflake guild, guild_mix& mix, std::vector<std::int16_t>& staged, std::size_t wanted) -> void;
    auto rewind(dpp::snowflake guild, guild_mix& mix) -> void;
    auto send(dpp::snowflake guild, guild_mix& mix, std::vector<std::int16_t> samples, const std::string& marker) -> bool;

    ports::voice_output* connection_;
    std::size_t lookahead_samples_;
    music_source* music_ = nullptr;

    std::mutex mutex_;
    std::map<dpp::snowflake, guild_mix> guilds_;
};

} // namespace latibot::audio
