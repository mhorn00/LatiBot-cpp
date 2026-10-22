#pragma once

#include "core/discord/voice_message.hpp"
#include "core/ports/result.hpp"
#include "sounds.hpp"

#include <dpp/coro/task.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace latibot::ports {
class http_client;
}

namespace latibot::soundboard {

// Turning a sound into a voice message (src/modules/soundboard/docs/Soundboard.md §3):
// its file from Discord's CDN, made Ogg Opus by ffmpeg, as Discord's own apps
// record voice messages, with the duration and the waveform read from its
// samples. The argument lists are plain functions, tested on their own.

/// The name the voice message's file has, which nobody sees.
inline constexpr std::string_view clip_filename = "voice-message.ogg";

/// The largest file a soundboard sound can be is 512 KiB; anything much
/// bigger is not one.
inline constexpr std::size_t max_sound_bytes = std::size_t{1} << 20U;

/// What every voice message is made at: one channel, 48 kHz, as Discord's
/// apps record them.
inline constexpr int clip_sample_rate = 48000;

/// ffmpeg's arguments to make `input` one channel of Ogg Opus on stdout, at
/// `volume`, the soundboard's own, when it is below 1.
[[nodiscard]] auto encode_arguments(const std::filesystem::path& input, double volume) -> std::vector<std::string>;

/// ffmpeg's arguments to decode `input` the same way to 16-bit samples on
/// stdout, which the duration and the waveform are read from.
[[nodiscard]] auto decode_arguments(const std::filesystem::path& input, double volume) -> std::vector<std::string>;

/// The voice message made from `ogg`, the encoded file, and `pcm`, the same
/// sound as 16-bit samples at `clip_sample_rate`, little-endian.
[[nodiscard]] auto make_clip(std::string ogg, std::string_view pcm) -> discord::voice_message;

/// What ffmpeg's stderr says went wrong, fit to show: its last line, cut
/// short, or a stock sentence.
[[nodiscard]] auto describe_failure(std::string_view errors) -> std::string;

/// Makes a sound into a voice message: ffmpeg in the bot, a mock in tests.
class clip_maker {
public:
    virtual ~clip_maker() = default;

    clip_maker() = default;
    clip_maker(const clip_maker&) = delete;
    auto operator=(const clip_maker&) -> clip_maker& = delete;

    /// An error's message is fit to show whoever asked.
    virtual auto make(sound wanted) -> dpp::task<ports::result<discord::voice_message>> = 0;
};

/// `clip_maker` through ffmpeg, on a worker thread of its own so that DPP's
/// threads never wait on it. The sound is fetched over `http`, written to a
/// file of its own in the system's temporary folder for ffmpeg to read, and
/// removed once ffmpeg is done.
///
/// What it makes is kept, at most `cache_size` sounds, since a sound's file
/// never changes: a sound played again answers at once.
class ffmpeg_clips final : public clip_maker {
public:
    /// `ffmpeg` is nothing when it could not be found, and every sound is
    /// then refused, saying so.
    ffmpeg_clips(ports::http_client& http, std::optional<std::filesystem::path> ffmpeg,
                 std::chrono::milliseconds timeout = std::chrono::seconds{10}, std::size_t cache_size = 128);

    /// Fails anything still waiting, then waits for the worker.
    ~ffmpeg_clips() override;

    ffmpeg_clips(const ffmpeg_clips&) = delete;
    auto operator=(const ffmpeg_clips&) -> ffmpeg_clips& = delete;
    ffmpeg_clips(ffmpeg_clips&&) = delete;
    auto operator=(ffmpeg_clips&&) -> ffmpeg_clips& = delete;

    auto make(sound wanted) -> dpp::task<ports::result<discord::voice_message>> override;

    /// The same from a file already fetched, done on the calling thread.
    /// What the worker runs.
    [[nodiscard]] auto make_now(const std::filesystem::path& input, double volume) const -> ports::result<discord::voice_message>;

private:
    struct job;
    /// A sound at a volume, since the volume is part of what was made.
    using cache_key = std::pair<dpp::snowflake, double>;

    auto work(const std::stop_token& stopping) -> void;
    [[nodiscard]] auto cached(const cache_key& key) -> std::optional<discord::voice_message>;
    auto remember(const cache_key& key, const discord::voice_message& made) -> void;

    ports::http_client* http_;
    std::optional<std::filesystem::path> ffmpeg_;
    std::chrono::milliseconds timeout_;
    std::size_t cache_size_;

    std::mutex cache_mutex_;
    std::map<cache_key, discord::voice_message> cache_;
    /// The cache's keys, oldest first, for letting the oldest go.
    std::deque<cache_key> cache_order_;

    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<std::unique_ptr<job>> queue_;

    /// Last, so the worker stops before anything it uses goes.
    std::jthread worker_;
};

} // namespace latibot::soundboard
