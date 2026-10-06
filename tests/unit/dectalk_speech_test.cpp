// The speech capability as DECtalk offers it (docs/features/Language_Model.md
// §2.5): what the language model's spoken replies go through.

#include "core/audio/dectalk_speech.hpp"
#include "core/audio/speech_queue.hpp"
#include "core/commands/speak.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/voice_sessions.hpp"

#include "mocks/mock_tts.hpp"
#include "mocks/mock_voice.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{100};
constexpr dpp::snowflake channel{200};
constexpr dpp::snowflake alice{300};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::config::guild_settings settings{db};
    latibot::events::voice_sessions sessions;
    latibot::testing::mock_tts tts;
    latibot::testing::mock_voice voice;
    latibot::audio::speech_queue queue{voice};
    latibot::audio::dectalk_speech speech{tts, queue, sessions, settings};

    fixture() { latibot::db::migrate(db); }
};

} // namespace

TEST_CASE("replies are spoken only in a voice session's text channel", "[audio]") {
    fixture test;
    CHECK_FALSE(test.speech.speaks_in(guild, channel));

    test.sessions.start({.guild_id = guild, .voice_channel = dpp::snowflake{8000}, .text_channel = channel, .started_by = alice});
    CHECK(test.speech.speaks_in(guild, channel));
    CHECK_FALSE(test.speech.speaks_in(guild, dpp::snowflake{201}));
    CHECK_FALSE(test.speech.speaks_in(dpp::snowflake{101}, channel));
}

TEST_CASE("the model's reply is sanitized at the model's trust level", "[audio]") {
    const fixture test;
    const std::string said = test.speech.prepare_for_model(R"([:play "C:\x.wav"][:rate 250]ahoy there)", guild);
    CHECK_FALSE(said.contains("play"));
    CHECK(said.contains("[:rate 250]"));
    CHECK(said.contains("ahoy there"));
}

TEST_CASE("a spoken reply is synthesized and queued under whoever asked", "[audio][coro]") {
    fixture test;
    test.voice.connected[guild] = true;

    test.speech.say(guild, alice, "ahoy there").sync_wait_for(2s);
    REQUIRE(test.tts.requests.size() == 1);
    CHECK(test.tts.requests[0].text == "ahoy there");
    CHECK(test.voice.plays.size() == 1);
    CHECK(test.queue.current_owner(guild) == alice);
}

TEST_CASE("a spoken reply keeps to the server's limits", "[audio][coro]") {
    fixture test;
    test.voice.connected[guild] = true;
    test.settings.set_int(guild, latibot::commands::tts_max_characters_key, 5);

    test.speech.say(guild, alice, "ahoy there").sync_wait_for(2s);
    REQUIRE(test.tts.requests.size() == 1);
    CHECK(test.tts.requests[0].text.size() <= 5);
}

TEST_CASE("the model is told the inline commands and every built-in voice", "[audio]") {
    const fixture test;
    const std::string guide = test.speech.guide_for_model();
    CHECK(guide.starts_with("## Speaking"));
    CHECK(guide.contains("[:rate 120]"));
    CHECK(guide.contains("[:np] paul"));
}
