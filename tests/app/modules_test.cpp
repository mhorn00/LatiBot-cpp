// The bot as this build makes it: every module the build includes, through
// the same generated list the executable uses (docs/modules/Module_Plan_Final.md
// §4.8). What can only be checked with all of them is here: the comparison of
// every module's schema with the old migrations, and adoption.

#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/db/schema_versions.hpp"
#include "core/db/schemas.hpp"
#include "core/events/triggers.hpp"
#include "core/modules/module.hpp"

#include "support/test_host.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using latibot::db::database;
using latibot::db::module_schema;
using latibot::modules::module_list;
using latibot::testing::test_host;

namespace {

/// Every module the old migrations made tables for is built, or the
/// comparison and adoption tests have nothing whole to compare.
auto every_module_built(const module_list& modules) -> bool {
    std::set<std::string_view> built;
    for (const module_schema& schema : latibot::db::builtin_schemas()) {
        built.insert(schema.module);
    }
    for (const auto& each : modules) {
        built.insert(each->name());
    }
    return std::ranges::all_of(latibot::db::adopted_modules(), [&built](std::string_view name) { return built.contains(name); });
}

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

auto count_rows(database& db, std::string_view table) -> int {
    auto query = db.prepare("SELECT count(*) FROM " + std::string(table));
    return query.step() ? query.get<int>(0) : -1;
}

} // namespace

TEST_CASE("every module this build includes starts on the host, each with its own name", "[app]") {
    test_host bot;
    const module_list modules = latibot::modules::start_modules(latibot::modules::enabled_modules, bot, bot.offered);

    std::set<std::string_view> names;
    for (const auto& each : modules) {
        INFO(each->name());
        CHECK(names.insert(each->name()).second);
    }
    // Its tables are recorded under its name.
    for (const auto& [module, version] : latibot::db::recorded_versions(bot.data)) {
        CHECK(version >= 1);
    }
}

TEST_CASE("every module's version 1 is exactly what migrations 1 to 15 built", "[app]") {
    // The comparison test (§7.3). An adopted database and a new one must be
    // the same database, or a module's later steps would meet different
    // tables depending on how old the install is. Remove after: you say so,
    // with the old migrations.
    test_host flattened;
    const module_list modules = latibot::modules::start_modules(latibot::modules::enabled_modules, flattened, flattened.offered);
    if (!every_module_built(modules)) SKIP("this build leaves out a module the old migrations made tables for");

    database legacy{std::filesystem::path(database::in_memory)};
    REQUIRE(latibot::db::migrate(legacy) == 15);

    const auto before = describe(legacy);
    const auto after = describe(flattened.data);

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

TEST_CASE("an old database is brought to migration 15, then adopted with its data, and no module creates anything", "[app]") {
    // What every running install meets once: the old migrations, unedited,
    // then each module of the time recorded at version 1, which is what 15
    // is. Remove after: you say so, with the old migrations.
    database old{std::filesystem::path(database::in_memory)};
    const auto before_llm = latibot::db::schema().first(10);
    REQUIRE(latibot::db::migrate(old, before_llm) == 10);
    old.execute("INSERT INTO triggers (guild_id, pattern, match_mode, cooldown_s, enabled) VALUES (1, '420', 'whole_word', 30, 1);");
    old.execute("INSERT INTO midnight_messages (guild_id, channel_id, timezone, message) VALUES (1, 2, 'UTC', 'midnight!');");

    CHECK(latibot::db::prepare_schema_versions(old) == latibot::db::schema_origin::adopted);
    CHECK(old.user_version() == 15);
    for (const std::string_view module : latibot::db::adopted_modules()) {
        INFO(module);
        bool recorded = false;
        for (const auto& [name, version] : latibot::db::recorded_versions(old)) {
            if (name == module) recorded = version == 1;
        }
        CHECK(recorded);
    }

    // The core's and every module's schema find their version 1 recorded,
    // and create nothing.
    for (const module_schema& schema : latibot::db::builtin_schemas()) {
        CHECK(latibot::db::apply_schema(old, schema) == 1);
    }
    test_host on_old;
    const module_list modules = latibot::modules::enabled_modules(on_old);
    for (const auto& each : modules) {
        if (each->schema().empty()) continue;
        CHECK(latibot::db::apply_schema(old, {.module = each->name(), .steps = each->schema()}) == 1);
    }

    CHECK(latibot::events::trigger_store(old).for_guild(dpp::snowflake{1}).size() == 1);
    CHECK(count_rows(old, "midnight_messages") == 1);
}

TEST_CASE("adoption records every module of migration 15, and only those", "[app]") {
    // A module built in later must find its row, or it would create tables
    // that are already there.
    test_host bot;
    const module_list modules = latibot::modules::enabled_modules(bot);
    if (!every_module_built(modules)) SKIP("this build leaves out a module the old migrations made tables for");

    std::set<std::string_view> every;
    for (const module_schema& schema : latibot::db::builtin_schemas()) {
        every.insert(schema.module);
    }
    for (const auto& each : modules) {
        if (!each->schema().empty()) every.insert(each->name());
    }
    const std::set<std::string_view> adopted(latibot::db::adopted_modules().begin(), latibot::db::adopted_modules().end());
    CHECK(adopted == every);
}
