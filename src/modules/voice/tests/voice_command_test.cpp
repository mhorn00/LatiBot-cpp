// /voice (src/modules/voice/docs/Voice_Channels.md §2.2).

#include "voice_command.hpp"

#include "core/commands/registry.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "voice/voice_sessions.hpp"

#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/dpp.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>

using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{100};

struct settings_fixture {
    latibot::db::database db{std::filesystem::path(latibot::db::database::in_memory)};
    latibot::config::guild_settings settings{db};

    settings_fixture() { latibot::testing::create_schema(db); }
};

} // namespace

TEST_CASE("the voice command registers, its flags checked against its subcommands", "[voice]") {
    // registry::add refuses an override naming a subcommand the command does
    // not have, which would otherwise stop the bot at startup.
    settings_fixture test;
    latibot::events::voice_sessions sessions;

    latibot::commands::registry commands;
    REQUIRE_NOTHROW(commands.add(std::make_unique<latibot::commands::voice_command>(sessions, test.settings)));

    const auto* voice = commands.find("voice");
    REQUIRE(voice != nullptr);
    CHECK(voice->info().responses_for("start").result == dpp::m_suppress_notifications);
    CHECK(voice->info().responses_for("grace").result == dpp::m_ephemeral);

    // The custom voices are /tts voices now, dectalk's.
    const auto paths = latibot::commands::subcommand_paths(voice->build("voice", dpp::snowflake{1}));
    CHECK(std::ranges::find(paths, "lab") == paths.end());
}

TEST_CASE("the voice grace defaults to 30 seconds and is clamped", "[voice]") {
    settings_fixture test;

    CHECK(latibot::commands::voice_grace_for(test.settings, guild) == 30s);

    test.settings.set_int(guild, latibot::events::voice_grace_key, 5);
    CHECK(latibot::commands::voice_grace_for(test.settings, guild) == 5s);

    test.settings.set_int(guild, latibot::events::voice_grace_key, -3);
    CHECK(latibot::commands::voice_grace_for(test.settings, guild) == 0s);
}
