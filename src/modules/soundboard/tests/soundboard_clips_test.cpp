// Turning a sound into a voice message, without ffmpeg: its arguments, what
// is made from its output, and the ways it fails before ffmpeg runs
// (src/modules/soundboard/docs/Soundboard.md §3).

#include "clips.hpp"

#include "mocks/mock_http.hpp"
#include "support/finished_within.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using latibot::soundboard::ffmpeg_clips;
using latibot::soundboard::sound;
using latibot::testing::mock_http;
using namespace std::chrono_literals;

namespace {

auto quack() -> sound {
    return {.id = dpp::snowflake{42}, .name = "quack", .guild_id = {}, .emoji = {}, .volume = 1.0};
}

auto has(const std::vector<std::string>& arguments, std::string_view wanted) -> bool {
    return std::ranges::find(arguments, wanted) != arguments.end();
}

/// Where `first` is followed straight away by `second`.
auto pair_in(const std::vector<std::string>& arguments, std::string_view first, std::string_view second) -> bool {
    const auto at = std::ranges::find(arguments, first);
    return at != arguments.end() && std::next(at) != arguments.end() && *std::next(at) == second;
}

} // namespace

TEST_CASE("ffmpeg makes one channel of Ogg Opus at 48 kHz on stdout", "[soundboard]") {
    const auto arguments = latibot::soundboard::encode_arguments("in.mp3", 1.0);

    CHECK(pair_in(arguments, "-i", "in.mp3"));
    CHECK(pair_in(arguments, "-ac", "1"));
    CHECK(pair_in(arguments, "-ar", "48000"));
    CHECK(pair_in(arguments, "-c:a", "libopus"));
    CHECK(pair_in(arguments, "-f", "ogg"));
    CHECK(arguments.back() == "pipe:1");
    // Nothing read from stdin, nothing longer than a soundboard sound could
    // be by a wide margin, and no filter at full volume.
    CHECK(has(arguments, "-nostdin"));
    CHECK(pair_in(arguments, "-t", "30"));
    CHECK_FALSE(has(arguments, "-af"));
}

TEST_CASE("ffmpeg decodes the same way to 16-bit samples, at the sound's volume", "[soundboard]") {
    const auto arguments = latibot::soundboard::decode_arguments("in.ogg", 0.25);

    CHECK(pair_in(arguments, "-ac", "1"));
    CHECK(pair_in(arguments, "-ar", "48000"));
    CHECK(pair_in(arguments, "-f", "s16le"));
    CHECK(pair_in(arguments, "-af", "volume=0.250"));
    CHECK(pair_in(latibot::soundboard::encode_arguments("in.ogg", 0.25), "-af", "volume=0.250"));
    CHECK(arguments.back() == "pipe:1");
}

TEST_CASE("a clip's duration and waveform come from its samples", "[soundboard]") {
    // Half a second at 48 kHz: silence, then loud.
    std::string pcm(48000, '\0');
    for (std::size_t i = 24000; i < pcm.size(); i += 2) {
        pcm[i] = '\x00';
        pcm[i + 1] = '\x40'; // 0x4000
    }

    const auto made = latibot::soundboard::make_clip("OggS...", pcm);

    CHECK(made.file == "OggS...");
    CHECK(made.filename == "voice-message.ogg");
    CHECK(made.mimetype == "audio/ogg");
    CHECK(made.duration == 500ms);
    CHECK(made.waveform.size() == 344); // 256 bars, as base64
    CHECK(made.waveform.starts_with("AAAA"));
}

TEST_CASE("ffmpeg's last line says why it failed", "[soundboard]") {
    CHECK(latibot::soundboard::describe_failure("first\nin.mp3: Invalid data found when processing input\n\n") ==
          "ffmpeg couldn't read that sound: in.mp3: Invalid data found when processing input");
    CHECK(latibot::soundboard::describe_failure("") == "ffmpeg couldn't read that sound");
}

TEST_CASE("without ffmpeg every sound is refused, saying so, without fetching it", "[soundboard]") {
    mock_http http;
    ffmpeg_clips clips(http, std::nullopt);

    const auto made = latibot::testing::finished_within(clips.make(quack()), 5s);

    REQUIRE(made.has_value());
    REQUIRE_FALSE(made->has_value());
    CHECK(made->error().message == "ffmpeg isn't installed where i can find it");
    CHECK(http.requests.empty());
}

TEST_CASE("a sound Discord will not give is refused with its status", "[soundboard]") {
    mock_http http;
    http.queue(404, "");
    ffmpeg_clips clips(http, std::filesystem::path("ffmpeg"));

    const auto made = latibot::testing::finished_within(clips.make(quack()), 5s);

    REQUIRE(made.has_value());
    REQUIRE_FALSE(made->has_value());
    CHECK(made->error().message == "Discord wouldn't give me that sound (HTTP 404)");
    REQUIRE(http.requests.size() == 1);
    CHECK(http.requests.front().url == "https://cdn.discordapp.com/soundboard-sounds/42");
    CHECK(http.requests.front().method == latibot::ports::http_method::get);
}

TEST_CASE("a sound that cannot be fetched, or is far too big, is refused", "[soundboard]") {
    mock_http http;
    http.queue_error("connection reset");
    http.queue(200, std::string(latibot::soundboard::max_sound_bytes + 1, 'x'));
    http.queue(200, "");
    ffmpeg_clips clips(http, std::filesystem::path("ffmpeg"));

    for (const std::string_view expected : {"couldn't fetch that sound from Discord", "that file isn't a soundboard sound i can use",
                                            "that file isn't a soundboard sound i can use"}) {
        const auto made = latibot::testing::finished_within(clips.make(quack()), 5s);
        REQUIRE(made.has_value());
        REQUIRE_FALSE(made->has_value());
        CHECK(made->error().message == expected);
    }
}

TEST_CASE("ffmpeg that cannot be started is a refusal, not a crash", "[soundboard]") {
    mock_http http;
    http.queue(200, "not really an mp3");
    ffmpeg_clips clips(http, std::filesystem::path("no such ffmpeg here"));

    const auto made = latibot::testing::finished_within(clips.make(quack()), 10s);

    REQUIRE(made.has_value());
    REQUIRE_FALSE(made->has_value());
    CHECK(made->error().message == "i couldn't run ffmpeg; it's in the log");
}
