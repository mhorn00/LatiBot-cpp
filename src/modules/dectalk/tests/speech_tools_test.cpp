// The language model's speech tools (src/modules/dectalk/docs/Speech.md §2.6).

#include "speech_tools.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "speech_queue.hpp"
#include "voice_store.hpp"

#include "mock_tts.hpp"
#include "mocks/mock_model_tools.hpp"
#include "mocks/mock_voice.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/permissions.h>

#include <vector>

namespace {

constexpr dpp::snowflake guild{100};
constexpr dpp::snowflake text_channel{200};
constexpr dpp::snowflake lounge{300};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::config::guild_settings settings{db};
    latibot::config::bootstrap bootstrap;
    latibot::audio::voice_store voices{db};
    latibot::testing::mock_tts tts;
    latibot::testing::mock_voice voice;
    latibot::audio::speech_queue queue{voice};
    latibot::testing::mock_model_tools tools;

    dpp::snowflake bot_in;
    dpp::snowflake alice_in = lounge;
    std::vector<dpp::snowflake> joined;
    std::vector<latibot::discord::voice_message> posted;

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::audio::dectalk_schema());
        voice.connected[guild] = true;
        latibot::commands::add_speech_tools(
            tools, {.speech = {.engine = &tts, .queue = &queue, .settings = &settings, .bootstrap = &bootstrap, .voices = &voices},
                    .channel_of = [this](dpp::snowflake, dpp::snowflake user) { return user == alice ? alice_in : dpp::snowflake{}; },
                    .bot_channel = [this](dpp::snowflake) { return bot_in; },
                    .connect =
                        [this](dpp::snowflake, dpp::snowflake channel) {
                            joined.push_back(channel);
                            return true;
                        },
                    .post_voice = [this](dpp::snowflake,
                                         latibot::discord::voice_message message) -> dpp::task<latibot::ports::result<dpp::snowflake>> {
                        posted.push_back(std::move(message));
                        co_return dpp::snowflake{77};
                    }});
    }

    auto as(dpp::snowflake who, std::uint64_t permissions = dpp::p_send_messages) -> latibot::capabilities::model_tool_context {
        return tools.context(guild, text_channel, who, permissions);
    }
};

} // namespace

TEST_CASE("speak joins whoever asked when the bot is not in voice, and queues it under them", "[dectalk]") {
    fixture test;
    const auto said = test.tools.run("speak", {{"text", "hello there"}, {"voice", "betty"}, {"rate", 250}}, test.as(alice));
    REQUIRE_FALSE(said.is_error);
    CHECK(said.content.starts_with("saying it"));
    CHECK(test.joined == std::vector{lounge});
    REQUIRE(test.tts.requests.size() == 1);
    CHECK(test.tts.requests[0].text == "hello there");
    CHECK(test.tts.requests[0].voice.voice == "betty");
    CHECK(test.tts.requests[0].voice.rate == 250);
    CHECK(test.queue.current_owner(guild) == alice);
}

TEST_CASE("speak is sanitized at the model's trust level, whoever asked", "[dectalk]") {
    fixture test;
    test.bootstrap.trusted_users = {alice};
    test.bot_in = lounge;
    test.tools.run("speak", {{"text", R"([:play "C:\x.wav"][:rate 250]ahoy)"}}, test.as(alice, dpp::p_administrator));
    REQUIRE(test.tts.requests.size() == 1);
    CHECK_FALSE(test.tts.requests[0].text.contains("play"));
    CHECK(test.tts.requests[0].text.contains("ahoy"));
    CHECK(test.joined.empty());
}

TEST_CASE("speak refuses when nobody is in voice, an unknown voice, or text past the limit", "[dectalk]") {
    fixture test;
    CHECK(test.tools.run("speak", {{"text", "hi"}}, test.as(bob)).is_error);
    CHECK(test.tools.run("speak", {{"text", "hi"}, {"voice", "chris"}}, test.as(alice)).is_error);
    CHECK(test.tools.run("speak", {{"text", std::string(1001, 'a')}}, test.as(alice)).is_error);
    CHECK(test.tts.requests.empty());
}

TEST_CASE("voice_message posts DECtalk's voice message in the channel", "[dectalk]") {
    fixture test;
    const auto sent = test.tools.run("voice_message", {{"text", "hello"}}, test.as(bob));
    REQUIRE_FALSE(sent.is_error);
    REQUIRE(test.posted.size() == 1);
    CHECK(test.posted[0].mimetype == "audio/wav");
    CHECK(test.posted[0].file.starts_with("RIFF"));
    CHECK_FALSE(test.posted[0].waveform.empty());
}

TEST_CASE("stop_speech is for whoever asked, or an admin", "[dectalk]") {
    fixture test;
    CHECK(test.tools.run("stop_speech", nlohmann::json::object(), test.as(alice)).is_error);

    test.tools.run("speak", {{"text", "a long thing to say"}}, test.as(alice));
    CHECK(test.tools.run("stop_speech", nlohmann::json::object(), test.as(bob)).is_error);
    CHECK_FALSE(test.tools.run("stop_speech", {{"skip", true}}, test.as(alice)).is_error);

    test.tools.run("speak", {{"text", "another"}}, test.as(alice));
    CHECK_FALSE(test.tools.run("stop_speech", nlohmann::json::object(), test.as(bob, dpp::p_administrator)).is_error);
    CHECK(test.queue.size(guild) == 0);
}
