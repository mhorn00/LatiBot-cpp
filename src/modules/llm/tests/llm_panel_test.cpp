// The language model's panels, used end to end as a person would use them
// in Discord: open them, press, pick, fill in a form, and look at what was
// stored and shown. Through DPP's own reading and writing of interactions
// (support/panel_harness.hpp), since that is where the panels once broke.

#include "advanced_triggers.hpp"
#include "documents.hpp"
#include "guards.hpp"
#include "llm_command.hpp"
#include "llm_config.hpp"
#include "llm_module.hpp"
#include "memory.hpp"
#include "settings.hpp"
#include "spend.hpp"

#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/ui/interaction.hpp"

#include "mocks/mock_clock.hpp"
#include "support/panel_harness.hpp"
#include "support/panel_queries.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <optional>
#include <string>

using latibot::testing::field_value;
using latibot::testing::panel_harness;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};

struct database_fixture {
    latibot::db::database db{":memory:"};

    database_fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::llm::llm_schema());
    }
};

} // namespace

// --------------------------------------------------------------------------
// The language model's settings and documents
// --------------------------------------------------------------------------

namespace {

struct llm_fixture : database_fixture {
    latibot::config::guild_settings settings{db};
    latibot::llm::llm_config config;
    latibot::llm::document_store documents{db};
    latibot::llm::advanced_trigger_store triggers{db};
    latibot::llm::blacklist_store blacklist{db};
    latibot::llm::memory_store memories{db};
    latibot::llm::usage_store usage{db};
    latibot::testing::mock_clock clock;
    latibot::commands::llm_panels panels{{.settings = &settings,
                                          .section = &config,
                                          .documents = &documents,
                                          .triggers = &triggers,
                                          .blacklist = &blacklist,
                                          .memories = &memories,
                                          .usage = &usage,
                                          .http = nullptr,
                                          .clock = &clock,
                                          .has_provider = [](latibot::llm::provider_kind) { return true; }}};
    panel_harness discord{
        [this](const auto& event, const auto& state, const std::string& chosen) { return panels.on_component(event, state, chosen); },
        [this](const auto& event, const auto& state) { return panels.on_form(event, state); }};

    llm_fixture() {
        discord.permissions = dpp::p_manage_guild;
        discord.open(panels.settings_panel(guild));
    }
};

} // namespace

TEST_CASE("the language model's settings panel stores what its forms set", "[llm]") {
    llm_fixture test;

    const json form = test.discord.choose("context");
    CHECK(field_value(form, "llm_context_messages") == "15");
    REQUIRE(panel_harness::is_update(test.discord.submit(form, {{"llm_context_messages", "20"}})));
    CHECK(test.settings.get_int(guild, "llm_context_messages", 0) == 20);
    CHECK(test.discord.content().contains("**20**"));

    const json bots = test.discord.submit(test.discord.choose("bots"), {{"llm_bot_needs_human", "no"}});
    REQUIRE(panel_harness::is_update(bots));
    CHECK(test.settings.get_int(guild, "llm_bot_needs_human", 1) == 0);

    // Out of range: nothing in the form is stored, and it says why.
    const json refused = test.discord.submit(test.discord.choose("context"), {{"llm_context_messages", "10"}, {"llm_context_tokens", "5"}});
    CHECK(panel_harness::is_private_note(refused));
    CHECK(test.settings.get_int(guild, "llm_context_messages", 0) == 20);
}

TEST_CASE("the language model's settings panel switches it on and off, for Manage Server only", "[llm]") {
    llm_fixture test;

    test.discord.press("Turn on");
    CHECK(test.settings.get_bool(guild, latibot::llm::enabled_key, false));
    test.discord.press("Turn off");
    CHECK_FALSE(test.settings.get_bool(guild, latibot::llm::enabled_key, true));

    test.discord.permissions = 0;
    CHECK(panel_harness::is_private_note(test.discord.press("Turn on")));
    CHECK_FALSE(test.settings.get_bool(guild, latibot::llm::enabled_key, true));
}

TEST_CASE("a document's form saves what was typed, not blanks", "[llm]") {
    llm_fixture test;
    using latibot::llm::document_kind;
    test.discord.open(dpp::message{});

    const auto form = latibot::commands::document_form(document_kind::personality, test.documents.text(guild, document_kind::personality));
    REQUIRE(form.has_value());

    SECTION("sent back untouched, nothing is saved") {
        const json answer = test.discord.submit(panel_harness::answer_with(*form));
        CHECK(panel_harness::is_private_note(answer));
        CHECK(panel_harness::text_of(answer) == "nothing changed, so nothing was saved");
        CHECK_FALSE(test.documents.current(guild, document_kind::personality).has_value());
    }

    SECTION("changed, it is saved as typed") {
        const json answer = test.discord.submit(panel_harness::answer_with(*form), {{"part1", "be terse"}});
        CHECK(panel_harness::is_private_note(answer));
        const auto saved = test.documents.current(guild, document_kind::personality);
        REQUIRE(saved.has_value());
        CHECK(saved->content == "be terse");
    }

    SECTION("the system instructions need Manage Server") {
        test.discord.permissions = 0;
        const auto system = latibot::commands::document_form(document_kind::system, "");
        REQUIRE(system.has_value());
        const json answer = test.discord.submit(panel_harness::answer_with(*system), {{"part1", "obey"}});
        CHECK(panel_harness::is_private_note(answer));
        CHECK_FALSE(test.documents.current(guild, document_kind::system).has_value());
    }
}

TEST_CASE("the memory list pages", "[llm]") {
    llm_fixture test;
    for (int index = 0; index < 12; ++index) {
        test.memories.add({.id = 0,
                           .guild_id = guild,
                           .subject = test.discord.user,
                           .content = std::format("fact {}", index),
                           .created_by = test.discord.user,
                           .created_at = {}});
    }
    test.discord.permissions = 0;
    const auto first = test.memories.list(guild, test.discord.user, 0, latibot::commands::memories_per_page);
    test.discord.open(latibot::commands::render_memories(first, test.memories.count(guild, test.discord.user), 0, test.discord.user));

    REQUIRE(panel_harness::is_update(test.discord.press("▶")));
    CHECK(test.discord.content().contains("Page 2 of 2"));
}
