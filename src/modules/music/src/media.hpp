#pragma once

#include "core/ports/result.hpp"

#include <dpp/coro/task.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace latibot::ports {

// Where music comes from (src/modules/music/docs/Music.md §4.4, §4.5): reading what a
// link is, and turning a track into audio. yt-dlp and ffmpeg behind these in
// the bot, mocks in the tests.

/// One track a link names.
struct media_item {
    std::string title;

    /// The page to fetch it from again when its turn comes.
    std::string url;
    std::string uploader;

    /// Empty when the site does not say, and for live streams.
    std::optional<std::chrono::seconds> duration;
    bool live = false;
};

/// What a link turned out to be.
struct media_lookup {
    /// In order. For a playlist, the first `max_items` of it.
    std::vector<media_item> items;

    /// The playlist's title, or empty for a single track.
    std::string playlist_title;

    /// How many tracks the playlist has in all, when the site says; at
    /// least `items.size()`.
    std::size_t playlist_size = 0;

    [[nodiscard]] auto is_playlist() const noexcept -> bool { return !playlist_title.empty() || items.size() > 1; }
};

/// Reads what a link is, without fetching its audio.
class media_resolver {
public:
    virtual ~media_resolver() = default;
    media_resolver() = default;
    media_resolver(const media_resolver&) = delete;
    auto operator=(const media_resolver&) -> media_resolver& = delete;

    /// At most `max_items` of a playlist. An error's message is fit to show
    /// whoever asked.
    virtual auto lookup(std::string url, std::size_t max_items) -> dpp::task<result<media_lookup>> = 0;
};

/// How a stream of audio is doing.
enum class stream_state : std::uint8_t {
    /// Producing audio, or about to.
    running,
    /// It ended, and everything it produced has been read.
    finished,
    /// It broke; `error()` says why. What it produced before may have been
    /// read already.
    failed,
};

/// One track's audio as 48 kHz stereo 16-bit samples, arriving while it is
/// fetched and decoded.
class pcm_stream {
public:
    virtual ~pcm_stream() = default;
    pcm_stream() = default;
    pcm_stream(const pcm_stream&) = delete;
    auto operator=(const pcm_stream&) -> pcm_stream& = delete;

    /// Copies out what has arrived, at most `into.size()` samples. Never
    /// waits: 0 means nothing yet, or nothing more.
    virtual auto read(std::span<std::int16_t> into) -> std::size_t = 0;

    [[nodiscard]] virtual auto state() const -> stream_state = 0;

    /// Why it failed, fit to show whoever queued the track.
    [[nodiscard]] virtual auto error() const -> std::string = 0;
};

/// Starts fetching and decoding a track.
class stream_opener {
public:
    virtual ~stream_opener() = default;
    stream_opener() = default;
    stream_opener(const stream_opener&) = delete;
    auto operator=(const stream_opener&) -> stream_opener& = delete;

    /// Never null. A stream that cannot even start comes back already
    /// failed, so there is one way to handle a track that will not play.
    virtual auto open(const std::string& url) -> std::unique_ptr<pcm_stream> = 0;
};

} // namespace latibot::ports
