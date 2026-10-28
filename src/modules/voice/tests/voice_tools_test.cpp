// The language model's join_voice and leave_voice
// (src/modules/voice/docs/Voice_Channels.md §2.4).

#include "voice_tools.hpp"

#include "mocks/mock_model_tools.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/permissions.h>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake text_channel{2000};
constexpr dpp::snowflake lounge{3001};
constexpr dpp::snowflake gaming{3002};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

struct fixture {
    latibot::testing::mock_model_tools tools;
    std::map<dpp::snowflake, dpp::snowflake> in_voice;
    dpp::snowflake bot_in;
    bool reachable = true;
    std::vector<dpp::snowflake> connected;
    int left = 0;

    fixture() {
        latibot::commands::add_voice_tools(tools, {.channel_of =
                                                       [this](dpp::snowflake, dpp::snowflake user) {
                                                           const auto found = in_voice.find(user);
                                                           return found == in_voice.end() ? dpp::snowflake{} : found->second;
                                                       },
                                                   .bot_channel = [this](dpp::snowflake) { return bot_in; },
                                                   .connect =
                                                       [this](dpp::snowflake, dpp::snowflake channel) {
                                                           if (reachable) connected.push_back(channel);
                                                           return reachable;
                                                       },
                                                   .disconnect =
                                                       [this](dpp::snowflake) {
                                                           if (reachable) ++left;
                                                           return reachable;
                                                       }});
        tools.aliases = {{"ualice", alice}, {"ubob", bob}};
    }

    auto as(dpp::snowflake who, std::uint64_t permissions = dpp::p_speak) -> latibot::capabilities::model_tool_context {
        return tools.context(guild, text_channel, who, permissions);
    }
};

} // namespace

TEST_CASE("join_voice joins the asker's channel, or someone else's, or moves there", "[voice]") {
    fixture test;
    test.in_voice = {{alice, lounge}, {bob, gaming}};

    CHECK(test.tools.run("join_voice", nlohmann::json::object(), test.as(alice)).content == "joining ualice's voice channel");
    test.bot_in = lounge;
    CHECK(test.tools.run("join_voice", nlohmann::json::object(), test.as(alice)).content.contains("already"));
    CHECK(test.tools.run("join_voice", {{"user", "ubob"}}, test.as(alice)).content == "moving to ubob's voice channel");
    CHECK(test.connected == std::vector{lounge, gaming});
}

TEST_CASE("join_voice refuses someone not in voice, an unknown alias, and an asker without Speak", "[voice]") {
    fixture test;
    test.in_voice = {{bob, gaming}};
    CHECK(test.tools.run("join_voice", nlohmann::json::object(), test.as(alice)).is_error);
    CHECK(test.tools.run("join_voice", {{"user", "unobody"}}, test.as(alice)).is_error);
    CHECK(test.tools.run("join_voice", {{"user", "ubob"}}, test.as(alice, dpp::p_send_messages)).is_error);
    test.reachable = false;
    CHECK(test.tools.run("join_voice", {{"user", "ubob"}}, test.as(alice)).is_error);
    CHECK(test.connected.empty());
}

TEST_CASE("leave_voice leaves, when the bot is in voice", "[voice]") {
    fixture test;
    CHECK(test.tools.run("leave_voice", nlohmann::json::object(), test.as(alice)).is_error);
    test.bot_in = lounge;
    CHECK(test.tools.run("leave_voice", nlohmann::json::object(), test.as(alice, dpp::p_send_messages)).is_error);
    CHECK_FALSE(test.tools.run("leave_voice", nlohmann::json::object(), test.as(alice)).is_error);
    CHECK(test.left == 1);
}
