// Steal this emote, used end to end as a person would use it in Discord: open
// the panel, page, pick, name, add, and look at what Discord was asked to do.

#include "steal_command.hpp"

#include "mocks/mock_clock.hpp"
#include "mocks/mock_discord.hpp"
#include "mocks/mock_http.hpp"
#include "support/discord_limits.hpp"
#include "support/panel_harness.hpp"
#include "support/panel_queries.hpp"

#include <dpp/json.h>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

using latibot::commands::find_emotes;
using latibot::commands::find_stickers;
using latibot::commands::found_emote;
using latibot::commands::render_steal_panel;
using latibot::commands::steal_panel;
using latibot::commands::steal_pick_view;
using latibot::commands::valid_emote_name;
using latibot::commands::valid_sticker_name;
using latibot::ports::emoji_server;
using latibot::testing::has_button;
using latibot::testing::option_in;
using latibot::testing::panel_harness;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake member{11};
constexpr dpp::snowflake home{1000};
constexpr dpp::snowflake away{2000};

auto server(dpp::snowflake id, std::string name, std::uint32_t still = 0, std::uint32_t animated = 0) -> emoji_server {
    return {.id = id,
            .name = std::move(name),
            .still_emoji = still,
            .animated_emoji = animated,
            .slots = 50,
            .sticker_slots = 5,
            .bot_can_add = true};
}

auto sticker(dpp::snowflake id, std::string name, dpp::sticker_format format) -> found_emote {
    return {.id = id, .name = std::move(name), .animated = false, .sticker = format};
}

/// Holds downloads back when asked to, so a test can do something while an
/// upload is under way: with the mocks alone it is over before the press ends.
class held_http final : public latibot::ports::http_client {
public:
    explicit held_http(latibot::ports::http_client& inner) : inner_(&inner) {}

    bool hold = false;

    auto release() -> void { released_.set_value(); }

    auto send(latibot::ports::http_request request) -> dpp::task<latibot::ports::result<latibot::ports::http_response>> override {
        if (hold) co_await released_.get_awaitable();
        co_return co_await inner_->send(std::move(request));
    }

private:
    latibot::ports::http_client* inner_;
    dpp::promise<void> released_;
};

struct fixture {
    latibot::testing::mock_discord discord;
    latibot::testing::mock_http http;
    held_http downloads{http};
    latibot::testing::mock_clock clock;
    std::vector<dpp::task<void>> uploads;
    steal_panel router{discord, downloads, clock, {home, away, dpp::snowflake{7000}}, [this](dpp::task<void> work, std::string) {
                           uploads.push_back(std::move(work));
                       }};
    panel_harness panel{
        [this](const auto& event, const auto& state, const std::string& chosen) { return router.on_component(event, state, chosen); },
        [this](const auto& event, const auto& state) { return router.on_form(event, state); }};

    fixture() { discord.emoji_server_list = {server(home, "Home"), server(away, "Away")}; }

    auto open(std::string_view content) -> void {
        const auto id = router.open(member, find_emotes(content)).sync_wait_for(2s);
        REQUIRE(id.has_value());
        panel.open(router.first_page(*id));
    }

    auto open(std::vector<found_emote> found, std::size_t left_out = 0) -> void {
        const auto id = router.open(member, std::move(found), left_out).sync_wait_for(2s);
        REQUIRE(id.has_value());
        panel.open(router.first_page(*id));
    }

    /// Lets the uploads a press started run to their end, and shows the panel
    /// as they left it.
    auto finish_uploads() -> void {
        for (dpp::task<void>& upload : uploads) {
            CHECK(upload.sync_wait_for(2s));
        }
        uploads.clear();
        if (!discord.interaction_edits.empty()) panel.open(discord.interaction_edits.back().second);
    }
};

} // namespace

TEST_CASE("find_emotes reads a message's custom emotes, in order, each once", "[emotes]") {
    const auto found = find_emotes("look <:cat:123> and <a:party_dog:456>, then <:cat:123> again and <@789> and <#10>");
    REQUIRE(found.size() == 2);
    CHECK(found[0].id == dpp::snowflake{123});
    CHECK(found[0].name == "cat");
    CHECK_FALSE(found[0].animated);
    CHECK(found[1].id == dpp::snowflake{456});
    CHECK(found[1].name == "party_dog");
    CHECK(found[1].animated);
}

TEST_CASE("find_emotes leaves out what only looks like an emote", "[emotes]") {
    CHECK(find_emotes("nothing here").empty());
    CHECK(find_emotes("a unicode 🐱 emoji, and 3 < 4 > 2").empty());
    CHECK(find_emotes("<:cat:notanumber> <:c:1> <cat:1> <:cat:> <::1>").empty());
    CHECK(find_emotes("`<:cat:123>` and ```\n<a:dog:456>\n```").empty());
    CHECK(find_emotes("broken <:cat:123").empty());
}

TEST_CASE("an emote's name is what Discord takes", "[emotes]") {
    CHECK(valid_emote_name("ab"));
    CHECK(valid_emote_name("Party_Cat_2"));
    CHECK(valid_emote_name(std::string(32, 'a')));
    CHECK_FALSE(valid_emote_name("a"));
    CHECK_FALSE(valid_emote_name(std::string(33, 'a')));
    CHECK_FALSE(valid_emote_name("party cat"));
    CHECK_FALSE(valid_emote_name("party-cat"));
    CHECK_FALSE(valid_emote_name(""));
}

TEST_CASE("Steal this emote is a message's Apps menu command, open to everyone", "[emotes]") {
    fixture test;
    latibot::commands::steal_command steal(test.router);
    const dpp::slashcommand payload = steal.build(std::string(latibot::commands::steal_command_name), dpp::snowflake{2});
    CHECK(payload.type == dpp::ctxm_message);
    CHECK(payload.name == "Steal this emote");
    CHECK_FALSE(steal.info().default_member_permissions.has_value());
}

TEST_CASE("the panel offers only trusted servers the bot can add to", "[emotes]") {
    fixture test;
    constexpr dpp::snowflake untrusted{3000};
    constexpr dpp::snowflake bot_cannot{7000};

    test.discord.emoji_server_list.push_back(server(untrusted, "Not trusted"));
    test.discord.emoji_server_list.push_back(server(bot_cannot, "Bot cannot"));
    test.discord.emoji_server_list.back().bot_can_add = false;

    test.open("<:cat:123>");
    const dpp::message shown = test.panel.panel();
    CHECK(option_in(shown, std::to_string(home)).has_value());
    CHECK(option_in(shown, std::to_string(away)).has_value());
    CHECK_FALSE(option_in(shown, std::to_string(untrusted)).has_value());
    CHECK_FALSE(option_in(shown, std::to_string(bot_cannot)).has_value());
}

TEST_CASE("the panel says so when there is nowhere to add to", "[emotes]") {
    fixture test;
    test.discord.emoji_server_list = {server(dpp::snowflake{3000}, "Not trusted")};
    test.open("<:cat:123>");
    CHECK(test.panel.content().contains("`trusted_guilds`"));
    CHECK_FALSE(option_in(test.panel.panel(), std::to_string(3000)).has_value());
    CHECK(test.panel.panel().embeds.at(0).image->url == "https://cdn.discordapp.com/emojis/123.png");
}

TEST_CASE("a server with no room for this kind of emote is left out, counted by kind", "[emotes]") {
    fixture test;
    test.discord.emoji_server_list[0].still_emoji = 50;
    test.discord.emoji_server_list[1].animated_emoji = 50;
    test.open("<:still:123> <a:moving:456>");

    SECTION("a still emote") {
        const dpp::message shown = test.panel.panel();
        CHECK_FALSE(option_in(shown, std::to_string(home)).has_value());
        const auto offered = option_in(shown, std::to_string(away));
        REQUIRE(offered.has_value());
        CHECK(offered->description == "50 of 50 still slots free");
        CHECK(test.panel.content().contains("1 of my servers have no still slots free"));
    }
    SECTION("an animated one") {
        test.panel.press("▶");
        const dpp::message shown = test.panel.panel();
        CHECK(option_in(shown, std::to_string(home)).has_value());
        CHECK_FALSE(option_in(shown, std::to_string(away)).has_value());
    }
}

TEST_CASE("the panel pages through the emotes, each keeping its own choices", "[emotes]") {
    fixture test;
    test.open("<:one:1> <a:two:2>");

    CHECK(test.panel.content().contains("`one`"));
    CHECK(test.panel.content().contains("Page 1 of 2"));
    test.panel.choose(std::to_string(home));
    test.panel.submit(test.panel.press("Name"), {{"name", "first"}});
    CHECK(test.panel.content().contains("`first`"));

    test.panel.press("▶");
    CHECK(test.panel.content().contains("`two`"));
    CHECK(test.panel.content().contains("animated"));
    CHECK(test.panel.content().contains("Page 2 of 2"));
    CHECK(test.panel.panel().embeds.at(0).image->url == "https://cdn.discordapp.com/emojis/2.gif");
    CHECK_FALSE(option_in(test.panel.panel(), std::to_string(home))->is_default);

    test.panel.press("◀");
    CHECK(test.panel.content().contains("`first`"));
    CHECK(test.panel.content().contains("Goes to **Home**"));
    CHECK(test.panel.panel().embeds.at(0).image->url == "https://cdn.discordapp.com/emojis/1.png");
    CHECK(option_in(test.panel.panel(), std::to_string(home))->is_default);
}

TEST_CASE("one emote alone has no paging, and nothing to add until a server is picked", "[emotes]") {
    fixture test;
    test.open("<:cat:123>");
    CHECK_FALSE(has_button(test.panel.panel(), "▶"));
    REQUIRE(has_button(test.panel.panel(), "Add"));

    test.panel.choose(std::to_string(away));
    CHECK(has_button(test.panel.panel(), "Add to Away"));
}

TEST_CASE("a name Discord would refuse is refused, and the old one stays", "[emotes]") {
    fixture test;
    test.open("<:cat:123>");

    const auto form = test.panel.press("Name");
    CHECK(latibot::testing::field_value(form, "name") == "cat");
    const auto answer = test.panel.submit(form, {{"name", "no spaces"}});
    CHECK(panel_harness::is_private_note(answer));
    CHECK(test.panel.content().contains("`cat`"));
}

TEST_CASE("adding uploads the image under the chosen name, and the menu stops offering that server", "[emotes]") {
    fixture test;
    test.http.queue(200, "still image");
    test.open("<:cat:123>");
    test.panel.choose(std::to_string(home));
    test.panel.submit(test.panel.press("Name"), {{"name", "stolen_cat"}});

    test.panel.press("Add to Home");
    CHECK(has_button(test.panel.panel(), "Adding…"));
    test.finish_uploads();

    REQUIRE(test.http.requests.size() == 1);
    CHECK(test.http.requests[0].url == "https://cdn.discordapp.com/emojis/123.png");
    REQUIRE(test.discord.guild_emoji_uploads.size() == 1);
    CHECK(test.discord.guild_emoji_uploads[0].guild_id == home);
    CHECK(test.discord.guild_emoji_uploads[0].name == "stolen_cat");
    CHECK(test.discord.guild_emoji_uploads[0].image == "still image");
    CHECK_FALSE(test.discord.guild_emoji_uploads[0].animated);

    REQUIRE(test.discord.interaction_edits.size() == 1);
    CHECK(test.discord.interaction_edits[0].first == "token");
    CHECK(test.panel.content().contains("Added `stolen_cat` to Home"));
    CHECK_FALSE(option_in(test.panel.panel(), std::to_string(home)).has_value());
    CHECK(option_in(test.panel.panel(), std::to_string(away)).has_value());
}

TEST_CASE("an animated emote is uploaded as a GIF", "[emotes]") {
    fixture test;
    test.http.queue(200, "moving image");
    test.open("<a:party:456>");
    test.panel.choose(std::to_string(home));
    test.panel.press("Add to Home");
    test.finish_uploads();

    CHECK(test.http.requests.at(0).url == "https://cdn.discordapp.com/emojis/456.gif");
    REQUIRE(test.discord.guild_emoji_uploads.size() == 1);
    CHECK(test.discord.guild_emoji_uploads[0].animated);
}

TEST_CASE("an image too big to upload is fetched smaller", "[emotes]") {
    fixture test;
    test.http.queue(200, std::string(257 * 1024, 'x'));
    test.http.queue(200, "smaller");
    test.open("<:cat:123>");
    test.panel.choose(std::to_string(home));
    test.panel.press("Add to Home");
    test.finish_uploads();

    REQUIRE(test.http.requests.size() == 2);
    CHECK(test.http.requests[1].url == "https://cdn.discordapp.com/emojis/123.png?size=96");
    REQUIRE(test.discord.guild_emoji_uploads.size() == 1);
    CHECK(test.discord.guild_emoji_uploads[0].image == "smaller");
}

TEST_CASE("what changed since the panel opened stops an upload, and says why", "[emotes]") {
    fixture test;
    test.http.queue(200, "image");
    test.open("<:cat:123>");
    test.panel.choose(std::to_string(home));

    std::string expected;
    SECTION("the bot lost the permission") {
        test.discord.emoji_server_list[0].bot_can_add = false;
        expected = "i can't add to Home any more";
    }
    SECTION("the server filled up") {
        test.discord.emoji_server_list[0].still_emoji = 50;
        expected = "Home has no still slots free any more";
    }
    SECTION("the bot left") {
        test.discord.emoji_server_list.erase(test.discord.emoji_server_list.begin());
        expected = "i'm not in that server any more";
    }

    test.panel.press("Add to Home");
    test.finish_uploads();
    CHECK(test.discord.guild_emoji_uploads.empty());
    CHECK(test.panel.content().contains(expected));
}

TEST_CASE("a failed upload says why, and can be tried again", "[emotes]") {
    fixture test;
    test.open("<:cat:123>");
    test.panel.choose(std::to_string(home));

    SECTION("the image can't be had") {
        test.http.queue(404, "");
        test.panel.press("Add to Home");
        test.finish_uploads();
        CHECK(test.panel.content().contains("couldn't download"));
        CHECK(test.discord.guild_emoji_uploads.empty());
    }
    SECTION("Discord refuses it") {
        test.http.queue(200, "image");
        test.discord.guild_emoji_results.emplace_back(
            std::unexpected(latibot::ports::api_error{.http_status = 400, .message = "Maximum number of emojis reached"}));
        test.panel.press("Add to Home");
        test.finish_uploads();
        CHECK(test.panel.content().contains("Maximum number of emojis reached"));
    }

    test.http.queue(200, "image");
    test.panel.press("Add to Home");
    test.finish_uploads();
    CHECK(test.panel.content().contains("Added `cat` to Home"));
}

TEST_CASE("an upload finishing redraws the emote now showing, not the one added", "[emotes]") {
    fixture test;
    test.http.queue(200, "image");
    test.open("<:one:1> <:two:2>");
    test.panel.choose(std::to_string(home));
    test.downloads.hold = true;
    test.panel.press("Add to Home");
    test.panel.press("▶");
    test.downloads.release();
    test.finish_uploads();
    CHECK(test.panel.content().contains("`two`"));

    test.panel.press("◀");
    CHECK(test.panel.content().contains("Added `one` to Home"));
}

TEST_CASE("a request nobody has used for a while is forgotten, and the panel says so", "[emotes]") {
    fixture test;
    test.open("<:cat:123>");

    test.clock.advance(steal_panel::request_lifetime - 1s);
    test.panel.choose(std::to_string(home));
    CHECK(test.panel.content().contains("Goes to **Home**"));

    test.clock.advance(steal_panel::request_lifetime + 1s);
    test.panel.press("Add to Home");
    CHECK(test.panel.content().contains("has expired"));
    CHECK(test.panel.panel().components.empty());
    CHECK(test.uploads.empty());
}

TEST_CASE("the panel stays within Discord's limits at its fullest", "[emotes]") {
    std::string message;
    for (int index = 1; index <= 3; ++index) {
        message += std::format("<a:{}{}:{}> ", std::string(30, 'n'), index, index);
    }

    latibot::testing::mock_discord discord;
    latibot::testing::mock_http http;
    latibot::testing::mock_clock clock;
    std::vector<dpp::snowflake> trusted{home, away};
    for (std::uint64_t index = 1; index <= 40; ++index) {
        trusted.emplace_back(9000 + index);
    }
    steal_panel router{discord, http, clock, trusted, [](dpp::task<void>, std::string) {}};
    panel_harness panel{
        [&](const auto& event, const auto& state, const std::string& chosen) { return router.on_component(event, state, chosen); },
        [&](const auto& event, const auto& state) { return router.on_form(event, state); }};

    discord.emoji_server_list = {server(home, "Home"), server(away, "Away")};
    for (std::uint64_t index = 1; index <= 40; ++index) {
        const dpp::snowflake id{9000 + index};
        discord.emoji_server_list.push_back(server(id, std::string(120, 's') + std::to_string(index), 0, 49));
    }
    const auto id = router.open(member, find_emotes(message), 3).sync_wait_for(2s);
    REQUIRE(id.has_value());
    panel.open(router.first_page(*id));

    const dpp::message shown = panel.panel();
    latibot::testing::check_message_fits(shown);
    CHECK(panel.content().contains("Only the first 25 of 42 servers"));

    panel.choose(std::to_string(9001), steal_pick_view);
    latibot::testing::check_message_fits(panel.panel());
    panel.press("▶");
    latibot::testing::check_message_fits(panel.panel());
}

TEST_CASE("find_stickers reads a message's stickers, without the Lottie ones", "[emotes]") {
    nlohmann::json data = nlohmann::json::parse(R"({"sticker_items": [
        {"id": "1", "name": "Still", "format_type": 1},
        {"id": "2", "name": "Moving", "format_type": 2},
        {"id": "3", "name": "Wumpus", "format_type": 3},
        {"id": "4", "name": "Gif", "format_type": 4}
    ]})");
    dpp::message message;
    message.fill_from_json(&data);

    const auto found = find_stickers(message.stickers);
    REQUIRE(found.size() == 3);
    CHECK(found[0].name == "Still");
    CHECK(found[0].sticker == dpp::sf_png);
    CHECK(found[1].sticker == dpp::sf_apng);
    CHECK(found[2].id == dpp::snowflake{4});
    CHECK(found[2].sticker == dpp::sf_gif);
}

TEST_CASE("a sticker's name is what Discord takes", "[emotes]") {
    CHECK(valid_sticker_name("ab"));
    CHECK(valid_sticker_name("Party cat, again!"));
    CHECK(valid_sticker_name(std::string(30, 'a')));
    CHECK(valid_sticker_name("ねこねこ"));
    CHECK_FALSE(valid_sticker_name("a"));
    CHECK_FALSE(valid_sticker_name(std::string(31, 'a')));
    CHECK_FALSE(valid_sticker_name("two\nlines"));
}

TEST_CASE("a sticker shows the room for stickers, from Discord's count", "[emotes]") {
    fixture test;
    test.discord.sticker_counts[home] = 2;
    test.discord.sticker_counts[away] = 5;
    test.open({sticker(dpp::snowflake{1}, "Party cat", dpp::sf_png)});

    CHECK(test.panel.content().contains("**Steal this sticker**"));
    CHECK(test.panel.content().contains("`Party cat`"));
    CHECK(test.panel.panel().embeds.at(0).image->url == "https://cdn.discordapp.com/stickers/1.png");

    const auto offered = option_in(test.panel.panel(), std::to_string(home));
    REQUIRE(offered.has_value());
    CHECK(offered->description == "3 of 5 sticker slots free");
    CHECK_FALSE(option_in(test.panel.panel(), std::to_string(away)).has_value());
    CHECK(test.panel.content().contains("1 of my servers have no sticker slots free"));
}

TEST_CASE("a server that won't say how many stickers it has isn't offered for one", "[emotes]") {
    fixture test;
    test.discord.sticker_count_errors[home] = {.http_status = 500, .message = "try later"};
    test.open({sticker(dpp::snowflake{1}, "Party cat", dpp::sf_png)});
    CHECK_FALSE(option_in(test.panel.panel(), std::to_string(home)).has_value());
    CHECK(option_in(test.panel.panel(), std::to_string(away)).has_value());
}

TEST_CASE("a failed sticker count doesn't hold back an emote", "[emotes]") {
    fixture test;
    test.discord.sticker_count_errors[home] = {.http_status = 500, .message = "try later"};
    test.open("<:cat:123>");
    CHECK(option_in(test.panel.panel(), std::to_string(home)).has_value());
}

TEST_CASE("adding a sticker uploads it with its name as its tag, and counts it", "[emotes]") {
    fixture test;
    test.http.queue(200, "sticker image");
    test.discord.sticker_counts[home] = 2;
    test.open({sticker(dpp::snowflake{1}, "Party cat", dpp::sf_apng)});
    test.panel.choose(std::to_string(home));
    test.panel.submit(test.panel.press("Name"), {{"name", "Stolen cat"}});

    test.panel.press("Add to Home");
    test.finish_uploads();

    CHECK(test.http.requests.at(0).url == "https://cdn.discordapp.com/stickers/1.png");
    CHECK(test.discord.guild_emoji_uploads.empty());
    REQUIRE(test.discord.guild_sticker_uploads.size() == 1);
    CHECK(test.discord.guild_sticker_uploads[0].guild_id == home);
    CHECK(test.discord.guild_sticker_uploads[0].name == "Stolen cat");
    CHECK(test.discord.guild_sticker_uploads[0].tags == "Stolen cat");
    CHECK(test.discord.guild_sticker_uploads[0].image == "sticker image");
    CHECK_FALSE(test.discord.guild_sticker_uploads[0].gif);
    CHECK(test.panel.content().contains("Added `Stolen cat` to Home"));
    CHECK_FALSE(option_in(test.panel.panel(), std::to_string(home)).has_value());
}

TEST_CASE("a GIF sticker comes from Discord's media host, and goes up as a GIF", "[emotes]") {
    fixture test;
    test.http.queue(200, "moving image");
    test.open({sticker(dpp::snowflake{4}, "Gif cat", dpp::sf_gif)});
    test.panel.choose(std::to_string(home));
    test.panel.press("Add to Home");
    test.finish_uploads();

    CHECK(test.http.requests.at(0).url == "https://media.discordapp.net/stickers/4.gif");
    REQUIRE(test.discord.guild_sticker_uploads.size() == 1);
    CHECK(test.discord.guild_sticker_uploads[0].gif);
}

TEST_CASE("a sticker up to 512 KiB is uploaded as it is", "[emotes]") {
    fixture test;
    test.http.queue(200, std::string(512 * 1024, 'x'));
    test.open({sticker(dpp::snowflake{1}, "Big cat", dpp::sf_png)});
    test.panel.choose(std::to_string(home));
    test.panel.press("Add to Home");
    test.finish_uploads();
    CHECK(test.http.requests.size() == 1);
    CHECK(test.discord.guild_sticker_uploads.size() == 1);
}

TEST_CASE("a server that filled with stickers since the panel opened stops the upload", "[emotes]") {
    fixture test;
    test.open({sticker(dpp::snowflake{1}, "Party cat", dpp::sf_png)});
    test.panel.choose(std::to_string(home));
    test.discord.sticker_counts[home] = 5;
    test.panel.press("Add to Home");
    test.finish_uploads();
    CHECK(test.discord.guild_sticker_uploads.empty());
    CHECK(test.panel.content().contains("Home has no sticker slots free any more"));
}

TEST_CASE("a recount that fails leaves it to Discord", "[emotes]") {
    fixture test;
    test.http.queue(200, "image");
    test.open({sticker(dpp::snowflake{1}, "Party cat", dpp::sf_png)});
    test.panel.choose(std::to_string(home));
    test.discord.sticker_count_errors[home] = {.http_status = 500, .message = "try later"};
    test.discord.guild_sticker_results.emplace_back(
        std::unexpected(latibot::ports::api_error{.http_status = 400, .message = "Maximum number of stickers reached"}));
    test.panel.press("Add to Home");
    test.finish_uploads();
    REQUIRE(test.discord.guild_sticker_uploads.size() == 1);
    CHECK(test.panel.content().contains("Maximum number of stickers reached"));
}

TEST_CASE("a sticker's name form takes what a sticker's name may be", "[emotes]") {
    fixture test;
    test.open({sticker(dpp::snowflake{1}, "Party cat", dpp::sf_png)});

    const auto form = test.panel.press("Name");
    CHECK(latibot::testing::field_value(form, "name") == "Party cat");
    CHECK(panel_harness::is_private_note(test.panel.submit(form, {{"name", std::string(31, 'a')}})));
    CHECK(test.panel.content().contains("`Party cat`"));

    test.panel.submit(test.panel.press("Name"), {{"name", "Two words"}});
    CHECK(test.panel.content().contains("`Two words`"));
}

TEST_CASE("the panel mentions the stickers it left out", "[emotes]") {
    fixture test;
    test.open(find_emotes("<:cat:123>"), 1);
    CHECK(test.panel.content().contains("Left out 1 Lottie sticker(s)"));
}
