#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/replacements.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>

using latibot::events::replacement_record;
using latibot::events::replacement_state;
using latibot::events::replacement_store;

namespace {

constexpr dpp::snowflake ours{4000};

struct store_fixture {
    latibot::db::database db{":memory:"};
    replacement_store store{db};

    store_fixture() { latibot::db::migrate(db); }
};

replacement_record sample() {
    return {.message_id = ours,
            .guild_id = dpp::snowflake{1000},
            .channel_id = dpp::snowflake{2000},
            .original_message_id = dpp::snowflake{3000},
            .original_author_id = dpp::snowflake{5000},
            .state = replacement_state::pending,
            .created_at = std::chrono::sys_seconds{std::chrono::seconds{1'790'000'000}},
            .retried_at = std::nullopt,
            .links = {{.original_url = "https://x.com/a/status/1", .domain = "x.com", .spoilered = true, .mirrors = {}},
                      {.original_url = "https://tiktok.com/@a/video/2", .domain = "tiktok.com", .spoilered = false, .mirrors = {}}}};
}

} // namespace

TEST_CASE("a replacement round-trips with its links in order", "[db]") {
    store_fixture fixture;
    fixture.store.record(sample());

    const auto found = fixture.store.find(ours);
    REQUIRE(found.has_value());
    CHECK(found->guild_id == dpp::snowflake{1000});
    CHECK(found->channel_id == dpp::snowflake{2000});
    CHECK(found->original_message_id == dpp::snowflake{3000});
    CHECK(found->original_author_id == dpp::snowflake{5000});
    CHECK(found->state == replacement_state::pending);
    CHECK(found->created_at == sample().created_at);
    CHECK_FALSE(found->retried_at.has_value());

    REQUIRE(found->links.size() == 2);
    CHECK(found->links[0].original_url == "https://x.com/a/status/1");
    CHECK(found->links[0].spoilered);
    CHECK(found->links[1].domain == "tiktok.com");
}

TEST_CASE("an unattributed replacement stores no author", "[db]") {
    store_fixture fixture;
    auto entry = sample();
    entry.original_message_id.reset();
    entry.original_author_id.reset();
    fixture.store.record(entry);

    const auto found = fixture.store.find(ours);
    REQUIRE(found.has_value());
    CHECK_FALSE(found->original_author_id.has_value());
    CHECK_FALSE(found->original_message_id.has_value());
}

TEST_CASE("state changes and retries are recorded", "[db]") {
    store_fixture fixture;
    fixture.store.record(sample());

    CHECK(fixture.store.set_state(ours, replacement_state::failed));
    CHECK(fixture.store.find(ours)->state == replacement_state::failed);

    const std::chrono::sys_seconds at{std::chrono::seconds{1'790'000'100}};
    CHECK(fixture.store.mark_retried(ours, replacement_state::ok, at));
    CHECK(fixture.store.find(ours)->state == replacement_state::ok);
    CHECK(fixture.store.find(ours)->retried_at == at);

    CHECK_FALSE(fixture.store.set_state(dpp::snowflake{1}, replacement_state::ok));
}

TEST_CASE("recording a replacement again replaces its links", "[db]") {
    store_fixture fixture;
    fixture.store.record(sample());

    auto again = sample();
    again.links.pop_back();
    fixture.store.record(again);

    CHECK(fixture.store.find(ours)->links.size() == 1);
    CHECK(fixture.store.contains(ours));
    CHECK_FALSE(fixture.store.contains(dpp::snowflake{1}));
}

TEST_CASE("unsettled replacements are the pending and retrying ones, with their links", "[db]") {
    store_fixture fixture;

    const std::array states{replacement_state::pending, replacement_state::ok, replacement_state::failed, replacement_state::retrying};
    std::uint64_t id = 4000;
    for (const replacement_state state : states) {
        auto entry = sample();
        entry.message_id = dpp::snowflake{id++};
        entry.state = state;
        fixture.store.record(entry);
    }

    const auto unsettled = fixture.store.unsettled();
    REQUIRE(unsettled.size() == 2);
    CHECK(unsettled[0].message_id == dpp::snowflake{4000});
    CHECK(unsettled[0].state == replacement_state::pending);
    CHECK(unsettled[0].links.size() == 2);
    CHECK(unsettled[1].message_id == dpp::snowflake{4003});
    CHECK(unsettled[1].state == replacement_state::retrying);
}

TEST_CASE("replacement states have stable names", "[db]") {
    for (const auto state : {replacement_state::pending, replacement_state::ok, replacement_state::failed, replacement_state::retrying}) {
        CHECK(latibot::events::replacement_state_from_string(latibot::events::to_string(state)) == state);
    }
    CHECK_FALSE(latibot::events::replacement_state_from_string("bogus").has_value());
}
