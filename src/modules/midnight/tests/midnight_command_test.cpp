#include "midnight_command.hpp"

#include "core/commands/registry.hpp"
#include "core/db/database.hpp"

#include "mocks/mock_clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <string>
#include <vector>

using latibot::commands::describe;
using latibot::commands::render_midnight_list;
using latibot::events::midnight_entry;

namespace {

auto entry_in(std::string timezone) -> midnight_entry {
    return {.id = 4,
            .guild_id = dpp::snowflake{1000},
            .channel_id = dpp::snowflake{3000},
            .timezone = std::move(timezone),
            .message = "it is a new day",
            .enabled = true,
            .last_fired_date = {}};
}

} // namespace

TEST_CASE("an empty list says how to add one", "[midnight]") {
    CHECK(render_midnight_list({}).contains("/midnight add"));
}

TEST_CASE("a listed entry names its channel, zone and message", "[midnight]") {
    const std::string line = describe(entry_in("America/Chicago"));

    CHECK(line.contains("`4`"));
    CHECK(line.contains("<#3000>"));
    CHECK(line.contains("America/Chicago"));
    CHECK(line.contains("it is a new day"));
}

TEST_CASE("an entry that is off says so", "[midnight]") {
    midnight_entry entry = entry_in("UTC");
    entry.enabled = false;

    CHECK(describe(entry).contains("(off)"));

    // And one that is on does not carry the noise.
    CHECK_FALSE(describe(entry_in("UTC")).contains("(off)"));
}

TEST_CASE("an entry that has posted says when", "[midnight]") {
    midnight_entry entry = entry_in("UTC");
    entry.last_fired_date = "2026-09-23";

    CHECK(describe(entry).contains("last posted 2026-09-23"));

    // A new one has nothing to report rather than an empty date.
    CHECK_FALSE(describe(entry_in("UTC")).contains("last posted"));
}

TEST_CASE("every entry appears in the list", "[midnight]") {
    std::vector<midnight_entry> entries{entry_in("UTC"), entry_in("Asia/Tokyo")};
    entries[1].id = 5;

    const std::string body = render_midnight_list(entries);
    CHECK(body.contains("`4`"));
    CHECK(body.contains("`5`"));
    CHECK(body.contains("Asia/Tokyo"));
}

TEST_CASE("an entry that notifies or hides previews says so", "[midnight]") {
    midnight_entry entry = entry_in("UTC");
    entry.message_flags = dpp::m_suppress_embeds;
    CHECK(describe(entry).contains("(notifies, no previews)"));

    entry.enabled = false;
    CHECK(describe(entry).contains("(off, notifies, no previews)"));
}

TEST_CASE("the midnight command passes the registry's checks, answers privately and posts silently", "[midnight]") {
    // The checks registry::add makes of every command, for this one
    // (docs/modules/Module_Plan_Final.md §10).
    latibot::db::database db{":memory:"};
    latibot::events::midnight_store store(db);
    latibot::testing::mock_clock clock;

    latibot::commands::registry commands;
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::midnight_command>(store, clock)));

    // The messages it sets up carry flags of their own, silent by default,
    // and its posts are kept silent to match.
    const latibot::commands::midnight_command midnight(store, clock);
    CHECK(midnight.info().responses_for("add").result == dpp::m_ephemeral);
    CHECK(midnight.info().responses_for("add").post == dpp::m_suppress_notifications);
}
