// What /speak and /tts decide (plan §12.6, §12.7).

#include "core/commands/speak.hpp"
#include "core/commands/voice.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/voice_sessions.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

using namespace std::chrono_literals;
using latibot::commands::may_stop_speech;
using latibot::commands::plan_speak;
using latibot::commands::speak_refusal;
using latibot::commands::speak_route;
using latibot::commands::speech_limits;
using latibot::commands::speech_limits_for;

namespace {

constexpr dpp::snowflake guild{100};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

struct settings_fixture {
    latibot::db::database db{std::filesystem::path(latibot::db::database::in_memory)};
    latibot::config::guild_settings settings{db};

    settings_fixture() { latibot::db::migrate(db); }
};

} // namespace

TEST_CASE("speech goes where the bot is, or joins whoever asked", "[commands]") {
    const dpp::snowflake bot_channel{1};
    const dpp::snowflake caller_channel{2};

    CHECK(plan_speak(bot_channel, caller_channel).route == speak_route::bot_channel);
    CHECK(plan_speak(bot_channel, caller_channel).channel == bot_channel);
    CHECK(plan_speak(bot_channel, {}).route == speak_route::bot_channel);

    CHECK(plan_speak({}, caller_channel).route == speak_route::join_caller);
    CHECK(plan_speak({}, caller_channel).channel == caller_channel);

    CHECK(plan_speak({}, {}).route == speak_route::nowhere);
}

TEST_CASE("speech refuses blank text and text over the guild's limit", "[commands]") {
    const speech_limits limits{.max_characters = 10};

    CHECK(speak_refusal("hello", limits) == std::nullopt);
    CHECK(speak_refusal("0123456789", limits) == std::nullopt);
    CHECK(speak_refusal("   ", limits) == "there's nothing to say");
    CHECK(speak_refusal("01234567890", limits) == "that's 11 characters; this server's limit is 10");

    // Counted in characters, not bytes.
    CHECK(speak_refusal("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", limits) == std::nullopt);
}

TEST_CASE("speech limits default, are per guild, and are clamped", "[commands]") {
    settings_fixture test;

    const speech_limits defaults = speech_limits_for(test.settings, guild);
    CHECK(defaults.max_characters == 1000);
    CHECK(defaults.max_duration == 60s);

    test.settings.set_int(guild, latibot::commands::tts_max_characters_key, 250);
    test.settings.set_int(guild, latibot::commands::tts_max_seconds_key, 99999);
    const speech_limits changed = speech_limits_for(test.settings, guild);
    CHECK(changed.max_characters == 250);
    CHECK(changed.max_duration == 600s);

    CHECK(speech_limits_for(test.settings, dpp::snowflake{999}).max_characters == 1000);
}

TEST_CASE("speech is stopped by whoever asked for it, an admin or a trusted user", "[commands]") {
    CHECK(may_stop_speech(alice, alice, false, false));
    CHECK_FALSE(may_stop_speech(bob, alice, false, false));
    CHECK(may_stop_speech(bob, alice, true, false));
    CHECK(may_stop_speech(bob, alice, false, true));

    // With nothing playing, only admins and trusted users.
    CHECK_FALSE(may_stop_speech(alice, std::nullopt, false, false));
    CHECK(may_stop_speech(alice, std::nullopt, false, true));
}

TEST_CASE("the voice grace defaults to 30 seconds and is clamped", "[commands]") {
    settings_fixture test;

    CHECK(latibot::commands::voice_grace_for(test.settings, guild) == 30s);

    test.settings.set_int(guild, latibot::events::voice_grace_key, 5);
    CHECK(latibot::commands::voice_grace_for(test.settings, guild) == 5s);

    test.settings.set_int(guild, latibot::events::voice_grace_key, -3);
    CHECK(latibot::commands::voice_grace_for(test.settings, guild) == 0s);
}
