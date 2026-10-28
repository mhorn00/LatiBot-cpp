// The speech capability as DECtalk offers it (src/modules/llm/docs/Language_Model.md
// §2.5): what the language model's spoken replies go through.

#include "dectalk_speech.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "speak_command.hpp"
#include "speech_queue.hpp"
#include "voice/voice_sessions.hpp"

#include "mock_tts.hpp"
#include "mocks/mock_voice.hpp"
#include "support/schema.hpp"
#include "voice_params.hpp"
#include "voice_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

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
    latibot::audio::voice_store voices{db};
    latibot::audio::dectalk_speech speech{tts, queue, sessions, settings, &voices};

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::audio::dectalk_schema());
    }
};

} // namespace

TEST_CASE("replies are spoken only in a voice session's text channel", "[dectalk]") {
    fixture test;
    CHECK_FALSE(test.speech.speaks_in(guild, channel));

    test.sessions.start({.guild_id = guild, .voice_channel = dpp::snowflake{8000}, .text_channel = channel, .started_by = alice});
    CHECK(test.speech.speaks_in(guild, channel));
    CHECK_FALSE(test.speech.speaks_in(guild, dpp::snowflake{201}));
    CHECK_FALSE(test.speech.speaks_in(dpp::snowflake{101}, channel));
}

TEST_CASE("the model's reply is sanitized at the model's trust level", "[dectalk]") {
    const fixture test;
    const std::string said = test.speech.prepare_for_model(R"([:play "C:\x.wav"][:rate 250]ahoy there)", guild);
    CHECK_FALSE(said.contains("play"));
    CHECK(said.contains("[:rate 250]"));
    CHECK(said.contains("ahoy there"));
}

TEST_CASE("a spoken reply is synthesized and queued under whoever asked", "[dectalk][coro]") {
    fixture test;
    test.voice.connected[guild] = true;

    test.speech.say(guild, alice, "ahoy there", {}).sync_wait_for(2s);
    REQUIRE(test.tts.requests.size() == 1);
    CHECK(test.tts.requests[0].text == "ahoy there");
    CHECK(test.voice.plays.size() == 1);
    CHECK(test.queue.current_owner(guild) == alice);
}

TEST_CASE("a spoken reply is said in the voice asked for, or Paul's", "[dectalk][coro]") {
    fixture test;
    test.voice.connected[guild] = true;
    latibot::audio::custom_voice deep{.base = "harry", .edits = {}};
    REQUIRE(deep.set("ap", 90));
    test.voices.save(guild, {.name = "deep", .voice = deep, .created_by = alice, .updated_at = {}});

    test.speech.say(guild, alice, "one", "betty").sync_wait_for(2s);
    test.speech.say(guild, alice, "two", "deep").sync_wait_for(2s);
    test.speech.say(guild, alice, "three", "deleted-since").sync_wait_for(2s);
    test.speech.say(guild, alice, "four", {}).sync_wait_for(2s);

    REQUIRE(test.tts.requests.size() == 4);
    CHECK(test.tts.requests[0].voice.voice == "betty");
    CHECK(test.tts.requests[1].voice.voice == "harry");
    CHECK(test.tts.requests[1].voice.custom_params.contains("ap 90"));
    CHECK(test.tts.requests[2].voice.voice == "paul");
    CHECK(test.tts.requests[2].voice.custom_params.empty());
    CHECK(test.tts.requests[3].voice.voice == "paul");
}

TEST_CASE("the voices offered are the built-in ones, Paul first, then the server's", "[dectalk]") {
    fixture test;
    test.voices.save(guild, {.name = "deep", .voice = {}, .created_by = alice, .updated_at = {}});

    const std::vector<std::string> names = test.speech.voices(guild);
    REQUIRE(names.size() == latibot::audio::builtin_voices().size() + 1);
    CHECK(names.front() == "paul");
    CHECK(names.back() == "deep");
    CHECK(test.speech.voices(dpp::snowflake{101}).size() == latibot::audio::builtin_voices().size());
}

TEST_CASE("a spoken reply keeps to the server's limits", "[dectalk][coro]") {
    fixture test;
    test.voice.connected[guild] = true;
    test.settings.set_int(guild, latibot::commands::tts_max_characters_key, 5);

    test.speech.say(guild, alice, "ahoy there", {}).sync_wait_for(2s);
    REQUIRE(test.tts.requests.size() == 1);
    CHECK(test.tts.requests[0].text.size() <= 5);
}

TEST_CASE("the model is told the inline commands and every built-in voice", "[dectalk]") {
    const fixture test;
    const std::string guide = test.speech.guide_for_model();
    CHECK(guide.starts_with("## Speaking"));
    CHECK(guide.contains("[:rate 120]"));
    CHECK(guide.contains("[:np] paul"));
}
