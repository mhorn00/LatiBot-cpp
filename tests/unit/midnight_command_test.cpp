#include "core/commands/midnight.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using latibot::commands::describe;
using latibot::commands::render_midnight_list;
using latibot::events::midnight_entry;

namespace {

midnight_entry entry_in(std::string timezone) {
    return {.id = 4,
            .guild_id = dpp::snowflake{1000},
            .channel_id = dpp::snowflake{3000},
            .timezone = std::move(timezone),
            .message = "it is a new day",
            .enabled = true,
            .last_fired_date = {}};
}

} // namespace

TEST_CASE("an empty list says how to add one", "[commands]") {
    CHECK(render_midnight_list({}).find("/midnight add") != std::string::npos);
}

TEST_CASE("a listed entry names its channel, zone and message", "[commands]") {
    const std::string line = describe(entry_in("America/Chicago"));

    CHECK(line.find("`4`") != std::string::npos);
    CHECK(line.find("<#3000>") != std::string::npos);
    CHECK(line.find("America/Chicago") != std::string::npos);
    CHECK(line.find("it is a new day") != std::string::npos);
}

TEST_CASE("an entry that is off says so", "[commands]") {
    midnight_entry entry = entry_in("UTC");
    entry.enabled = false;

    CHECK(describe(entry).find("(off)") != std::string::npos);

    // And one that is on does not carry the noise.
    CHECK(describe(entry_in("UTC")).find("(off)") == std::string::npos);
}

TEST_CASE("an entry that has posted says when", "[commands]") {
    midnight_entry entry = entry_in("UTC");
    entry.last_fired_date = "2026-09-23";

    CHECK(describe(entry).find("last posted 2026-09-23") != std::string::npos);

    // A new one has nothing to report rather than an empty date.
    CHECK(describe(entry_in("UTC")).find("last posted") == std::string::npos);
}

TEST_CASE("every entry appears in the list", "[commands]") {
    std::vector<midnight_entry> entries{entry_in("UTC"), entry_in("Asia/Tokyo")};
    entries[1].id = 5;

    const std::string body = render_midnight_list(entries);
    CHECK(body.find("`4`") != std::string::npos);
    CHECK(body.find("`5`") != std::string::npos);
    CHECK(body.find("Asia/Tokyo") != std::string::npos);
}

TEST_CASE("an entry that notifies or hides previews says so", "[commands]") {
    midnight_entry entry = entry_in("UTC");
    entry.message_flags = dpp::m_suppress_embeds;
    CHECK(describe(entry).find("(notifies, no previews)") != std::string::npos);

    entry.enabled = false;
    CHECK(describe(entry).find("(off, notifies, no previews)") != std::string::npos);
}
