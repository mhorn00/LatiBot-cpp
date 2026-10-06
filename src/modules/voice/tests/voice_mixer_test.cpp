// The mixer: speech and music on one voice connection
// (docs/features/Music.md §4.2). Music is a ramp of numbered samples, so
// "music resumed exactly where it stopped" is a check on the numbers heard.

#include "voice/voice_mixer.hpp"

#include "mocks/mock_voice.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

using latibot::audio::music_source;
using latibot::audio::voice_mixer;
using latibot::testing::mock_voice;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{100};

/// Samples in a millisecond of 48 kHz stereo.
constexpr std::size_t per_ms = 96;

/// Music whose n-th sample is n % 20000 + 1, never 0 or negative, so music
/// is told apart from speech (negative) and padding (0). Tracks are
/// `track_samples` long, each ending with the marker `music:<track>`.
class ramp_source final : public music_source {
public:
    std::size_t track_samples = 0; // 0: one endless track
    std::size_t tracks = 1;
    std::size_t read_total = 0;
    std::size_t track = 1;
    std::size_t in_track = 0;
    std::vector<std::string> markers_heard;

    auto read(dpp::snowflake /*guild*/, std::span<std::int16_t> into) -> chunk override {
        if (track > tracks) return {.samples = 0, .end_marker = {}, .idle = true};
        std::size_t count = into.size();
        if (track_samples != 0) count = std::min(count, track_samples - in_track);
        for (std::size_t i = 0; i < count; ++i) {
            into[i] = static_cast<std::int16_t>(((read_total + i) % 20000) + 1);
        }
        read_total += count;
        in_track += count;
        if (track_samples != 0 && in_track == track_samples) {
            const std::string marker = std::format("music:{}", track);
            ++track;
            in_track = 0;
            return {.samples = count, .end_marker = marker, .idle = false};
        }
        return {.samples = count, .end_marker = {}, .idle = false};
    }

    auto on_marker(dpp::snowflake /*guild*/, std::string_view marker) -> void override { markers_heard.emplace_back(marker); }
};

struct fixture {
    mock_voice connection;
    voice_mixer mixer{connection};
    ramp_source music;

    fixture() {
        connection.connected[guild] = true;
        mixer.set_music(&music);
    }

    /// Plays `span`, reporting every marker passed to the mixer, as the
    /// shell does with DPP's marker events.
    auto play_for(std::chrono::milliseconds span) -> void {
        for (const std::string& marker : connection.advance(guild, span)) {
            mixer.on_marker(guild, marker);
        }
    }

    [[nodiscard]] auto heard() -> const std::vector<std::int16_t>& { return connection.heard[guild]; }
};

/// Whether the music in what was heard, with speech and padding left out,
/// is the ramp from its first sample on, with nothing missing or repeated.
auto music_is_continuous(const std::vector<std::int16_t>& heard) -> bool {
    std::size_t next = 0;
    for (const std::int16_t sample : heard) {
        if (sample <= 0) continue;
        if (sample != static_cast<std::int16_t>((next % 20000) + 1)) return false;
        ++next;
    }
    return true;
}

auto speech(std::chrono::milliseconds length) -> std::vector<std::int16_t> {
    return std::vector<std::int16_t>(static_cast<std::size_t>(length.count()) * per_ms, -5);
}

} // namespace

TEST_CASE("music is kept a few seconds ahead, in whole packets", "[voice]") {
    fixture test;
    test.mixer.wake(guild);
    CHECK(test.connection.remaining(guild) == 3000ms);

    test.play_for(1000ms);
    test.mixer.tick();
    CHECK(test.connection.remaining(guild) == 3000ms);

    for (const auto& played : test.connection.plays) {
        CHECK(played.samples % voice_mixer::packet_samples == 0);
    }
    test.play_for(10000ms);
    CHECK(music_is_continuous(test.heard()));
}

TEST_CASE("speech interrupts music at once, and music resumes exactly where it stopped", "[voice]") {
    fixture test;
    test.mixer.wake(guild);
    test.play_for(1000ms);

    const auto said = speech(500ms);
    REQUIRE(test.mixer.play(guild, said, "tts:1"));

    // The music still queued was taken back: only the speech is queued.
    CHECK(test.connection.remaining(guild) == 500ms);
    CHECK(test.connection.queued[guild] == std::vector<std::string>{"tts:1"});

    test.play_for(500ms);
    test.play_for(2000ms);

    const auto& heard = test.heard();
    REQUIRE(heard.size() == 3500 * per_ms);
    CHECK(heard[1000 * per_ms] == -5);
    CHECK(heard[(1500 * per_ms) - 1] == -5);
    CHECK(heard[1500 * per_ms] > 0);
    CHECK(music_is_continuous(heard));
}

TEST_CASE("music waits for every utterance queued, not just the first", "[voice]") {
    fixture test;
    test.mixer.wake(guild);
    test.play_for(200ms);

    REQUIRE(test.mixer.play(guild, speech(100ms), "tts:1"));
    REQUIRE(test.mixer.play(guild, speech(100ms), "tts:2"));
    test.play_for(100ms);
    CHECK(test.connection.remaining(guild) == 100ms); // the second utterance, and no music yet

    test.play_for(100ms);
    CHECK(test.connection.remaining(guild) == 3000ms);
    test.play_for(1000ms);
    CHECK(music_is_continuous(test.heard()));
}

TEST_CASE("stopping or skipping speech lets the music back in", "[voice]") {
    fixture test;
    test.mixer.wake(guild);
    test.play_for(300ms);

    SECTION("stop") {
        REQUIRE(test.mixer.play(guild, speech(5000ms), "tts:1"));
        test.play_for(100ms);
        test.mixer.stop(guild);
    }
    SECTION("skip") {
        REQUIRE(test.mixer.play(guild, speech(5000ms), "tts:1"));
        test.play_for(100ms);
        test.mixer.skip(guild);
    }
    CHECK(test.connection.remaining(guild) == 3000ms);
    test.play_for(1000ms);
    CHECK(music_is_continuous(test.heard()));
}

TEST_CASE("stopping speech when there is none leaves the music alone", "[voice]") {
    fixture test;
    test.mixer.wake(guild);
    test.mixer.stop(guild);
    test.mixer.skip(guild);
    CHECK(test.connection.stops == 0);
    CHECK(test.connection.skips == 0);
    CHECK(test.connection.remaining(guild) == 3000ms);
}

TEST_CASE("a track's end marker is reported once it has been heard", "[voice]") {
    fixture test;
    test.music.track_samples = (1000 * per_ms) + 7; // not a whole number of packets
    test.music.tracks = 2;
    test.mixer.wake(guild);

    test.play_for(900ms);
    CHECK(test.music.markers_heard.empty());
    test.play_for(200ms);
    CHECK(test.music.markers_heard == std::vector<std::string>{"music:1"});
    test.play_for(1100ms);
    CHECK(test.music.markers_heard == std::vector<std::string>{"music:1", "music:2"});
    CHECK(music_is_continuous(test.heard()));
}

TEST_CASE("a track end taken back by speech is still reported, after the speech", "[voice]") {
    fixture test;
    test.music.track_samples = 1000 * per_ms;
    test.music.tracks = 2;
    test.mixer.wake(guild);
    test.play_for(500ms); // the end marker is queued, half a second away

    REQUIRE(test.mixer.play(guild, speech(200ms), "tts:1"));
    test.play_for(200ms);
    CHECK(test.music.markers_heard.empty());
    test.play_for(600ms);
    CHECK(test.music.markers_heard == std::vector<std::string>{"music:1"});
    CHECK(music_is_continuous(test.heard()));
}

TEST_CASE("pausing music stops it at once, and resuming loses nothing", "[voice]") {
    fixture test;
    test.mixer.wake(guild);
    test.play_for(700ms);

    test.mixer.hold_music(guild);
    CHECK(test.connection.remaining(guild) == 0ms);
    test.mixer.tick();
    CHECK(test.connection.remaining(guild) == 0ms);
    CHECK(test.mixer.music_unplayed(guild) == 2300 * per_ms);

    test.mixer.release_music(guild);
    test.play_for(4000ms);
    CHECK(music_is_continuous(test.heard()));
}

TEST_CASE("dropping music clears it, but never speech", "[voice]") {
    fixture test;
    test.mixer.wake(guild);
    test.play_for(100ms);

    SECTION("music playing") {
        test.mixer.drop_music(guild);
        CHECK(test.connection.remaining(guild) == 0ms);
        CHECK(test.mixer.music_unplayed(guild) == 0);
    }
    SECTION("speech playing over paused music") {
        REQUIRE(test.mixer.play(guild, speech(500ms), "tts:1"));
        test.mixer.drop_music(guild);
        CHECK(test.connection.remaining(guild) == 500ms);
        CHECK(test.connection.queued[guild] == std::vector<std::string>{"tts:1"});
    }
}

TEST_CASE("a new connection sends a track's end marker again", "[voice]") {
    fixture test;
    test.music.track_samples = 1000 * per_ms;
    test.music.tracks = 1;
    test.mixer.wake(guild);

    // The bot moves: the old connection and its queue are gone.
    test.connection.buffer[guild].clear();
    test.connection.queued[guild].clear();
    test.mixer.on_ready(guild);

    test.play_for(100ms);
    CHECK(test.music.markers_heard == std::vector<std::string>{"music:1"});
}

TEST_CASE("an idle source is no longer visited until woken", "[voice]") {
    fixture test;
    test.music.tracks = 0;
    test.mixer.wake(guild);
    CHECK(test.connection.plays.empty());

    test.music.tracks = 1;
    test.mixer.tick();
    CHECK(test.connection.plays.empty());
    test.mixer.wake(guild);
    CHECK_FALSE(test.connection.plays.empty());
}

TEST_CASE("nothing is fed without a connection, or once the guild is forgotten", "[voice]") {
    fixture test;
    test.connection.connected[guild] = false;
    test.mixer.wake(guild);
    CHECK(test.connection.plays.empty());

    test.connection.connected[guild] = true;
    test.mixer.on_ready(guild);
    CHECK_FALSE(test.connection.plays.empty());

    test.mixer.forget(guild);
    const auto before = test.connection.plays.size();
    test.mixer.tick();
    CHECK(test.connection.plays.size() == before);
}
