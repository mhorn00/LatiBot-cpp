// /soundboard: what it registers and who sees it
// (src/modules/soundboard/docs/Soundboard.md §3).

#include "soundboard_command.hpp"
#include "clips.hpp"
#include "sounds.hpp"

#include <dpp/dpp.h>

#include <catch2/catch_test_macros.hpp>

using latibot::commands::soundboard_command;
using latibot::commands::soundboard_services;

TEST_CASE("the soundboard command takes one sound, suggested as it is typed", "[soundboard]") {
    const latibot::soundboard::sound_catalog sounds;
    const soundboard_command command(soundboard_services{.sounds = &sounds, .clips = nullptr, .http = nullptr, .looked_in = {}});

    const dpp::slashcommand payload = command.build("soundboard", dpp::snowflake{1});

    REQUIRE(payload.options.size() == 1);
    const dpp::command_option& sound = payload.options.front();
    CHECK(sound.name == "sound");
    CHECK(sound.type == dpp::co_string);
    CHECK(sound.required);
    CHECK(sound.autocomplete);
}

TEST_CASE("the soundboard command is for whoever may use the soundboard, and its voice message arrives silently", "[soundboard]") {
    const latibot::soundboard::sound_catalog sounds;
    const soundboard_command command(soundboard_services{.sounds = &sounds, .clips = nullptr, .http = nullptr, .looked_in = {}});

    const auto& info = command.info();
    CHECK(info.name == "soundboard");
    CHECK(info.guild_only);
    REQUIRE(info.default_member_permissions.has_value());
    CHECK(*info.default_member_permissions == dpp::permission(dpp::p_use_soundboard));
    CHECK(info.required_bot_permissions == 0);
    CHECK(info.responses.result == dpp::m_suppress_notifications);
    CHECK(info.responses.refusal == dpp::m_ephemeral);
}

TEST_CASE("the soundboard command names a sound it cannot find", "[soundboard]") {
    CHECK(latibot::commands::unknown_sound_reply(" quack ") == "i can't see a soundboard sound called \"quack\"");
}
