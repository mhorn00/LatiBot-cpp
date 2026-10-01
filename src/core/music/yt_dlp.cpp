#include "core/music/yt_dlp.hpp"

#include "core/music/links.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/coro/awaitable.h>
#include <dpp/json.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <utility>

namespace latibot::music {
namespace {

auto media_error(std::string message) -> ports::api_error {
    return {.http_status = 0, .message = std::move(message)};
}

auto text_at(const nlohmann::json& object, const char* key) -> std::string {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

auto is_http(std::string_view url) -> bool {
    return url.starts_with("https://") || url.starts_with("http://");
}

auto path_of(const std::optional<cookie_copy>& cookies) -> std::optional<std::filesystem::path> {
    if (!cookies) return std::nullopt;
    return cookies->path();
}

/// `--cookies` and the file, second, after `--ignore-config`: the `--` and
/// the link stay last. Signed in, yt-dlp's warnings are kept: they are what
/// say the cookies are no longer valid, and only a failure's are logged.
auto with_cookies(std::vector<std::string> arguments, const std::optional<std::filesystem::path>& cookies) -> std::vector<std::string> {
    if (!cookies) return arguments;
    std::erase(arguments, "--no-warnings");
    arguments.insert(arguments.begin() + 1, {"--cookies", cookies->string()});
    return arguments;
}

/// One track, from a single video's details or a flat playlist's entry.
/// Nothing when it has no link to fetch it by, or cannot be played yet.
auto item_from(const nlohmann::json& entry) -> std::optional<ports::media_item> {
    if (!entry.is_object()) return std::nullopt;

    const std::string live_status = text_at(entry, "live_status");
    if (live_status == "is_upcoming") return std::nullopt;

    // A single video names its page; a flat entry sometimes only its url,
    // and for some sites that is not a link at all.
    std::string url = text_at(entry, "webpage_url");
    if (!is_http(url)) url = text_at(entry, "url");
    if (!is_http(url)) url = text_at(entry, "original_url");
    if (!is_http(url)) return std::nullopt;

    ports::media_item item;
    item.url = url;
    item.title = text_at(entry, "title");
    if (item.title.empty()) item.title = text_at(entry, "fulltitle");
    if (item.title.empty()) item.title = url;

    for (const char* key : {"uploader", "channel", "artist", "creator"}) {
        item.uploader = text_at(entry, key);
        if (!item.uploader.empty()) break;
    }

    const auto live = entry.find("is_live");
    item.live = live_status == "is_live" || (live != entry.end() && live->is_boolean() && live->get<bool>());

    const auto duration = entry.find("duration");
    if (!item.live && duration != entry.end() && duration->is_number()) {
        const double seconds = duration->get<double>();
        if (seconds > 0) item.duration = std::chrono::seconds{static_cast<std::int64_t>(std::llround(seconds))};
    }
    return item;
}

/// A playlist's title and entries, at most `max_items` of them, and how
/// many it has in all.
auto read_playlist(const nlohmann::json& document, std::size_t max_items) -> ports::media_lookup {
    ports::media_lookup lookup;
    lookup.playlist_title = text_at(document, "title");
    if (lookup.playlist_title.empty()) lookup.playlist_title = "a playlist";

    std::size_t listed = 0;
    const auto entries = document.find("entries");
    if (entries != document.end() && entries->is_array()) {
        listed = entries->size();
        for (const nlohmann::json& entry : *entries) {
            if (lookup.items.size() == max_items) break;
            if (auto item = item_from(entry)) lookup.items.push_back(std::move(*item));
        }
    }
    const auto count = document.find("playlist_count");
    const std::size_t said = count != document.end() && count->is_number_unsigned() ? count->get<std::size_t>() : 0;
    lookup.playlist_size = std::max({said, listed, lookup.items.size()});
    return lookup;
}

} // namespace

// --------------------------------------------------------------------------
// Arguments
// --------------------------------------------------------------------------

auto lookup_arguments(const std::string& url, std::size_t max_items, const std::optional<std::filesystem::path>& cookies)
    -> std::vector<std::string> {
    return with_cookies(
        {
            "--ignore-config",
            "--no-warnings",
            // A video link that also names a playlist is the video; a playlist
            // link is still the playlist.
            "--no-playlist",
            "--flat-playlist",
            "--dump-single-json",
            "--playlist-end",
            std::to_string(max_items),
            "--encoding",
            "utf-8",
            "--",
            url,
        },
        cookies);
}

auto fetch_arguments(const std::string& url, const std::optional<std::filesystem::path>& ffmpeg,
                     const std::optional<std::filesystem::path>& cookies) -> std::vector<std::string> {
    std::vector<std::string> arguments{
        "--ignore-config", "--no-warnings", "--no-playlist", "--quiet", "--no-progress", "--no-part", "-f", "bestaudio/best", "-o", "-",
    };
    // Live streams and some sites are fetched through ffmpeg by yt-dlp
    // itself, which has to be told where it is when it is not on PATH.
    if (ffmpeg) {
        arguments.emplace_back("--ffmpeg-location");
        arguments.push_back(ffmpeg->string());
    }
    arguments.emplace_back("--");
    arguments.push_back(url);
    return with_cookies(std::move(arguments), cookies);
}

auto decode_arguments(bool even_loudness) -> std::vector<std::string> {
    std::vector<std::string> arguments{"-hide_banner", "-loglevel", "error", "-i", "pipe:0", "-vn"};
    if (even_loudness) {
        // One pass, as the audio arrives: EBU R128 at -16 LUFS, the loudness
        // most streaming services aim at.
        arguments.emplace_back("-af");
        arguments.emplace_back("loudnorm=I=-16:TP=-1.5:LRA=11");
    }
    for (const char* argument : {"-f", "s16le", "-ar", "48000", "-ac", "2", "pipe:1"}) {
        arguments.emplace_back(argument);
    }
    return arguments;
}

// --------------------------------------------------------------------------
// Reading yt-dlp's answers
// --------------------------------------------------------------------------

auto parse_lookup(std::string_view json_text, std::size_t max_items) -> ports::result<ports::media_lookup> {
    const nlohmann::json document = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (document.is_discarded() || !document.is_object()) return media_error("yt-dlp's answer about that link could not be read");

    ports::media_lookup lookup;
    const auto entries = document.find("entries");
    if (text_at(document, "_type") == "playlist" || (entries != document.end() && entries->is_array())) {
        lookup = read_playlist(document, max_items);
    } else if (auto item = item_from(document)) {
        lookup.items.push_back(std::move(*item));
        lookup.playlist_size = 1;
    }

    if (lookup.items.empty()) return media_error("there's nothing i can play at that link");
    return lookup;
}

auto describe_failure(std::string_view errors) -> std::string {
    constexpr std::size_t longest = 300;
    std::string_view chosen;
    std::string_view last;
    for (const std::string_view line : util::lines(errors)) {
        const std::string_view trimmed = util::trim(line);
        if (trimmed.empty()) continue;
        last = trimmed;
        if (trimmed.starts_with("ERROR:")) chosen = util::trim(trimmed.substr(6));
    }
    if (chosen.empty()) chosen = last;
    if (chosen.empty()) return "yt-dlp couldn't read that link";
    return util::truncate(chosen, longest);
}

// --------------------------------------------------------------------------
// The resolver
// --------------------------------------------------------------------------

struct ytdlp_resolver::job {
    std::string url;
    std::size_t max_items = 0;
    dpp::promise<ports::result<ports::media_lookup>> done;
};

ytdlp_resolver::ytdlp_resolver(std::filesystem::path ytdlp, std::chrono::milliseconds timeout, int workers,
                               std::optional<cookie_source> cookies)
    : ytdlp_(std::move(ytdlp)), timeout_(timeout), cookies_(std::move(cookies)) {
    for (int i = 0; i < std::max(workers, 1); ++i) {
        workers_.emplace_back([this](const std::stop_token& stopping) { work(stopping); });
    }
}

ytdlp_resolver::~ytdlp_resolver() {
    for (std::jthread& worker : workers_) {
        worker.request_stop();
    }
    wake_.notify_all();
    workers_.clear();

    // Anything never started still has somebody waiting on it. A promise
    // that cannot be set leaves nothing more to do on the way out.
    for (const std::unique_ptr<job>& waiting : queue_) {
        try {
            waiting->done.set_value(media_error("the bot is shutting down"));
        } catch (...) { // NOLINT(bugprone-empty-catch)
        }
    }
}

auto ytdlp_resolver::lookup(std::string url, std::size_t max_items) -> dpp::task<ports::result<ports::media_lookup>> {
    auto queued = std::make_unique<job>();
    queued->url = std::move(url);
    queued->max_items = max_items;
    auto finished = queued->done.get_awaitable();
    {
        const std::scoped_lock lock(mutex_);
        queue_.push_back(std::move(queued));
    }
    wake_.notify_one();
    co_return co_await finished;
}

auto ytdlp_resolver::lookup_now(const std::string& url, std::size_t max_items) const -> ports::result<ports::media_lookup> {
    if (resolves_to_private(host_of(url))) return media_error("that link points into a private network, which i won't fetch from");

    try {
        // Signed out first, unless this link needed signing in before.
        bool signed_in = cookies_ && cookies_->needed_for(url);
        util::run_result ran = run_lookup(url, max_items, signed_in);
        if (cookies_ && !signed_in && !ran.timed_out && ran.exit_code != 0 && needs_sign_in(ran.errors)) {
            util::log().debug("yt-dlp must sign in to read {}, so it tries again signed in: {}", url, ran.errors);
            signed_in = true;
            ran = run_lookup(url, max_items, true);
        }
        if (ran.timed_out) {
            return media_error(std::format("reading that link took more than {}; the site may be down",
                                           std::chrono::duration_cast<std::chrono::seconds>(timeout_)));
        }
        if (ran.exit_code != 0) {
            // A warning when signed in: the owner set the cookies up for
            // this, and yt-dlp's warnings above its error say why they did
            // not work.
            if (signed_in) {
                util::log().warn("yt-dlp could not read {} signed in either: {}", url, ran.errors);
            } else {
                util::log().debug("yt-dlp could not read {}: {}", url, ran.errors);
            }
            return media_error(describe_failure(ran.errors));
        }
        auto lookup = parse_lookup(ran.output, max_items);
        // Its tracks are fetched signed in too, rather than refused first.
        if (signed_in && lookup.ok()) {
            cookies_->remember(url);
            for (const ports::media_item& item : lookup.value().items) {
                cookies_->remember(item.url);
            }
        }
        return lookup;
    } catch (const util::process_error& error) {
        util::log().error("could not run yt-dlp: {}", error.what());
        return media_error("i couldn't run yt-dlp; it's in the log");
    }
}

auto ytdlp_resolver::run_lookup(const std::string& url, std::size_t max_items, bool signed_in) const -> util::run_result {
    // Removed as this returns, once yt-dlp has ended: `run` waits for it.
    const std::optional<cookie_copy> cookies = signed_in && cookies_ ? cookies_->copy() : std::nullopt;
    return util::run({.path = ytdlp_, .arguments = lookup_arguments(url, max_items, path_of(cookies))}, timeout_);
}

auto ytdlp_resolver::work(const std::stop_token& stopping) -> void {
    while (true) {
        std::unique_ptr<job> next;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stopping, [this] { return !queue_.empty(); });
            if (stopping.stop_requested() || queue_.empty()) return;
            next = std::move(queue_.front());
            queue_.pop_front();
        }
        // Completed here, whatever happened: a promise left unset would
        // leave its caller suspended for ever.
        next->done.set_value(lookup_now(next->url, next->max_items));
    }
}

// --------------------------------------------------------------------------
// Streams
// --------------------------------------------------------------------------

process_stream::process_stream(std::vector<util::program> programs, std::chrono::milliseconds stall_limit, std::size_t buffer_samples,
                               std::optional<cookie_copy> cookies)
    : programs_(std::move(programs)),
      stall_limit_(stall_limit),
      buffer_limit_(std::max<std::size_t>(buffer_samples, 1)),
      last_data_(std::chrono::steady_clock::now()),
      errors_(programs_.size()),
      cookies_(std::move(cookies)) {
    try {
        pipeline_ = std::make_unique<util::pipeline>(programs_, [this](std::size_t index, std::string_view line) {
            util::log().debug("{}: {}", programs_.at(index).path.filename().string(), line);
            const std::scoped_lock lock(mutex_);
            errors_.at(index) = std::string(line);
        });
    } catch (const util::process_error& error) {
        util::log().error("could not start a track: {}", error.what());
        fail("i couldn't start yt-dlp or ffmpeg; it's in the log");
        return;
    }
    reader_ = std::jthread([this](const std::stop_token& stopping) { pump(stopping); });
}

process_stream::~process_stream() {
    // Killing the programs ends the reader's read; stopping it ends its wait
    // for room. The reader is then joined as it goes, before the pipeline.
    if (pipeline_) pipeline_->kill();
    reader_.request_stop();
    space_.notify_all();
    if (reader_.joinable()) reader_.join();
}

auto process_stream::fail(std::string reason) -> void {
    const std::scoped_lock lock(mutex_);
    if (error_.empty()) error_ = std::move(reason);
    ended_ = true;
}

auto process_stream::pump(const std::stop_token& stopping) -> void {
    std::array<std::byte, 65536> bytes{};
    std::optional<std::byte> odd;

    while (!stopping.stop_requested()) {
        const std::size_t got = pipeline_->read(bytes);
        if (got == 0) break;

        std::vector<std::int16_t> arrived;
        arrived.reserve((got / 2) + 1);
        std::size_t at = 0;
        if (odd) {
            const std::array<std::byte, 2> pair{*odd, bytes[0]};
            arrived.push_back(std::bit_cast<std::int16_t>(pair));
            odd.reset();
            at = 1;
        }
        for (; at + 1 < got; at += 2) {
            const std::array<std::byte, 2> pair{bytes.at(at), bytes.at(at + 1)};
            arrived.push_back(std::bit_cast<std::int16_t>(pair));
        }
        if (at < got) odd = bytes.at(at);

        std::unique_lock lock(mutex_);
        // Waits for room, so the programs never run further ahead than the
        // buffer: a full pipe stops them until this reads again.
        space_.wait(lock, stopping, [this] { return samples_.size() < buffer_limit_; });
        if (stopping.stop_requested()) return;
        samples_.insert(samples_.end(), arrived.begin(), arrived.end());
        last_data_ = std::chrono::steady_clock::now();
    }
    if (stopping.stop_requested()) return;

    const std::vector<int> codes = pipeline_->wait();
    const std::scoped_lock lock(mutex_);
    ended_ = true;
    for (std::size_t index = 0; index < codes.size(); ++index) {
        if (codes[index] == 0) continue;
        // The first program to fail is the one to blame: yt-dlp failing
        // leaves ffmpeg with nothing to decode, and it fails as well.
        const std::string program = programs_.at(index).path.stem().string();
        error_ =
            errors_.at(index).empty() ? std::format("{} stopped with code {}", program, codes[index]) : describe_failure(errors_.at(index));
        util::log().info("a track failed: {} exited with {}: {}", program, codes[index], error_);
        break;
    }
}

auto process_stream::read(std::span<std::int16_t> into) -> std::size_t {
    std::size_t count = 0;
    {
        const std::scoped_lock lock(mutex_);
        count = std::min(into.size(), samples_.size());
        std::copy_n(samples_.begin(), count, into.begin());
        samples_.erase(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(count));
    }
    if (count > 0) space_.notify_all();
    return count;
}

auto process_stream::state() const -> ports::stream_state {
    const std::scoped_lock lock(mutex_);
    if (!samples_.empty()) return ports::stream_state::running;
    if (!error_.empty()) return ports::stream_state::failed;
    if (ended_) return ports::stream_state::finished;
    if (std::chrono::steady_clock::now() - last_data_ > stall_limit_) return ports::stream_state::failed;
    return ports::stream_state::running;
}

auto process_stream::error() const -> std::string {
    const std::scoped_lock lock(mutex_);
    if (!error_.empty()) return error_;
    if (!ended_ && samples_.empty() && std::chrono::steady_clock::now() - last_data_ > stall_limit_) {
        return std::format("no audio arrived for {}", std::chrono::duration_cast<std::chrono::seconds>(stall_limit_));
    }
    return {};
}

ytdlp_opener::ytdlp_opener(std::filesystem::path ytdlp, std::filesystem::path ffmpeg, bool even_loudness,
                           std::optional<cookie_source> cookies)
    : ytdlp_(std::move(ytdlp)), ffmpeg_(std::move(ffmpeg)), even_loudness_(even_loudness), cookies_(std::move(cookies)) {}

sign_in_retry::sign_in_retry(std::unique_ptr<ports::pcm_stream> first, opener signed_in)
    : current_(std::move(first)), signed_in_(std::move(signed_in)) {}

auto sign_in_retry::settle() const -> void {
    if (!signed_in_ || delivered_ || current_->state() != ports::stream_state::failed) return;
    const std::string reason = current_->error();
    if (!needs_sign_in(reason)) return;
    util::log().debug("yt-dlp must sign in to fetch a track, so it tries again signed in: {}", reason);
    // The first try has ended; replacing it waits for nothing.
    current_ = std::exchange(signed_in_, nullptr)();
}

auto sign_in_retry::read(std::span<std::int16_t> into) -> std::size_t {
    const std::scoped_lock lock(mutex_);
    settle();
    const std::size_t got = current_->read(into);
    if (got > 0) delivered_ = true;
    return got;
}

auto sign_in_retry::state() const -> ports::stream_state {
    const std::scoped_lock lock(mutex_);
    settle();
    return current_->state();
}

auto sign_in_retry::error() const -> std::string {
    const std::scoped_lock lock(mutex_);
    settle();
    return current_->error();
}

namespace {

auto open_track(const std::filesystem::path& ytdlp, const std::filesystem::path& ffmpeg, bool even_loudness, const std::string& url,
                std::optional<cookie_copy> cookies) -> std::unique_ptr<ports::pcm_stream> {
    std::vector<util::program> programs{
        {.path = ytdlp, .arguments = fetch_arguments(url, ffmpeg, path_of(cookies))},
        {.path = ffmpeg, .arguments = decode_arguments(even_loudness)},
    };
    return std::make_unique<process_stream>(std::move(programs), std::chrono::seconds{30}, process_stream::default_buffer,
                                            std::move(cookies));
}

} // namespace

auto ytdlp_opener::open(const std::string& url) -> std::unique_ptr<ports::pcm_stream> {
    if (!cookies_) return open_track(ytdlp_, ffmpeg_, even_loudness_, url, std::nullopt);
    if (cookies_->needed_for(url)) return open_track(ytdlp_, ffmpeg_, even_loudness_, url, cookies_->copy());

    // Copies, not `this`: the retry belongs to the stream, which the player
    // holds. Moving it is only as noexcept as moving what it copied, which
    // clang-tidy cannot see through; it is moved once, into the retry, and
    // nothing relies on that not throwing.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    auto signed_in = [ytdlp = ytdlp_, ffmpeg = ffmpeg_, even = even_loudness_, url, cookies = *cookies_] {
        cookies.remember(url);
        return open_track(ytdlp, ffmpeg, even, url, cookies.copy());
    };
    return std::make_unique<sign_in_retry>(open_track(ytdlp_, ffmpeg_, even_loudness_, url, std::nullopt), std::move(signed_in));
}

} // namespace latibot::music
