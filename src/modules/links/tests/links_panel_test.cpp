// The URL rule panel, used end to end as a person would use it in Discord:
// open it, press, pick, fill in a form, and look at what was stored and
// shown. Through DPP's own reading and writing of interactions
// (support/panel_harness.hpp), since that is where the panels once broke.

#include "links/module.hpp"
#include "links/url_rules.hpp"
#include "links_command.hpp"

#include "core/db/database.hpp"

#include "support/panel_harness.hpp"
#include "support/panel_queries.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

using latibot::testing::field_value;
using latibot::testing::option_in;
using latibot::testing::panel_harness;
using json = nlohmann::json;

namespace {

constexpr dpp::snowflake guild{1000};

struct database_fixture {
    latibot::db::database db{":memory:"};

    database_fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::links::schema());
    }
};

} // namespace

// --------------------------------------------------------------------------
// The URL rule panel
// --------------------------------------------------------------------------

namespace {

struct url_fixture : database_fixture {
    latibot::events::url_rule_store store{db};
    latibot::commands::url_panel router{store};
    panel_harness discord{
        [this](const auto& event, const auto& state, const std::string& chosen) { return router.on_component(event, state, chosen); },
        [this](const auto& event, const auto& state) { return router.on_form(event, state); }};

    url_fixture() { discord.open(latibot::commands::render_url_panel(store, guild, 0)); }

    /// Adds a rule and opens the panel again.
    auto seed(std::string domain, std::string mirror) -> void {
        store.set(guild, {.domain = std::move(domain), .mirrors = {{.host = std::move(mirror), .translate_suffix = {}}}});
        discord.open(latibot::commands::render_url_panel(store, guild, 0));
    }

    [[nodiscard]] auto mirrors_of(std::string_view domain) const -> std::string {
        const auto rule = store.find(guild, domain);
        return rule ? latibot::commands::describe_mirrors(rule->mirrors) : std::string("(none)");
    }
};

} // namespace

TEST_CASE("the URL panel adds a rule", "[links]") {
    url_fixture test;

    const json answer =
        test.discord.submit(test.discord.press("Add rule"), {{"domain", "x.com"}, {"mirrors", "fxtwitter.com/en\nvxtwitter.com"}});
    REQUIRE(panel_harness::is_update(answer));

    CHECK(test.mirrors_of("x.com") == "fxtwitter.com/en, vxtwitter.com");
    CHECK(test.discord.content().contains("x.com added"));
    const auto picked = option_in(test.discord.panel(), "x.com");
    REQUIRE(picked.has_value());
    CHECK(picked->is_default);
}

TEST_CASE("the URL panel edits a rule's mirrors, and an untouched form changes nothing", "[links]") {
    url_fixture test;
    test.seed("x.com", "fxtwitter.com");
    test.discord.choose("x.com");

    const json form = test.discord.press("Edit");
    CHECK(field_value(form, "domain") == "x.com");
    CHECK(field_value(form, "mirrors") == "fxtwitter.com");
    REQUIRE(panel_harness::is_update(test.discord.submit(form)));
    CHECK(test.mirrors_of("x.com") == "fxtwitter.com");

    REQUIRE(panel_harness::is_update(test.discord.submit(test.discord.press("Edit"), {{"mirrors", "vxtwitter.com\nfixupx.com"}})));
    CHECK(test.mirrors_of("x.com") == "vxtwitter.com, fixupx.com");
    CHECK(test.discord.content().contains("x.com changed"));
}

TEST_CASE("the URL panel renames a rule by editing its site", "[links]") {
    url_fixture test;
    test.seed("x.com", "fxtwitter.com");
    test.discord.choose("x.com");

    REQUIRE(panel_harness::is_update(test.discord.submit(test.discord.press("Edit"), {{"domain", "twitter.com"}})));
    CHECK_FALSE(test.store.find(guild, "x.com").has_value());
    CHECK(test.mirrors_of("twitter.com") == "fxtwitter.com");
}

TEST_CASE("the URL panel will not save over another site's rule", "[links]") {
    url_fixture test;
    test.seed("x.com", "fxtwitter.com");
    test.seed("twitter.com", "vxtwitter.com");

    SECTION("adding a site that has one") {
        const json answer = test.discord.submit(test.discord.press("Add rule"), {{"domain", "twitter.com"}, {"mirrors", "fixupx.com"}});
        CHECK(panel_harness::is_private_note(answer));
        CHECK(panel_harness::text_of(answer).contains("already a rule for twitter.com"));
    }
    SECTION("renaming onto one") {
        test.discord.choose("x.com");
        const json answer = test.discord.submit(test.discord.press("Edit"), {{"domain", "twitter.com"}});
        CHECK(panel_harness::is_private_note(answer));
    }

    CHECK(test.mirrors_of("x.com") == "fxtwitter.com");
    CHECK(test.mirrors_of("twitter.com") == "vxtwitter.com");
}

TEST_CASE("the URL panel deletes a rule once confirmed, and turns replacement on and off", "[links]") {
    url_fixture test;
    test.seed("x.com", "fxtwitter.com");

    test.discord.press("Turn replacement on");
    CHECK(test.store.enabled(guild));
    test.discord.press("Turn replacement off");
    CHECK_FALSE(test.store.enabled(guild));

    test.discord.choose("x.com");
    test.discord.press("Delete");
    CHECK(test.store.find(guild, "x.com").has_value());
    test.discord.press("Delete x.com");
    CHECK_FALSE(test.store.find(guild, "x.com").has_value());
    CHECK(test.discord.content().contains("deleted the rule for x.com"));
}
