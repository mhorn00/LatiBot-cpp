// /urlrepl, its panel, and /urltoggle (plan v4 §9.5).

#include "core/commands/urlrepl.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/url_rules.hpp"
#include "core/ui/paginator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <variant>
#include <vector>

using latibot::commands::build_rule;
using latibot::commands::render_test;
using latibot::events::url_rule;

namespace {

constexpr dpp::snowflake guild{1000};

url_rule rule_from(std::string_view domain, std::string_view mirrors) {
    auto built = build_rule(domain, mirrors);
    REQUIRE(std::holds_alternative<url_rule>(built));
    return std::get<url_rule>(built);
}

std::string problem_with(std::string_view domain, std::string_view mirrors) {
    auto built = build_rule(domain, mirrors);
    REQUIRE(std::holds_alternative<std::string>(built));
    return std::get<std::string>(built);
}

struct store_fixture {
    latibot::db::database db{":memory:"};
    latibot::events::url_rule_store store{db};

    store_fixture() { latibot::db::migrate(db); }
};

/// Every custom_id and label on a message, checked against Discord's limits
/// (plan v4 §21.4).
void check_components_fit(const dpp::message& message) {
    CHECK(message.components.size() <= 5);
    for (const dpp::component& row : message.components) {
        CHECK(row.components.size() <= 5);
        for (const dpp::component& part : row.components) {
            INFO("custom_id: " << part.custom_id);
            CHECK(part.custom_id.size() <= latibot::ui::custom_id_limit);
            CHECK(part.label.size() <= 80);
            CHECK(part.options.size() <= 25);
            for (const dpp::select_option& option : part.options) {
                CHECK(option.label.size() <= 100);
                CHECK(option.value.size() <= 100);
                CHECK(option.description.size() <= 100);
            }
        }
    }
}

} // namespace

// --------------------------------------------------------------------------
// Building a rule from what somebody typed
// --------------------------------------------------------------------------

TEST_CASE("mirrors may be typed on one line or one per line", "[commands]") {
    const auto spaced = rule_from("x.com", "fxtwitter.com/en vxtwitter.com");
    const auto commas = rule_from("x.com", "fxtwitter.com/en, vxtwitter.com");
    const auto lines = rule_from("x.com", "fxtwitter.com/en\nvxtwitter.com\n");

    for (const url_rule& rule : {spaced, commas, lines}) {
        REQUIRE(rule.mirrors.size() == 2);
        CHECK(rule.mirrors[0].host == "fxtwitter.com");
        CHECK(rule.mirrors[0].translate_suffix == "/en");
        CHECK(rule.mirrors[1].host == "vxtwitter.com");
    }
}

TEST_CASE("the site is reduced to what links are matched by", "[commands]") {
    CHECK(rule_from("https://www.X.com/home", "fxtwitter.com").domain == "x.com");
}

TEST_CASE("a mirror listed twice is kept once, in its first place", "[commands]") {
    const auto rule = rule_from("x.com", "fxtwitter.com vxtwitter.com fxtwitter.com");
    CHECK(rule.mirrors.size() == 2);
}

TEST_CASE("rules that could not work are refused with a reason", "[commands]") {
    CHECK(problem_with("x.com", "") == "a rule needs at least one mirror to send links to");
    CHECK(problem_with("x.com", "x.com").find("can't be its own mirror") != std::string::npos);
    CHECK(problem_with("localhost", "fxtwitter.com").find("doesn't look like a site") != std::string::npos);
    CHECK(problem_with("x.com", "notamirror").find("doesn't look like a mirror") != std::string::npos);
    CHECK(problem_with("x.com", "a.com b.com c.com d.com e.com f.com g.com h.com i.com").find("most one rule takes") != std::string::npos);
}

TEST_CASE("a rule describes itself in one line", "[commands]") {
    CHECK(latibot::commands::describe(rule_from("x.com", "fxtwitter.com/en vxtwitter.com")) ==
          "**x.com** → fxtwitter.com/en, vxtwitter.com");
}

// --------------------------------------------------------------------------
// The dry run
// --------------------------------------------------------------------------

TEST_CASE("the dry run shows the post and accounts for every link", "[commands]") {
    const std::vector<url_rule> rules{rule_from("x.com", "fxtwitter.com vxtwitter.com")};
    const std::string reply = render_test("||https://x.com/a/status/1|| https://example.com/b <https://x.com/c>", rules, false);

    CHECK(reply.find("🔗 ||[_](https://fxtwitter.com/a/status/1)||") != std::string::npos);
    CHECK(reply.find("replaced using the x.com rule, trying fxtwitter.com, vxtwitter.com (spoilered") != std::string::npos);
    CHECK(reply.find("no rule for example.com") != std::string::npos);
    CHECK(reply.find("turns its preview off") != std::string::npos);
    CHECK(reply.find("opted out") == std::string::npos);
}

TEST_CASE("the dry run says when the person running it has opted out", "[commands]") {
    const std::vector<url_rule> rules{rule_from("x.com", "fxtwitter.com")};
    CHECK(render_test("https://x.com/a", rules, true).find("opted out") != std::string::npos);
}

TEST_CASE("the dry run says when there is nothing to do", "[commands]") {
    const std::vector<url_rule> rules{rule_from("x.com", "fxtwitter.com")};
    CHECK(render_test("no links here", rules, false) == "There are no links in that.");
    CHECK(render_test("https://example.com", rules, false).starts_with("**Nothing would be posted.**"));
}

TEST_CASE("a dry run of a long message stays under Discord's limit", "[commands]") {
    const std::vector<url_rule> rules{rule_from("x.com", "fxtwitter.com")};
    std::string content;
    for (int index = 0; index < 200; ++index) {
        content += "https://example.com/some/rather/long/path/" + std::to_string(index) + " ";
    }

    const std::string reply = render_test(content, rules, false);
    CHECK(reply.size() <= 2000);
    CHECK(reply.find("more") != std::string::npos);
}

// --------------------------------------------------------------------------
// The list and the panel
// --------------------------------------------------------------------------

TEST_CASE("an empty list says how to start one", "[commands]") {
    store_fixture fixture;
    const auto message = latibot::commands::render_url_rule_list(fixture.store, guild, 0);
    CHECK(message.content.find("/urlrepl set") != std::string::npos);
    CHECK(message.components.empty());
}

TEST_CASE("the panel lists a page of rules with a menu to pick one", "[commands]") {
    store_fixture fixture;
    for (const char* domain : {"a.com", "b.com", "c.com", "d.com", "e.com", "f.com", "g.com"}) {
        fixture.store.set(guild, rule_from(domain, "mirror.example"));
    }

    const auto first = latibot::commands::render_url_panel(fixture.store, guild, 0);
    CHECK(first.content.find("**a.com**") != std::string::npos);
    CHECK(first.content.find("**f.com**") == std::string::npos);
    check_components_fit(first);

    // Menu and footer; no Edit/Delete until something is picked.
    REQUIRE(first.components.size() == 2);
    CHECK(first.components[0].components[0].options.size() == latibot::commands::url_rules_per_page);
}

TEST_CASE("picking a rule offers Edit and Delete for it", "[commands]") {
    store_fixture fixture;
    fixture.store.set(guild, rule_from("x.com", "fxtwitter.com"));

    const auto picked = latibot::commands::render_url_panel(fixture.store, guild, 0, "x.com");
    REQUIRE(picked.components.size() == 3);
    check_components_fit(picked);

    const auto& row = picked.components[1].components;
    REQUIRE(row.size() == 2);
    CHECK(row[0].label == "Edit");
    CHECK(latibot::ui::decode(row[0].custom_id)->argument == "x.com");

    const auto confirming = latibot::commands::render_url_panel(fixture.store, guild, 0, "x.com", true);
    CHECK(confirming.components[1].components[0].label == "Delete x.com");
    CHECK(latibot::ui::decode(confirming.components[1].components[0].custom_id)->view == latibot::commands::url_confirm_view);
}

TEST_CASE("the panel follows a rule to the page it sorts onto", "[commands]") {
    store_fixture fixture;
    for (const char* domain : {"a.com", "b.com", "c.com", "d.com", "e.com", "z.com"}) {
        fixture.store.set(guild, rule_from(domain, "mirror.example"));
    }

    const auto panel = latibot::commands::render_url_panel(fixture.store, guild, 0, "z.com");
    CHECK(panel.content.find("**z.com**") != std::string::npos);
    CHECK(panel.content.find("Page 2 of 2") != std::string::npos);
}

TEST_CASE("the URL rule modal fits inside Discord's limits", "[commands]") {
    const url_rule existing = rule_from("x.com", "fxtwitter.com/en vxtwitter.com");

    const std::array<const url_rule*, 2> shapes{nullptr, &existing};
    for (const url_rule* rule : shapes) {
        const auto form = latibot::commands::url_rule_form(0, rule);

        CHECK(form.title.size() <= 45);
        CHECK(form.custom_id.size() <= latibot::ui::custom_id_limit);
        REQUIRE(form.components.size() == 2);

        for (const auto& row : form.components) {
            for (const dpp::component& input : row) {
                INFO("label: " << input.label);
                CHECK_FALSE(input.label.empty());
                CHECK(input.label.size() <= 45);
                CHECK(input.placeholder.size() <= 100);
            }
        }
    }

    // Editing shows the mirrors one per line, in order, ready to reorder.
    const auto form = latibot::commands::url_rule_form(0, &existing);
    CHECK(std::get<std::string>(form.components[1][0].value) == "fxtwitter.com/en\nvxtwitter.com");
    CHECK(latibot::ui::decode(form.custom_id)->argument == "x.com");
}

TEST_CASE("the commands are registered the way Discord expects", "[commands]") {
    store_fixture fixture;
    const latibot::commands::urlrepl_command urlrepl(fixture.store);
    const latibot::commands::urltoggle_command toggle(fixture.store);

    const dpp::slashcommand repl = urlrepl.build("urlrepl", dpp::snowflake{1});
    CHECK(repl.default_member_permissions.can(dpp::p_manage_guild));
    REQUIRE(repl.options.size() == 5);

    const dpp::slashcommand opt_out = toggle.build("urltoggle", dpp::snowflake{1});
    // Everybody may opt themselves out.
    CHECK_FALSE(toggle.info().default_member_permissions.has_value());
    CHECK(opt_out.options.size() == 1);
}
