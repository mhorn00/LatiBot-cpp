#include "core/audio/pcm.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace latibot::audio {

auto apply_volume(std::span<std::int16_t> samples, int percent) -> void {
    if (percent == 100) return;
    const int scale = std::max(percent, 0);

    constexpr int lowest = std::numeric_limits<std::int16_t>::min();
    constexpr int highest = std::numeric_limits<std::int16_t>::max();
    for (std::int16_t& sample : samples) {
        sample = static_cast<std::int16_t>(std::clamp(sample * scale / 100, lowest, highest));
    }
}

auto to_discord(std::span<const std::int16_t> mono, std::uint32_t source_rate) -> std::vector<std::int16_t> {
    if (mono.empty() || source_rate == 0) return {};

    // Rounded to the nearest frame, so a second in is a second out.
    const auto input_frames = static_cast<std::uint64_t>(mono.size());
    const std::uint64_t output_frames = ((input_frames * discord_sample_rate) + (source_rate / 2)) / source_rate;

    std::vector<std::int16_t> out;
    out.reserve(static_cast<std::size_t>(output_frames) * discord_channels);

    // Positions are kept as a whole part and a remainder over the output
    // rate, rather than as a double, so a long utterance does not drift.
    const std::size_t last = mono.size() - 1;
    for (std::uint64_t frame = 0; frame < output_frames; ++frame) {
        const std::uint64_t scaled = frame * source_rate;
        const auto index = static_cast<std::size_t>(scaled / discord_sample_rate);
        const auto remainder = static_cast<std::int64_t>(scaled % discord_sample_rate);

        const std::int64_t from = mono[std::min(index, last)];
        const std::int64_t to = mono[std::min(index + 1, last)];
        const auto sample = static_cast<std::int16_t>(from + ((to - from) * remainder / discord_sample_rate));

        for (std::uint8_t channel = 0; channel < discord_channels; ++channel) {
            out.push_back(sample);
        }
    }
    return out;
}

} // namespace latibot::audio
