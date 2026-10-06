// Each module's schema, recorded in schema_versions, and how an old database
// is adopted into it (docs/modules/Module_Plan_Final.md §7).

#include "core/db/schema_versions.hpp"

#include "core/db/database.hpp"
#include "core/db/error.hpp"
#include "core/db/migrations.hpp"
#include "core/db/schemas.hpp"
#include "core/events/midnight.hpp"
#include "core/events/triggers.hpp"

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

// --------------------------------------------------------------------------
// Describing a schema, for the comparison test
// --------------------------------------------------------------------------

/// SQL as it means rather than as it was typed: no comments, and one space
/// wherever there was any.
auto normalised(std::string_view sql) -> std::string {
    std::string out;
    bool space = false;
    for (std::size_t at = 0; at < sql.size(); ++at) {
        if (sql.substr(at, 2) == "--") {
            const std::size_t end = sql.find('\n', at);
            at = end == std::string_view::npos ? sql.size() : end;
            space = true;
            continue;
        }
        const char c = sql[at];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

/// Every row a query returns, each as its columns joined by '|'.
auto rows_of(database& db, std::string_view sql, std::string_view argument) -> std::string {
    std::string text;
    auto query = db.prepare(sql, argument);
    while (query.step()) {
        for (int column = 0; column < query.column_count(); ++column) {
            text += query.is_null(column) ? std::string("NULL") : query.get<std::string>(column);
            text += '|';
        }
        text += '\n';
    }
    return text;
}

/// Each table, index, view and trigger, as what makes it what it is
/// (§7.3): a table by its columns in order, its foreign keys, its indexes
/// and their columns, and whether it is WITHOUT ROWID; the rest by their
/// SQL, normalised. schema_versions is left out: only one side has it.
auto describe(database& db) -> std::map<std::string, std::string> {
    struct object {
        std::string type;
        std::string name;
        std::string sql;
    };
    std::vector<object> objects;
    {
        auto query = db.prepare("SELECT type, name, sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' AND name <> 'schema_versions'");
        while (query.step()) {
            objects.push_back({.type = query.get<std::string>(0),
                               .name = query.get<std::string>(1),
                               .sql = query.is_null(2) ? "" : query.get<std::string>(2)});
        }
    }

    std::map<std::string, std::string> described;
    for (const object& each : objects) {
        std::string text;
        if (each.type == "table") {
            text += "columns:\n" + rows_of(db, "SELECT * FROM pragma_table_xinfo(?)", each.name);
            text += "foreign keys:\n" + rows_of(db, "SELECT * FROM pragma_foreign_key_list(?)", each.name);
            text += "shape:\n" + rows_of(db, "SELECT type, ncol, wr, strict FROM pragma_table_list(?)", each.name);
            text +=
                "indexes:\n" + rows_of(db, "SELECT name, \"unique\", origin, partial FROM pragma_index_list(?) ORDER BY name", each.name);
            if (each.sql.starts_with("CREATE VIRTUAL TABLE")) text += "sql: " + normalised(each.sql) + "\n";
        } else if (each.type == "index") {
            text += "columns:\n" + rows_of(db, "SELECT * FROM pragma_index_xinfo(?)", each.name);
            text += "sql: " + normalised(each.sql) + "\n";
        } else {
            text += "sql: " + normalised(each.sql) + "\n";
        }
        described.emplace(each.type + " " + each.name, std::move(text));
    }
    return described;
}

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
    CHECK(table_exists(db, "midnight_messages"));
    CHECK(table_exists(db, "llm_aliases"));
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

TEST_CASE("an old database is brought to migration 15, then adopted with its data", "[db]") {
    // What every running install meets once: the old migrations, unedited,
    // then each module of the time recorded at version 1, which is what 15
    // is. Remove after: you say so, with the old migrations.
    memory_database fixture;
    database& db = fixture.db;
    const auto before_llm = latibot::db::schema().first(10);
    REQUIRE(latibot::db::migrate(db, before_llm) == 10);
    db.execute("INSERT INTO triggers (guild_id, pattern, match_mode, cooldown_s, enabled) VALUES (1, '420', 'whole_word', 30, 1);");
    db.execute("INSERT INTO midnight_messages (guild_id, channel_id, timezone, message) VALUES (1, 2, 'UTC', 'midnight!');");

    CHECK(latibot::db::prepare_schema_versions(db) == schema_origin::adopted);

    CHECK(db.user_version() == 15);
    CHECK(table_exists(db, "llm_aliases"));
    CHECK(versions_of(db) == every_module_at_one());

    // Nothing is created twice.
    for (const module_schema& schema : latibot::testing::all_schemas()) {
        CHECK(latibot::db::apply_schema(db, schema) == 1);
    }
    CHECK(latibot::events::trigger_store(db).for_guild(dpp::snowflake{1}).size() == 1);
    CHECK(latibot::events::midnight_store(db).for_guild(dpp::snowflake{1}).size() == 1);
}

TEST_CASE("adoption records every module of migration 15, and only those", "[db]") {
    // A module built in later must find its row, or it would create tables
    // that are already there.
    std::set<std::string_view> adopted(latibot::db::adopted_modules().begin(), latibot::db::adopted_modules().end());
    std::set<std::string_view> every;
    for (const module_schema& schema : latibot::testing::all_schemas()) {
        every.insert(schema.module);
    }
    CHECK(adopted == every);
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

TEST_CASE("every module's version 1 is exactly what migrations 1 to 15 built", "[db]") {
    // The comparison test (§7.3). An adopted database and a new one must
    // be the same database, or a module's later steps would meet different
    // tables depending on how old the install is. Remove after: you say so,
    // with the old migrations.
    memory_database legacy_fixture;
    database& legacy = legacy_fixture.db;
    REQUIRE(latibot::db::migrate(legacy) == 15);

    memory_database flattened_fixture;
    database& flattened = flattened_fixture.db;
    latibot::testing::create_schema(flattened);

    const auto before = describe(legacy);
    const auto after = describe(flattened);

    std::set<std::string> names;
    for (const auto& [name, text] : before) {
        names.insert(name);
    }
    for (const auto& [name, text] : after) {
        names.insert(name);
    }
    for (const std::string& name : names) {
        INFO(name);
        REQUIRE(before.contains(name));
        REQUIRE(after.contains(name));
        CHECK(before.at(name) == after.at(name));
    }
    // Something was compared: the tables of every module, and more.
    CHECK(names.size() > 40);
}
