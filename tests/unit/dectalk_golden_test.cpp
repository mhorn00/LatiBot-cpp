// Golden audio: what DECtalk says for a few fixed phrases, down to the
// sample (plan §17.5).
//
// tests/golden/dectalk.txt holds each phrase's sample count and a hash of
// its samples. On a mismatch the audio actually produced is written beside
// it as <name>.wav, so the difference can be listened to rather than guessed
// at. After a deliberate change (a DECtalk update, a new preamble), run the
// tests with LATIBOT_UPDATE_GOLDEN=1 to rewrite the file, and listen to the
// .wav files before committing it.

#include "core/audio/dectalk_engine.hpp"
#include "core/audio/wav.hpp"
#include "core/util/env.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace std::chrono_literals;
using latibot::ports::speech_request;

namespace {

struct phrase {
    std::string_view name;
    speech_request request;
};

// Each one exercises something different: the default voice, another
// voice, the rate, the text normaliser, inline commands.
auto phrases() -> std::array<phrase, 5> {
    return {{
        {.name = "paul", .request = {.text = "Hello there. This is the golden test phrase."}},
        {.name = "betty", .request = {.text = "Hello there. This is the golden test phrase.", .voice = {.voice = "betty"}}},
        {.name = "harry_fast", .request = {.text = "Hello there.", .voice = {.voice = "harry", .rate = 350}}},
        {.name = "numbers", .request = {.text = "It is 12:30 on 3/4/2025, and it costs $19.99."}},
        {.name = "inline", .request = {.text = "[:dv ap 200 pr 200]Up here.[:np] Down here.[:tone 440 250]"}},
    }};
}

struct fingerprint {
    std::size_t samples = 0;
    std::uint64_t hash = 0;

    auto operator==(const fingerprint&) const -> bool = default;
};

/// FNV-1a over the samples' bytes: stable, and enough to notice any change.
auto fingerprint_of(const std::vector<std::int16_t>& samples) -> fingerprint {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::int16_t sample : samples) {
        const auto bits = static_cast<std::uint16_t>(sample);
        for (const std::uint16_t byte : {static_cast<std::uint16_t>(bits & 0xFFU), static_cast<std::uint16_t>(bits >> 8U)}) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
    }
    return {.samples = samples.size(), .hash = hash};
}

auto golden_directory() -> std::filesystem::path {
    return std::filesystem::path(LATIBOT_TESTS_DIR) / "golden";
}

/// name -> fingerprint, from lines of "name samples hash".
auto read_golden(const std::filesystem::path& file) -> std::map<std::string, fingerprint> {
    std::map<std::string, fingerprint> golden;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.starts_with('#')) continue;
        std::istringstream fields(line);
        std::string name;
        fingerprint expected;
        fields >> name >> expected.samples >> std::hex >> expected.hash;
        if (fields) golden.emplace(name, expected);
    }
    return golden;
}

} // namespace

TEST_CASE("DECtalk's audio matches the golden fingerprints", "[audio][golden][fs][coro][threads]") {
    const std::filesystem::path file = golden_directory() / "dectalk.txt";
    const bool updating = latibot::util::env_var("LATIBOT_UPDATE_GOLDEN").value_or("") == "1";
    const auto golden = read_golden(file);

    latibot::audio::dectalk_engine engine;
    std::string rewritten = "# name samples fnv1a64 (tests/unit/dectalk_golden_test.cpp)\n";

    for (const phrase& each : phrases()) {
        const auto outcome = engine.synthesize(each.request).sync_wait_for(20s);
        REQUIRE(outcome.has_value());
        REQUIRE(outcome->ok());
        const auto& audio = outcome->value();

        const fingerprint actual = fingerprint_of(audio.samples);
        rewritten += std::format("{} {} {:016x}\n", each.name, actual.samples, actual.hash);

        const auto expected = golden.find(std::string(each.name));
        const bool matches = expected != golden.end() && expected->second == actual;
        const std::filesystem::path wav = golden_directory() / std::format("{}.wav", each.name);
        if (matches) {
            std::filesystem::remove(wav);
            continue;
        }

        std::ofstream(wav, std::ios::binary) << latibot::audio::wav_file(audio.samples, audio.sample_rate, audio.channels);
        if (!updating) {
            FAIL_CHECK(std::format("\"{}\" changed: {} samples, hash {:016x}; listen to {}", each.name, actual.samples, actual.hash,
                                   wav.string()));
        }
    }

    if (updating) std::ofstream(file, std::ios::binary) << rewritten;
}
