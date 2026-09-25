#include "core/commands/trigger.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/ui/paginator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <string>
#include <vector>

using latibot::commands::describe;
using latibot::commands::format_responses;
using latibot::commands::parse_responses;
using namespace std::chrono_literals;

TEST_CASE("responses are one per line", "[commands]") {
    const auto responses = parse_responses("nice\nvery nice\n");

    REQUIRE(responses.size() == 2);
    CHECK(responses[0].text == "nice");
    CHECK(responses[0].weight == 1);
    CHECK(responses[1].text == "very nice");
}

TEST_CASE("a leading number and bar sets the weight", "[commands]") {
    const auto responses = parse_responses("3 | common\nrare");

    REQUIRE(responses.size() == 2);
    CHECK(responses[0].text == "common");
    CHECK(responses[0].weight == 3);
    CHECK(responses[1].text == "rare");
    CHECK(responses[1].weight == 1);
}

TEST_CASE("a bar that is not a weight stays part of the response", "[commands]") {
    // People write "a | b" meaning the text, and losing half of it would be
    // worse than not supporting weights at all.
    const auto responses = parse_responses("this | that\n|leading bar");

    REQUIRE(responses.size() == 2);
    CHECK(responses[0].text == "this | that");
    CHECK(responses[0].weight == 1);
    CHECK(responses[1].text == "|leading bar");
}

TEST_CASE("blank lines are skipped", "[commands]") {
    const auto responses = parse_responses("\n\nnice\n   \n\nalso nice\n\n");

    REQUIRE(responses.size() == 2);
    CHECK(responses[0].text == "nice");
    CHECK(responses[1].text == "also nice");
}

TEST_CASE("nothing usable parses to nothing", "[commands]") {
    // The command turns this into an error rather than saving a trigger that
    // matches and then has nothing to say.
    CHECK(parse_responses("").empty());
    CHECK(parse_responses("   \n\t\n").empty());
}

TEST_CASE("responses round trip through their text form", "[commands]") {
    const std::string original = "3 | common\nrare";
    const auto responses = parse_responses(original);

    // The weight is only written back when it is not the default, so editing
    // a trigger nobody weighted does not add noise to the box.
    CHECK(format_responses(responses) == original);
    CHECK(format_responses(parse_responses("just this")) == "just this");
}

TEST_CASE("a trigger describes itself in one line", "[commands]") {
    latibot::events::trigger entry{.id = 7,
                                   .guild_id = dpp::snowflake{1},
                                   .pattern = "420",
                                   .mode = latibot::events::match_mode::whole_word,
                                   .cooldown = 30s,
                                   .enabled = true,
                                   .responses = {{.text = "nice", .weight = 1}}};

    CHECK(describe(entry) == "`7` **420** (whole word, 30s) -> 1 response");

    SECTION("a zero cooldown says so rather than showing 0s") {
        entry.cooldown = 0s;
        CHECK(describe(entry) == "`7` **420** (whole word, no cooldown) -> 1 response");
    }

    SECTION("a disabled trigger is marked") {
        entry.enabled = false;
        CHECK(describe(entry) == "`7` **420** (whole word, 30s, disabled) -> 1 response");
    }

    SECTION("several responses pluralise") {
        entry.responses.push_back({.text = "very nice", .weight = 1});
        CHECK(describe(entry) == "`7` **420** (whole word, 30s) -> 2 responses");
    }
}

TEST_CASE("the modal keeps fields it cannot read rather than resetting them", "[commands]") {
    using latibot::commands::apply_form;
    using latibot::commands::form_fields;

    latibot::events::trigger entry{.id = 1,
                                   .guild_id = dpp::snowflake{1},
                                   .pattern = "420",
                                   .mode = latibot::events::match_mode::substring,
                                   .cooldown = 45s,
                                   .enabled = true,
                                   .responses = {{.text = "nice", .weight = 1}}};

    // Someone typing "thirty" into a box should not silently lose what was
    // there, which is the difference between an optional field and a reset.
    const auto problem = apply_form(entry, {.pattern = "69", .responses = "nice", .mode = "sideways", .cooldown = "thirty"});

    CHECK_FALSE(problem.has_value());
    CHECK(entry.pattern == "69");
    CHECK(entry.mode == latibot::events::match_mode::substring);
    CHECK(entry.cooldown == 45s);
}

TEST_CASE("the modal applies the fields it can read", "[commands]") {
    using latibot::commands::apply_form;

    latibot::events::trigger entry{.pattern = "420", .mode = latibot::events::match_mode::whole_word, .cooldown = 30s};

    const auto problem = apply_form(entry, {.pattern = "  69  ", .responses = "2 | nice\nvery nice", .mode = "anywhere", .cooldown = "0"});

    REQUIRE_FALSE(problem.has_value());
    CHECK(entry.pattern == "69");
    CHECK(entry.mode == latibot::events::match_mode::substring);
    CHECK(entry.cooldown == 0s);
    REQUIRE(entry.responses.size() == 2);
    CHECK(entry.responses[0].weight == 2);
}

TEST_CASE("the modal refuses a trigger that could not work", "[commands]") {
    using latibot::commands::apply_form;

    latibot::events::trigger entry{.pattern = "420", .responses = {{.text = "nice", .weight = 1}}};

    SECTION("a blank pattern would match every message") {
        const auto problem = apply_form(entry, {.pattern = "   ", .responses = "nice"});
        REQUIRE(problem.has_value());
        CHECK(entry.pattern == "420"); // unchanged
    }

    SECTION("no responses leaves nothing to say") {
        const auto problem = apply_form(entry, {.pattern = "69", .responses = "  \n \n"});
        REQUIRE(problem.has_value());
        CHECK(entry.pattern == "420");
    }
}

TEST_CASE("the trigger modal fits inside Discord's limits", "[commands]") {
    // Production caught this one instead of a test: a label of 50 characters
    // came back as "50035 Invalid Form Body ... data.components[1].label:
    // Must be between 1 and 45 in length", which is only visible once the
    // modal is actually opened against the API.
    using latibot::commands::trigger_form;

    constexpr std::size_t label_limit = 45;
    constexpr std::size_t id_limit = 100;
    constexpr std::size_t placeholder_limit = 100;

    latibot::events::trigger existing{.id = 7, .pattern = "420", .cooldown = 30s, .responses = {{.text = "nice", .weight = 3}}};

    const std::array<const latibot::events::trigger*, 2> shapes{nullptr, &existing};
    for (const latibot::events::trigger* entry : shapes) {
        const auto form = trigger_form(0, entry);

        CHECK(form.title.size() <= label_limit);
        CHECK_FALSE(form.title.empty());
        CHECK(form.custom_id.size() <= id_limit);
        REQUIRE_FALSE(form.components.empty());

        for (const auto& row : form.components) {
            for (const dpp::component& input : row) {
                INFO("label: " << input.label);
                CHECK_FALSE(input.label.empty());
                CHECK(input.label.size() <= label_limit);
                CHECK(input.custom_id.size() <= id_limit);
                CHECK(input.placeholder.size() <= placeholder_limit);
            }
        }
    }
}

TEST_CASE("a trigger that answers bots says so when described", "[commands]") {
    latibot::events::trigger entry{.id = 3, .pattern = "420", .cooldown = 30s, .responses = {{.text = "nice", .weight = 1}}};

    CHECK(describe(entry).find("answers bots") == std::string::npos);

    entry.respond_to_bots = true;
    CHECK(describe(entry).find("answers bots") != std::string::npos);
}

TEST_CASE("a trigger says when its replies notify or hide previews", "[commands]") {
    latibot::events::trigger entry{.id = 3, .pattern = "420", .cooldown = 30s, .responses = {{.text = "nice", .weight = 1}}};

    // Silent with previews is the default, and says nothing.
    CHECK(describe(entry) == "`3` **420** (whole word, 30s) -> 1 response");

    entry.message_flags = dpp::m_suppress_embeds;
    CHECK(describe(entry) == "`3` **420** (whole word, 30s, notifies, no previews) -> 1 response");
}

TEST_CASE("the panel offers to change how a trigger's replies are posted", "[commands]") {
    latibot::db::database db{":memory:"};
    latibot::db::migrate(db);
    latibot::events::trigger_store store(db);
    const dpp::snowflake guild{1000};
    const std::int64_t id =
        store.add({.guild_id = guild, .pattern = "420", .cooldown = 30s, .enabled = true, .responses = {{.text = "nice", .weight = 1}}});

    const auto labels_for = [&](std::string_view view, const dpp::message& panel) {
        std::vector<std::string> found;
        for (const dpp::component& row : panel.components) {
            for (const dpp::component& part : row.components) {
                const auto state = latibot::ui::decode(part.custom_id);
                if (state && state->view == view) {
                    CHECK(state->argument == std::to_string(id));
                    found.push_back(part.label);
                }
            }
        }
        return found;
    };

    const auto panel = latibot::commands::render_trigger_panel(store, guild, 0, id);
    CHECK(panel.components.size() <= 5);
    for (const dpp::component& row : panel.components) {
        CHECK(row.components.size() <= 5);
    }
    // The labels say what pressing does, from the trigger's current state.
    CHECK(labels_for(latibot::commands::trigger_silent_view, panel) == std::vector<std::string>{"Reply with notifications"});
    CHECK(labels_for(latibot::commands::trigger_previews_view, panel) == std::vector<std::string>{"Hide link previews"});

    auto entry = store.find(id, guild);
    entry->message_flags = dpp::m_suppress_embeds;
    store.update(*entry);
    const auto changed = latibot::commands::render_trigger_panel(store, guild, 0, id);
    CHECK(labels_for(latibot::commands::trigger_silent_view, changed) == std::vector<std::string>{"Reply silently"});
    CHECK(labels_for(latibot::commands::trigger_previews_view, changed) == std::vector<std::string>{"Show link previews"});

    // Confirming a delete is about the delete, nothing else.
    const auto confirming = latibot::commands::render_trigger_panel(store, guild, 0, id, /*confirming_delete=*/true);
    CHECK(labels_for(latibot::commands::trigger_silent_view, confirming).empty());
}

TEST_CASE("each panel toggle flips one thing and names it for the log", "[commands]") {
    using latibot::commands::toggle_for;
    latibot::events::trigger entry{.id = 3, .pattern = "420", .cooldown = 30s, .responses = {{.text = "nice", .weight = 1}}};

    CHECK(toggle_for(latibot::commands::trigger_silent_view)(entry) == "set to reply with notifications");
    CHECK(entry.message_flags == 0);
    CHECK(toggle_for(latibot::commands::trigger_previews_view)(entry) == "set to hide link previews");
    CHECK(entry.message_flags == dpp::m_suppress_embeds);
    CHECK(toggle_for(latibot::commands::trigger_toggle_view)(entry) == "disabled");
    CHECK_FALSE(entry.enabled);
    CHECK(toggle_for(latibot::commands::trigger_bots_view)(entry) == "set to answer bots");
    CHECK(entry.respond_to_bots);

    CHECK(toggle_for(latibot::commands::trigger_edit_view) == nullptr);
}
