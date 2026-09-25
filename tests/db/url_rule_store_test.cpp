#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/url_rules.hpp"

#include "support/temp_directory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

using latibot::events::url_rule;
using latibot::events::url_rule_store;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake other_guild{2000};
constexpr dpp::snowflake member{3000};

struct store_fixture {
    latibot::db::database db{":memory:"};
    url_rule_store store{db};

    store_fixture() { latibot::db::migrate(db); }
};

url_rule x_rule() {
    return {.domain = "x.com",
            .mirrors = {{.host = "fxtwitter.com", .translate_suffix = "/en"}, {.host = "vxtwitter.com", .translate_suffix = ""}}};
}

} // namespace

TEST_CASE("a rule comes back with its mirrors in order", "[db]") {
    store_fixture fixture;
    fixture.store.set(guild, x_rule());

    const auto found = fixture.store.find(guild, "x.com");
    REQUIRE(found.has_value());
    REQUIRE(found->mirrors.size() == 2);
    CHECK(found->mirrors[0].host == "fxtwitter.com");
    CHECK(found->mirrors[0].translate_suffix == "/en");
    CHECK(found->mirrors[1].host == "vxtwitter.com");
    CHECK(found->mirrors[1].translate_suffix.empty());
}

TEST_CASE("setting a rule replaces its mirrors, which is how reordering works", "[db]") {
    store_fixture fixture;
    fixture.store.set(guild, x_rule());
    fixture.store.set(guild, {.domain = "x.com", .mirrors = {{.host = "vxtwitter.com", .translate_suffix = ""}}});

    const auto found = fixture.store.find(guild, "x.com");
    REQUIRE(found.has_value());
    REQUIRE(found->mirrors.size() == 1);
    CHECK(found->mirrors[0].host == "vxtwitter.com");
}

TEST_CASE("rules belong to one guild", "[db]") {
    store_fixture fixture;
    fixture.store.set(guild, x_rule());
    fixture.store.set(guild, {.domain = "tiktok.com", .mirrors = {{.host = "tfxktok.com", .translate_suffix = ""}}});

    CHECK(fixture.store.for_guild(guild).size() == 2);
    CHECK(fixture.store.for_guild(other_guild).empty());
    CHECK_FALSE(fixture.store.find(other_guild, "x.com").has_value());

    // Alphabetical, which is the order the list and the panel show.
    CHECK(fixture.store.for_guild(guild).front().domain == "tiktok.com");
}

TEST_CASE("removing a rule says whether there was one", "[db]") {
    store_fixture fixture;
    fixture.store.set(guild, x_rule());

    CHECK(fixture.store.remove(guild, "x.com"));
    CHECK_FALSE(fixture.store.remove(guild, "x.com"));
    CHECK_FALSE(fixture.store.find(guild, "x.com").has_value());
}

TEST_CASE("a mirror is remembered after its rule is gone", "[db]") {
    // The backfill has to recognise messages from rules that no longer exist.
    store_fixture fixture;
    fixture.store.set(guild, x_rule());
    fixture.store.remove(guild, "x.com");

    const auto known = fixture.store.known_mirrors(guild);
    CHECK(known.at("fxtwitter.com") == "x.com");
    CHECK(known.at("vxtwitter.com") == "x.com");
    CHECK(fixture.store.known_mirrors(other_guild).empty());
}

TEST_CASE("an opt-out toggles, and is kept per guild", "[db]") {
    store_fixture fixture;

    CHECK_FALSE(fixture.store.opted_out(guild, member));
    CHECK(fixture.store.toggle_opt_out(guild, member));
    CHECK(fixture.store.opted_out(guild, member));
    CHECK_FALSE(fixture.store.opted_out(other_guild, member));

    CHECK_FALSE(fixture.store.toggle_opt_out(guild, member));
    CHECK_FALSE(fixture.store.opted_out(guild, member));
}

TEST_CASE("replacement is off in a guild until it is turned on, per guild", "[db]") {
    store_fixture fixture;
    fixture.store.set(guild, x_rule());

    // Having rules is not the same as wanting them applied.
    CHECK_FALSE(fixture.store.enabled(guild));

    fixture.store.set_enabled(guild, true);
    CHECK(fixture.store.enabled(guild));
    CHECK_FALSE(fixture.store.enabled(other_guild));

    fixture.store.set_enabled(guild, false);
    CHECK_FALSE(fixture.store.enabled(guild));
}

TEST_CASE("turning replacement on outlasts a restart", "[db][fs]") {
    const latibot::testing::temp_directory folder;
    const auto file = folder.path() / "bot.db";

    {
        latibot::db::database db{file};
        latibot::db::migrate(db);
        url_rule_store(db).set_enabled(guild, true);
    }

    // A fresh connection and a fresh store, as the next start would have.
    latibot::db::database reopened{file};
    latibot::db::migrate(reopened);
    CHECK(url_rule_store(reopened).enabled(guild));
    CHECK_FALSE(url_rule_store(reopened).enabled(other_guild));
}

TEST_CASE("the Java rule file imports once, and never over an existing rule", "[db][fs]") {
    store_fixture fixture;
    const latibot::testing::temp_directory folder;
    const auto file = folder.path() / "UrlReplacements.txt";
    {
        std::ofstream out(file);
        out << "tiktok.com|tfxktok.com^vxtiktok.com\nx.com|stupid.example^fxtwitter.com\n";
    }

    // Somebody already set x.com up by hand; the import must not undo that.
    fixture.store.set(guild, {.domain = "x.com", .mirrors = {{.host = "vxtwitter.com", .translate_suffix = ""}}});

    CHECK(latibot::events::import_url_rules_file(fixture.store, guild, file) == 1);
    CHECK(fixture.store.find(guild, "x.com")->mirrors.front().host == "vxtwitter.com");
    CHECK(fixture.store.find(guild, "tiktok.com")->mirrors.size() == 2);

    CHECK(latibot::events::import_url_rules_file(fixture.store, guild, file) == 0);
}

TEST_CASE("a missing rule file is not an error", "[db][fs]") {
    store_fixture fixture;
    const latibot::testing::temp_directory folder;

    CHECK_FALSE(latibot::events::import_url_rules_file(fixture.store, guild, folder.path() / "nope.txt").has_value());
}
