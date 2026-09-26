#include "core/audio/dectalk_engine.hpp"

#include "core/audio/pcm.hpp"
#include "core/audio/voice_params.hpp"
#include "core/util/log.hpp"

#include <dpp/coro/awaitable.h>

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// ttsapi.h needs these two first, and in this order.
#include <mmreg.h>
#include <mmsystem.h>
#include <ttsapi.h>

namespace latibot::audio {
namespace {

// Four buffers of about three quarters of a second each. The engine fills
// them far faster than real time, so their size only sets how often the
// callback runs.
constexpr std::size_t buffer_count = 4;
constexpr std::size_t samples_per_buffer = 8192;
constexpr std::uint32_t dectalk_sample_rate = 11025;

/// The silence left after the last sound: 50 ms, enough that speech does not
/// end abruptly. DECtalk ends every utterance with about 400 ms of it, which
/// would otherwise sit between queued utterances and at the end of every
/// voice message.
constexpr std::size_t trailing_silence_kept = dectalk_sample_rate / 20;

struct buffer {
    TTS_BUFFER_T header{};
    std::array<std::int16_t, samples_per_buffer> samples{};
};

/// What the callback and the worker share while one utterance is spoken.
///
/// DECtalk hands buffers back in the order they were queued, and the pointer
/// it passes to the callback is cut to 32 bits (plan §2.2). So the buffers in
/// flight are kept in queue order, and each buffer message takes the front
/// one; the 32 bits are only compared, as a check.
struct session {
    LPTTS_HANDLE_T handle = nullptr;
    std::array<buffer, buffer_count> buffers{};

    std::mutex mutex;
    std::condition_variable changed;
    std::deque<buffer*> in_flight;
    std::vector<std::int16_t> samples;
    std::size_t max_samples = 0;
    bool truncated = false;
    bool synced = false;
    int out_of_order = 0;
};

/// The message id DECtalk sends a filled buffer with. On Windows it is a
/// registered window message rather than a constant (plan §2.2).
auto buffer_message() -> UINT {
    static const UINT id = RegisterWindowMessageA("DECtalkBufferMessage");
    return id;
}

auto error_message() -> UINT {
    static const UINT id = RegisterWindowMessageA("DECtalkErrorMessage");
    return id;
}

// DECtalk takes a plain function and a 32-bit instance value, too narrow for
// a pointer, so the callback finds the utterance through this. There is
// only ever one: the worker speaks one request at a time.
std::mutex current_mutex;
session* current = nullptr;

/// Keeps what a filled buffer holds, up to the session's limit.
auto keep(session& speaking, const buffer& filled) -> void {
    const std::size_t count = std::min<std::size_t>(filled.header.dwBufferLength / sizeof(std::int16_t), samples_per_buffer);
    const std::size_t room = speaking.max_samples - std::min(speaking.max_samples, speaking.samples.size());
    const std::size_t kept = std::min(count, room);
    speaking.samples.insert(speaking.samples.end(), filled.samples.begin(), filled.samples.begin() + static_cast<std::ptrdiff_t>(kept));
    if (kept < count || speaking.samples.size() >= speaking.max_samples) speaking.truncated = true;
}

auto on_dectalk_message(LONG first, LONG second, DWORD /*instance*/, UINT message) -> void {
    if (message == error_message()) {
        util::log().warn("DECtalk reported error {} ({})", first, second);
        return;
    }
    if (message != buffer_message()) return;

    const std::scoped_lock hold(current_mutex);
    if (current == nullptr) return;
    session& speaking = *current;

    std::unique_lock lock(speaking.mutex);
    if (speaking.in_flight.empty()) {
        ++speaking.out_of_order;
        return;
    }
    buffer* filled = speaking.in_flight.front();
    speaking.in_flight.pop_front();

    // Both as the 32 bits they are: the callback's LONG is signed, so an
    // address with bit 31 set arrives negative.
    const auto low_bits = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&filled->header));
    const auto reported = std::bit_cast<std::uint32_t>(second);
    if (low_bits != reported) ++speaking.out_of_order;

    keep(speaking, *filled);
    filled->header.dwBufferLength = 0;
    speaking.changed.notify_all();

    // Past the limit the audio is thrown away, but the engine is still fed:
    // starved of buffers it blocks, and then so does shutting it down.
    speaking.in_flight.push_back(filled);
    lock.unlock();
    TextToSpeechAddBuffer(speaking.handle, &filled->header);
}

auto describe(MMRESULT status) -> std::string {
    switch (status) {
    case MMSYSERR_NOMEM:
        return "out of memory";
    case MMSYSERR_ERROR:
        return "the dictionary could not be loaded";
    case MMSYSERR_INVALPARAM:
        return "an invalid parameter";
    default:
        return std::format("error {}", status);
    }
}

auto engine_error(std::string message) -> ports::api_error {
    return {.http_status = 0, .message = std::move(message)};
}

} // namespace

struct dectalk_engine::job {
    std::uint64_t id = 0;
    ports::speech_request request;
    dpp::promise<ports::result<ports::pcm_audio>> done;
};

auto dectalk_engine::default_dictionary() -> std::filesystem::path {
    // Any function in the DLL names the module it came from.
    HMODULE module = nullptr;
    const auto* address = reinterpret_cast<LPCWSTR>(&TextToSpeechStartupExFonix);
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, address, &module) == 0) {
        return "dtalk_us.dic";
    }

    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return "dtalk_us.dic";
    path.resize(length);
    return std::filesystem::path(path).parent_path() / "dtalk_us.dic";
}

dectalk_engine::dectalk_engine(std::filesystem::path dictionary, std::chrono::milliseconds max_synthesis)
    : dictionary_(std::move(dictionary)), max_synthesis_(max_synthesis) {
    if (!std::filesystem::exists(dictionary_)) {
        util::log().warn("the DECtalk dictionary is missing ({}); speech will fail until it is there", dictionary_.string());
    }
    worker_ = std::jthread([this](const std::stop_token& stopping) { work(stopping); });
}

dectalk_engine::~dectalk_engine() {
    worker_.request_stop();
    wake_.notify_all();
}

auto dectalk_engine::synthesize(ports::speech_request request) -> dpp::task<ports::result<ports::pcm_audio>> {
    auto queued = std::make_unique<job>();
    queued->request = std::move(request);
    auto finished = queued->done.get_awaitable();
    {
        const std::scoped_lock lock(mutex_);
        queued->id = next_id_++;
        queue_.push_back(std::move(queued));
    }
    wake_.notify_all();
    co_return co_await finished;
}

auto dectalk_engine::stop() -> void {
    {
        const std::scoped_lock lock(mutex_);
        cancel_below_ = next_id_;
    }
    wake_.notify_all();

    // The utterance being spoken, if any, waits on its own condition. Taking
    // its mutex before notifying means the worker is either about to check
    // `cancelled` or already waiting, never between the two.
    const std::scoped_lock hold(current_mutex);
    if (current != nullptr) {
        const std::scoped_lock lock(current->mutex);
        current->changed.notify_all();
    }
}

auto dectalk_engine::cancelled(std::uint64_t id) const -> bool {
    const std::scoped_lock lock(mutex_);
    return id < cancel_below_;
}

auto dectalk_engine::work(const std::stop_token& stopping) -> void {
    while (true) {
        std::unique_ptr<job> next;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stopping, [this] { return !queue_.empty(); });
            if (queue_.empty()) break; // stopping, with nothing left
            next = std::move(queue_.front());
            queue_.pop_front();
        }

        // Completed here, on this thread, whatever happened: a promise left
        // unset would leave its caller suspended for ever.
        if (stopping.stop_requested()) {
            next->done.set_value(engine_error("the speech engine is shutting down"));
        } else if (cancelled(next->id)) {
            next->done.set_value(engine_error("stopped"));
        } else {
            next->done.set_value(speak(next->request, next->id));
        }
    }
}

namespace {

/// How an utterance ended.
enum class ending : std::uint8_t {
    /// The engine said everything.
    finished,
    /// It reached the request's `max_duration`.
    truncated,
    /// `stop` was called.
    cancelled,
    /// It ran past `max_synthesis`.
    timed_out,
};

/// Queues the buffers, speaks `text` and waits for one of the endings.
/// `is_cancelled` is polled whenever the session changes.
auto run(session& speaking, std::string text, std::chrono::steady_clock::time_point deadline, const std::function<bool()>& is_cancelled)
    -> ending {
    for (buffer& each : speaking.buffers) {
        each.header.lpData = reinterpret_cast<LPSTR>(each.samples.data());
        each.header.dwMaximumBufferLength = static_cast<DWORD>(samples_per_buffer * sizeof(std::int16_t));
        {
            const std::scoped_lock lock(speaking.mutex);
            speaking.in_flight.push_back(&each);
        }
        TextToSpeechAddBuffer(speaking.handle, &each.header);
    }

    TextToSpeechSpeak(speaking.handle, text.data(), TTS_FORCE);

    // Sync blocks until the engine has finished, so it gets a thread of its
    // own, and this one watches the limits meanwhile.
    std::jthread syncer([&speaking] {
        TextToSpeechSync(speaking.handle);
        const std::scoped_lock lock(speaking.mutex);
        speaking.synced = true;
        speaking.changed.notify_all();
    });

    ending result = ending::finished;
    {
        std::unique_lock lock(speaking.mutex);
        while (!speaking.synced && !speaking.truncated && !is_cancelled()) {
            if (speaking.changed.wait_until(lock, deadline) == std::cv_status::timeout) break;
        }
        if (speaking.synced) {
            result = ending::finished;
        } else if (speaking.truncated) {
            result = ending::truncated;
        } else if (is_cancelled()) {
            result = ending::cancelled;
        } else {
            result = ending::timed_out;
        }
    }

    if (result == ending::finished) {
        // The last buffer is only part full, and the engine's until asked for.
        LPTTS_BUFFER_T tail = nullptr;
        if (TextToSpeechReturnBuffer(speaking.handle, &tail) == MMSYSERR_NOERROR && tail != nullptr) {
            const std::scoped_lock lock(speaking.mutex);
            if (!speaking.in_flight.empty() && &speaking.in_flight.front()->header == tail) {
                keep(speaking, *speaking.in_flight.front());
                speaking.in_flight.pop_front();
            } else {
                ++speaking.out_of_order;
            }
        }
    } else {
        // Reset abandons whatever is still being synthesized, which releases
        // the Sync still waiting on it.
        TextToSpeechReset(speaking.handle, FALSE);
    }
    syncer.join();
    return result;
}

} // namespace

auto dectalk_engine::speak(const ports::speech_request& request, std::uint64_t id) -> ports::result<ports::pcm_audio> {
    auto speaking = std::make_unique<session>();
    speaking->max_samples = static_cast<std::size_t>(std::max<std::int64_t>(request.max_duration.count(), 0) * dectalk_sample_rate / 1000);

    // Checked here rather than left to DECtalk: a start that fails to load
    // the dictionary leaves objects behind that make every later start in
    // the process fail too, including ones given the right path.
    std::error_code error;
    if (!std::filesystem::is_regular_file(dictionary_, error)) {
        return engine_error(std::format("the DECtalk dictionary is missing ({})", dictionary_.string()));
    }

    std::string dictionary = dictionary_.string();
    const MMRESULT started =
        TextToSpeechStartupExFonix(&speaking->handle, WAVE_MAPPER, DO_NOT_USE_AUDIO_DEVICE, &on_dectalk_message, 0, dictionary.data());
    if (started != MMSYSERR_NOERROR) return engine_error(std::format("DECtalk would not start: {}", describe(started)));

    {
        const std::scoped_lock hold(current_mutex);
        current = speaking.get();
    }

    std::optional<ending> ended;
    const MMRESULT opened = TextToSpeechOpenInMemory(speaking->handle, WAVE_FORMAT_1M16);
    if (opened == MMSYSERR_NOERROR) {
        ended = run(*speaking, voice_preamble(request.voice) + request.text, std::chrono::steady_clock::now() + max_synthesis_,
                    [this, id] { return cancelled(id); });
        TextToSpeechCloseInMemory(speaking->handle);
    }
    TextToSpeechShutdown(speaking->handle);

    {
        const std::scoped_lock hold(current_mutex);
        current = nullptr;
    }

    if (speaking->out_of_order != 0) {
        util::log().error("DECtalk returned {} buffer(s) out of order; the audio may be garbled", speaking->out_of_order);
    }
    if (!ended) return engine_error(std::format("DECtalk would not write to memory: {}", describe(opened)));

    switch (*ended) {
    case ending::cancelled:
        return engine_error("stopped");
    case ending::timed_out:
        util::log().warn("DECtalk took longer than {} to say {} characters; abandoned", max_synthesis_, request.text.size());
        return engine_error("that took too long to say");
    case ending::finished:
    case ending::truncated:
        break;
    }

    ports::pcm_audio audio;
    audio.samples = std::move(speaking->samples);
    audio.sample_rate = dectalk_sample_rate;
    audio.channels = 1;
    audio.truncated = *ended == ending::truncated || speaking->truncated;
    // Before the volume, so what counts as silence is DECtalk's own level.
    trim_trailing_silence(audio.samples, trailing_silence_kept);
    apply_volume(audio.samples, request.voice.volume);
    return audio;
}

} // namespace latibot::audio
