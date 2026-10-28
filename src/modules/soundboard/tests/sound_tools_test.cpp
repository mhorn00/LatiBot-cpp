// The language model's play_sound (src/modules/soundboard/docs/Soundboard.md §5),
// and the body that plays a sound in voice (§4).

#include "sound_tools.hpp"
#include "clips.hpp"
#include "sounds.hpp"

#include "mocks/mock_model_tools.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/permissions.h>

#include <vector>

namespace {

constexpr dpp::snowflake guild{100};
constexpr dpp::snowflake text_channel{200};
constexpr dpp::snowflake lounge{300};
constexpr dpp::snowflake alice{11};

/// Makes every sound a one-byte voice message.
class fake_clips final : public latibot::soundboard::clip_maker {
public:
    auto make(latibot::soundboard::sound wanted) -> dpp::task<latibot::ports::result<latibot::discord::voice_message>> override {
        co_return latibot::discord::voice_message{
            .file = "x", .filename = wanted.name + ".ogg", .mimetype = "audio/ogg", .duration = {}, .waveform = {}};
    }
};

struct fixture {
    latibot::soundboard::sound_catalog sounds;
    fake_clips clips;
    latibot::testing::mock_model_tools tools;
    dpp::snowflake bot_in;
    std::vector<std::pair<dpp::snowflake, std::string>> played;
    std::vector<std::pair<dpp::snowflake, std::string>> posted;

    fixture() {
        sounds.set_defaults({{.id = dpp::snowflake{1}, .name = "airhorn", .guild_id = {}, .emoji = {}, .volume = 1.0}});
        latibot::commands::add_sound_tools(
            tools, {.sounds = &sounds,
                    .clips = &clips,
                    .http = nullptr,
                    .looked_in = {},
                    .bot_voice_channel = [this](dpp::snowflake) { return bot_in; },
                    .play_in_voice = [this](dpp::snowflake channel,
                                            const latibot::soundboard::sound& sound) -> dpp::task<latibot::ports::result<void>> {
                        played.emplace_back(channel, sound.name);
                        co_return latibot::ports::result<void>{};
                    },
                    .post_voice = [this](dpp::snowflake channel,
                                         latibot::discord::voice_message message) -> dpp::task<latibot::ports::result<dpp::snowflake>> {
                        posted.emplace_back(channel, message.filename);
                        co_return dpp::snowflake{5};
                    }});
    }

    auto run(nlohmann::json input, std::uint64_t permissions = dpp::p_use_soundboard) -> latibot::capabilities::model_tool_result {
        return tools.run("play_sound", std::move(input), tools.context(guild, text_channel, alice, permissions));
    }
};

} // namespace

TEST_CASE("play_sound plays in voice when the bot is there, and posts a voice message when it is not", "[soundboard]") {
    fixture test;
    CHECK(test.run({{"sound", "airhorn"}}).content.contains("voice message"));
    REQUIRE(test.posted.size() == 1);
    CHECK(test.posted[0].first == text_channel);

    test.bot_in = lounge;
    CHECK(test.run({{"sound", "airhorn"}}).content == "playing airhorn in the voice channel");
    CHECK(test.played == std::vector<std::pair<dpp::snowflake, std::string>>{{lounge, "airhorn"}});

    test.run({{"sound", "airhorn"}, {"as_message", true}});
    CHECK(test.posted.size() == 2);
}

TEST_CASE("play_sound refuses an unknown sound, and anyone without Use Soundboard", "[soundboard]") {
    fixture test;
    CHECK(test.run({{"sound", "kazoo"}}).is_error);
    CHECK(test.run({{"sound", "airhorn"}}, dpp::p_send_messages).is_error);
    CHECK(test.played.empty());
    CHECK(test.posted.empty());
}

TEST_CASE("a sound played in voice names the server it is from, unless it is Discord's own", "[soundboard]") {
    const auto own = latibot::soundboard::play_request({.id = dpp::snowflake{7}, .name = "a", .guild_id = {}, .emoji = {}, .volume = 1});
    CHECK(own.dump() == R"({"sound_id":"7"})");
    const auto theirs =
        latibot::soundboard::play_request({.id = dpp::snowflake{7}, .name = "a", .guild_id = dpp::snowflake{9}, .emoji = {}, .volume = 1});
    CHECK(theirs["source_guild_id"] == "9");
}
