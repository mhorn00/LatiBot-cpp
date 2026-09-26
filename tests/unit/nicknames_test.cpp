#include "core/events/nicknames.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

using latibot::events::describe_change;
using latibot::events::describes;
using latibot::events::is_new_nickname;
using latibot::events::may_attribute;
using latibot::events::nickname_change;
using latibot::events::nickname_source;
using latibot::events::render_history_text;
using latibot::events::show_author;
using latibot::events::show_nickname;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake member{3000};
constexpr dpp::snowflake moderator{4000};
constexpr dpp::snowflake self{9999};

/// 2026-09-23 12:00:00 UTC, which is 1790164800 in Discord's markup.
const auto noon = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::September / 23} + 12h;

auto change_to(std::optional<std::string> nickname) -> nickname_change {
    return {.user_id = member, .nickname = std::move(nickname), .changed_at = noon};
}

} // namespace

// --------------------------------------------------------------------------
// Is this a change at all
// --------------------------------------------------------------------------

TEST_CASE("a first sighting is recorded only when there is a nickname to record", "[events]") {
    CHECK(is_new_nickname(std::nullopt, "worm scientist"));

    // Somebody who has never had a nickname has not changed anything, so
    // joining a server does not write a row saying so.
    CHECK_FALSE(is_new_nickname(std::nullopt, std::nullopt));
}

TEST_CASE("the same nickname again is not a change", "[events]") {
    const auto latest = std::optional(change_to("worm scientist"));

    CHECK_FALSE(is_new_nickname(latest, "worm scientist"));
    CHECK(is_new_nickname(latest, "something else"));
}

TEST_CASE("clearing a nickname is a change", "[events]") {
    const auto had_one = std::optional(change_to("worm scientist"));
    const auto had_none = std::optional(change_to(std::nullopt));

    CHECK(is_new_nickname(had_one, std::nullopt));

    // And clearing an already-cleared nickname is not, which is what stops a
    // startup sweep writing a row for every member with no nickname.
    CHECK_FALSE(is_new_nickname(had_none, std::nullopt));
}

// --------------------------------------------------------------------------
// Attribution
// --------------------------------------------------------------------------

TEST_CASE("an audit entry describes a row by member and resulting nickname", "[events]") {
    const nickname_change row = change_to("worm scientist");

    CHECK(describes(row, member, "worm scientist"));
    CHECK_FALSE(describes(row, dpp::snowflake{1}, "worm scientist"));
    CHECK_FALSE(describes(row, member, "worm scientist "));
    CHECK_FALSE(describes(row, member, std::nullopt));
}

TEST_CASE("an audit entry can describe a cleared nickname", "[events]") {
    const nickname_change row = change_to(std::nullopt);

    CHECK(describes(row, member, std::nullopt));
    CHECK_FALSE(describes(row, member, "worm scientist"));
}

TEST_CASE("the bot is never recorded as the one who made a change", "[events]") {
    const nickname_change row = change_to("worm scientist");

    // Discord's audit log names the bot, because the bot is what called the
    // API. Believing it is how the only certain attribution gets lost.
    CHECK_FALSE(may_attribute(row, self, self));
    CHECK(may_attribute(row, moderator, self));
}

TEST_CASE("an audit entry with no actor attributes nothing", "[events]") {
    CHECK_FALSE(may_attribute(change_to("worm scientist"), dpp::snowflake{}, self));
}

TEST_CASE("a row that already names somebody is left alone", "[events]") {
    nickname_change row = change_to("worm scientist");
    row.changed_by = moderator;

    CHECK_FALSE(may_attribute(row, dpp::snowflake{5000}, self));
}

TEST_CASE("an audit entry's nickname arrives as JSON rather than as text", "[events]") {
    // DPP dumps the value back to JSON before handing it over, so a nickname
    // comes quoted and a cleared one comes as the word null.
    CHECK(latibot::events::audit_nickname(R"("worm scientist")") == "worm scientist");
    CHECK_FALSE(latibot::events::audit_nickname("null").has_value());
    CHECK_FALSE(latibot::events::audit_nickname("").has_value());

    // Quotes and backslashes survive the round trip, which is the whole point
    // of it being JSON.
    CHECK(latibot::events::audit_nickname(R"("say \"420\"")") == R"(say "420")");
}

TEST_CASE("an unreadable audit value is treated as no nickname", "[events]") {
    CHECK_FALSE(latibot::events::audit_nickname("{not json").has_value());
    CHECK_FALSE(latibot::events::audit_nickname("42").has_value());
}

// --------------------------------------------------------------------------
// Changes the bot made itself
// --------------------------------------------------------------------------

TEST_CASE("a change the bot just made is claimed once", "[events]") {
    latibot::events::pending_nicknames pending;

    pending.expect(dpp::snowflake{1}, member, "worm scientist", noon);

    CHECK(pending.claim(dpp::snowflake{1}, member, "worm scientist", noon + 1s));

    // Consumed, so the same nickname set again later is a change in its own
    // right rather than a second helping of this one.
    CHECK_FALSE(pending.claim(dpp::snowflake{1}, member, "worm scientist", noon + 2s));
    CHECK(pending.size() == 0);
}

TEST_CASE("an expectation only matches the change it was made for", "[events]") {
    latibot::events::pending_nicknames pending;

    pending.expect(dpp::snowflake{1}, member, "worm scientist", noon);

    CHECK_FALSE(pending.claim(dpp::snowflake{2}, member, "worm scientist", noon));
    CHECK_FALSE(pending.claim(dpp::snowflake{1}, dpp::snowflake{7}, "worm scientist", noon));
    CHECK_FALSE(pending.claim(dpp::snowflake{1}, member, "something else", noon));
    CHECK(pending.claim(dpp::snowflake{1}, member, "worm scientist", noon));
}

TEST_CASE("an expectation stops applying once it has expired", "[events]") {
    latibot::events::pending_nicknames pending;

    pending.expect(dpp::snowflake{1}, member, "worm scientist", noon);

    // A member update that never arrived must not swallow a real change made
    // an hour later.
    CHECK_FALSE(pending.claim(dpp::snowflake{1}, member, "worm scientist", noon + 1h));
}

TEST_CASE("expired expectations are cleared out as new ones arrive", "[events]") {
    latibot::events::pending_nicknames pending;

    pending.expect(dpp::snowflake{1}, member, "one", noon);
    pending.expect(dpp::snowflake{1}, member, "two", noon);
    REQUIRE(pending.size() == 2);

    pending.expect(dpp::snowflake{1}, member, "three", noon + 1h);
    CHECK(pending.size() == 1);
}

TEST_CASE("a change Discord refused stops being expected", "[events]") {
    latibot::events::pending_nicknames pending;

    pending.expect(dpp::snowflake{1}, member, "worm scientist", noon);
    pending.forget(dpp::snowflake{1}, member, "worm scientist");

    CHECK(pending.size() == 0);
    CHECK_FALSE(pending.claim(dpp::snowflake{1}, member, "worm scientist", noon));
}

TEST_CASE("clearing a nickname is expected and claimed like any other change", "[events]") {
    latibot::events::pending_nicknames pending;

    pending.expect(dpp::snowflake{1}, member, std::nullopt, noon);

    CHECK_FALSE(pending.claim(dpp::snowflake{1}, member, "worm scientist", noon));
    CHECK(pending.claim(dpp::snowflake{1}, member, std::nullopt, noon));
}

// --------------------------------------------------------------------------
// Display
// --------------------------------------------------------------------------

TEST_CASE("a cleared nickname reads as cleared rather than as a blank", "[events]") {
    CHECK(show_nickname("worm scientist") == "worm scientist");
    CHECK(show_nickname(std::nullopt) == "*(cleared)*");
    CHECK(show_nickname(std::string{}) == "*(cleared)*");
}

TEST_CASE("who changed it is a mention, unknown, or nothing", "[events]") {
    nickname_change named = change_to("worm scientist");
    named.changed_by = moderator;
    CHECK(show_author(named) == "<@4000>");

    // Watched happen, nobody claimed it: genuinely unknown, and worth saying.
    CHECK(show_author(change_to("worm scientist")) == "unknown");

    // Imported: the Java bot mostly guessed, so there is nothing honest to say.
    nickname_change imported = change_to("worm scientist");
    imported.source = nickname_source::imported;
    CHECK(show_author(imported).empty());
}

TEST_CASE("a history line carries the nickname, the time and the author", "[events]") {
    nickname_change row = change_to("worm scientist");
    row.changed_by = moderator;

    // Pinned separately, so a wrong constant below reads as a wrong constant
    // rather than as a broken format.
    REQUIRE(std::chrono::duration_cast<std::chrono::seconds>(noon.time_since_epoch()).count() == 1790164800);

    // <t:...:f> so every reader sees it in their own timezone.
    CHECK(describe_change(row) == "**worm scientist** — <t:1790164800:f> by <@4000>");
}

TEST_CASE("an imported history line says nothing about who", "[events]") {
    nickname_change row = change_to("worm scientist");
    row.source = nickname_source::imported;

    CHECK(describe_change(row) == "**worm scientist** — <t:1790164800:f>");
}

TEST_CASE("the attachment spells out times rather than leaving markup in a file", "[events]") {
    nickname_change named = change_to("worm scientist");
    named.changed_by = moderator;

    nickname_change cleared = change_to(std::nullopt);
    cleared.source = nickname_source::imported;

    const std::vector<nickname_change> history{named, cleared};
    const std::string text = render_history_text(history, "somebody (3000)");

    CHECK(text.find("Nickname history for somebody (3000)") != std::string::npos);
    CHECK(text.find("2 entries") != std::string::npos);
    CHECK(text.find("2026-09-23 12:00:00  worm scientist  (by 4000)") != std::string::npos);
    CHECK(text.find("2026-09-23 12:00:00  (cleared)\n") != std::string::npos);

    // No Discord markup: a .txt attachment has nothing to render it.
    CHECK(text.find("<t:") == std::string::npos);
}

TEST_CASE("one entry is not described as one entries", "[events]") {
    const std::vector<nickname_change> history{change_to("worm scientist")};

    CHECK(render_history_text(history, "somebody").find("1 entry,") != std::string::npos);
}
