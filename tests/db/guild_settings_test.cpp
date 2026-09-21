#include "core/config/guild_settings.hpp"

#include "core/db/database.hpp"
#include "core/db/migrations.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using latibot::config::guild_settings;
using latibot::db::database;

namespace {

struct settings_fixture {
    database db{std::filesystem::path(database::in_memory)};
    guild_settings settings{db};

    settings_fixture() { latibot::db::migrate(db); }
};

constexpr dpp::snowflake guild_a{111111111111111111ULL};
constexpr dpp::snowflake guild_b{222222222222222222ULL};

} // namespace

TEST_CASE("an unset key falls back to the caller's default", "[db][config]") {
    settings_fixture fixture;

    CHECK(fixture.settings.get(guild_a, "goodbye_message", "bye") == "bye");
    CHECK(fixture.settings.get_int(guild_a, "tts_max_seconds", 60) == 60);
    CHECK(fixture.settings.get_bool(guild_a, "llm_enabled", true));
    CHECK(fixture.settings.get_real(guild_a, "trigger_chance", 0.25) == 0.25);
    CHECK_FALSE(fixture.settings.find(guild_a, "goodbye_message").has_value());
}

TEST_CASE("values survive a set and get round trip", "[db][config]") {
    settings_fixture fixture;

    fixture.settings.set(guild_a, "goodbye_message", "see ya");
    fixture.settings.set_int(guild_a, "tts_max_seconds", 90);
    fixture.settings.set_bool(guild_a, "llm_enabled", false);
    fixture.settings.set_real(guild_a, "trigger_chance", 0.75);

    CHECK(fixture.settings.get(guild_a, "goodbye_message", "bye") == "see ya");
    CHECK(fixture.settings.get_int(guild_a, "tts_max_seconds", 60) == 90);
    CHECK_FALSE(fixture.settings.get_bool(guild_a, "llm_enabled", true));
    CHECK(fixture.settings.get_real(guild_a, "trigger_chance", 0.25) == 0.75);
}

TEST_CASE("setting a key again replaces the value", "[db][config]") {
    settings_fixture fixture;

    fixture.settings.set(guild_a, "goodbye_message", "first");
    fixture.settings.set(guild_a, "goodbye_message", "second");

    CHECK(fixture.settings.get(guild_a, "goodbye_message", "") == "second");
    CHECK(fixture.settings.all(guild_a).size() == 1);
}

TEST_CASE("guilds do not see each other's settings", "[db][config]") {
    settings_fixture fixture;

    fixture.settings.set(guild_a, "goodbye_message", "server A");

    CHECK(fixture.settings.get(guild_b, "goodbye_message", "default") == "default");
    CHECK(fixture.settings.all(guild_b).empty());
}

TEST_CASE("erase removes a key and reports whether it existed", "[db][config]") {
    settings_fixture fixture;

    fixture.settings.set(guild_a, "goodbye_message", "bye");

    CHECK(fixture.settings.erase(guild_a, "goodbye_message"));
    CHECK_FALSE(fixture.settings.find(guild_a, "goodbye_message").has_value());
    CHECK_FALSE(fixture.settings.erase(guild_a, "goodbye_message"));
}

TEST_CASE("a value that cannot be parsed falls back instead of throwing", "[db][config]") {
    // One hand-edited or corrupted row should not take a feature down.
    settings_fixture fixture;

    fixture.settings.set(guild_a, "tts_max_seconds", "ninety");
    fixture.settings.set(guild_a, "trigger_chance", "lots");
    fixture.settings.set(guild_a, "llm_enabled", "maybe");

    CHECK(fixture.settings.get_int(guild_a, "tts_max_seconds", 60) == 60);
    CHECK(fixture.settings.get_real(guild_a, "trigger_chance", 0.25) == 0.25);
    CHECK(fixture.settings.get_bool(guild_a, "llm_enabled", true));
}

TEST_CASE("booleans accept the usual spellings", "[db][config]") {
    settings_fixture fixture;

    for (const auto* truthy : {"1", "true", "TRUE", "yes", "on"}) {
        fixture.settings.set(guild_a, "flag", truthy);
        CHECK(fixture.settings.get_bool(guild_a, "flag", false));
    }
    for (const auto* falsy : {"0", "false", "FALSE", "no", "off"}) {
        fixture.settings.set(guild_a, "flag", falsy);
        CHECK_FALSE(fixture.settings.get_bool(guild_a, "flag", true));
    }
}

TEST_CASE("partly numeric text is not accepted as a number", "[db][config]") {
    settings_fixture fixture;

    fixture.settings.set(guild_a, "tts_max_seconds", "90s");
    fixture.settings.set(guild_a, "trigger_chance", "0.5x");

    CHECK(fixture.settings.get_int(guild_a, "tts_max_seconds", 60) == 60);
    CHECK(fixture.settings.get_real(guild_a, "trigger_chance", 0.25) == 0.25);
}

TEST_CASE("all() lists everything set for one guild", "[db][config]") {
    settings_fixture fixture;

    fixture.settings.set(guild_a, "b_key", "2");
    fixture.settings.set(guild_a, "a_key", "1");
    fixture.settings.set(guild_b, "other", "x");

    const auto listed = fixture.settings.all(guild_a);
    REQUIRE(listed.size() == 2);
    CHECK(listed.at("a_key") == "1");
    CHECK(listed.at("b_key") == "2");
}
