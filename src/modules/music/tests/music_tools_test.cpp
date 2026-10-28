// The language model's /music (src/modules/music/docs/Music.md §3.6).

#include "music_tools.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "music_player.hpp"
#include "voice/voice_mixer.hpp"

#include "mock_media.hpp"
#include "mocks/mock_model_tools.hpp"
#include "mocks/mock_voice.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/permissions.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{100};
constexpr dpp::snowflake text_channel{300};
constexpr dpp::snowflake lounge{400};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

auto item(const std::string& name, std::chrono::seconds length = 180s) -> latibot::ports::media_item {
    return {.title = name, .url = "https://x.com/" + name, .uploader = {}, .duration = length, .live = false};
}

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::config::guild_settings settings{db};
    latibot::testing::mock_voice connection;
    latibot::audio::voice_mixer mixer{connection};
    latibot::testing::mock_opener opener;
    latibot::testing::mock_resolver resolver;
    latibot::music::music_player player{opener, mixer,
                                        latibot::music::player_options{.volume_percent = [](dpp::snowflake) { return 100; },
                                                                       .track_limit = [](dpp::snowflake) { return std::nullopt; },
                                                                       .notify = [](dpp::snowflake, std::string) {}}};
    latibot::testing::mock_model_tools tools;
    dpp::snowflake bot_in;
    std::vector<dpp::snowflake> joined;

    fixture() {
        latibot::testing::create_schema(db);
        connection.connected[guild] = true;
        mixer.set_music(&player);
        // Long enough never to finish in a test.
        for (const char* name : {"one", "two"}) {
            opener.tracks[std::string("https://x.com/") + name] = {.samples = 96000 * 600, .level = 1000, .fails_with = {}, .held = false};
        }
        resolver.answers["https://x.com/one"] =
            latibot::ports::media_lookup{.items = {item("one")}, .playlist_title = {}, .playlist_size = 1};
        resolver.answers["https://x.com/two"] =
            latibot::ports::media_lookup{.items = {item("two")}, .playlist_title = {}, .playlist_size = 1};
        tools.aliases = {{"ualice", alice}, {"ubob", bob}};
        latibot::commands::add_music_tools(
            tools, {.music = {.player = &player, .resolver = &resolver, .settings = &settings, .unavailable = {}},
                    .channel_of = [](dpp::snowflake, dpp::snowflake user) { return user == alice ? lounge : dpp::snowflake{}; },
                    .bot_channel = [this](dpp::snowflake) { return bot_in; },
                    .connect =
                        [this](dpp::snowflake, dpp::snowflake channel) {
                            joined.push_back(channel);
                            bot_in = channel;
                            return true;
                        }});
    }

    auto as(dpp::snowflake who, std::uint64_t permissions = dpp::p_speak) -> latibot::capabilities::model_tool_context {
        return tools.context(guild, text_channel, who, permissions);
    }
};

} // namespace

TEST_CASE("play_music queues a link, joining whoever asked, and music_queue shows it", "[music]") {
    fixture test;
    REQUIRE_FALSE(test.tools.run("play_music", {{"link", "https://x.com/one"}}, test.as(alice)).is_error);
    CHECK(test.joined == std::vector{lounge});
    REQUIRE_FALSE(test.tools.run("play_music", {{"link", "https://x.com/two"}}, test.as(bob)).is_error);

    const auto queue = test.tools.run("music_queue", nlohmann::json::object(), test.as(bob, 0));
    CHECK(queue.content.contains("Playing: \"one\", 3:00, queued by ualice"));
    CHECK(queue.content.contains("1. \"two\", 3:00, queued by ubob"));
}

TEST_CASE("play_music takes links only, needs someone in voice, and Speak", "[music]") {
    fixture test;
    CHECK(test.tools.run("play_music", {{"link", "some lofi"}}, test.as(alice)).is_error);
    CHECK(test.tools.run("play_music", {{"link", "https://x.com/one"}}, test.as(bob)).is_error);
    CHECK(test.tools.run("play_music", {{"link", "https://x.com/one"}}, test.as(alice, dpp::p_send_messages)).is_error);
    CHECK(test.tools.run("play_music", {{"link", "https://x.com/nothing"}}, test.as(alice)).is_error);
    CHECK_FALSE(test.player.status(guild).current);
}

TEST_CASE("music_control pauses, resumes, skips, repeats and stops", "[music]") {
    fixture test;
    CHECK(test.tools.run("music_control", {{"action", "skip"}}, test.as(alice)).is_error);
    test.tools.run("play_music", {{"link", "https://x.com/one"}}, test.as(alice));
    test.tools.run("play_music", {{"link", "https://x.com/two"}}, test.as(alice));

    CHECK(test.tools.run("music_control", {{"action", "pause"}}, test.as(alice)).content == "paused");
    CHECK(test.tools.run("music_control", {{"action", "pause"}}, test.as(alice)).content.contains("already"));
    CHECK(test.tools.run("music_control", {{"action", "resume"}}, test.as(alice)).content == "resumed");
    CHECK(test.tools.run("music_control", {{"action", "repeat"}, {"mode", "queue"}}, test.as(alice)).content == "repeat is queue");
    CHECK(test.tools.run("music_control", {{"action", "skip"}}, test.as(alice)).content.contains("now playing \"two\""));
    CHECK(test.tools.run("music_control", {{"action", "dance"}}, test.as(alice)).is_error);
    CHECK(test.tools.run("music_control", {{"action", "stop"}}, test.as(alice, 0)).is_error);
    CHECK_FALSE(test.tools.run("music_control", {{"action", "stop"}}, test.as(alice)).is_error);
    CHECK_FALSE(test.player.status(guild).current);
}
