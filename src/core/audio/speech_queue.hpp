#pragma once

#include "core/ports/voice_output.hpp"

#include <dpp/snowflake.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::audio {

/// What happened to an utterance handed to the queue.
enum class speech_outcome : std::uint8_t {
    /// Queued on the voice connection.
    playing,
    /// Held until the connection being set up is ready.
    waiting,
    /// Dropped: `stop` was called while it was being synthesized.
    stopped,
};

/// What each guild is saying, in order (plan §13).
///
/// Utterances are queued on the voice connection whole, each followed by a
/// marker naming it, so playback reports each one finishing, and `skip` can
/// drop exactly one. It is the only thing that plays audio for now. The
/// plan's mixer, which pauses music for speech, arrives with music: it
/// would be an abstraction with one user until then (plan §21.5).
///
/// Thread-safe: commands, DPP's voice events and timers all reach it.
class speech_queue {
public:
    explicit speech_queue(ports::voice_output& output);

    /// Taken before synthesizing, and handed back to `enqueue`, so that a
    /// `stop` arriving while the audio is still being made stops it too.
    [[nodiscard]] auto ticket(dpp::snowflake guild) -> std::uint64_t;

    /// Hands on 48 kHz stereo audio for `owner`.
    auto enqueue(dpp::snowflake guild, dpp::snowflake owner, std::uint64_t ticket, std::vector<std::int16_t> audio) -> speech_outcome;

    /// The guild's connection is ready: plays what was waiting for it.
    auto on_ready(dpp::snowflake guild) -> void;

    /// Playback passed `marker`, so the utterance it names has finished.
    /// Markers this queue did not write are ignored.
    auto on_marker(dpp::snowflake guild, std::string_view marker) -> void;

    /// Drops the utterance playing now. False when nothing was.
    auto skip(dpp::snowflake guild) -> bool;

    /// Drops everything: playing, waiting, and being synthesized. Returns how
    /// many utterances were playing or waiting.
    auto stop(dpp::snowflake guild) -> std::size_t;

    /// The connection is gone: forgets what the guild had, without asking
    /// the connection to stop.
    auto forget(dpp::snowflake guild) -> void;

    /// Who asked for the utterance playing now, if one is.
    [[nodiscard]] auto current_owner(dpp::snowflake guild) const -> std::optional<dpp::snowflake>;

    /// Utterances playing or waiting.
    [[nodiscard]] auto size(dpp::snowflake guild) const -> std::size_t;

    /// The marker written after utterance `id`.
    [[nodiscard]] static auto marker_for(std::uint64_t id) -> std::string;

private:
    struct utterance {
        std::uint64_t id = 0;
        dpp::snowflake owner;
    };

    struct waiting_utterance {
        std::uint64_t id = 0;
        dpp::snowflake owner;
        std::vector<std::int16_t> audio;
    };

    struct guild_speech {
        std::uint64_t generation = 0;
        std::deque<utterance> playing;
        std::deque<waiting_utterance> waiting;
    };

    ports::voice_output* output_;

    mutable std::mutex mutex_;
    std::map<dpp::snowflake, guild_speech> guilds_;
    std::uint64_t next_id_ = 1;
};

} // namespace latibot::audio
