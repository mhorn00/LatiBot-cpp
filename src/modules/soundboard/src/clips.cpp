#include "clips.hpp"

#include "core/ports/http_client.hpp"
#include "core/util/log.hpp"
#include "core/util/process.hpp"
#include "core/util/text.hpp"

#include <atomic>
#include <format>
#include <fstream>
#include <system_error>

namespace latibot::soundboard {
namespace {

/// Longer than any soundboard sound, which is at most 5.2 seconds: a file
/// that is not one cannot become a long voice message.
constexpr std::string_view longest_clip_seconds = "30";

/// What ffmpeg may write to stdout: 30 seconds of samples is under 3 MiB.
constexpr std::size_t ffmpeg_output_limit = std::size_t{8} << 20U;

auto failure(std::string message) -> ports::api_error {
    return ports::api_error{.http_status = 0, .message = std::move(message)};
}

/// What both runs of ffmpeg share: one sound in, one channel at 48 kHz out.
auto common_arguments(const std::filesystem::path& input, double volume) -> std::vector<std::string> {
    std::vector<std::string> arguments{"-hide_banner", "-nostdin", "-loglevel", "error", "-i",
                                       input.string(), "-map",     "0:a:0",     "-t",    std::string(longest_clip_seconds)};
    if (volume < 1.0) {
        arguments.emplace_back("-af");
        arguments.push_back(std::format("volume={:.3f}", volume));
    }
    arguments.insert(arguments.end(), {"-ac", "1", "-ar", std::to_string(clip_sample_rate)});
    return arguments;
}

} // namespace

auto encode_arguments(const std::filesystem::path& input, double volume) -> std::vector<std::string> {
    std::vector<std::string> arguments = common_arguments(input, volume);
    arguments.insert(arguments.end(), {"-c:a", "libopus", "-b:a", "64k", "-f", "ogg", "pipe:1"});
    return arguments;
}

auto decode_arguments(const std::filesystem::path& input, double volume) -> std::vector<std::string> {
    std::vector<std::string> arguments = common_arguments(input, volume);
    arguments.insert(arguments.end(), {"-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"});
    return arguments;
}

auto make_clip(std::string ogg, std::string_view pcm) -> discord::voice_message {
    std::vector<std::int16_t> samples(pcm.size() / 2);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto low = static_cast<std::uint8_t>(pcm[2 * i]);
        const auto high = static_cast<std::uint8_t>(pcm[(2 * i) + 1]);
        samples[i] = static_cast<std::int16_t>(static_cast<std::uint16_t>(low | (high << 8U)));
    }
    const auto duration = std::chrono::milliseconds{static_cast<std::int64_t>(samples.size()) * 1000 / clip_sample_rate};
    return discord::voice_message{.file = std::move(ogg),
                                  .filename = std::string(clip_filename),
                                  .mimetype = "audio/ogg",
                                  .duration = duration,
                                  .waveform = discord::waveform_base64(samples)};
}

auto describe_failure(std::string_view errors) -> std::string {
    constexpr std::size_t longest = 200;
    std::string_view last;
    for (const std::string_view line : util::lines(errors)) {
        if (const std::string_view trimmed = util::trim(line); !trimmed.empty()) last = trimmed;
    }
    if (last.empty()) return "ffmpeg couldn't read that sound";
    return std::format("ffmpeg couldn't read that sound: {}", util::truncate(last, longest));
}

// --------------------------------------------------------------------------
// ffmpeg
// --------------------------------------------------------------------------

struct ffmpeg_clips::job {
    dpp::snowflake sound_id;
    std::string file;
    double volume = 1.0;
    dpp::promise<ports::result<discord::voice_message>> done;
};

ffmpeg_clips::ffmpeg_clips(ports::http_client& http, std::optional<std::filesystem::path> ffmpeg, std::chrono::milliseconds timeout,
                           std::size_t cache_size)
    : http_(&http),
      ffmpeg_(std::move(ffmpeg)),
      timeout_(timeout),
      cache_size_(cache_size),
      worker_([this](const std::stop_token& stopping) { work(stopping); }) {}

ffmpeg_clips::~ffmpeg_clips() {
    worker_.request_stop();
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();

    // Anything never started still has somebody waiting on it. A promise
    // that cannot be set leaves nothing more to do on the way out.
    for (const std::unique_ptr<job>& waiting : queue_) {
        try {
            waiting->done.set_value(std::unexpected(failure("the bot is shutting down")));
        } catch (...) { // NOLINT(bugprone-empty-catch)
        }
    }
}

auto ffmpeg_clips::make(sound wanted) -> dpp::task<ports::result<discord::voice_message>> {
    const cache_key key{wanted.id, wanted.volume};
    if (auto made = cached(key)) co_return std::move(*made);
    if (!ffmpeg_) co_return std::unexpected(failure("ffmpeg isn't installed where i can find it"));

    auto fetched = co_await http_->send(
        {.url = sound_url(wanted.id), .method = ports::http_method::get, .body = {}, .content_type = {}, .headers = {}});
    if (!fetched.has_value()) {
        util::log().warn("could not fetch soundboard sound {}: {}", wanted.id, fetched.error().message);
        co_return std::unexpected(failure("couldn't fetch that sound from Discord"));
    }
    if (fetched->status != 200) {
        util::log().warn("Discord's CDN answered {} for soundboard sound {}", fetched->status, wanted.id);
        co_return std::unexpected(failure(std::format("Discord wouldn't give me that sound (HTTP {})", fetched->status)));
    }
    if (fetched->body.empty() || fetched->body.size() > max_sound_bytes) {
        co_return std::unexpected(failure("that file isn't a soundboard sound i can use"));
    }

    auto queued = std::make_unique<job>();
    queued->sound_id = wanted.id;
    queued->file = std::move(fetched->body);
    queued->volume = wanted.volume;
    auto finished = queued->done.get_awaitable();
    {
        const std::scoped_lock lock(mutex_);
        queue_.push_back(std::move(queued));
    }
    wake_.notify_one();

    auto made = co_await finished;
    if (made.has_value()) remember(key, made.value());
    co_return made;
}

auto ffmpeg_clips::make_now(const std::filesystem::path& input, double volume) const -> ports::result<discord::voice_message> {
    if (!ffmpeg_) return std::unexpected(failure("ffmpeg isn't installed where i can find it"));

    // Twice over the same file, rather than once with two outputs: one run
    // writes the file Discord gets, the other the samples it is measured by.
    const auto run_ffmpeg = [&](std::vector<std::string> arguments) -> ports::result<std::string> {
        util::run_result ran =
            util::run({.path = *ffmpeg_, .arguments = std::move(arguments), .working_directory = {}}, timeout_, ffmpeg_output_limit);
        if (ran.timed_out) {
            return std::unexpected(
                failure(std::format("ffmpeg took more than {}", std::chrono::duration_cast<std::chrono::seconds>(timeout_))));
        }
        if (ran.exit_code != 0) {
            util::log().debug("ffmpeg could not convert a soundboard sound: {}", ran.errors);
            return std::unexpected(failure(describe_failure(ran.errors)));
        }
        return std::move(ran.output);
    };

    try {
        auto encoded = run_ffmpeg(encode_arguments(input, volume));
        if (!encoded.has_value()) return std::unexpected(std::move(encoded.error()));
        const auto decoded = run_ffmpeg(decode_arguments(input, volume));
        if (!decoded.has_value()) return std::unexpected(decoded.error());
        if (encoded->empty() || decoded->size() < 2) return std::unexpected(failure("that sound has no audio in it"));
        return make_clip(std::move(encoded.value()), decoded.value());
    } catch (const util::process_error& error) {
        util::log().error("could not run ffmpeg: {}", error.what());
        return std::unexpected(failure("i couldn't run ffmpeg; it's in the log"));
    }
}

auto ffmpeg_clips::cached(const cache_key& key) -> std::optional<discord::voice_message> {
    const std::scoped_lock lock(cache_mutex_);
    const auto found = cache_.find(key);
    if (found == cache_.end()) return std::nullopt;
    return found->second;
}

auto ffmpeg_clips::remember(const cache_key& key, const discord::voice_message& made) -> void {
    if (cache_size_ == 0) return;
    const std::scoped_lock lock(cache_mutex_);
    if (!cache_.emplace(key, made).second) return;
    cache_order_.push_back(key);
    while (cache_order_.size() > cache_size_) {
        cache_.erase(cache_order_.front());
        cache_order_.pop_front();
    }
}

auto ffmpeg_clips::work(const std::stop_token& stopping) -> void {
    static std::atomic<unsigned> files_made{0};
    while (true) {
        std::unique_ptr<job> next;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stopping, [this] { return !queue_.empty(); });
            if (stopping.stop_requested() || queue_.empty()) return;
            next = std::move(queue_.front());
            queue_.pop_front();
        }

        // A file of its own for ffmpeg to read, since it is given nothing on
        // stdin; removed whatever happened.
        std::error_code error;
        const std::filesystem::path input =
            std::filesystem::temp_directory_path(error) / std::format("latibot-sound-{}-{}", next->sound_id.str(), files_made.fetch_add(1));
        ports::result<discord::voice_message> made = std::unexpected(failure("couldn't write that sound down for ffmpeg; it's in the log"));
        if (error) {
            util::log().error("there is no temporary folder to put a soundboard sound in: {}", error.message());
        } else if (std::ofstream file(input, std::ios::binary);
                   !file.write(next->file.data(), static_cast<std::streamsize>(next->file.size()))) {
            util::log().error("could not write soundboard sound {} to {}", next->sound_id, input.string());
        } else {
            file.close();
            made = make_now(input, next->volume);
        }
        std::filesystem::remove(input, error);

        // Completed here, whatever happened: a promise left unset would
        // leave its caller suspended for ever.
        next->done.set_value(std::move(made));
    }
}

} // namespace latibot::soundboard
