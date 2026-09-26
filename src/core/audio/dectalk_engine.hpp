#pragma once

#include "core/ports/tts_engine.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace latibot::audio {

/// DECtalk behind the `tts_engine` port (plan §12).
///
/// One worker thread takes requests in order. Each utterance gets an engine
/// of its own, started for it and shut down after it (plan §21.16): that
/// costs about 45 ms, and it is the only way to start from DECtalk's defaults
/// every time, since the engine's own reset stops it producing audio at all
/// in memory mode. Synthesis runs at hundreds of times real time, so a
/// request is answered with the whole utterance rather than streamed.
///
/// A request finishes early, with what it has so far, at its
/// `max_duration`. One that takes longer than `max_synthesis` of wall time is
/// abandoned: some inline commands wait on the clock instead of producing
/// audio, and one of those must not hold up everything queued behind it.
///
/// Requests complete on the worker thread, so the code after a `co_await`
/// on `synthesize` runs there until it next suspends. That code should be
/// short (hand the audio on and reply), since the next utterance waits for
/// it.
class dectalk_engine final : public ports::tts_engine {
public:
    /// dtalk_us.dic beside dectalk.dll, where the build puts it
    /// (cmake/dectalk.cmake).
    [[nodiscard]] static auto default_dictionary() -> std::filesystem::path;

    explicit dectalk_engine(std::filesystem::path dictionary = default_dictionary(),
                            std::chrono::milliseconds max_synthesis = std::chrono::seconds{10});

    /// Fails anything still queued, then waits for the worker.
    ~dectalk_engine() override;

    dectalk_engine(const dectalk_engine&) = delete;
    auto operator=(const dectalk_engine&) -> dectalk_engine& = delete;
    dectalk_engine(dectalk_engine&&) = delete;
    auto operator=(dectalk_engine&&) -> dectalk_engine& = delete;

    auto synthesize(ports::speech_request request) -> dpp::task<ports::result<ports::pcm_audio>> override;

    auto stop() -> void override;

private:
    struct job;

    auto work(const std::stop_token& stopping) -> void;

    /// Whether `stop` was called after job `id` was queued.
    [[nodiscard]] auto cancelled(std::uint64_t id) const -> bool;

    /// Synthesizes one request on a fresh engine.
    [[nodiscard]] auto speak(const ports::speech_request& request, std::uint64_t id) -> ports::result<ports::pcm_audio>;

    std::filesystem::path dictionary_;
    std::chrono::milliseconds max_synthesis_;

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<std::unique_ptr<job>> queue_;
    std::uint64_t next_id_ = 1;

    /// Jobs numbered below this were queued before the last `stop`.
    std::uint64_t cancel_below_ = 0;

    /// Last, so the worker is stopped before anything it uses goes.
    std::jthread worker_;
};

} // namespace latibot::audio
