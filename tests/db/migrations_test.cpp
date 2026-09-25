#include "core/db/migrations.hpp"

#include "core/db/database.hpp"
#include "core/db/error.hpp"
#include "core/events/triggers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <utility>

using latibot::db::database;
using latibot::db::db_error;
using latibot::db::migration;

namespace {

struct memory_database {
    database db{std::filesystem::path(database::in_memory)};
};

constexpr std::array<migration, 2> two_steps{{
    {.version = 1, .name = "first", .sql = "CREATE TABLE first_table (id INTEGER PRIMARY KEY);"},
    {.version = 2, .name = "second", .sql = "CREATE TABLE second_table (id INTEGER PRIMARY KEY);"},
}};

bool table_exists(database& db, std::string_view name) {
    auto query = db.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = ?", name);
    return query.step() && query.get<int>(0) == 1;
}

} // namespace

TEST_CASE("a fresh database migrates to the current schema", "[db]") {
    memory_database fixture;
    database& db = fixture.db;

    REQUIRE(db.user_version() == 0);

    const int version = latibot::db::migrate(db);

    CHECK(std::cmp_equal(version, latibot::db::schema().size()));
    CHECK(db.user_version() == version);
    CHECK(table_exists(db, "guild_settings"));
}

TEST_CASE("migrating twice is a no-op", "[db]") {
    memory_database fixture;
    database& db = fixture.db;

    const int first = latibot::db::migrate(db);
    const int second = latibot::db::migrate(db);

    CHECK(first == second);
    CHECK(db.user_version() == second);
}

TEST_CASE("only migrations newer than user_version are applied", "[db]") {
    memory_database fixture;
    database& db = fixture.db;

    // Pretend version 1 has already shipped and been applied by hand.
    db.execute("CREATE TABLE first_table (id INTEGER PRIMARY KEY);");
    db.set_user_version(1);

    const int version = latibot::db::migrate(db, two_steps);

    CHECK(version == 2);
    CHECK(table_exists(db, "second_table"));
}

TEST_CASE("a failing migration rolls back and keeps the previous version", "[db]") {
    memory_database fixture;
    database& db = fixture.db;

    constexpr std::array<migration, 2> broken{{
        {.version = 1, .name = "good", .sql = "CREATE TABLE good_table (id INTEGER PRIMARY KEY);"},
        {.version = 2, .name = "bad", .sql = "CREATE TABLE half_table (id INTEGER PRIMARY KEY); THIS IS NOT SQL;"},
    }};

    REQUIRE_THROWS_AS(latibot::db::migrate(db, broken), db_error);

    // Version 1 applied cleanly and stays; version 2 left nothing behind.
    CHECK(db.user_version() == 1);
    CHECK(table_exists(db, "good_table"));
    CHECK_FALSE(table_exists(db, "half_table"));
}

TEST_CASE("a gap in the migration versions is rejected", "[db]") {
    memory_database fixture;
    database& db = fixture.db;

    constexpr std::array<migration, 2> gapped{{
        {.version = 1, .name = "first", .sql = "CREATE TABLE first_table (id INTEGER PRIMARY KEY);"},
        {.version = 3, .name = "skips two", .sql = "CREATE TABLE third_table (id INTEGER PRIMARY KEY);"},
    }};

    REQUIRE_THROWS_AS(latibot::db::migrate(db, gapped), db_error);
    CHECK(db.user_version() == 1);
}

TEST_CASE("the shipped schema is append-only and correctly numbered", "[db]") {
    // Guards the rule from plan v4 §5.2: versions run 1, 2, 3 ... with no
    // gaps, so a database migrated by an older build can always catch up.
    int expected = 1;
    for (const migration& step : latibot::db::schema()) {
        CHECK(step.version == expected);
        CHECK_FALSE(step.name.empty());
        CHECK_FALSE(step.sql.empty());
        ++expected;
    }
}

TEST_CASE("an existing database gains the allowlist without losing its triggers", "[db]") {
    // The upgrade every running install takes: version 2 has triggers in it
    // already, and migration 3 adds a column to that table. Getting this
    // wrong loses real data, and a fresh-database test would not notice.
    memory_database fixture;
    database& db = fixture.db;

    const auto before_allowlist = latibot::db::schema().first(2);
    REQUIRE(latibot::db::migrate(db, before_allowlist) == 2);

    db.execute("INSERT INTO triggers (guild_id, pattern, match_mode, cooldown_s, enabled) VALUES (1, '420', 'whole_word', 30, 1);");
    db.execute("INSERT INTO trigger_responses (trigger_id, response, weight) VALUES (last_insert_rowid(), 'nice', 1);");

    REQUIRE(latibot::db::migrate(db) == static_cast<int>(latibot::db::schema().size()));
    CHECK(table_exists(db, "allowed_bots"));

    const latibot::events::trigger_store store(db);
    const auto all = store.for_guild(dpp::snowflake{1});
    REQUIRE(all.size() == 1);
    CHECK(all[0].pattern == "420");
    REQUIRE(all[0].responses.size() == 1);
    CHECK(all[0].responses[0].text == "nice");

    // A trigger that predates the column stays quiet around bots.
    CHECK_FALSE(all[0].respond_to_bots);
}
