// /urlrepl, its panel, and /urltoggle (plan v4 §9.5).

#include "core/commands/urlrepl.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/url_rules.hpp"
#include "core/ui/paginator.hpp"

#include "support/discord_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <string>
#include <utility>
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
    const std::string reply = render_test("||https://x.com/a/status/1|| https://example.com/b <https://x.com/c>", rules, false, true);

    CHECK(reply.find("🔗 ||[_](https://fxtwitter.com/a/status/1)||") != std::string::npos);
    CHECK(reply.find("replaced using the x.com rule, trying fxtwitter.com, vxtwitter.com (spoilered") != std::string::npos);
    CHECK(reply.find("no rule for example.com") != std::string::npos);
    CHECK(reply.find("turns its preview off") != std::string::npos);
    CHECK(reply.find("opted out") == std::string::npos);
}

TEST_CASE("the dry run says when the person running it has opted out", "[commands]") {
    const std::vector<url_rule> rules{rule_from("x.com", "fxtwitter.com")};
    CHECK(render_test("https://x.com/a", rules, true, true).find("opted out") != std::string::npos);
}

TEST_CASE("the dry run works while replacement is off, and says that it is", "[commands]") {
    // Trying rules out before switching them on is the point of it.
    const std::vector<url_rule> rules{rule_from("x.com", "fxtwitter.com")};
    const std::string off = render_test("https://x.com/a", rules, false, false);
    CHECK(off.find("**Would post:**") != std::string::npos);
    CHECK(off.find("`/urlrepl enable`") != std::string::npos);

    CHECK(render_test("https://x.com/a", rules, false, true).find("`/urlrepl enable`") == std::string::npos);
}

TEST_CASE("the dry run says when there is nothing to do", "[commands]") {
    const std::vector<url_rule> rules{rule_from("x.com", "fxtwitter.com")};
    CHECK(render_test("no links here", rules, false, true) == "There are no links in that.");
    CHECK(render_test("https://example.com", rules, false, true).starts_with("**Nothing would be posted.**"));
}

TEST_CASE("a dry run of a long message stays under Discord's limit", "[commands]") {
    const std::vector<url_rule> rules{rule_from("x.com", "fxtwitter.com")};
    std::string content;
    for (int index = 0; index < 200; ++index) {
        content += "https://example.com/some/rather/long/path/" + std::to_string(index) + " ";
    }

    const std::string reply = render_test(content, rules, false, true);
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

TEST_CASE("a long list pages", "[commands]") {
    store_fixture fixture;
    for (const char* domain : {"a.com", "b.com", "c.com", "d.com", "e.com", "f.com", "g.com", "h.com", "i.com", "j.com", "k.com"}) {
        fixture.store.set(guild, rule_from(domain, "mirror.example"));
    }

    // First, middle and last: the paging buttons clamp at either end.
    for (const int page : {0, 1, 2}) {
        const auto list = latibot::commands::render_url_rule_list(fixture.store, guild, page);
        latibot::testing::check_message_fits(list);
        CHECK(list.components.size() == 1);
    }
}

TEST_CASE("the list and the panel say whether replacement is on", "[commands]") {
    store_fixture fixture;
    fixture.store.set(guild, rule_from("x.com", "fxtwitter.com"));

    CHECK(latibot::commands::render_url_rule_list(fixture.store, guild, 0).content.find("**off**") != std::string::npos);
    CHECK(latibot::commands::render_url_panel(fixture.store, guild, 0).content.find("**off**") != std::string::npos);

    fixture.store.set_enabled(guild, true);
    CHECK(latibot::commands::render_url_rule_list(fixture.store, guild, 0).content.find("**on**") != std::string::npos);
    CHECK(latibot::commands::render_url_panel(fixture.store, guild, 0).content.find("**on**") != std::string::npos);
}

TEST_CASE("the panel's switch asks for the opposite of what is set", "[commands]") {
    store_fixture fixture;

    using label_and_argument = std::pair<std::string, std::string>;
    const auto find_switch = [&]() -> std::optional<label_and_argument> {
        const auto panel = latibot::commands::render_url_panel(fixture.store, guild, 0);
        latibot::testing::check_message_fits(panel);
        for (const dpp::component& row : panel.components) {
            for (const dpp::component& part : row.components) {
                const auto state = latibot::ui::decode(part.custom_id);
                if (state && state->view == latibot::commands::url_switch_view) {
                    return label_and_argument{part.label, state->argument};
                }
            }
        }
        return std::nullopt;
    };

    // The argument is the state wanted rather than "toggle", so a second
    // press from a panel that has not refreshed changes nothing.
    CHECK(find_switch() == label_and_argument{"Turn replacement on", "on"});
    fixture.store.set_enabled(guild, true);
    CHECK(find_switch() == label_and_argument{"Turn replacement off", "off"});
}

TEST_CASE("turning replacement on or off says what changed", "[commands]") {
    store_fixture fixture;
    const latibot::commands::user_label who{.name = "someone", .id = dpp::snowflake{5000}};

    CHECK(latibot::commands::switch_url_replacement(fixture.store, guild, true, who, ""));
    CHECK(fixture.store.enabled(guild));
    CHECK_FALSE(latibot::commands::switch_url_replacement(fixture.store, guild, true, who, ""));

    CHECK(latibot::commands::switch_url_replacement(fixture.store, guild, false, who, ""));
    CHECK_FALSE(fixture.store.enabled(guild));

    using latibot::commands::render_switch;
    CHECK(render_switch(false, true, 3) == "Link replacement was already on here.");
    CHECK(render_switch(true, true, 0).find("no rules yet") != std::string::npos);
    CHECK(render_switch(true, true, 1).find("Its one rule applies") != std::string::npos);
    CHECK(render_switch(true, true, 3).find("Its 3 rules apply") != std::string::npos);
    CHECK(render_switch(true, false, 3).find("rules are kept") != std::string::npos);
}

TEST_CASE("the panel lists a page of rules with a menu to pick one", "[commands]") {
    store_fixture fixture;
    for (const char* domain : {"a.com", "b.com", "c.com", "d.com", "e.com", "f.com", "g.com"}) {
        fixture.store.set(guild, rule_from(domain, "mirror.example"));
    }

    const auto first = latibot::commands::render_url_panel(fixture.store, guild, 0);
    CHECK(first.content.find("**a.com**") != std::string::npos);
    CHECK(first.content.find("**f.com**") == std::string::npos);
    latibot::testing::check_message_fits(first);

    // Menu and footer; no Edit/Delete until something is picked.
    REQUIRE(first.components.size() == 2);
    CHECK(first.components[0].components[0].options.size() == latibot::commands::url_rules_per_page);
}

TEST_CASE("picking a rule offers Edit and Delete for it", "[commands]") {
    store_fixture fixture;
    fixture.store.set(guild, rule_from("x.com", "fxtwitter.com"));

    const auto picked = latibot::commands::render_url_panel(fixture.store, guild, 0, "x.com");
    REQUIRE(picked.components.size() == 3);
    latibot::testing::check_message_fits(picked);

    const auto& row = picked.components[1].components;
    REQUIRE(row.size() == 2);
    CHECK(row[0].label == "Edit");
    CHECK(latibot::ui::decode(row[0].custom_id)->argument == "x.com");

    const auto confirming = latibot::commands::render_url_panel(fixture.store, guild, 0, "x.com", true);
    CHECK(confirming.components[1].components[0].label == "Delete x.com");
    CHECK(latibot::ui::decode(confirming.components[1].components[0].custom_id)->view == latibot::commands::url_confirm_view);
}

TEST_CASE("confirming a delete on the first or last page fits, and Cancel keeps the rule picked", "[commands]") {
    // Cancel once encoded the same state as ◀ on the first page and ▶ on
    // the last, and Discord refuses a message with a custom_id twice.
    store_fixture fixture;
    for (const char* domain : {"a.com", "b.com", "c.com", "d.com", "e.com", "f.com"}) {
        fixture.store.set(guild, rule_from(domain, "mirror.example"));
    }

    for (const auto& [page, domain] : {std::pair{0, "a.com"}, std::pair{1, "f.com"}}) {
        INFO("page " << page);
        const auto confirming = latibot::commands::render_url_panel(fixture.store, guild, page, domain, /*confirming_delete=*/true);
        latibot::testing::check_message_fits(confirming);

        REQUIRE(confirming.components.size() == 3);
        const auto& row = confirming.components[1].components;
        REQUIRE(row.size() == 2);
        CHECK(row[1].label == "Cancel");
        const auto cancel = latibot::ui::decode(row[1].custom_id);
        REQUIRE(cancel.has_value());
        CHECK(cancel->view == latibot::commands::url_panel_view);
        CHECK(cancel->argument == domain);
    }
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
        latibot::testing::check_modal_fits(form);
        CHECK(form.components.size() == 2);
    }

    // Editing shows the mirrors one per line, in order, ready to reorder.
    const auto form = latibot::commands::url_rule_form(0, &existing);
    CHECK(std::get<std::string>(form.components[1][0].value) == "fxtwitter.com/en\nvxtwitter.com");
    CHECK(latibot::ui::decode(form.custom_id)->argument == "x.com");
}

TEST_CASE("anyone may opt themselves out, and only Manage Server may for somebody else", "[commands]") {
    // Everyone may run /urltoggle, so Discord's permissions cannot tell the
    // two apart; this is the check (plan §21.13).
    using latibot::commands::urltoggle_refusal;
    const dpp::snowflake me{1};
    const dpp::snowflake them{2};

    CHECK_FALSE(urltoggle_refusal(me, me, dpp::permission{}).has_value());
    CHECK(urltoggle_refusal(me, them, dpp::permission{}) == "changing that for somebody else needs Manage Server");
    CHECK(urltoggle_refusal(me, them, dpp::permission(dpp::p_manage_messages)).has_value());
    CHECK_FALSE(urltoggle_refusal(me, them, dpp::permission(dpp::p_manage_guild)).has_value());
    CHECK_FALSE(urltoggle_refusal(me, them, dpp::permission(dpp::p_administrator)).has_value());
}

TEST_CASE("the commands are registered the way Discord expects", "[commands]") {
    store_fixture fixture;
    const latibot::commands::urlrepl_command urlrepl(fixture.store);
    const latibot::commands::urltoggle_command toggle(fixture.store);

    const dpp::slashcommand repl = urlrepl.build("urlrepl", dpp::snowflake{1});
    CHECK(repl.default_member_permissions.can(dpp::p_manage_guild));
    REQUIRE(repl.options.size() == 7);
    CHECK(repl.options[0].name == "enable");
    CHECK(repl.options[1].name == "disable");

    const dpp::slashcommand opt_out = toggle.build("urltoggle", dpp::snowflake{1});
    // Everybody may opt themselves out.
    CHECK_FALSE(toggle.info().default_member_permissions.has_value());
    CHECK(opt_out.options.size() == 1);
}
