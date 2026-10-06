// The trigger panel, used end to end as a person would use it in Discord:
// open it, press, pick, fill in a form, and look at what was stored and
// shown. Through DPP's own reading and writing of interactions
// (support/panel_harness.hpp), since that is where the panels once broke.

#include "trigger_command.hpp"
#include "triggers.hpp"

#include "core/db/database.hpp"

#include "support/panel_harness.hpp"
#include "support/panel_queries.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <optional>
#include <string>

using latibot::testing::field_value;
using latibot::testing::has_button;
using latibot::testing::option_in;
using latibot::testing::panel_harness;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};

struct database_fixture {
    latibot::db::database db{":memory:"};

    database_fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::events::triggers_schema());
    }
};

} // namespace

// --------------------------------------------------------------------------
// The trigger panel
// --------------------------------------------------------------------------

namespace {

struct trigger_fixture : database_fixture {
    latibot::events::trigger_store store{db};
    latibot::commands::trigger_panel router{store};
    panel_harness discord{
        [this](const auto& event, const auto& state, const std::string& chosen) { return router.on_component(event, state, chosen); },
        [this](const auto& event, const auto& state) { return router.on_form(event, state); }};

    trigger_fixture() { discord.open(latibot::commands::render_trigger_panel(store, guild, 0)); }

    /// Adds a trigger and opens the panel again, as someone would run
    /// `/trigger panel` to see it.
    auto seed(std::string pattern) -> std::int64_t {
        const std::int64_t id = store.add({.guild_id = guild,
                                           .pattern = std::move(pattern),
                                           .mode = latibot::events::match_mode::whole_word,
                                           .cooldown = 30s,
                                           .enabled = true,
                                           .responses = {{.text = "nice", .weight = 1}, {.text = "very nice", .weight = 3}}});
        discord.open(latibot::commands::render_trigger_panel(store, guild, 0));
        return id;
    }
};

} // namespace

TEST_CASE("the trigger panel adds a trigger as it was typed", "[triggers]") {
    trigger_fixture test;

    const json form = test.discord.press("Add");
    CHECK(field_value(form, "mode") == "word");
    CHECK(field_value(form, "cooldown") == std::to_string(latibot::events::default_trigger_cooldown.count()));

    const json answer =
        test.discord.submit(form, {{"pattern", " hello "}, {"responses", "hi there\n3 | hey"}, {"mode", "Anywhere"}, {"cooldown", "10"}});
    REQUIRE(panel_harness::is_update(answer));

    const auto all = test.store.for_guild(guild);
    REQUIRE(all.size() == 1);
    CHECK(all[0].pattern == "hello");
    CHECK(all[0].mode == latibot::events::match_mode::substring);
    CHECK(all[0].cooldown == 10s);
    REQUIRE(all[0].responses.size() == 2);
    CHECK(all[0].responses[1].text == "hey");
    CHECK(all[0].responses[1].weight == 3);

    // The panel shows it, picked, ready for the next change.
    CHECK(test.discord.content().contains(std::format("added trigger `{}`", all[0].id)));
    const auto picked = option_in(test.discord.panel(), std::to_string(all[0].id));
    REQUIRE(picked.has_value());
    CHECK(picked->is_default);
    CHECK(has_button(test.discord.panel(), "Edit"));
}

TEST_CASE("the trigger panel's form, sent back untouched, changes nothing", "[triggers]") {
    trigger_fixture test;
    const std::int64_t id = test.seed("hello");
    const latibot::events::trigger before = *test.store.find(id, guild);

    test.discord.choose(std::to_string(id));
    const json form = test.discord.press("Edit");
    CHECK(field_value(form, "pattern") == "hello");
    CHECK(field_value(form, "responses") == "nice\n3 | very nice");
    CHECK(field_value(form, "mode") == "word");
    CHECK(field_value(form, "cooldown") == "30");

    REQUIRE(panel_harness::is_update(test.discord.submit(form)));
    const auto after = test.store.find(id, guild);
    REQUIRE(after.has_value());
    CHECK(after->pattern == before.pattern);
    CHECK(after->mode == before.mode);
    CHECK(after->cooldown == before.cooldown);
    CHECK(after->responses.size() == before.responses.size());
    CHECK(test.discord.content().contains(std::format("saved trigger `{}`", id)));
}

TEST_CASE("the trigger panel's form saves what it can read, and says what it kept", "[triggers]") {
    trigger_fixture test;
    const std::int64_t id = test.seed("hello");

    test.discord.choose(std::to_string(id));
    const json answer =
        test.discord.submit(test.discord.press("Edit"), {{"pattern", "goodbye"}, {"mode", "whole word"}, {"cooldown", "thirty"}});
    REQUIRE(panel_harness::is_update(answer));

    const auto after = test.store.find(id, guild);
    REQUIRE(after.has_value());
    CHECK(after->pattern == "goodbye");
    CHECK(after->mode == latibot::events::match_mode::whole_word);
    CHECK(after->cooldown == 30s);
    CHECK(test.discord.content().contains("\"thirty\" isn't a number of seconds"));
}

TEST_CASE("the trigger panel refuses a form that could not work, and keeps the trigger", "[triggers]") {
    trigger_fixture test;
    const std::int64_t id = test.seed("hello");

    test.discord.choose(std::to_string(id));
    const json answer = test.discord.submit(test.discord.press("Edit"), {{"pattern", "   "}});
    CHECK(panel_harness::is_private_note(answer));
    CHECK(test.store.find(id, guild)->pattern == "hello");
}

TEST_CASE("the trigger panel's buttons flip what they say, and say the new state", "[triggers]") {
    trigger_fixture test;
    const std::int64_t id = test.seed("hello");
    test.discord.choose(std::to_string(id));

    test.discord.press("Disable");
    CHECK_FALSE(test.store.find(id, guild)->enabled);
    CHECK(has_button(test.discord.panel(), "Enable"));

    test.discord.press("Answer bots");
    CHECK(test.store.find(id, guild)->respond_to_bots);
    CHECK(has_button(test.discord.panel(), "Ignore bots"));

    // A trigger replies silently unless told otherwise.
    test.discord.press("Reply with notifications");
    CHECK((test.store.find(id, guild)->message_flags & dpp::m_suppress_notifications) == 0);
    CHECK(has_button(test.discord.panel(), "Reply silently"));

    test.discord.press("Hide link previews");
    CHECK((test.store.find(id, guild)->message_flags & dpp::m_suppress_embeds) != 0);
    CHECK(has_button(test.discord.panel(), "Show link previews"));

    test.discord.press("Enable");
    CHECK(test.store.find(id, guild)->enabled);
}

TEST_CASE("the trigger panel deletes only once it is confirmed", "[triggers]") {
    trigger_fixture test;
    const std::int64_t id = test.seed("hello");
    test.discord.choose(std::to_string(id));

    test.discord.press("Delete");
    CHECK(test.store.find(id, guild).has_value());
    test.discord.press("Cancel");
    CHECK(test.store.find(id, guild).has_value());

    test.discord.press("Delete");
    test.discord.press(std::format("Delete {}", id));
    CHECK_FALSE(test.store.find(id, guild).has_value());
    CHECK_FALSE(has_button(test.discord.panel(), "Edit"));
}

TEST_CASE("a trigger added past the first page is shown on its own page, picked", "[triggers]") {
    trigger_fixture test;
    for (std::size_t index = 0; index < latibot::commands::triggers_per_page; ++index) {
        test.seed(std::format("pattern{}", index));
    }

    test.discord.submit(test.discord.press("Add"), {{"pattern", "the ninth"}, {"responses", "yes"}});

    CHECK(test.discord.content().contains("the ninth"));
    CHECK(test.discord.content().contains("Page 2 of 2"));
    const auto added = test.store.for_guild(guild).back();
    const auto picked = option_in(test.discord.panel(), std::to_string(added.id));
    REQUIRE(picked.has_value());
    CHECK(picked->is_default);
}
