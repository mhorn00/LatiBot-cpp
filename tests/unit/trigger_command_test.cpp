#include "core/commands/trigger.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/ui/paginator.hpp"

#include "support/discord_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
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

    const latibot::events::trigger existing{.id = 7, .pattern = "420", .cooldown = 30s, .responses = {{.text = "nice", .weight = 3}}};

    const std::array<const latibot::events::trigger*, 2> shapes{nullptr, &existing};
    for (const latibot::events::trigger* entry : shapes) {
        const auto form = trigger_form(0, entry);
        latibot::testing::check_modal_fits(form);
        CHECK_FALSE(form.components.empty());
    }
}

TEST_CASE("a long trigger list pages", "[commands]") {
    latibot::db::database db{":memory:"};
    latibot::db::migrate(db);
    latibot::events::trigger_store store(db);
    const dpp::snowflake guild{1000};
    for (int index = 0; index < 20; ++index) {
        store.add({.guild_id = guild,
                   .pattern = std::format("pattern {}", index),
                   .cooldown = 30s,
                   .enabled = true,
                   .responses = {{.text = "nice", .weight = 1}}});
    }

    // Three pages: first, middle and last, since paging clamps at either end.
    for (const int page : {0, 1, 2}) {
        const auto list = latibot::commands::render_trigger_list(store, guild, page);
        latibot::testing::check_message_fits(list);
        CHECK(list.content.find(std::format("Page {} of 3", page + 1)) != std::string::npos);
    }

    CHECK(latibot::commands::render_trigger_list(store, dpp::snowflake{2000}, 0).components.empty());
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
    latibot::testing::check_message_fits(panel);
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

TEST_CASE("confirming a delete on the first or last page fits, and Cancel keeps the trigger picked", "[commands]") {
    // Cancel once encoded the same state as ◀ on the first page and ▶ on
    // the last, and Discord refuses a message with a custom_id twice.
    latibot::db::database db{":memory:"};
    latibot::db::migrate(db);
    latibot::events::trigger_store store(db);
    const dpp::snowflake guild{1000};

    std::vector<std::int64_t> ids;
    for (std::size_t index = 0; index <= latibot::commands::triggers_per_page; ++index) {
        ids.push_back(store.add({.guild_id = guild,
                                 .pattern = std::format("pattern {}", index),
                                 .cooldown = 30s,
                                 .enabled = true,
                                 .responses = {{.text = "nice", .weight = 1}}}));
    }

    for (const auto& [page, id] : {std::pair{0, ids.front()}, std::pair{1, ids.back()}}) {
        INFO("page " << page);
        const auto confirming = latibot::commands::render_trigger_panel(store, guild, page, id, /*confirming_delete=*/true);
        latibot::testing::check_message_fits(confirming);

        const dpp::component* cancel_button = nullptr;
        for (const dpp::component& row : confirming.components) {
            for (const dpp::component& part : row.components) {
                if (part.label == "Cancel") {
                    cancel_button = &part;
                }
            }
        }
        REQUIRE(cancel_button != nullptr);
        const auto cancel = latibot::ui::decode(cancel_button->custom_id);
        REQUIRE(cancel.has_value());
        CHECK(cancel->view == latibot::commands::trigger_panel_view);
        CHECK(cancel->argument == std::to_string(id));
    }
}

TEST_CASE("the longest pattern the command takes still fits the panel", "[commands]") {
    // /trigger add takes 200 characters and a menu option's label only 100.
    // One label too long and Discord refuses the whole panel, for everybody.
    latibot::db::database db{":memory:"};
    latibot::db::migrate(db);
    latibot::events::trigger_store store(db);
    const dpp::snowflake guild{1000};

    std::string accented;
    for (int index = 0; index < 200; ++index) {
        accented += "é";
    }
    for (const std::string& pattern : {std::string(200, 'a'), accented}) {
        store.add({.guild_id = guild, .pattern = pattern, .cooldown = 30s, .enabled = true, .responses = {{.text = "nice", .weight = 1}}});
    }

    const auto panel = latibot::commands::render_trigger_panel(store, guild, 0);
    latibot::testing::check_message_fits(panel);

    // The list above the menu still shows the pattern in full.
    CHECK(panel.content.find(std::string(200, 'a')) != std::string::npos);
}

TEST_CASE("the trigger modal takes no more than the command does", "[commands]") {
    const latibot::events::trigger existing{.id = 7, .pattern = "420", .cooldown = 30s, .responses = {{.text = "nice", .weight = 1}}};
    const auto form = latibot::commands::trigger_form(0, &existing);

    const auto max_length_of = [&](std::string_view id) -> std::optional<std::int32_t> {
        for (const auto& row : form.components) {
            for (const dpp::component& input : row) {
                if (input.custom_id == id) {
                    return input.max_length;
                }
            }
        }
        return std::nullopt;
    };

    CHECK(max_length_of("pattern") == 200);
    CHECK(max_length_of("responses") == 2000);
}

TEST_CASE("each panel toggle flips one thing and names it for the log", "[commands]") {
    using latibot::commands::toggle_for;
    // Each toggle changes it, through a pointer to function the check cannot
    // follow.
    // NOLINTNEXTLINE(misc-const-correctness)
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
