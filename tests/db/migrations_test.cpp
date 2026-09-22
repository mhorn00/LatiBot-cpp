#include "core/db/migrations.hpp"

#include "core/db/database.hpp"
#include "core/db/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <span>
#include <string>

using latibot::db::database;
using latibot::db::db_error;
using latibot::db::migration;

namespace {

struct memory_database {
    database db{std::filesystem::path(database::in_memory)};
};

constexpr std::array<migration, 2> two_steps{{
    {1, "first", "CREATE TABLE first_table (id INTEGER PRIMARY KEY);"},
    {2, "second", "CREATE TABLE second_table (id INTEGER PRIMARY KEY);"},
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

    CHECK(version == static_cast<int>(latibot::db::schema().size()));
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
        {1, "good", "CREATE TABLE good_table (id INTEGER PRIMARY KEY);"},
        {2, "bad", "CREATE TABLE half_table (id INTEGER PRIMARY KEY); THIS IS NOT SQL;"},
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
        {1, "first", "CREATE TABLE first_table (id INTEGER PRIMARY KEY);"},
        {3, "skips two", "CREATE TABLE third_table (id INTEGER PRIMARY KEY);"},
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
