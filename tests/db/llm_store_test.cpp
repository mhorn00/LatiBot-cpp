#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/llm/advanced_triggers.hpp"
#include "core/llm/documents.hpp"
#include "core/llm/guards.hpp"
#include "core/llm/memory.hpp"
#include "core/llm/models.hpp"
#include "core/llm/settings.hpp"
#include "core/llm/spend.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

using latibot::llm::document_kind;
using latibot::llm::memory;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake other_guild{2000};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

/// Noon on 2026-03-15, UTC.
constexpr std::chrono::sys_seconds noon{std::chrono::sys_days{std::chrono::year{2026} / 3 / 15} + 12h};

struct fixture {
    latibot::db::database db{":memory:"};

    fixture() { latibot::db::migrate(db); }
};

auto remembered(std::string content, std::optional<dpp::snowflake> about = std::nullopt, dpp::snowflake in = guild,
                std::chrono::sys_seconds at = noon) -> memory {
    return {.id = 0, .guild_id = in, .subject = about, .content = std::move(content), .created_by = alice, .created_at = at};
}

} // namespace

// --------------------------------------------------------------------------
// Usage and the spend caps
// --------------------------------------------------------------------------

TEST_CASE("a recorded call is priced, and counted in its day and month", "[db]") {
    fixture test;
    latibot::llm::usage_store usage(test.db);
    const auto* haiku = latibot::llm::find_model("claude-haiku-4-5");
    REQUIRE(haiku != nullptr);

    // $1 of input at $1 per million.
    const double cost =
        usage.record(guild, *haiku, {.input_tokens = 1'000'000, .output_tokens = 0, .cache_write_tokens = 0, .cache_read_tokens = 0}, noon);
    CHECK(cost == Catch::Approx(1.0));
    usage.record(guild, *haiku, {.input_tokens = 500'000, .output_tokens = 0, .cache_write_tokens = 0, .cache_read_tokens = 0}, noon - 48h);

    const auto status = latibot::llm::spend_status_at(usage, {.daily = 2.0, .monthly = 20.0}, noon);
    CHECK(status.today == Catch::Approx(1.0));
    CHECK(status.this_month == Catch::Approx(1.5));
    CHECK_FALSE(status.over());
    CHECK(status.period.empty());
}

TEST_CASE("reaching a cap says which one, and the month outranks the day", "[db]") {
    fixture test;
    latibot::llm::usage_store usage(test.db);
    const auto* haiku = latibot::llm::find_model("claude-haiku-4-5");
    REQUIRE(haiku != nullptr);
    usage.record(other_guild, *haiku, {.input_tokens = 2'000'000, .output_tokens = 0, .cache_write_tokens = 0, .cache_read_tokens = 0},
                 noon);

    // Caps are for the whole bot, whichever guild spent it.
    const auto daily = latibot::llm::spend_status_at(usage, {.daily = 2.0, .monthly = 20.0}, noon);
    CHECK(daily.over_daily);
    CHECK_FALSE(daily.over_monthly);
    CHECK(daily.period == "2026-03-15");

    const auto monthly = latibot::llm::spend_status_at(usage, {.daily = 2.0, .monthly = 2.0}, noon);
    CHECK(monthly.over_monthly);
    CHECK(monthly.period == "2026-03");

    // Tomorrow the day is fresh; the month is not.
    const auto tomorrow = latibot::llm::spend_status_at(usage, {.daily = 2.0, .monthly = 20.0}, noon + 24h);
    CHECK_FALSE(tomorrow.over());
    CHECK(usage.spent_between(guild, latibot::llm::month_start(noon), noon + 1s) == Catch::Approx(0.0));
}

TEST_CASE("a cap notice is due once per guild and period", "[db]") {
    latibot::llm::spend_notices notices;
    CHECK(notices.first(guild, "2026-03-15"));
    CHECK_FALSE(notices.first(guild, "2026-03-15"));
    CHECK(notices.first(other_guild, "2026-03-15"));
    CHECK(notices.first(guild, "2026-03-16"));
}

// --------------------------------------------------------------------------
// Documents
// --------------------------------------------------------------------------

TEST_CASE("a document nobody edited reads as its default", "[db]") {
    fixture test;
    const latibot::llm::document_store documents(test.db);

    CHECK_FALSE(documents.current(guild, document_kind::personality).has_value());
    CHECK(documents.text(guild, document_kind::personality) == latibot::llm::default_document(document_kind::personality));
    CHECK(documents.text(guild, document_kind::system).empty());
    CHECK(documents.history(guild, document_kind::personality).empty());
}

TEST_CASE("every edit is a new version, per guild and per kind", "[db]") {
    fixture test;
    latibot::llm::document_store documents(test.db);

    CHECK(documents.save(guild, document_kind::personality, "first", alice, noon) == 1);
    CHECK(documents.save(guild, document_kind::personality, "second", bob, noon + 1h, "tweak") == 2);
    CHECK(documents.save(guild, document_kind::system, "rules", alice, noon) == 1);
    CHECK(documents.save(other_guild, document_kind::personality, "elsewhere", alice, noon) == 1);

    CHECK(documents.text(guild, document_kind::personality) == "second");
    const auto history = documents.history(guild, document_kind::personality);
    REQUIRE(history.size() == 2);
    CHECK(history[0].version == 2);
    CHECK(history[0].edited_by == bob);
    CHECK(history[0].note == "tweak");
    CHECK(history[1].content == "first");
}

TEST_CASE("a revert saves the old text as a new version, and can itself be reverted", "[db]") {
    fixture test;
    latibot::llm::document_store documents(test.db);
    documents.save(guild, document_kind::personality, "first", alice, noon);
    documents.save(guild, document_kind::personality, "bad edit", bob, noon + 1h);

    CHECK(documents.revert(guild, document_kind::personality, 1, alice, noon + 2h) == 3);
    CHECK(documents.text(guild, document_kind::personality) == "first");
    CHECK(documents.current(guild, document_kind::personality)->note == "reverted to version 1");

    // Version 0 is the default, which every guild has.
    CHECK(documents.revert(guild, document_kind::personality, 0, alice, noon + 3h) == 4);
    CHECK(documents.text(guild, document_kind::personality) == latibot::llm::default_document(document_kind::personality));

    CHECK_FALSE(documents.revert(guild, document_kind::personality, 99, alice, noon).has_value());
    CHECK(documents.history(guild, document_kind::personality).size() == 4);
}

// --------------------------------------------------------------------------
// Memory
// --------------------------------------------------------------------------

TEST_CASE("memories are found by the words in them, only in their own guild", "[db]") {
    fixture test;
    latibot::llm::memory_store memories(test.db);
    memories.add(remembered("Alice is allergic to peanuts", alice));
    memories.add(remembered("The server's movie night is on Fridays"));
    memories.add(remembered("Peanuts the dog belongs to someone else", std::nullopt, other_guild));

    const auto found = memories.search(guild, "what about PEANUTS?", 5);
    REQUIRE(found.size() == 1);
    CHECK(found[0].content == "Alice is allergic to peanuts");
    CHECK(found[0].subject == alice);

    CHECK(memories.search(guild, "the and you", 5).empty());
    CHECK(memories.search(guild, "\"movie\" OR NEAR(", 5).size() == 1);
}

TEST_CASE("a removed memory leaves the search index too", "[db]") {
    fixture test;
    latibot::llm::memory_store memories(test.db);
    const auto id = memories.add(remembered("Bob plays the tuba"));

    CHECK_FALSE(memories.remove(id, other_guild));
    CHECK(memories.remove(id, guild));
    CHECK_FALSE(memories.remove(id, guild));
    CHECK(memories.search(guild, "tuba", 5).empty());
    CHECK_FALSE(memories.find(id, guild).has_value());
}

TEST_CASE("memories list newest first, and clear by person or all at once", "[db]") {
    fixture test;
    latibot::llm::memory_store memories(test.db);
    memories.add(remembered("old about alice", alice, guild, noon));
    memories.add(remembered("new about alice", alice, guild, noon + 1h));
    memories.add(remembered("about bob", bob, guild, noon + 2h));
    memories.add(remembered("general", std::nullopt, guild, noon + 3h));

    const auto about_alice = memories.list(guild, alice, 0, 10);
    REQUIRE(about_alice.size() == 2);
    CHECK(about_alice[0].content == "new about alice");
    CHECK(memories.count(guild) == 4);
    CHECK(memories.list(guild, std::nullopt, 1, 2).size() == 2);

    CHECK(memories.clear(guild, alice) == 2);
    CHECK(memories.count(guild, alice) == 0);
    CHECK(memories.count(guild) == 2);
    CHECK(memories.clear(guild) == 2);
    CHECK(memories.count(guild) == 0);
}

TEST_CASE("the memories shown up front are about the author, then what matches", "[db]") {
    fixture test;
    latibot::llm::memory_store memories(test.db);
    memories.add(remembered("Alice prefers tea", alice, guild, noon));
    memories.add(remembered("Bob prefers coffee", bob, guild, noon));
    memories.add(remembered("Tea is served at four", std::nullopt, guild, noon));

    const auto shown = latibot::llm::relevant_memories(memories, guild, alice, "any tea left?", 4);
    REQUIRE(shown.size() == 2);
    CHECK(shown[0].content == "Alice prefers tea");
    CHECK(shown[1].content == "Tea is served at four");
}

// --------------------------------------------------------------------------
// Blacklist, advanced triggers, settings
// --------------------------------------------------------------------------

TEST_CASE("the blacklist blocks a user or anyone with a role", "[db]") {
    fixture test;
    latibot::llm::blacklist_store blacklist(test.db);
    constexpr dpp::snowflake muted_role{77};

    CHECK(blacklist.add(guild, latibot::llm::block_kind::user, bob));
    CHECK_FALSE(blacklist.add(guild, latibot::llm::block_kind::user, bob));
    CHECK(blacklist.add(guild, latibot::llm::block_kind::role, muted_role));

    const std::vector<dpp::snowflake> none;
    const std::vector<dpp::snowflake> muted{dpp::snowflake{5}, muted_role};
    CHECK(blacklist.blocks(guild, bob, none));
    CHECK(blacklist.blocks(guild, alice, muted));
    CHECK_FALSE(blacklist.blocks(guild, alice, none));
    CHECK_FALSE(blacklist.blocks(other_guild, bob, none));

    CHECK(blacklist.list(guild).size() == 2);
    CHECK(blacklist.remove(guild, latibot::llm::block_kind::user, bob));
    CHECK_FALSE(blacklist.blocks(guild, bob, none));
}

TEST_CASE("advanced triggers are stored per guild and edited in place", "[db]") {
    fixture test;
    latibot::llm::advanced_trigger_store triggers(test.db);

    latibot::llm::advanced_trigger entry{.id = 0,
                                         .guild_id = guild,
                                         .pattern = "pineapple pizza",
                                         .mode = latibot::events::match_mode::substring,
                                         .context_prompt = "Defend it with unreasonable passion.",
                                         .probability = 0.5,
                                         .cooldown = 600s,
                                         .enabled = true,
                                         .created_by = alice};
    entry.id = triggers.add(entry);

    const auto found = triggers.find(entry.id, guild);
    REQUIRE(found.has_value());
    CHECK(found->mode == latibot::events::match_mode::substring);
    CHECK(found->probability == Catch::Approx(0.5));
    CHECK(found->cooldown == 600s);
    CHECK_FALSE(triggers.find(entry.id, other_guild).has_value());

    entry.enabled = false;
    CHECK(triggers.update(entry));
    CHECK_FALSE(triggers.for_guild(guild)[0].enabled);

    entry.guild_id = other_guild;
    CHECK_FALSE(triggers.update(entry));
    CHECK(triggers.remove(entry.id, guild));
    CHECK(triggers.for_guild(guild).empty());
}

TEST_CASE("a guild's model settings are read clamped, with the model falling back to the config's", "[db]") {
    fixture test;
    latibot::config::guild_settings settings(test.db);
    const latibot::config::bootstrap config;

    const auto fresh = latibot::llm::load_llm_settings(settings, guild, config);
    CHECK_FALSE(fresh.enabled);
    CHECK(fresh.model == "claude-haiku-4-5");
    CHECK(fresh.context_messages == 15);
    CHECK(fresh.personality_role == guild);

    settings.set_bool(guild, latibot::llm::enabled_key, true);
    settings.set(guild, latibot::llm::model_key, "claude-sonnet-5-5");
    settings.set_int(guild, "llm_context_messages", 9000);
    settings.set_int(guild, "llm_bot_needs_human", 1);
    settings.set(guild, latibot::llm::personality_role_key, "555");

    const auto changed = latibot::llm::load_llm_settings(settings, guild, config);
    CHECK(changed.enabled);
    CHECK(changed.model == "claude-sonnet-5-5");
    CHECK(changed.context_messages == 50);
    CHECK(changed.pacing.needs_human);
    CHECK(changed.personality_role == dpp::snowflake{555});

    settings.set(guild, latibot::llm::model_key, "gpt-2");
    CHECK(latibot::llm::load_llm_settings(settings, guild, config).model == "claude-haiku-4-5");
}
