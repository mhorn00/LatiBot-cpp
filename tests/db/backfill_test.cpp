// /linkstats recompute: reading history back into the reaction statistics
// (plan v4 §9.7), against the Discord mock and a real database.

#include "core/events/backfill.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/legacy_replacements.hpp"
#include "core/events/reactions.hpp"
#include "core/events/replacements.hpp"
#include "core/events/url_rules.hpp"

#include "mocks/mock_clock.hpp"
#include "mocks/mock_discord.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using latibot::events::backfill_report;
using latibot::events::backfill_request;
using latibot::events::first_id_at;
using latibot::events::stat_kind;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{2000};
constexpr dpp::snowflake other_channel{2001};
constexpr dpp::snowflake bot{42};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};
constexpr dpp::snowflake carol{13};

const std::chrono::sys_seconds day_one{std::chrono::sys_days{std::chrono::year{2025} / 3 / 1}};

/// An id for a message posted `later` after day one.
dpp::snowflake id_at(std::chrono::seconds later) {
    return first_id_at(day_one + later);
}

dpp::message message(dpp::snowflake id, dpp::snowflake author, const std::string& content, bool is_bot = false) {
    dpp::message made(channel, content);
    made.id = id;
    made.author.id = author;
    if (is_bot) {
        made.author.flags |= dpp::u_bot;
    }
    return made;
}

dpp::reaction reaction(std::string name, std::uint32_t count, dpp::snowflake emoji_id = {}) {
    dpp::reaction made;
    made.emoji_name = std::move(name);
    made.emoji_id = emoji_id;
    made.count = count;
    return made;
}

/// A channel's history, newest first, as Discord pages it.
std::vector<dpp::message> history() {
    dpp::message replacement = message(id_at(10s), bot, "🔗[_](https://fxtwitter.com/alice/status/1)", true);
    replacement.reactions = {reaction("💀", 2), reaction("skull", 1, dpp::snowflake{7001})};

    return {
        replacement,
        message(id_at(5s), bob, "lmao"),
        message(id_at(0s), alice, "look https://x.com/alice/status/1"),
    };
}

backfill_request request(bool fresh = false) {
    return {.guild_id = guild, .channel_ids = {channel}, .since = day_one - 24h, .until = std::nullopt, .bot_id = bot, .fresh = fresh};
}

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::events::url_rule_store rules{db};
    latibot::events::replacement_store replacements{db};
    latibot::events::reaction_store reactions{db};
    latibot::events::backfill_progress_store progress{db};
    latibot::testing::mock_clock clock{day_one + 365 * 24h};
    latibot::testing::mock_discord discord;
    latibot::events::backfill_service service{discord, rules, replacements, reactions, progress, clock};

    fixture() {
        latibot::db::migrate(db);
        // A rule long since changed: the old mirror is still known.
        rules.set(guild, {.domain = "x.com", .mirrors = {{.host = "fxtwitter.com", .translate_suffix = ""}}});
        rules.set(guild, {.domain = "x.com", .mirrors = {{.host = "vxtwitter.com", .translate_suffix = ""}}});
    }

    /// Scripts one pass over `history()`: the page, then who reacted.
    void script_history() {
        discord.message_pages.emplace_back(history());
        discord.reaction_pages.emplace_back(std::vector<dpp::snowflake>{bob, carol});
        discord.reaction_pages.emplace_back(std::vector<dpp::snowflake>{alice});
    }

    backfill_report run(backfill_request wanted) { return *service.run(std::move(wanted)).sync_wait_for(2s); }
};

} // namespace

TEST_CASE("a recompute credits an old replacement to whoever posted the link", "[events][coro]") {
    fixture test;
    test.script_history();

    const backfill_report report = test.run(request());

    CHECK(report.scanned == 3);
    CHECK(report.replacements == 1);
    CHECK(report.attributed == 1);
    CHECK(report.unattributed == 0);
    CHECK(report.reactions == 3);
    CHECK(report.channels_done == 1);
    CHECK_FALSE(report.cancelled);

    const auto stored = test.replacements.find(id_at(10s));
    REQUIRE(stored.has_value());
    CHECK(stored->original_author_id == alice);
    CHECK(stored->original_message_id == id_at(0s));

    // Bob and Carol's skulls are Alice's; her own reaction is a self one.
    CHECK(test.reactions.total(guild, {.kind = stat_kind::received, .user_id = alice}) == 2);
    CHECK(test.reactions.total(guild, {.kind = stat_kind::self, .user_id = alice}) == 1);

    // Custom emoji ask for name:id, Unicode ones for themselves.
    REQUIRE(test.discord.reaction_requests.size() == 2);
    CHECK(test.discord.reaction_requests[0].emoji == "💀");
    CHECK(test.discord.reaction_requests[1].emoji == "skull:7001");
}

TEST_CASE("an old replacement is filed under the site its mirror stood in for", "[events][coro]") {
    fixture test;
    test.script_history();
    test.run(request());

    const auto stored = test.replacements.find(id_at(10s));
    REQUIRE(stored.has_value());
    REQUIRE(stored->links.size() == 1);
    CHECK(stored->links[0].domain == "x.com");

    const latibot::events::stat_query on_x{.kind = stat_kind::received, .domain = "x.com"};
    const latibot::events::stat_query on_tiktok{.kind = stat_kind::received, .domain = "tiktok.com"};
    CHECK(test.reactions.total(guild, on_x) == 2);
    CHECK(test.reactions.total(guild, on_tiktok) == 0);
}

TEST_CASE("a replacement after somebody else's link is reported, not credited to them", "[events][coro]") {
    fixture test;
    test.discord.message_pages.emplace_back(std::vector<dpp::message>{
        message(id_at(10s), bot, "🔗[_](https://fxtwitter.com/alice/status/1)", true),
        message(id_at(5s), bob, "https://x.com/bob/status/2"),
    });

    const backfill_report report = test.run(request());
    CHECK(report.unattributed == 1);
    CHECK(report.mismatched == 1);
    CHECK_FALSE(test.replacements.find(id_at(10s))->original_author_id.has_value());
}

TEST_CASE("a recompute is safe to run twice", "[events][coro]") {
    fixture test;
    test.script_history();
    test.run(request());

    test.script_history();
    const backfill_report again = test.run(request(/*fresh=*/true));

    CHECK(again.reactions == 3);
    CHECK(test.reactions.total(guild, {.kind = stat_kind::received}) == 2);
}

TEST_CASE("a finished channel is not scanned again unless asked", "[events][coro]") {
    fixture test;
    test.script_history();
    test.run(request());

    const backfill_report again = test.run(request());
    CHECK(again.scanned == 0);
    // No page was even asked for.
    CHECK(test.discord.history_requests.size() == 1);
}

TEST_CASE("the walk stops at the start of the range", "[events][coro]") {
    fixture test;
    test.script_history();

    backfill_request wanted = request();
    wanted.since = day_one + 3s;
    const backfill_report report = test.run(wanted);

    // The replacement and Bob's chat are inside it; Alice's post is not, but
    // it is still used to find whose link the replacement was.
    CHECK(report.scanned == 2);
    CHECK(test.replacements.find(id_at(10s))->original_author_id == alice);
}

TEST_CASE("the end of the range is where paging starts", "[events][coro]") {
    fixture test;
    backfill_request wanted = request();
    wanted.until = day_one + 24h;

    test.run(wanted);
    REQUIRE(test.discord.history_requests.size() == 1);
    CHECK(test.discord.history_requests[0].before == first_id_at(day_one + 24h));
}

TEST_CASE("a replacement the bot recorded itself is not re-attributed", "[events][coro]") {
    fixture test;
    test.replacements.record({.message_id = id_at(10s),
                              .guild_id = guild,
                              .channel_id = channel,
                              .original_message_id = std::nullopt,
                              .original_author_id = carol,
                              .state = latibot::events::replacement_state::ok,
                              .created_at = day_one + 10s,
                              .retried_at = std::nullopt,
                              .links = {}});
    test.script_history();

    test.run(request());
    CHECK(test.replacements.find(id_at(10s))->original_author_id == carol);
}

TEST_CASE("messages in no known format are listed by id", "[events][coro]") {
    fixture test;
    test.discord.message_pages.emplace_back(
        std::vector<dpp::message>{message(id_at(10s), bot, "here [tweet](https://fxtwitter.com/alice/status/1)", true)});

    const backfill_report report = test.run(request());
    CHECK(report.replacements == 0);
    REQUIRE(report.unparsed.size() == 1);
    CHECK(report.unparsed[0] == id_at(10s));
}

TEST_CASE("a replacement with nobody to credit still counts its reactions", "[events][coro]") {
    fixture test;
    dpp::message lonely = message(id_at(10s), bot, "🔗[_](https://fxtwitter.com/alice/status/1)", true);
    lonely.reactions = {reaction("💀", 1)};
    test.discord.message_pages.emplace_back(std::vector<dpp::message>{lonely});
    test.discord.reaction_pages.emplace_back(std::vector<dpp::snowflake>{bob});

    const backfill_report report = test.run(request());
    CHECK(report.unattributed == 1);
    CHECK(test.reactions.total(guild, {.kind = stat_kind::given, .user_id = bob}) == 1);
    CHECK(test.reactions.total(guild, {.kind = stat_kind::received}) == 0);
}

TEST_CASE("a channel the bot cannot read is reported and the rest carry on", "[events][coro]") {
    fixture test;
    test.discord.message_pages.emplace_back(latibot::api_error{.http_status = 403, .message = "Missing Access"});
    test.script_history();

    backfill_request wanted = request();
    wanted.channel_ids = {other_channel, channel};
    const backfill_report report = test.run(wanted);

    REQUIRE(report.problems.size() == 1);
    CHECK(report.problems[0].find("Missing Access") != std::string::npos);
    CHECK(report.replacements == 1);
}

TEST_CASE("a failed reaction lookup keeps the counts that were there", "[events][coro]") {
    fixture test;
    test.script_history();
    test.run(request());

    test.discord.message_pages.emplace_back(history());
    test.discord.reaction_pages.emplace_back(latibot::api_error{.http_status = 500, .message = "oops"});

    const backfill_report again = test.run(request(/*fresh=*/true));
    CHECK_FALSE(again.problems.empty());
    CHECK(test.reactions.total(guild, {.kind = stat_kind::received}) == 2);
}

TEST_CASE("one recompute per guild, and it can be cancelled", "[events][coro]") {
    fixture test;

    CHECK(test.service.begin(guild));
    CHECK_FALSE(test.service.begin(guild));
    CHECK(test.service.running(guild));

    CHECK(test.service.cancel(guild));
    test.script_history();
    const backfill_report report = test.run(request());
    CHECK(report.cancelled);
    CHECK(report.scanned == 0);

    test.service.end(guild);
    CHECK_FALSE(test.service.running(guild));
    CHECK_FALSE(test.service.cancel(guild));
}

TEST_CASE("without any known mirror there is nothing to recognise", "[events][coro]") {
    latibot::db::database db{":memory:"};
    latibot::db::migrate(db);
    const latibot::events::url_rule_store rules{db};
    latibot::events::replacement_store replacements{db};
    latibot::events::reaction_store reactions{db};
    latibot::events::backfill_progress_store progress{db};
    latibot::testing::mock_clock clock;
    latibot::testing::mock_discord discord;
    latibot::events::backfill_service service{discord, rules, replacements, reactions, progress, clock};

    const auto report =
        service.run({.guild_id = guild, .channel_ids = {channel}, .since = day_one, .until = {}, .bot_id = bot, .fresh = false})
            .sync_wait_for(2s);
    REQUIRE(report.has_value());
    CHECK_FALSE(report->problems.empty());
    CHECK(discord.history_requests.empty());
}
