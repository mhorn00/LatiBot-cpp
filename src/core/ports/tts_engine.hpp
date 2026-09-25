#pragma once

#include "core/ports/result.hpp"

#include <dpp/coro/task.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace latibot::ports {

/// Which voice to speak with. `custom_params` carries a saved custom voice's
/// `[:dv ...]` pairs (plan §12.6).
struct voice_settings {
    std::string voice = "paul";
    int rate = 200;
    int volume = 100;
    std::string custom_params;
};

/// Raw PCM as DECtalk produces it, before resampling for Discord.
struct pcm_audio {
    std::vector<std::int16_t> samples;
    std::uint32_t sample_rate = 11025;
    std::uint8_t channels = 1;

    [[nodiscard]] std::chrono::milliseconds duration() const {
        if (sample_rate == 0 || channels == 0) {
            return std::chrono::milliseconds{0};
        }
        const auto frames = samples.size() / channels;
        return std::chrono::milliseconds{(frames * 1000) / sample_rate};
    }
};

/// Speech synthesis.
///
/// The mixer and the commands talk to this rather than to DECtalk directly,
/// so they can be tested without the engine (plan §17.3).
class tts_engine {
public:
    virtual ~tts_engine() = default;

    tts_engine() = default;
    tts_engine(const tts_engine&) = delete;
    tts_engine& operator=(const tts_engine&) = delete;

    /// `text` must already have been through the sanitizer (plan §12.5).
    virtual dpp::task<result<pcm_audio>> synthesize(std::string text, voice_settings settings) = 0;

    /// Abandons the current utterance and anything queued.
    virtual void stop() = 0;
};

} // namespace latibot::ports
