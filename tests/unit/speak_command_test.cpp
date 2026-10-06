// What /speak and /tts decide (docs/features/Speech.md §2.1, §2.4).

#include "core/audio/voice_store.hpp"
#include "core/commands/registry.hpp"
#include "core/commands/speak.hpp"
#include "core/commands/voice.hpp"
#include "core/commands/voice_lab.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/events/voice_sessions.hpp"

#include "mocks/mock_clock.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

using namespace std::chrono_literals;
using latibot::commands::may_stop_speech;
using latibot::commands::speak_refusal;
using latibot::commands::speech_limits;
using latibot::commands::speech_limits_for;

namespace {

constexpr dpp::snowflake guild{100};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

struct settings_fixture {
    latibot::db::database db{std::filesystem::path(latibot::db::database::in_memory)};
    latibot::config::guild_settings settings{db};

    settings_fixture() { latibot::testing::create_schema(db); }
};

} // namespace

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

TEST_CASE("the voice commands register, their flags checked against their subcommands", "[commands]") {
    // registry::add refuses an override naming a subcommand the command does
    // not have, which would otherwise stop the bot at startup.
    settings_fixture test;
    latibot::audio::voice_store voices(test.db);
    latibot::events::voice_sessions sessions;
    latibot::testing::mock_clock clock;
    latibot::commands::voice_drafts drafts(clock);
    latibot::commands::voice_lab lab(drafts, voices, clock, {});

    latibot::commands::registry commands;
    REQUIRE_NOTHROW(commands.add(std::make_unique<latibot::commands::speak_command>(latibot::commands::speech_services{})));
    REQUIRE_NOTHROW(commands.add(std::make_unique<latibot::commands::tts_command>(latibot::commands::speech_services{}, lab)));
    REQUIRE_NOTHROW(commands.add(std::make_unique<latibot::commands::voice_command>(sessions, test.settings)));

    const auto* voice = commands.find("voice");
    REQUIRE(voice != nullptr);
    CHECK(voice->info().responses_for("start").result == dpp::m_suppress_notifications);
    CHECK(voice->info().responses_for("grace").result == dpp::m_ephemeral);

    // The custom voices are /tts voices, and answer privately like the rest
    // of /tts.
    const auto* tts = commands.find("tts");
    REQUIRE(tts != nullptr);
    CHECK(tts->info().responses_for("voices lab").result == dpp::m_ephemeral);
    CHECK(tts->info().responses_for("voices delete").result == dpp::m_ephemeral);
    const auto paths = latibot::commands::subcommand_paths(tts->build("tts", dpp::snowflake{1}));
    CHECK(std::ranges::find(paths, "voices lab") != paths.end());
    CHECK(std::ranges::find(paths, "voices list") != paths.end());
    CHECK(std::ranges::find(paths, "voices delete") != paths.end());
    const auto voice_paths = latibot::commands::subcommand_paths(voice->build("voice", dpp::snowflake{1}));
    CHECK(std::ranges::find(voice_paths, "lab") == voice_paths.end());
}

TEST_CASE("the voice grace defaults to 30 seconds and is clamped", "[commands]") {
    settings_fixture test;

    CHECK(latibot::commands::voice_grace_for(test.settings, guild) == 30s);

    test.settings.set_int(guild, latibot::events::voice_grace_key, 5);
    CHECK(latibot::commands::voice_grace_for(test.settings, guild) == 5s);

    test.settings.set_int(guild, latibot::events::voice_grace_key, -3);
    CHECK(latibot::commands::voice_grace_for(test.settings, guild) == 0s);
}
