// Custom voices, kept per guild (plan §12.6).

#include "core/audio/voice_store.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

using latibot::audio::saved_voice;
using latibot::audio::voice_store;
using latibot::db::database;

namespace {

constexpr dpp::snowflake guild{111111111111111111ULL};
constexpr dpp::snowflake other_guild{222222222222222222ULL};
constexpr dpp::snowflake maker{333333333333333333ULL};

struct store_fixture {
    database db{std::filesystem::path(database::in_memory)};
    voice_store voices{db};

    store_fixture() { latibot::db::migrate(db); }
};

auto voice(std::string name, const std::string& base, const std::string& edits) -> saved_voice {
    return {.name = std::move(name),
            .voice = latibot::audio::parse_custom_voice(edits, base).voice,
            .created_by = maker,
            .updated_at = std::chrono::sys_seconds{std::chrono::seconds{1'700'000'000}}};
}

} // namespace

TEST_CASE("a saved voice reads back as it was saved", "[db]") {
    store_fixture test;
    test.voices.save(guild, voice("robo", "harry", "ap 200 pr 150"));

    const auto found = test.voices.find(guild, "robo");
    REQUIRE(found.has_value());
    CHECK(found->voice.base == "harry");
    CHECK(found->voice.dv_parameters() == "ap 200 pr 150");
    CHECK(found->created_by == maker);
    CHECK(found->updated_at == std::chrono::sys_seconds{std::chrono::seconds{1'700'000'000}});
}

TEST_CASE("voice names are found in any case, and per guild", "[db]") {
    store_fixture test;
    test.voices.save(guild, voice("Robo", "harry", ""));

    CHECK(test.voices.find(guild, "ROBO").has_value());
    CHECK(test.voices.find(guild, " robo ").has_value());
    CHECK_FALSE(test.voices.find(other_guild, "robo").has_value());
    CHECK(test.voices.count(guild) == 1);
    CHECK(test.voices.count(other_guild) == 0);
}

TEST_CASE("saving under a name that exists replaces the voice but keeps its maker", "[db]") {
    store_fixture test;
    test.voices.save(guild, voice("robo", "harry", "ap 200"));

    saved_voice changed = voice("robo", "kit", "pr 50");
    changed.created_by = dpp::snowflake{42};
    test.voices.save(guild, changed);

    const auto found = test.voices.find(guild, "robo");
    REQUIRE(found.has_value());
    CHECK(found->voice.base == "kit");
    CHECK(found->voice.dv_parameters() == "pr 50");
    CHECK(found->created_by == maker);
    CHECK(test.voices.count(guild) == 1);
}

TEST_CASE("voices are listed by name and removed one at a time", "[db]") {
    store_fixture test;
    test.voices.save(guild, voice("zed", "paul", ""));
    test.voices.save(guild, voice("abe", "betty", ""));

    const auto listed = test.voices.list(guild);
    REQUIRE(listed.size() == 2);
    CHECK(listed[0].name == "abe");
    CHECK(listed[1].name == "zed");

    CHECK(test.voices.remove(guild, "ABE"));
    CHECK_FALSE(test.voices.remove(guild, "abe"));
    CHECK(test.voices.list(guild).size() == 1);
}
