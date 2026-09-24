#include "core/events/nickname_import.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"

#include "support/temp_directory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

using latibot::events::import_nicknames;
using latibot::events::import_nicknames_file;
using latibot::events::import_report;
using latibot::events::nickname_store;
using latibot::events::read_nicknames_json;

namespace {

constexpr dpp::snowflake guild{142409638556467200};
constexpr dpp::snowflake member{111111111111111111};

struct store_fixture {
    latibot::db::database db{":memory:"};
    nickname_store store{db};

    store_fixture() { latibot::db::migrate(db); }
};

const std::string two_entries = R"json({
    "142409638556467200": [{
        "member": {"id": "111111111111111111", "username": "somebody"},
        "nicknames": [
            {"datetime": "2023-11-25 01:58:23", "changedById": "222222222222222222", "nickname": "one"},
            {"datetime": "2023-11-26 01:58:23", "changedById": "111111111111111111", "nickname": "two"}
        ]
    }]
})json";

} // namespace

TEST_CASE("an import writes the history it read", "[db]") {
    store_fixture fixture;

    const import_report report = read_nicknames_json(two_entries);
    CHECK(import_nicknames(fixture.store, report) == 2);

    const auto history = fixture.store.history(guild, member);
    REQUIRE(history.size() == 2);

    // Newest first, as everything else reads it.
    CHECK(history.front().nickname == "two");
    CHECK(history.back().nickname == "one");
}

TEST_CASE("importing the same file twice adds nothing the second time", "[db]") {
    store_fixture fixture;

    const import_report report = read_nicknames_json(two_entries);
    REQUIRE(import_nicknames(fixture.store, report) == 2);

    // Which is what lets the file simply be left where it is, rather than
    // needing to be moved or marked after one run (plan v4 §8.3).
    CHECK(import_nicknames(fixture.store, report) == 0);
    CHECK(fixture.store.count(guild, member) == 2);
}

TEST_CASE("an import does not disturb history the bot recorded itself", "[db]") {
    store_fixture fixture;

    // Somebody's nickname today, watched live and attributed.
    const auto now = std::chrono::system_clock::now();
    fixture.store.record({.guild_id = guild,
                          .user_id = member,
                          .nickname = "current",
                          .changed_at = now,
                          .changed_by = dpp::snowflake{333},
                          .source = latibot::events::nickname_source::command,
                          .imported_raw = {}});

    REQUIRE(import_nicknames(fixture.store, read_nicknames_json(two_entries)) == 2);

    const auto history = fixture.store.history(guild, member);
    REQUIRE(history.size() == 3);
    CHECK(history.front().nickname == "current");
    CHECK(history.front().changed_by == dpp::snowflake{333});
}

TEST_CASE("a cleared nickname is imported once, not once per run", "[db]") {
    store_fixture fixture;

    // NULL never equals NULL in SQL, so a cleared nickname is the row most
    // likely to be imported again on every start.
    const std::string cleared = R"json({
        "142409638556467200": [{
            "member": {"id": "111111111111111111"},
            "nicknames": [{"datetime": "2023-11-25 01:58:23", "nickname": ""}]
        }]
    })json";

    const import_report report = read_nicknames_json(cleared);
    REQUIRE(import_nicknames(fixture.store, report) == 1);
    CHECK(import_nicknames(fixture.store, report) == 0);
}

TEST_CASE("no file to import is not a problem", "[db][fs]") {
    store_fixture fixture;
    const latibot::testing::temp_directory temp;

    CHECK_FALSE(import_nicknames_file(fixture.store, temp.path() / "nicknames.json").has_value());
}

TEST_CASE("a file beside the database is read and imported", "[db][fs]") {
    store_fixture fixture;
    const latibot::testing::temp_directory temp;

    const auto path = temp.path() / "nicknames.json";
    {
        std::ofstream file(path);
        file << two_entries;
    }

    const auto added = import_nicknames_file(fixture.store, path);
    REQUIRE(added.has_value());
    CHECK(*added == 2);
    CHECK(fixture.store.count(guild, member) == 2);
}
