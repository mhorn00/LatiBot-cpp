// Each module's schema, recorded in schema_versions, and how an old database
// is adopted into it (docs/modules/Module_Plan_Final.md §7).

#include "core/db/schema_versions.hpp"

#include "core/db/database.hpp"
#include "core/db/error.hpp"
#include "core/db/migrations.hpp"
#include "core/db/schemas.hpp"

#include "support/capture_log.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using latibot::db::database;
using latibot::db::db_error;
using latibot::db::migration;
using latibot::db::module_schema;
using latibot::db::schema_origin;

namespace {

struct memory_database {
    database db{std::filesystem::path(database::in_memory)};
};

auto table_exists(database& db, std::string_view name) -> bool {
    auto query = db.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = ?", name);
    return query.step() && query.get<int>(0) == 1;
}

auto versions_of(database& db) -> std::map<std::string, int> {
    std::map<std::string, int> versions;
    for (auto& [module, version] : latibot::db::recorded_versions(db)) {
        versions.emplace(std::move(module), version);
    }
    return versions;
}

auto every_module_at_one() -> std::map<std::string, int> {
    std::map<std::string, int> versions;
    for (const module_schema& schema : latibot::testing::all_schemas()) {
        versions.emplace(std::string(schema.module), 1);
    }
    return versions;
}

constexpr std::array<migration, 2> widget_steps{{
    {.version = 1, .name = "widgets", .sql = "CREATE TABLE widgets (id INTEGER PRIMARY KEY);"},
    {.version = 2, .name = "widget colours", .sql = "ALTER TABLE widgets ADD COLUMN colour TEXT;"},
}};

} // namespace

TEST_CASE("a new database gets schema_versions, and every module its version 1", "[db]") {
    memory_database fixture;
    database& db = fixture.db;

    CHECK(latibot::db::prepare_schema_versions(db) == schema_origin::fresh);
    for (const module_schema& schema : latibot::testing::all_schemas()) {
        CHECK(latibot::db::apply_schema(db, schema) == 1);
    }

    CHECK(versions_of(db) == every_module_at_one());
    CHECK(table_exists(db, "guild_settings"));
    CHECK(table_exists(db, "tts_voices"));
    // The old numbering is never used.
    CHECK(db.user_version() == 0);
}

TEST_CASE("starting again changes nothing", "[db]") {
    memory_database fixture;
    database& db = fixture.db;
    latibot::testing::create_schema(db);

    CHECK(latibot::db::prepare_schema_versions(db) == schema_origin::existing);
    const latibot::testing::capture_log captured(latibot::util::log_level::info);
    latibot::testing::create_schema(db);

    CHECK(versions_of(db) == every_module_at_one());
    CHECK_FALSE(captured.contains(latibot::util::log_level::info, "applied"));
}

TEST_CASE("an old database is brought to migration 15, then adopted", "[db]") {
    // What every running install meets once: the old migrations, unedited,
    // then each module of the time recorded at version 1. Remove after: you
    // say so, with the old migrations. tests/app checks the data survives,
    // and that no module creates anything twice.
    memory_database fixture;
    database& db = fixture.db;
    const auto before_llm = latibot::db::schema().first(10);
    REQUIRE(latibot::db::migrate(db, before_llm) == 10);

    CHECK(latibot::db::prepare_schema_versions(db) == schema_origin::adopted);

    CHECK(db.user_version() == 15);
    CHECK(table_exists(db, "llm_aliases"));
    std::map<std::string, int> expected;
    for (const std::string_view module : latibot::db::adopted_modules()) {
        expected.emplace(std::string(module), 1);
    }
    CHECK(versions_of(db) == expected);
    for (const module_schema& schema : latibot::db::builtin_schemas()) {
        CHECK(latibot::db::apply_schema(db, schema) == 1);
    }
}

TEST_CASE("a database the bot did not write is refused", "[db]") {
    memory_database fixture;
    database& db = fixture.db;

    SECTION("tables, but no versions of either kind") {
        db.execute("CREATE TABLE something_else (id INTEGER PRIMARY KEY);");
        CHECK_THROWS_AS(latibot::db::prepare_schema_versions(db), db_error);
    }
    SECTION("a user_version past the old migrations") {
        db.execute("CREATE TABLE something_else (id INTEGER PRIMARY KEY);");
        db.set_user_version(16);
        CHECK_THROWS_AS(latibot::db::prepare_schema_versions(db), db_error);
    }
    CHECK_FALSE(table_exists(db, "schema_versions"));
}

TEST_CASE("a module's later steps apply above its recorded version", "[db]") {
    memory_database fixture;
    database& db = fixture.db;
    latibot::db::prepare_schema_versions(db);

    const std::span<const migration> first_only = std::span(widget_steps).first(1);
    CHECK(latibot::db::apply_schema(db, {.module = "widgets", .steps = first_only}) == 1);
    db.execute("INSERT INTO widgets (id) VALUES (7);");

    CHECK(latibot::db::apply_schema(db, {.module = "widgets", .steps = widget_steps}) == 2);
    CHECK(versions_of(db).at("widgets") == 2);
    auto query = db.prepare("SELECT id, colour IS NULL FROM widgets");
    REQUIRE(query.step());
    CHECK(query.get<int>(0) == 7);
}

TEST_CASE("a failing schema step rolls back, and its module keeps the version before", "[db]") {
    memory_database fixture;
    database& db = fixture.db;
    latibot::db::prepare_schema_versions(db);

    constexpr std::array<migration, 2> broken{{
        {.version = 1, .name = "good", .sql = "CREATE TABLE good_table (id INTEGER PRIMARY KEY);"},
        {.version = 2, .name = "bad", .sql = "CREATE TABLE half_table (id INTEGER PRIMARY KEY); THIS IS NOT SQL;"},
    }};
    CHECK_THROWS_AS(latibot::db::apply_schema(db, {.module = "broken", .steps = broken}), db_error);

    CHECK(versions_of(db).at("broken") == 1);
    CHECK(table_exists(db, "good_table"));
    CHECK_FALSE(table_exists(db, "half_table"));
}

TEST_CASE("a gap in a module's steps is refused", "[db]") {
    memory_database fixture;
    database& db = fixture.db;
    latibot::db::prepare_schema_versions(db);

    constexpr std::array<migration, 1> starts_at_two{{
        {.version = 2, .name = "skips one", .sql = "CREATE TABLE second_table (id INTEGER PRIMARY KEY);"},
    }};
    CHECK_THROWS_AS(latibot::db::apply_schema(db, {.module = "gapped", .steps = starts_at_two}), db_error);
    CHECK_FALSE(versions_of(db).contains("gapped"));
}

TEST_CASE("every module's steps run 1, 2, 3 with no gaps", "[db]") {
    for (const module_schema& schema : latibot::testing::all_schemas()) {
        INFO(schema.module);
        CHECK_FALSE(schema.module.empty());
        int expected = 1;
        for (const migration& step : schema.steps) {
            CHECK(step.version == expected);
            CHECK_FALSE(step.name.empty());
            CHECK_FALSE(step.sql.empty());
            ++expected;
        }
    }
}
