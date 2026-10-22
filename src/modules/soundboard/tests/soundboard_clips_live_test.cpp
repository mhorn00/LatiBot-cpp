// The real ffmpeg making a voice message (src/modules/soundboard/docs/Soundboard.md §3).
// Hidden ([.]) and [live]: run with `latibot_soundboard_tests.exe "[live]"`.
// Skipped when ffmpeg is not installed.
//
// The sound is made by ffmpeg itself, a second of a tone as MP3, as most
// soundboard sounds are, so nothing is fetched from the network.

#include "clips.hpp"
#include "core/util/process.hpp"

#include "mocks/mock_http.hpp"
#include "support/finished_within.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("ffmpeg makes a sound into Ogg Opus with its duration, and a second time it is kept", "[soundboard][live][.]") {
    const auto ffmpeg = latibot::util::locate_program("ffmpeg", {});
    if (!ffmpeg) SKIP("ffmpeg is not installed");

    const auto tone = latibot::util::run({.path = *ffmpeg,
                                          .arguments = {"-hide_banner", "-nostdin", "-loglevel", "error", "-f", "lavfi", "-i",
                                                        "sine=frequency=440:duration=1", "-f", "mp3", "pipe:1"},
                                          .working_directory = {}},
                                         20s);
    REQUIRE(tone.exit_code == 0);
    REQUIRE_FALSE(tone.output.empty());

    latibot::testing::mock_http http;
    http.queue(200, tone.output);
    latibot::soundboard::ffmpeg_clips clips(http, ffmpeg);
    const latibot::soundboard::sound sound{.id = dpp::snowflake{7}, .name = "tone", .guild_id = {}, .emoji = {}, .volume = 0.5};

    const auto started = std::chrono::steady_clock::now();
    const auto made = latibot::testing::finished_within(clips.make(sound), 20s);
    const auto took = std::chrono::steady_clock::now() - started;

    REQUIRE(made.has_value());
    INFO((made->has_value() ? std::string() : made->error().message));
    REQUIRE(made->has_value());
    const latibot::discord::voice_message& message = made->value();
    CHECK(message.file.starts_with("OggS"));
    CHECK(message.mimetype == "audio/ogg");
    // MP3 pads its start, so a second of tone comes back a little longer.
    CHECK(message.duration >= 950ms);
    CHECK(message.duration <= 1100ms);
    CHECK(message.waveform.size() == 344);
    // Well inside Discord's three seconds for the first answer.
    CHECK(took < 2s);

    // Kept: nothing is fetched the second time, and none is queued.
    const auto again = latibot::testing::finished_within(clips.make(sound), 5s);
    REQUIRE(again.has_value());
    REQUIRE(again->has_value());
    CHECK(again->value().file == message.file);
    CHECK(http.requests.size() == 1);
}
