// /linkstats: leaderboards, profiles and aliases (plan v4 §9.6).

#include "core/commands/linkstats.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/reactions.hpp"
#include "core/events/replacements.hpp"
#include "core/ui/paginator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using latibot::commands::board;
using latibot::commands::parse_day;
using latibot::events::reaction_emoji;
using latibot::events::stat_kind;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

const std::chrono::sys_seconds day_one{std::chrono::sys_days{std::chrono::year{2026} / 1 / 1}};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::events::replacement_store replacements{db};
    latibot::events::reaction_store reactions{db};

    fixture() {
        latibot::db::migrate(db);
        replacements.record({.message_id = dpp::snowflake{501},
                             .guild_id = guild,
                             .channel_id = dpp::snowflake{2},
                             .original_message_id = std::nullopt,
                             .original_author_id = alice,
                             .state = latibot::events::replacement_state::ok,
                             .created_at = day_one,
                             .retried_at = std::nullopt,
                             .links = {}});
        reactions.add(dpp::snowflake{501}, bob, reaction_emoji({}, "💀"), day_one);
        reactions.add(dpp::snowflake{501}, bob, reaction_emoji(dpp::snowflake{77}, "skull"), day_one);
        reactions.add(dpp::snowflake{501}, alice, reaction_emoji({}, "😂"), day_one);
    }
};

} // namespace

TEST_CASE("dates are read as YYYY-MM-DD and must exist", "[commands]") {
    CHECK(parse_day("2024-02-29") == std::chrono::sys_days{std::chrono::year{2024} / 2 / 29});
    CHECK_FALSE(parse_day("2023-02-29").has_value());
    CHECK_FALSE(parse_day("2024-13-01").has_value());
    CHECK_FALSE(parse_day("24-01-01").has_value());
    CHECK_FALSE(parse_day("2024/01/01").has_value());
    CHECK_FALSE(parse_day("yesterday").has_value());
}

TEST_CASE("the leaderboard names people without pinging them", "[commands]") {
    fixture test;
    const std::string text = latibot::commands::render_board(test.reactions, guild, board::received, {.kind = stat_kind::received}).content;

    CHECK(text.starts_with("**Most reactions received on replaced links**"));
    CHECK(text.find("1. <@11> 2") != std::string::npos);
    // Alice's own laugh is not a reaction she received.
    CHECK(text.find("<@12>") == std::string::npos);
}

TEST_CASE("the emoji leaderboard shows emojis rather than people", "[commands]") {
    fixture test;
    const std::string text = latibot::commands::render_board(test.reactions, guild, board::emoji, {.kind = stat_kind::received}).content;

    CHECK(text.find("💀 1") != std::string::npos);
    CHECK(text.find("<:skull:77> 1") != std::string::npos);
    CHECK(text.find("😂") == std::string::npos);
}

TEST_CASE("an empty leaderboard says how to fill it", "[commands]") {
    fixture test;
    const std::string text =
        latibot::commands::render_board(test.reactions, dpp::snowflake{5}, board::given, {.kind = stat_kind::given}).content;
    CHECK(text.find("/linkstats recompute") != std::string::npos);
}

TEST_CASE("a profile shows received, given and self apart", "[commands]") {
    fixture test;
    const std::string text = latibot::commands::render_profile(test.reactions, guild, alice, {});

    CHECK(text.find("Reactions received: 2") != std::string::npos);
    CHECK(text.find("Reactions given: 0") != std::string::npos);
    CHECK(text.find("Reacted to their own links: 1 time\n") != std::string::npos);
}

TEST_CASE("a date range shows in the title as it was typed", "[commands]") {
    fixture test;
    const latibot::events::stat_query query{.kind = stat_kind::received,
                                            .since = std::chrono::sys_days{std::chrono::year{2026} / 1 / 1},
                                            .until = std::chrono::sys_days{std::chrono::year{2026} / 2 / 1}};
    const std::string text = latibot::commands::render_board(test.reactions, guild, board::received, query).content;
    CHECK(text.find("since 2026-01-01 until 2026-01-31") != std::string::npos);
}

TEST_CASE("an emoji can be named rather than drawn", "[commands]") {
    fixture test;

    const auto by_name = latibot::commands::resolve_emoji(test.reactions, guild, "Skull");
    REQUIRE(by_name.has_value());
    CHECK(by_name->key == "c:77");

    CHECK(latibot::commands::resolve_emoji(test.reactions, guild, ":skull:")->key == "c:77");
    CHECK(latibot::commands::resolve_emoji(test.reactions, guild, "💀")->key == "u:💀");

    // A key from autocomplete gets its name back for showing.
    CHECK(latibot::commands::resolve_emoji(test.reactions, guild, "c:77")->name == "skull");

    CHECK_FALSE(latibot::commands::resolve_emoji(test.reactions, guild, "nothing_like_it").has_value());
}

TEST_CASE("/linkstats is open to everyone, with aliases in a group", "[commands]") {
    fixture test;
    const latibot::commands::linkstats_command command(test.reactions);

    CHECK_FALSE(command.info().default_member_permissions.has_value());

    const dpp::slashcommand payload = command.build("linkstats", dpp::snowflake{1});
    const auto alias = std::ranges::find(payload.options, std::string("alias"), &dpp::command_option::name);
    REQUIRE(alias != payload.options.end());
    CHECK(alias->type == dpp::co_sub_command_group);
    CHECK(alias->options.size() == 3);
}

TEST_CASE("a recompute's report says what it found and what it could not read", "[commands]") {
    const latibot::events::backfill_request request{.guild_id = guild,
                                                    .channel_ids = {dpp::snowflake{1}, dpp::snowflake{2}},
                                                    .since = std::chrono::sys_days{std::chrono::year{2021} / 1 / 1},
                                                    .until = std::chrono::sys_days{std::chrono::year{2022} / 1 / 1},
                                                    .bot_id = dpp::snowflake{42},
                                                    .fresh = false};

    latibot::events::backfill_report report;
    report.channels_total = 2;
    report.channels_done = 2;
    report.scanned = 1234;
    report.replacements = 56;
    report.attributed = 50;
    report.unattributed = 6;
    report.reactions = 789;
    report.unparsed = {dpp::snowflake{111}, dpp::snowflake{222}};
    report.problems = {"could not read channel 3: Missing Access"};

    const std::string text = latibot::commands::render_backfill(report, request, true);
    CHECK(text.starts_with("**Link stats recomputed since 2021-01-01 until 2021-12-31**"));
    CHECK(text.find("Messages scanned: 1234") != std::string::npos);
    CHECK(text.find("Replacements found: 56 (50 credited to whoever posted the link, 6 not)") != std::string::npos);
    CHECK(text.find("Not understood: 2 `111` `222`") != std::string::npos);
    CHECK(text.find("Missing Access") != std::string::npos);
    CHECK(text.size() <= 2000);

    SECTION("while it runs") {
        report.channels_done = 1;
        const std::string running = latibot::commands::render_backfill(report, request, false);
        CHECK(running.find("Channel 2 of 2") != std::string::npos);
        CHECK(running.find("/linkstats recompute cancel") != std::string::npos);
    }

    SECTION("stopped early") {
        report.cancelled = true;
        const std::string stopped = latibot::commands::render_backfill(report, request, true);
        CHECK(stopped.starts_with("**Link stats recompute stopped"));
        CHECK(stopped.find("carries on from where it stopped") != std::string::npos);
    }
}

TEST_CASE("recompute is its own group, with a required start date", "[commands]") {
    fixture test;
    const latibot::commands::linkstats_command command(test.reactions);
    const dpp::slashcommand payload = command.build("linkstats", dpp::snowflake{1});

    const auto recompute = std::ranges::find(payload.options, std::string("recompute"), &dpp::command_option::name);
    REQUIRE(recompute != payload.options.end());
    REQUIRE(recompute->options.size() == 2);

    const dpp::command_option& start = recompute->options[0];
    CHECK(start.name == "start");
    REQUIRE_FALSE(start.options.empty());
    CHECK(start.options[0].name == "since");
    CHECK(start.options[0].required);

    // Reading history is what a recompute does; the permission check names it.
    CHECK((command.info().required_bot_permissions & dpp::p_read_message_history) != 0);
}

TEST_CASE("a long leaderboard pages, and every page is the same board", "[commands]") {
    fixture test;
    // Twelve more posters, each with one skull from Bob.
    for (std::uint64_t index = 0; index < 12; ++index) {
        const dpp::snowflake message{600 + index};
        test.replacements.record({.message_id = message,
                                  .guild_id = guild,
                                  .channel_id = dpp::snowflake{2},
                                  .original_message_id = std::nullopt,
                                  .original_author_id = dpp::snowflake{100 + index},
                                  .state = latibot::events::replacement_state::ok,
                                  .created_at = day_one,
                                  .retried_at = std::nullopt,
                                  .links = {}});
        test.reactions.add(message, bob, reaction_emoji({}, "💀"), day_one);
    }

    const latibot::events::stat_query query{.kind = stat_kind::received, .emoji_key = "u:💀"};
    const dpp::message first = latibot::commands::render_board(test.reactions, guild, board::received, query);
    CHECK(first.content.find("Page 1 of 2") != std::string::npos);
    CHECK(first.content.find("10. ") != std::string::npos);
    CHECK(first.content.find("11. ") == std::string::npos);
    REQUIRE(first.components.size() == 1);

    // The ▶ button carries the filters; decoding them gives the same board.
    const auto state = latibot::ui::decode(first.components[0].components[1].custom_id);
    REQUIRE(state.has_value());
    CHECK(state->view == latibot::commands::board_view);
    const auto decoded = latibot::commands::decode_board(state->argument);
    REQUIRE(decoded.has_value());
    CHECK(decoded->first == board::received);
    CHECK(decoded->second.emoji_key == "u:💀");

    const dpp::message second = latibot::commands::render_board(test.reactions, guild, decoded->first, decoded->second, state->page);
    CHECK(second.content.find("Page 2 of 2") != std::string::npos);
    CHECK(second.content.find("11. ") != std::string::npos);
    CHECK(second.content.find("13. ") != std::string::npos);
}

TEST_CASE("a board's filters survive the trip through a button", "[commands]") {
    const latibot::events::stat_query query{.kind = stat_kind::given,
                                            .emoji_key = "c:77",
                                            .user_id = {},
                                            .since = std::chrono::sys_days{std::chrono::year{2025} / 1 / 1},
                                            .until = std::chrono::sys_days{std::chrono::year{2025} / 7 / 1},
                                            .domain = "x.com"};

    const std::string packed = latibot::commands::encode_board(board::given, query);
    CHECK(packed.size() < 60);

    const auto unpacked = latibot::commands::decode_board(packed);
    REQUIRE(unpacked.has_value());
    CHECK(unpacked->first == board::given);
    CHECK(unpacked->second.kind == stat_kind::given);
    CHECK(unpacked->second.emoji_key == "c:77");
    CHECK(unpacked->second.domain == "x.com");
    CHECK(unpacked->second.since == query.since);
    CHECK(unpacked->second.until == query.until);

    CHECK_FALSE(latibot::commands::decode_board("nonsense").has_value());
    CHECK_FALSE(latibot::commands::decode_board("q;;;;").has_value());
    CHECK_FALSE(latibot::commands::decode_board("r;;;soon;").has_value());
}

TEST_CASE("a board can be limited to one site", "[commands]") {
    fixture test;
    test.replacements.record(
        {.message_id = dpp::snowflake{700},
         .guild_id = guild,
         .channel_id = dpp::snowflake{2},
         .original_message_id = std::nullopt,
         .original_author_id = bob,
         .state = latibot::events::replacement_state::ok,
         .created_at = day_one,
         .retried_at = std::nullopt,
         .links = {{.original_url = "https://tiktok.com/@b/video/1", .domain = "tiktok.com", .spoilered = false, .mirrors = {}}}});
    test.reactions.add(dpp::snowflake{700}, alice, reaction_emoji({}, "💀"), day_one);

    const latibot::events::stat_query tiktok{.kind = stat_kind::received, .domain = "tiktok.com"};
    const std::string text = latibot::commands::render_board(test.reactions, guild, board::received, tiktok).content;
    CHECK(text.starts_with("**Most reactions received on replaced tiktok.com links**"));
    CHECK(text.find("<@12> 1") != std::string::npos);
    CHECK(text.find("<@11>") == std::string::npos);

    CHECK(test.reactions.known_domains(guild) == std::vector<std::string>{"tiktok.com"});
}

TEST_CASE("what a leaderboard ranks is read from its option", "[commands]") {
    CHECK(latibot::commands::board_from_string("") == board::received);
    CHECK(latibot::commands::board_from_string("given") == board::given);
    CHECK(latibot::commands::board_from_string("self") == board::self);
    CHECK(latibot::commands::board_from_string("emoji") == board::emoji);
    CHECK_FALSE(latibot::commands::board_from_string("bogus").has_value());
}
