#pragma once

#include "core/ports/tts_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

namespace latibot::testing {

/// Stands in for DECtalk: produces a tone whose length follows the text, so
/// the mixer and the voice commands can be tested without the engine.
class mock_tts final : public ports::tts_engine {
public:
    /// How much audio one character of text produces.
    std::chrono::milliseconds per_character{50};

    /// Scripted failure for the next call, if set.
    std::optional<ports::api_error> next_error;

    std::vector<ports::speech_request> requests;
    int stop_count = 0;

    auto synthesize(ports::speech_request request) -> dpp::task<ports::result<ports::pcm_audio>> override {
        requests.push_back(request);

        if (next_error) {
            auto scripted = *next_error;
            next_error.reset();
            co_return scripted;
        }

        ports::pcm_audio audio;
        const auto wanted = per_character * static_cast<std::int64_t>(request.text.size());
        const auto milliseconds = std::min(wanted, request.max_duration);
        audio.truncated = wanted > request.max_duration;
        const auto sample_count = static_cast<std::size_t>(audio.sample_rate * milliseconds.count() / 1000);

        audio.samples.reserve(sample_count);
        for (std::size_t i = 0; i < sample_count; ++i) {
            // A 440 Hz tone: recognisable in a golden file, and not silence,
            // so a test can tell "spoke something" from "spoke nothing".
            const double phase = 2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / audio.sample_rate;
            audio.samples.push_back(static_cast<std::int16_t>(8000.0 * std::sin(phase)));
        }
        co_return audio;
    }

    auto stop() -> void override { ++stop_count; }
};

} // namespace latibot::testing
