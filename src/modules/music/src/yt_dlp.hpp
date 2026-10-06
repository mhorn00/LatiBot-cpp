#pragma once

#include "core/music/cookies.hpp"
#include "core/ports/media.hpp"
#include "core/util/process.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace latibot::music {

// yt-dlp and ffmpeg (docs/features/Music.md §4.4, §4.5): reading a link, and
// fetching a track into ffmpeg to become PCM. The argument lists and the
// reading of yt-dlp's JSON are plain functions, tested on their own.

/// What a run of yt-dlp is told beyond its link (docs/features/Music.md
/// §4.9, §5). Each part is left out when empty.
struct ytdlp_extras {
    /// Deno, which yt-dlp solves YouTube's JavaScript challenges with;
    /// without it, yt-dlp looks for Deno on `PATH` itself.
    std::optional<std::filesystem::path> deno;
    /// The address of bgutil's PO token provider, for its yt-dlp plugin.
    std::string pot_provider;
    /// The arguments that sign this run in (`sign_in_run`).
    std::vector<std::string> sign_in;
};

/// yt-dlp's arguments to read what `url` is: one track's details, or a
/// playlist's entries without fetching each, at most `max_items` of them.
///
/// `--ignore-config` keeps a `yt-dlp.conf` on the host out of it, and `--`
/// keeps a link that looks like an option from being read as one.
[[nodiscard]] auto lookup_arguments(const std::string& url, std::size_t max_items, const ytdlp_extras& extras = {})
    -> std::vector<std::string>;

/// yt-dlp's arguments to write a track's audio to stdout, for ffmpeg.
[[nodiscard]] auto fetch_arguments(const std::string& url, const std::optional<std::filesystem::path>& ffmpeg,
                                   const ytdlp_extras& extras = {}) -> std::vector<std::string>;

/// ffmpeg's arguments to decode stdin to 48 kHz stereo 16-bit samples on
/// stdout, evening out loudness when `even_loudness` is set.
[[nodiscard]] auto decode_arguments(bool even_loudness) -> std::vector<std::string>;

/// Reads `--dump-single-json` output. Entries with no usable link are left
/// out; upcoming streams, which have nothing to play yet, too.
[[nodiscard]] auto parse_lookup(std::string_view json_text, std::size_t max_items) -> ports::result<ports::media_lookup>;

/// What yt-dlp's stderr says went wrong, fit to show: its last `ERROR:`
/// line without the prefix, or its last line, or a stock sentence.
[[nodiscard]] auto describe_failure(std::string_view errors) -> std::string;

// --------------------------------------------------------------------------
// Reading links
// --------------------------------------------------------------------------

/// `media_resolver` through yt-dlp, on worker threads of its own: a read
/// can take many seconds, and must not hold up DPP's threads.
///
/// Before yt-dlp is run, the link's host is resolved, and a link into a
/// private or loopback network is refused (docs/features/Music.md §6).
/// With `cookies`, a link yt-dlp says needs signing in is read again,
/// signed in with a copy of its own (§4.9), and only that try's failure is
/// told.
class ytdlp_resolver final : public ports::media_resolver {
public:
    explicit ytdlp_resolver(std::filesystem::path ytdlp, std::chrono::milliseconds timeout = std::chrono::seconds{30}, int workers = 2,
                            std::optional<cookie_source> cookies = std::nullopt, ytdlp_extras extras = {});

    /// Fails anything still waiting, then waits for the workers.
    ~ytdlp_resolver() override;

    ytdlp_resolver(const ytdlp_resolver&) = delete;
    auto operator=(const ytdlp_resolver&) -> ytdlp_resolver& = delete;
    ytdlp_resolver(ytdlp_resolver&&) = delete;
    auto operator=(ytdlp_resolver&&) -> ytdlp_resolver& = delete;

    auto lookup(std::string url, std::size_t max_items) -> dpp::task<ports::result<ports::media_lookup>> override;

    /// The same, done on the calling thread. What the workers run.
    [[nodiscard]] auto lookup_now(const std::string& url, std::size_t max_items) const -> ports::result<ports::media_lookup>;

private:
    struct job;
    auto work(const std::stop_token& stopping) -> void;
    [[nodiscard]] auto run_lookup(const std::string& url, std::size_t max_items, bool signed_in) const -> util::run_result;

    std::filesystem::path ytdlp_;
    std::chrono::milliseconds timeout_;
    std::optional<cookie_source> cookies_;
    ytdlp_extras extras_;

    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<std::unique_ptr<job>> queue_;

    /// Last, so the workers stop before anything they use goes.
    std::vector<std::jthread> workers_;
};

// --------------------------------------------------------------------------
// Fetching and decoding
// --------------------------------------------------------------------------

/// A `pcm_stream` read from a pipeline of programs whose last one writes
/// raw 16-bit samples: `yt-dlp | ffmpeg` in the bot, a stand-in in tests.
///
/// A thread reads the pipeline into a buffer of at most `buffer_samples`,
/// waiting when it is full, so a program never gets further ahead than
/// that. A stream that produces nothing for `stall_limit` has failed.
///
/// `cookies`, a copy the first program signs in with, is kept until every
/// program has ended, and then removed.
class process_stream final : public ports::pcm_stream {
public:
    /// How much is read ahead: ten seconds of 48 kHz stereo.
    static constexpr std::size_t default_buffer = std::size_t{48000} * 2 * 10;

    process_stream(std::vector<util::program> programs, std::chrono::milliseconds stall_limit = std::chrono::seconds{30},
                   std::size_t buffer_samples = default_buffer, std::optional<cookie_copy> cookies = std::nullopt);

    /// Kills the programs, and waits for the reader.
    ~process_stream() override;

    process_stream(const process_stream&) = delete;
    auto operator=(const process_stream&) -> process_stream& = delete;
    process_stream(process_stream&&) = delete;
    auto operator=(process_stream&&) -> process_stream& = delete;

    auto read(std::span<std::int16_t> into) -> std::size_t override;
    [[nodiscard]] auto state() const -> ports::stream_state override;
    [[nodiscard]] auto error() const -> std::string override;

private:
    auto pump(const std::stop_token& stopping) -> void;
    auto fail(std::string reason) -> void;

    std::vector<util::program> programs_;
    std::chrono::milliseconds stall_limit_;
    std::size_t buffer_limit_;

    mutable std::mutex mutex_;
    std::condition_variable_any space_;
    std::deque<std::int16_t> samples_;
    bool ended_ = false;
    std::string error_;
    std::chrono::steady_clock::time_point last_data_;

    /// The last line each program wrote to stderr that looks like an error,
    /// for the reason a failure is given.
    std::vector<std::string> errors_;

    /// Before the pipeline, so it is removed after the programs have ended:
    /// yt-dlp writes it back as it ends.
    std::optional<cookie_copy> cookies_;

    std::unique_ptr<util::pipeline> pipeline_;

    /// Last, so it stops before anything it uses goes.
    std::jthread reader_;
};

/// A track fetched signed out, and fetched again signed in when it fails,
/// before any audio arrived, with yt-dlp saying it needs signing in (§4.9).
/// Only the second try's failure is told.
class sign_in_retry final : public ports::pcm_stream {
public:
    using opener = std::function<std::unique_ptr<ports::pcm_stream>()>;

    sign_in_retry(std::unique_ptr<ports::pcm_stream> first, opener signed_in);

    auto read(std::span<std::int16_t> into) -> std::size_t override;
    [[nodiscard]] auto state() const -> ports::stream_state override;
    [[nodiscard]] auto error() const -> std::string override;

private:
    /// Starts the signed-in try when it is due. With `mutex_` held.
    auto settle() const -> void;

    // Mutable: asking how the stream is doing is what notices the failure,
    // and `state()` is const.
    mutable std::mutex mutex_;
    mutable std::unique_ptr<ports::pcm_stream> current_;
    /// Emptied once used: there is one retry.
    mutable opener signed_in_;
    bool delivered_ = false;
};

/// Opens tracks as `yt-dlp | ffmpeg`. With `cookies`, signed out first and
/// signed in when that fails asking to (`sign_in_retry`), or straight away
/// for a link that needed it before.
class ytdlp_opener final : public ports::stream_opener {
public:
    ytdlp_opener(std::filesystem::path ytdlp, std::filesystem::path ffmpeg, bool even_loudness = true,
                 std::optional<cookie_source> cookies = std::nullopt, ytdlp_extras extras = {});

    auto open(const std::string& url) -> std::unique_ptr<ports::pcm_stream> override;

private:
    std::filesystem::path ytdlp_;
    std::filesystem::path ffmpeg_;
    bool even_loudness_;
    std::optional<cookie_source> cookies_;
    ytdlp_extras extras_;
};

} // namespace latibot::music
