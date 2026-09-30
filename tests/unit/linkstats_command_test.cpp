// /linkstats: leaderboards, profiles and aliases (plan v4 §9.6).

#include "core/commands/linkstats.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/reactions.hpp"
#include "core/events/replacements.hpp"
#include "core/ui/paginator.hpp"
#include "core/util/text.hpp"

#include "support/discord_limits.hpp"
#include "support/panel_harness.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

using latibot::commands::board;
using latibot::commands::parse_day;
using latibot::events::reaction_emoji;
using latibot::events::stat_kind;
using latibot::testing::panel_harness;

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

TEST_CASE("changing aliases and recomputing need Manage Server, and reading does not", "[commands]") {
    // Discord's default permissions cover the whole command, and the command
    // is open to everyone, so this is the only thing standing between anybody
    // and a recompute of years of history (plan §21.13).
    using latibot::commands::linkstats_refusal;

    const dpp::permission nobody{};
    const dpp::permission manager(dpp::p_manage_guild);
    const dpp::permission administrator(dpp::p_administrator);
    const dpp::permission moderator(dpp::p_manage_messages);

    for (const std::string_view open : {"top", "user", "reactions", "duplicates", "alias list"}) {
        INFO(open);
        CHECK_FALSE(linkstats_refusal(open, nobody).has_value());
    }

    for (const std::string_view gated : {"alias add", "alias remove", "recompute start", "recompute cancel"}) {
        INFO(gated);
        CHECK(linkstats_refusal(gated, nobody).has_value());
        CHECK(linkstats_refusal(gated, moderator).has_value());
        CHECK_FALSE(linkstats_refusal(gated, manager).has_value());
        CHECK_FALSE(linkstats_refusal(gated, administrator).has_value());
    }

    CHECK(linkstats_refusal("alias add", nobody) == "changing emoji aliases needs Manage Server");
    CHECK(linkstats_refusal("recompute start", nobody) == "recomputing link stats needs Manage Server");
}

TEST_CASE("custom emojis with names alike are listed a group at a time", "[commands]") {
    fixture test;
    CHECK(latibot::commands::render_similar(test.reactions, guild, 0, {}, true)
              .content.starts_with("No two custom emojis here have names alike"));

    // A second emote called skull, as when one is uploaded twice, and a
    // third a letter off.
    test.reactions.add(dpp::snowflake{501}, alice, reaction_emoji(dpp::snowflake{78}, "Skull"), day_one);
    test.reactions.add(dpp::snowflake{501}, alice, reaction_emoji(dpp::snowflake{79}, "skul"), day_one);

    SECTION("to anybody, as a list") {
        const dpp::message shown = latibot::commands::render_similar(test.reactions, guild, 0, {}, false);
        CHECK(shown.content.find("1. <:skull:77> `skull` 1") != std::string::npos);
        CHECK(shown.content.find("<:Skull:78>") != std::string::npos);
        CHECK(shown.content.find("<:skul:79>") != std::string::npos);
        CHECK(shown.content.find("Manage Server") != std::string::npos);
        CHECK(shown.content.find("Group 1 of 1") != std::string::npos);
        CHECK(shown.components.empty());
    }

    SECTION("to somebody who can merge them, with a menu to pick the one to keep") {
        const dpp::message shown = latibot::commands::render_similar(test.reactions, guild, 0, {}, true);
        REQUIRE(shown.components.size() == 1);
        CHECK(shown.components[0].components[0].options.size() == 3);
        latibot::testing::check_message_fits(shown);
    }

    SECTION("and once one is picked, a menu of what to merge into it") {
        const dpp::message shown = latibot::commands::render_similar(test.reactions, guild, 0, "c:77", true);
        REQUIRE(shown.components.size() == 2);
        CHECK(shown.content.find("Keeping <:skull:77>") != std::string::npos);
        const auto& merge = shown.components[1].components[0].options;
        REQUIRE(merge.size() == 3);
        CHECK(merge[0].value == latibot::commands::merge_everything);
        CHECK(merge[1].value == "c:78");
        CHECK(merge[2].value == "c:79");
        latibot::testing::check_message_fits(shown);
    }
}

TEST_CASE("emojis alike are merged from the list by somebody with Manage Server", "[commands]") {
    fixture test;
    test.reactions.add(dpp::snowflake{501}, alice, reaction_emoji(dpp::snowflake{78}, "Skull"), day_one);
    test.reactions.add(dpp::snowflake{501}, alice, reaction_emoji(dpp::snowflake{79}, "skul"), day_one);
    test.reactions.add(dpp::snowflake{501}, alice, reaction_emoji(dpp::snowflake{80}, "catjam"), day_one);
    test.reactions.add(dpp::snowflake{501}, alice, reaction_emoji(dpp::snowflake{81}, "catJAM"), day_one);

    panel_harness discord{[&](const auto& event, const auto& state, const std::string& chosen) {
                              return latibot::commands::on_linkstats_component(test.reactions, event, state, chosen);
                          },
                          [](const auto&, const auto&) { return false; }};
    discord.permissions = dpp::p_manage_guild;
    discord.open(latibot::commands::render_similar(test.reactions, guild, 0, {}, true));

    SECTION("one at a time") {
        CHECK(panel_harness::is_update(discord.choose("c:77")));
        CHECK(discord.content().find("Keeping <:skull:77>") != std::string::npos);

        CHECK(panel_harness::is_update(discord.choose("c:79", latibot::commands::merge_view)));
        CHECK(test.reactions.canonical(guild, "c:79") == "c:77");
        CHECK(test.reactions.canonical(guild, "c:78") == "c:78");
        CHECK(discord.content().find("Merged <:skul:79> into <:skull:77>.") != std::string::npos);
        // Still keeping the same one, for what is left of the group.
        CHECK(discord.content().find("Keeping <:skull:77>") != std::string::npos);
    }

    SECTION("all at once, and the next group takes its place") {
        discord.choose("c:77");
        CHECK(panel_harness::is_update(discord.choose(latibot::commands::merge_everything, latibot::commands::merge_view)));
        CHECK(test.reactions.canonical(guild, "c:78") == "c:77");
        CHECK(test.reactions.canonical(guild, "c:79") == "c:77");
        CHECK(discord.content().find("<:catjam:80>") != std::string::npos);
        CHECK(discord.content().find("Group 1 of 1") != std::string::npos);
    }

    SECTION("not by somebody who has lost Manage Server since") {
        discord.choose("c:77");
        discord.permissions = 0;
        const auto answer = discord.choose("c:78", latibot::commands::merge_view);
        CHECK(panel_harness::is_private_note(answer));
        CHECK(panel_harness::text_of(answer) == "merging emojis needs Manage Server");
        CHECK(test.reactions.canonical(guild, "c:78") == "c:78");
    }
}

TEST_CASE("aliases are listed as what counts as what, within Discord's limit", "[commands]") {
    fixture test;
    CHECK(latibot::commands::render_aliases(test.reactions, guild).starts_with("No emoji aliases here."));

    test.reactions.add(dpp::snowflake{501}, alice, reaction_emoji(dpp::snowflake{78}, "skull"), day_one);
    REQUIRE_FALSE(test.reactions.set_alias(guild, "c:78", "c:77").has_value());

    const std::string one = latibot::commands::render_aliases(test.reactions, guild);
    const std::size_t line = one.find(" counts as ");
    REQUIRE(line != std::string::npos);
    CHECK(one.find("78") < line);
    CHECK(one.find("77") > line);

    for (std::uint64_t index = 0; index < 200; ++index) {
        REQUIRE_FALSE(test.reactions.set_alias(guild, std::format("c:{}", 1000 + index), "c:77").has_value());
    }
    const std::string many = latibot::commands::render_aliases(test.reactions, guild);
    CHECK(many.size() <= 2000);
    CHECK(many.ends_with("…and more\n"));
}

TEST_CASE("dates are read as YYYY-MM-DD and must exist", "[commands]") {
    CHECK(parse_day("2024-02-29") == std::chrono::sys_days{std::chrono::year{2024} / 2 / 29});
    CHECK_FALSE(parse_day("2023-02-29").has_value());
    CHECK_FALSE(parse_day("2024-13-01").has_value());
    CHECK_FALSE(parse_day("24-01-01").has_value());
    CHECK_FALSE(parse_day("2024/01/01").has_value());
    CHECK_FALSE(parse_day("yesterday").has_value());
}

TEST_CASE("the leaderboard names people without pinging them", "[commands]") {
    const fixture test;
    const std::string text = latibot::commands::render_board(test.reactions, guild, board::received, {.kind = stat_kind::received}).content;

    CHECK(text.starts_with("**Most reactions received on replaced links**"));
    CHECK(text.find("1. <@11> 2") != std::string::npos);
    // Alice's own laugh is not a reaction she received.
    CHECK(text.find("<@12>") == std::string::npos);
}

TEST_CASE("the emoji leaderboard shows emojis rather than people", "[commands]") {
    const fixture test;
    const std::string text = latibot::commands::render_board(test.reactions, guild, board::emoji, {.kind = stat_kind::received}).content;

    CHECK(text.find("💀 1") != std::string::npos);
    CHECK(text.find("<:skull:77> 1") != std::string::npos);
    CHECK(text.find("😂") == std::string::npos);
}

TEST_CASE("an empty leaderboard says how to fill it", "[commands]") {
    const fixture test;
    const std::string text =
        latibot::commands::render_board(test.reactions, dpp::snowflake{5}, board::given, {.kind = stat_kind::given}).content;
    CHECK(text.find("/linkstats recompute") != std::string::npos);
}

TEST_CASE("a profile shows received, given and self apart", "[commands]") {
    const fixture test;
    const std::string text = latibot::commands::render_profile(test.reactions, guild, alice, {});

    CHECK(text.find("Reactions received: 2") != std::string::npos);
    CHECK(text.find("Reactions given: 0") != std::string::npos);
    CHECK(text.find("Reacted to their own links: 1 time\n") != std::string::npos);
}

TEST_CASE("a date range shows in the title as it was typed", "[commands]") {
    const fixture test;
    const latibot::events::stat_query query{.kind = stat_kind::received,
                                            .since = std::chrono::sys_days{std::chrono::year{2026} / 1 / 1},
                                            .until = std::chrono::sys_days{std::chrono::year{2026} / 2 / 1}};
    const std::string text = latibot::commands::render_board(test.reactions, guild, board::received, query).content;
    CHECK(text.find("since 2026-01-01 until 2026-01-31") != std::string::npos);
}

TEST_CASE("an emoji can be named rather than drawn", "[commands]") {
    const fixture test;

    const auto by_name = latibot::commands::resolve_emoji(test.reactions, guild, "Skull");
    REQUIRE(by_name.has_value());
    CHECK(by_name->key == "c:77");

    CHECK(latibot::commands::resolve_emoji(test.reactions, guild, ":skull:")->key == "c:77");
    CHECK(latibot::commands::resolve_emoji(test.reactions, guild, "💀")->key == "u:💀");

    // A key from autocomplete gets its name back for showing.
    CHECK(latibot::commands::resolve_emoji(test.reactions, guild, "c:77")->name == "skull");

    CHECK_FALSE(latibot::commands::resolve_emoji(test.reactions, guild, "nothing_like_it").has_value());
}

TEST_CASE("link stats are open to everyone, with aliases in a group", "[commands]") {
    fixture test;
    const latibot::commands::linkstats_command command(test.reactions);

    CHECK_FALSE(command.info().default_member_permissions.has_value());

    const dpp::slashcommand payload = command.build("linkstats", dpp::snowflake{1});
    const auto alias = std::ranges::find(payload.options, std::string("alias"), &dpp::command_option::name);
    REQUIRE(alias != payload.options.end());
    CHECK(alias->type == dpp::co_sub_command_group);
    CHECK(alias->options.size() == 3);
}

TEST_CASE("the longest site filter still leaves room for a board's paging", "[commands]") {
    // The filters ride in the ◀ / ▶ buttons' custom_id; a site too long for it
    // used to drop the paging without a word.
    fixture test;
    const latibot::commands::linkstats_command command(test.reactions);
    const dpp::slashcommand payload = command.build("linkstats", dpp::snowflake{1});
    const auto top = std::ranges::find(payload.options, std::string("top"), &dpp::command_option::name);
    REQUIRE(top != payload.options.end());
    const auto domain = std::ranges::find(top->options, std::string("domain"), &dpp::command_option::name);
    REQUIRE(domain != top->options.end());
    // DPP keeps a text option's longest length in max_value.
    CHECK(std::cmp_equal(std::get<std::int64_t>(domain->max_value), latibot::commands::domain_length_limit));

    // At that length, with a custom emoji and both dates, on page 999.
    const latibot::events::stat_query widest{.kind = stat_kind::given,
                                             .emoji_key = "c:1234567890123456789",
                                             .user_id = {},
                                             .since = std::chrono::sys_days{std::chrono::year{2025} / 1 / 1},
                                             .until = std::chrono::sys_days{std::chrono::year{2025} / 7 / 1},
                                             .domain = std::string(latibot::commands::domain_length_limit, 'x')};
    CHECK(latibot::ui::encode({.view = std::string(latibot::commands::board_view),
                               .page = 999,
                               .argument = latibot::commands::encode_board(board::given, widest)})
              .has_value());
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
    report.unparsed = {{.channel_id = dpp::snowflake{1}, .message_id = dpp::snowflake{111}},
                       {.channel_id = dpp::snowflake{2}, .message_id = dpp::snowflake{222}}};
    report.problems = {"could not read <#3>: Missing Access"};

    const std::string text = latibot::commands::render_backfill(report, request, true);
    CHECK(text.starts_with("**Link stats recomputed since 2021-01-01 until 2021-12-31**"));
    CHECK(text.find("Messages scanned: 1234") != std::string::npos);
    CHECK(text.find("Replacements found: 56 (50 credited to whoever posted the link, 6 not)") != std::string::npos);
    // Linked, so a click shows each one.
    CHECK(text.find("Not understood: in no replacement format known: 2\n"
                    "- https://discord.com/channels/1000/1/111\n"
                    "- https://discord.com/channels/1000/2/222\n") != std::string::npos);
    CHECK(text.find("Missing Access") != std::string::npos);
    CHECK(text.size() <= 2000);

    SECTION("with more to look at than a message has room for") {
        for (std::uint64_t index = 0; index < 500; ++index) {
            const latibot::events::message_place place{.channel_id = dpp::snowflake{1234567890123456789ULL},
                                                       .message_id = dpp::snowflake{1234567890123456789ULL + index}};
            report.unparsed.push_back(place);
            report.mismatched.push_back(place);
            report.unread.push_back(place);
        }
        for (int index = 0; index < 20; ++index) {
            report.learned_mirrors.emplace_back(std::format("mirror-number-{}.example", index), "x.com");
            report.problems.push_back(std::format("could not read <#{}>: Missing Access", 1234567890123456789ULL));
        }
        const std::string crowded = latibot::commands::render_backfill(report, request, true);
        CHECK(latibot::util::character_count(crowded) <= 2000);
        CHECK(crowded.find("Old mirrors recognised: ") != std::string::npos);
        CHECK(crowded.find(" and 17 more") != std::string::npos);
        CHECK(crowded.find("Not credited: after a link that is not the one it replaced: 500") != std::string::npos);
        CHECK(crowded.find("Reactions not read: their old counts are kept: 500") != std::string::npos);
    }

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
    latibot::testing::check_message_fits(first);
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
    latibot::testing::check_message_fits(second);
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

TEST_CASE("a finished recompute is answered with a ping to whoever started it", "[commands]") {
    const latibot::events::backfill_request request{.guild_id = guild,
                                                    .channel_ids = {dpp::snowflake{1}},
                                                    .since = std::chrono::sys_days{std::chrono::year{2021} / 1 / 1},
                                                    .until = std::nullopt,
                                                    .bot_id = dpp::snowflake{42},
                                                    .fresh = false};
    latibot::events::backfill_report report;
    report.replacements = 56;
    report.reactions = 1;

    const dpp::snowflake channel{3000};
    const dpp::snowflake progress{7000};

    SECTION("a reply to the report, pinging them and nobody else") {
        const dpp::message reply = latibot::commands::backfill_done_reply(report, request, channel, progress, alice);
        CHECK(reply.channel_id == channel);
        CHECK(reply.content == "<@11> the link stats recompute since 2021-01-01 is done: 56 replacements found, 1 reaction recorded.");
        CHECK(reply.message_reference.message_id == progress);

        const auto sent = nlohmann::json::parse(reply.build_json());
        CHECK(sent["allowed_mentions"]["users"] == nlohmann::json::array({"11"}));
        CHECK(sent["allowed_mentions"]["parse"].empty());
        CHECK(reply.file_data.empty());
    }

    SECTION("with every message worth a look in a file") {
        report.cancelled = true;
        report.mismatched = {{.channel_id = dpp::snowflake{1}, .message_id = dpp::snowflake{111}}};
        report.unread = {{.channel_id = dpp::snowflake{2}, .message_id = dpp::snowflake{222}}};

        const dpp::message reply = latibot::commands::backfill_done_reply(report, request, channel, progress, alice);
        CHECK(reply.content.find("since 2021-01-01 stopped:") != std::string::npos);
        CHECK(reply.content.ends_with("2 messages to look at, linked in the file."));
        REQUIRE(reply.file_data.size() == 1);
        CHECK(reply.file_data[0].name == "recompute-issues.txt");
        CHECK(reply.file_data[0].content ==
              "Link stats recompute since 2021-01-01\n\n"
              "Not credited: after a link that is not the one it replaced (1)\n"
              "https://discord.com/channels/1000/1/111\n"
              "\nReactions not read: their old counts are kept (1)\n"
              "https://discord.com/channels/1000/2/222\n");
    }

    SECTION("not as a reply when the report could not be posted") {
        const dpp::message reply = latibot::commands::backfill_done_reply(report, request, channel, {}, alice);
        CHECK(reply.message_reference.message_id.empty());
    }
}

TEST_CASE("every reaction, by emoji, for everyone or for one person", "[commands]") {
    fixture test;
    // A link of Bob's, which Alice reacted to.
    test.replacements.record({.message_id = dpp::snowflake{502},
                              .guild_id = guild,
                              .channel_id = dpp::snowflake{2},
                              .original_message_id = std::nullopt,
                              .original_author_id = bob,
                              .state = latibot::events::replacement_state::ok,
                              .created_at = day_one,
                              .retried_at = std::nullopt,
                              .links = {}});
    test.reactions.add(dpp::snowflake{502}, alice, reaction_emoji({}, "🔥"), day_one);

    SECTION("everyone's, received") {
        const std::string text =
            latibot::commands::render_board(test.reactions, guild, board::emoji, {.kind = stat_kind::received}).content;
        CHECK(text.starts_with("**Most used reactions received on replaced links**"));
        CHECK(text.find("_3 reactions with 3 different emojis_") != std::string::npos);
    }

    SECTION("what one person received") {
        const latibot::events::stat_query alices{.kind = stat_kind::received, .user_id = alice};
        const std::string text = latibot::commands::render_board(test.reactions, guild, board::emoji, alices).content;
        CHECK(text.starts_with("**Reactions <@11> received on replaced links**"));
        CHECK(text.find("💀 1") != std::string::npos);
        CHECK(text.find("<:skull:77> 1") != std::string::npos);
        CHECK(text.find("🔥") == std::string::npos);
    }

    SECTION("what one person gave") {
        const latibot::events::stat_query alices{.kind = stat_kind::given, .user_id = alice};
        const std::string text = latibot::commands::render_board(test.reactions, guild, board::emoji_given, alices).content;
        CHECK(text.starts_with("**Reactions <@11> gave on replaced links**"));
        CHECK(text.find("🔥 1") != std::string::npos);
        // Her laugh at her own link is neither.
        CHECK(text.find("😂") == std::string::npos);
    }
}

TEST_CASE("a page of one person's reactions stays theirs", "[commands]") {
    fixture test;
    // Thirty emojis from Bob on Alice's link: two pages.
    for (std::uint64_t index = 0; index < 30; ++index) {
        test.reactions.add(dpp::snowflake{501}, bob, reaction_emoji(dpp::snowflake{900 + index}, std::format("e{}", index)), day_one);
    }

    const latibot::events::stat_query alices{.kind = stat_kind::received, .user_id = alice};
    const dpp::message first = latibot::commands::render_board(test.reactions, guild, board::emoji, alices);
    CHECK(first.content.find("Page 1 of 2") != std::string::npos);
    CHECK(first.content.find("20. ") != std::string::npos);
    CHECK(first.content.find("21. ") == std::string::npos);
    latibot::testing::check_message_fits(first);
    REQUIRE(first.components.size() == 1);

    const auto state = latibot::ui::decode(first.components[0].components[1].custom_id);
    REQUIRE(state.has_value());
    const auto decoded = latibot::commands::decode_board(state->argument);
    REQUIRE(decoded.has_value());
    CHECK(decoded->first == board::emoji);
    CHECK(decoded->second.user_id == alice);

    const dpp::message second = latibot::commands::render_board(test.reactions, guild, decoded->first, decoded->second, state->page);
    CHECK(second.content.find("Page 2 of 2") != std::string::npos);
    CHECK(second.content.find("32. ") != std::string::npos);
}

TEST_CASE("a board's buttons from before people could be named still page", "[commands]") {
    const auto old = latibot::commands::decode_board("g;c:77;x.com;20089;");
    REQUIRE(old.has_value());
    CHECK(old->first == board::given);
    CHECK_FALSE(old->second.user_id.has_value());

    CHECK_FALSE(latibot::commands::decode_board("e;;;;;not-a-person").has_value());
    CHECK_FALSE(latibot::commands::decode_board("e;;;;;;").has_value());
}
