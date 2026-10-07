// Playing a server's queue through the mixer (src/modules/music/docs/Music.md §4.3),
// against scripted tracks and a mock connection.

#include "music_player.hpp"
#include "voice/voice_mixer.hpp"

#include "mock_media.hpp"
#include "mocks/mock_voice.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

using namespace latibot::music;
using latibot::audio::voice_mixer;
using latibot::testing::mock_opener;
using latibot::testing::mock_voice;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{100};
constexpr dpp::snowflake other_guild{200};
constexpr dpp::snowflake channel{300};

constexpr std::size_t per_second = 96000;

auto song(const std::string& name, std::optional<std::chrono::seconds> length = std::nullopt) -> track {
    track entry;
    entry.media.title = name;
    entry.media.url = "https://x.com/" + name;
    entry.media.duration = length;
    entry.channel = channel;
    return entry;
}

struct fixture {
    mock_voice connection;
    voice_mixer mixer{connection};
    mock_opener opener;
    std::vector<std::pair<dpp::snowflake, std::string>> notes;
    int volume = 100;
    std::optional<std::chrono::seconds> limit;
    music_player player{
        opener, mixer,
        player_options{.volume_percent = [this](dpp::snowflake) { return volume; },
                       .track_limit = [this](dpp::snowflake) { return limit; },
                       .notify = [this](dpp::snowflake where, std::string text) { notes.emplace_back(where, std::move(text)); }}};

    fixture() {
        connection.connected[guild] = true;
        connection.connected[other_guild] = true;
        mixer.set_music(&player);
    }

    /// A track `seconds` long, all samples at `level`.
    auto script(const std::string& name, double seconds, std::int16_t level = 1000) -> void {
        opener.tracks["https://x.com/" + name] = {
            .samples = static_cast<std::size_t>(seconds * per_second), .level = level, .fails_with = {}, .held = false};
    }

    /// Plays `span`, a second at a time with the mixer's tick between, as
    /// the bot does, reporting markers as DPP would.
    auto play_for(std::chrono::seconds span, dpp::snowflake where = guild) -> void {
        for (std::chrono::seconds done{0}; done < span; ++done) {
            mixer.tick();
            for (const std::string& marker : connection.advance(where, 1000ms)) {
                mixer.on_marker(where, marker);
            }
        }
    }

    [[nodiscard]] auto current_title(dpp::snowflake where = guild) -> std::string {
        const auto status = player.status(where);
        return status.current ? status.current->media.title : "";
    }
};

} // namespace

TEST_CASE("adding to a quiet server starts playing", "[music]") {
    fixture test;
    test.script("a", 5);
    const auto result = test.player.add(guild, {song("a")}, queue_position::end);
    CHECK(result.added == 1);
    CHECK(test.current_title() == "a");
    CHECK(test.connection.remaining(guild) == 3000ms);
}

TEST_CASE("the queue plays in order, moving on when each track has been heard", "[music]") {
    fixture test;
    test.script("a", 2.5);
    test.script("b", 2);
    test.player.add(guild, {song("a"), song("b")}, queue_position::end);

    test.play_for(2s);
    CHECK(test.current_title() == "a");
    // "b" is fetched while the end of "a" is still playing.
    CHECK(test.opener.times_opened("https://x.com/b") == 1);
    test.play_for(1s);
    CHECK(test.current_title() == "b");
    test.play_for(3s);
    CHECK(test.current_title().empty());
    CHECK(test.opener.times_opened("https://x.com/b") == 1);
}

TEST_CASE("repeating a track plays it again, from a fresh fetch", "[music]") {
    fixture test;
    test.script("a", 1);
    test.script("b", 1);
    test.player.add(guild, {song("a"), song("b")}, queue_position::end);
    test.player.set_repeat(guild, repeat_mode::track);

    test.play_for(4s);
    CHECK(test.current_title() == "a");
    CHECK(test.opener.times_opened("https://x.com/a") >= 3);
}

TEST_CASE("skip moves on with repeat on", "[music]") {
    fixture test;
    test.script("a", 10);
    test.script("b", 10);
    test.player.add(guild, {song("a"), song("b")}, queue_position::end);
    test.player.set_repeat(guild, repeat_mode::track);
    test.play_for(1s);

    const auto skipped = test.player.skip(guild);
    REQUIRE(skipped.has_value());
    CHECK(skipped->media.title == "a");
    CHECK(test.current_title() == "b");
    // What was queued of "a" went with it.
    test.play_for(1s);
    CHECK(test.connection.heard[guild].size() == 2 * per_second);
}

TEST_CASE("a track that cannot be fetched is noted, and the next one plays", "[music]") {
    fixture test;
    test.opener.tracks["https://x.com/broken"] = {.samples = 0, .level = 0, .fails_with = "Video unavailable", .held = false};
    test.script("b", 2);
    test.player.add(guild, {song("broken"), song("b")}, queue_position::end);
    test.player.set_repeat(guild, repeat_mode::track);

    test.play_for(1s);
    CHECK(test.current_title() == "b");
    REQUIRE(test.notes.size() == 1);
    CHECK(test.notes[0].first == channel);
    CHECK(test.notes[0].second == "couldn't play **broken**: Video unavailable");
    CHECK(test.opener.times_opened("https://x.com/broken") == 1);
}

TEST_CASE("a track that breaks mid-way plays what it had, then moves on, never repeating", "[music]") {
    fixture test;
    test.opener.tracks["https://x.com/flaky"] = {.samples = per_second, .level = 1000, .fails_with = "HTTP Error 403", .held = false};
    test.script("b", 2);
    test.player.add(guild, {song("flaky"), song("b")}, queue_position::end);
    test.player.set_repeat(guild, repeat_mode::track);

    test.play_for(2s);
    CHECK(test.current_title() == "b");
    CHECK(test.opener.times_opened("https://x.com/flaky") == 1);
    REQUIRE(test.notes.size() == 1);
}

TEST_CASE("play now plays at once, and the interrupted track starts over after it", "[music]") {
    fixture test;
    test.script("a", 10);
    test.script("urgent", 1);
    test.player.add(guild, {song("a")}, queue_position::end);
    test.play_for(3s);

    const auto result = test.player.add(guild, {song("urgent")}, queue_position::now);
    CHECK(result.replaces_current);
    CHECK(test.current_title() == "urgent");

    test.play_for(2s);
    CHECK(test.current_title() == "a");
    CHECK(test.opener.times_opened("https://x.com/a") == 2);
    CHECK(test.player.status(guild).elapsed < 2s);
}

TEST_CASE("pausing holds the music, and time into the track with it", "[music]") {
    fixture test;
    test.script("a", 30);
    test.player.add(guild, {song("a")}, queue_position::end);
    test.play_for(4s);
    CHECK(test.player.status(guild).elapsed == 4s);

    CHECK(test.player.toggle_pause(guild) == true);
    CHECK(test.player.status(guild).paused);
    test.play_for(5s);
    CHECK(test.player.status(guild).elapsed == 4s);

    CHECK(test.player.toggle_pause(guild) == false);
    test.play_for(2s);
    CHECK(test.player.status(guild).elapsed == 6s);
}

TEST_CASE("pause, skip and stop with nothing playing say so", "[music]") {
    fixture test;
    CHECK_FALSE(test.player.toggle_pause(guild).has_value());
    CHECK_FALSE(test.player.skip(guild).has_value());
    CHECK(test.player.stop(guild) == 0);
}

TEST_CASE("stop ends the music and empties the queue", "[music]") {
    fixture test;
    test.script("a", 10);
    test.script("b", 10);
    test.player.add(guild, {song("a"), song("b")}, queue_position::end);
    test.play_for(1s);

    CHECK(test.player.stop(guild) == 2);
    CHECK(test.connection.remaining(guild) == 0ms);
    CHECK(test.current_title().empty());
    test.play_for(1s);
    CHECK(test.connection.remaining(guild) == 0ms);
}

TEST_CASE("clear, shuffle and remove work on what is queued", "[music]") {
    fixture test;
    test.script("a", 10);
    test.player.add(guild, {song("a"), song("b"), song("c"), song("d")}, queue_position::end);

    CHECK(test.player.remove(guild, 2)->media.title == "c");
    CHECK(test.player.shuffle(guild) == 2);
    CHECK(test.player.clear(guild) == 2);
    CHECK(test.current_title() == "a");
    CHECK(test.player.status(guild).upcoming.empty());
}

TEST_CASE("the track limit cuts a long track off, with a note, but not a live stream", "[music]") {
    fixture test;
    test.limit = 2s;
    test.script("long", 10);
    test.script("radio", 10);
    track radio = song("radio");
    radio.media.live = true;
    test.player.add(guild, {song("long"), radio}, queue_position::end);

    test.play_for(3s);
    CHECK(test.current_title() == "radio");
    REQUIRE(test.notes.size() == 1);
    CHECK(test.notes[0].second.contains("limit"));

    test.play_for(4s);
    CHECK(test.current_title() == "radio");
}

TEST_CASE("the volume scales the samples", "[music]") {
    fixture test;
    test.volume = 50;
    test.script("a", 5, 1000);
    test.player.add(guild, {song("a")}, queue_position::end);
    test.play_for(1s);
    REQUIRE_FALSE(test.connection.heard[guild].empty());
    CHECK(test.connection.heard[guild].front() == 500);

    test.volume = 200;
    test.player.volume_changed(guild);
    test.play_for(4s);
    CHECK(test.connection.heard[guild].back() == 2000);
}

TEST_CASE("each server has its own queue", "[music]") {
    // The Java bot had one queue for the whole bot.
    fixture test;
    test.script("a", 10);
    test.script("b", 10);
    test.player.add(guild, {song("a")}, queue_position::end);
    test.player.add(other_guild, {song("b")}, queue_position::end);
    CHECK(test.current_title(guild) == "a");
    CHECK(test.current_title(other_guild) == "b");

    test.player.stop(guild);
    CHECK(test.current_title(other_guild) == "b");
}

TEST_CASE("leaving forgets the queue", "[music]") {
    // The Java bot's queue outlived leaving, and the next /play added to it.
    fixture test;
    test.script("a", 10);
    test.player.add(guild, {song("a"), song("b")}, queue_position::end);
    test.player.forget(guild);
    test.mixer.forget(guild);
    CHECK(test.current_title().empty());
    CHECK(test.player.status(guild).upcoming.empty());
}

TEST_CASE("speech pauses the music, which carries on after it", "[music]") {
    fixture test;
    test.script("a", 10, 1000);
    test.player.add(guild, {song("a")}, queue_position::end);
    test.play_for(2s);

    const std::vector<std::int16_t> speech(per_second / 2, -7);
    REQUIRE(test.mixer.play(guild, speech, "tts:1"));
    test.play_for(3s);

    const auto& heard = test.connection.heard[guild];
    CHECK(heard[2 * per_second] == -7);
    CHECK(heard[(2 * per_second) + (per_second / 2)] == 1000);
    CHECK(test.player.status(guild).elapsed == 4s);
}

TEST_CASE("markers are told apart", "[music]") {
    CHECK(music_marker(12) == "music:12");
    CHECK(playback_in("music:12") == 12);
    CHECK_FALSE(playback_in("tts:12").has_value());
    CHECK_FALSE(playback_in("music:x").has_value());
}
