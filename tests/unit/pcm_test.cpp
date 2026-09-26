// Volume and resampling for Discord (plan §12.3).

#include "core/audio/pcm.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

using latibot::audio::apply_volume;
using latibot::audio::to_discord;

namespace {

auto sine(double frequency, std::uint32_t rate, std::size_t count, double amplitude = 10000.0) -> std::vector<std::int16_t> {
    std::vector<std::int16_t> samples;
    samples.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        samples.push_back(
            static_cast<std::int16_t>(amplitude * std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(i) / rate)));
    }
    return samples;
}

/// Upward zero crossings of one channel of interleaved audio.
auto rising_crossings(const std::vector<std::int16_t>& interleaved, std::size_t channels) -> int {
    int crossings = 0;
    for (std::size_t i = channels; i < interleaved.size(); i += channels) {
        if (interleaved[i - channels] < 0 && interleaved[i] >= 0) ++crossings;
    }
    return crossings;
}

} // namespace

TEST_CASE("volume scales, clips and leaves 100 alone", "[audio]") {
    std::vector<std::int16_t> samples{1000, -1000, 30000, -30000, 0};

    SECTION("100 changes nothing") {
        apply_volume(samples, 100);
        CHECK(samples == std::vector<std::int16_t>{1000, -1000, 30000, -30000, 0});
    }
    SECTION("50 halves") {
        apply_volume(samples, 50);
        CHECK(samples == std::vector<std::int16_t>{500, -500, 15000, -15000, 0});
    }
    SECTION("200 doubles, clipping what no longer fits instead of wrapping") {
        apply_volume(samples, 200);
        CHECK(samples == std::vector<std::int16_t>{2000, -2000, 32767, -32768, 0});
    }
    SECTION("0 and below are silence") {
        apply_volume(samples, -5);
        CHECK(samples == std::vector<std::int16_t>(5, 0));
    }
}

TEST_CASE("resampling keeps the length and doubles every sample into stereo", "[audio]") {
    // One second in is one second out: 11025 frames become 48000.
    const auto out = to_discord(sine(440.0, 11025, 11025), 11025);

    CHECK(out.size() == std::size_t{48000} * 2);
    for (std::size_t i = 0; i < out.size(); i += 2) {
        if (out[i] != out[i + 1]) FAIL("left and right differ at frame " << i / 2);
    }
}

TEST_CASE("resampling keeps the frequency", "[audio]") {
    // 440 Hz for one second has 440 rising zero crossings, give or take the
    // one at the very start.
    const auto out = to_discord(sine(440.0, 11025, 11025), 11025);

    const int crossings = rising_crossings(out, 2);
    CHECK(crossings >= 439);
    CHECK(crossings <= 441);
}

TEST_CASE("resampling interpolates between the source samples", "[audio]") {
    // A ramp stays a ramp: every output sample lies between its neighbours
    // in the input, and the first matches exactly.
    const std::vector<std::int16_t> ramp{0, 1000, 2000, 3000};
    const auto out = to_discord(ramp, 12000); // exactly 4 output frames per input frame

    REQUIRE(out.size() == std::size_t{16} * 2);
    CHECK(out[0] == 0);
    CHECK(out[std::size_t{2} * 1] == 250);
    CHECK(out[std::size_t{2} * 2] == 500);
    CHECK(out[std::size_t{2} * 4] == 1000);
    CHECK(out[std::size_t{2} * 15] == 3000); // the last frame holds the last sample
}

TEST_CASE("resampling nothing gives nothing", "[audio]") {
    CHECK(to_discord({}, 11025).empty());
    CHECK(to_discord(std::vector<std::int16_t>{1, 2, 3}, 0).empty());
}

TEST_CASE("resampling a minute of speech", "[audio][!benchmark]") {
    const auto minute = sine(440.0, 11025, std::size_t{11025} * 60);
    BENCHMARK("11025 Hz mono to 48 kHz stereo, 60 s") {
        return to_discord(minute, 11025);
    };
}
