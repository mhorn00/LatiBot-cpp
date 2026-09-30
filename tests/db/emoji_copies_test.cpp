// The bot's own copies of the emojis it has seen (docs/features/Link_Stats.md
// §10): what to copy, downloading and uploading them, and pruning.

#include "core/events/emoji_copies.hpp"
#include "core/commands/linkstats.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/reactions.hpp"
#include "core/events/replacements.hpp"

#include "mocks/mock_clock.hpp"
#include "mocks/mock_discord.hpp"
#include "mocks/mock_http.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

using latibot::events::emoji_copier;
using latibot::events::image_state;
using latibot::events::reaction_emoji;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake post{501};

const std::chrono::sys_seconds day_one{std::chrono::sys_days{std::chrono::year{2026} / 1 / 1}};

/// A GIF with two frames, which is what makes one move.
auto moving_gif(char tag = 'a') -> std::string {
    return std::string("GIF89a") + tag + "\x21\xF9\x04" + "frame" + "\x21\xF9\x04" + "frame";
}

auto still_png(char tag = 'a') -> std::string {
    return std::string("\x89PNG\r\n\x1a\n") + tag + "pixels";
}

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::events::replacement_store replacements{db};
    latibot::events::reaction_store reactions{db};
    latibot::events::emoji_copy_store store{db};
    latibot::testing::mock_http http;
    latibot::testing::mock_discord discord;
    latibot::testing::mock_clock clock{day_one};

    fixture() {
        latibot::db::migrate(db);
        replacements.record({.message_id = post,
                             .guild_id = guild,
                             .channel_id = dpp::snowflake{2},
                             .original_message_id = std::nullopt,
                             .original_author_id = dpp::snowflake{11},
                             .state = latibot::events::replacement_state::ok,
                             .created_at = day_one,
                             .retried_at = std::nullopt,
                             .links = {}});
    }

    /// `times` reactions with the custom emoji `id`, from different people.
    auto react(std::uint64_t id, std::string_view name, int times) -> void {
        for (int person = 0; person < times; ++person) {
            reactions.add(post, dpp::snowflake{static_cast<std::uint64_t>(100 + person)}, reaction_emoji(dpp::snowflake{id}, name),
                          day_one);
        }
    }

    auto round(std::int64_t min_uses = 1) -> emoji_copier::round_report {
        emoji_copier copier(store, http, discord, clock, min_uses);
        return *copier.run_round().sync_wait_for(2s);
    }

    [[nodiscard]] auto plan(std::int64_t min_uses = 1) const -> latibot::events::copy_plan {
        return store.plan(min_uses, std::chrono::floor<std::chrono::seconds>(clock.now()));
    }
};

} // namespace

// --------------------------------------------------------------------------
// The pieces
// --------------------------------------------------------------------------

TEST_CASE("a GIF moves when it has more than one frame", "[events]") {
    CHECK(latibot::events::is_animated_gif(moving_gif()));
    CHECK_FALSE(latibot::events::is_animated_gif("GIF89a\x21\xF9\x04still"));
    CHECK_FALSE(latibot::events::is_animated_gif(still_png()));
}

TEST_CASE("images are told apart by their SHA-256", "[events]") {
    CHECK(latibot::events::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(latibot::events::sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("a copy's name is one Discord accepts", "[events]") {
    CHECK(latibot::events::copy_name("kekw", dpp::snowflake{1}) == "kekw");
    CHECK(latibot::events::copy_name("pepe-sad!", dpp::snowflake{1}) == "pepesad");
    CHECK(latibot::events::copy_name("?", dpp::snowflake{123456789}) == "emoji_456789");
    CHECK(latibot::events::copy_name(std::string(40, 'x'), dpp::snowflake{1}).size() == 32);
}

// --------------------------------------------------------------------------
// What to copy
// --------------------------------------------------------------------------

TEST_CASE("every emote used enough is copied, most used first", "[db]") {
    fixture test;
    test.react(1, "kekw", 3);
    test.react(2, "pog", 5);
    test.react(3, "rare", 1);
    // A Unicode emoji is drawn by everybody's own device; nothing to copy.
    test.reactions.add(post, dpp::snowflake{100}, reaction_emoji({}, "💀"), day_one);

    CHECK(test.plan(1).fetch == std::vector<std::string>{"c:2", "c:1", "c:3"});
    CHECK(test.plan(3).fetch == std::vector<std::string>{"c:2", "c:1"});
    // Nought is off.
    CHECK(test.plan(0).fetch.empty());
}

TEST_CASE("an emote merged by an alias gets one copy between its emojis", "[db]") {
    fixture test;
    test.react(1, "kekw", 1);
    test.react(2, "KEKW", 2);
    REQUIRE_FALSE(test.reactions.set_alias(guild, "c:2", "c:1").has_value());

    // Counted together, the one kept is copied, even though it alone is
    // under the threshold.
    CHECK(test.plan(3).fetch == std::vector<std::string>{"c:1"});

    SECTION("and when the one kept is lost, the next stands in") {
        test.store.record_image("c:1", image_state::lost, std::nullopt, false, day_one);
        CHECK(test.plan(3).fetch == std::vector<std::string>{"c:2"});

        test.store.record_image("c:2", image_state::fetched, "hash-two", false, day_one);
        CHECK(test.plan(3).fetch == std::vector<std::string>{"c:2"});

        test.store.add_copy({.image_sha256 = "hash-two", .copy_id = dpp::snowflake{900}, .name = "KEKW"}, false, day_one);
        CHECK(test.plan(3).fetch.empty());

        // A week on, the lost one is tried again, in case the CDN has it.
        test.clock.advance(latibot::events::retry_after(image_state::lost) + 1s);
        CHECK(test.plan(3).fetch == std::vector<std::string>{"c:1"});
    }
}

TEST_CASE("copies nothing wants are pruned, once nothing is left to fetch", "[db]") {
    fixture test;
    test.react(1, "kekw", 1);
    test.react(2, "pog", 5);
    test.store.record_image("c:1", image_state::fetched, "hash-one", false, day_one);
    test.store.record_image("c:2", image_state::fetched, "hash-two", false, day_one);
    test.store.add_copy({.image_sha256 = "hash-one", .copy_id = dpp::snowflake{901}, .name = "kekw"}, false, day_one);
    test.store.add_copy({.image_sha256 = "hash-two", .copy_id = dpp::snowflake{902}, .name = "pog"}, false, day_one);

    CHECK(test.plan(1).prune.empty());

    // The threshold goes up: kekw no longer qualifies.
    const auto raised = test.plan(2);
    REQUIRE(raised.prune.size() == 1);
    CHECK(raised.prune[0].copy_id == dpp::snowflake{901});

    // Something new to fetch might have one of their images, so pruning waits.
    test.react(3, "new", 5);
    CHECK(test.plan(2).prune.empty());
}

// --------------------------------------------------------------------------
// Copying
// --------------------------------------------------------------------------

TEST_CASE("an animated emoji is copied as a GIF, a still one as a PNG", "[db][coro]") {
    fixture test;
    test.react(1, "dance", 2);
    test.react(2, "kekw", 1);

    test.http.queue(200, moving_gif());
    test.http.queue(415, "");
    test.http.queue(200, still_png());

    const auto report = test.round();
    CHECK(report.copied == 2);

    REQUIRE(test.discord.emoji_uploads.size() == 2);
    CHECK(test.discord.emoji_uploads[0].name == "dance");
    CHECK(test.discord.emoji_uploads[0].animated);
    CHECK_FALSE(test.discord.emoji_uploads[1].animated);
    CHECK(test.discord.emoji_uploads[1].image == still_png());

    REQUIRE(test.http.requests.size() == 3);
    CHECK(test.http.requests[0].url == "https://cdn.discordapp.com/emojis/1.gif");
    CHECK(test.http.requests[0].method == latibot::ports::http_method::get);
    CHECK(test.http.requests[2].url == "https://cdn.discordapp.com/emojis/2.png");

    // The statistics show the copy from now on, which the bot can use
    // anywhere, however long the original lasts.
    const auto shown = test.reactions.describe("c:1");
    REQUIRE(shown.copy.has_value());
    CHECK(latibot::events::display_emoji(shown).starts_with("<a:dance:"));

    // And the next round has nothing to do.
    CHECK(test.round().copied == 0);
    CHECK(test.discord.emoji_uploads.size() == 2);
}

TEST_CASE("the same image is copied once, and names stay unique", "[db][coro]") {
    fixture test;
    test.react(1, "kekw", 3);
    test.react(2, "kekw", 2);
    test.react(3, "kekw", 1);

    // Two servers' kekw with the same picture, and a third that differs.
    test.http.queue(415, "");
    test.http.queue(200, still_png('a'));
    test.http.queue(415, "");
    test.http.queue(200, still_png('a'));
    test.http.queue(415, "");
    test.http.queue(200, still_png('b'));

    const auto report = test.round();
    CHECK(report.copied == 2);
    CHECK(report.shared == 1);
    REQUIRE(test.discord.emoji_uploads.size() == 2);
    CHECK(test.discord.emoji_uploads[0].name == "kekw");
    CHECK(test.discord.emoji_uploads[1].name == "kekw_2");

    // Both of the first two show the one copy.
    CHECK(test.reactions.copy_of("c:1")->id == test.reactions.copy_of("c:2")->id);
}

TEST_CASE("an emoji the CDN no longer has is lost, after both hosts are tried", "[db][coro]") {
    fixture test;
    test.react(1, "gone", 1);
    for (int attempt = 0; attempt < 4; ++attempt) {
        test.http.queue(404, "");
    }

    const auto report = test.round();
    CHECK(report.lost == 1);
    CHECK(test.discord.emoji_uploads.empty());
    REQUIRE(test.http.requests.size() == 4);
    CHECK(test.http.requests[3].url == "https://media.discordapp.net/emojis/1.png");

    // Not tried again until its week is up.
    CHECK(test.plan().fetch.empty());
}

TEST_CASE("the media proxy is tried when the CDN refuses", "[db][coro]") {
    fixture test;
    test.react(1, "saved", 1);
    test.http.queue(404, "");
    test.http.queue(404, "");
    test.http.queue(415, "");
    test.http.queue(200, still_png());

    CHECK(test.round().copied == 1);
    CHECK(test.http.requests.back().url == "https://media.discordapp.net/emojis/1.png");
}

TEST_CASE("an image too big even when smaller is not uploaded", "[db][coro]") {
    fixture test;
    test.react(1, "huge", 1);
    const std::string huge(latibot::events::emoji_image_limit + 1, 'x');
    for (int host = 0; host < 2; ++host) {
        test.http.queue(415, "");
        test.http.queue(200, huge);
        test.http.queue(200, huge);
    }

    const auto report = test.round();
    CHECK(report.lost == 1);
    CHECK(test.discord.emoji_uploads.empty());
    CHECK(test.http.requests[2].url == "https://cdn.discordapp.com/emojis/1.png?size=96");
}

TEST_CASE("a failed download or upload is tried again the next day", "[db][coro]") {
    fixture test;
    test.react(1, "flaky", 1);

    SECTION("the download") {
        test.http.queue_error("connection reset");
        test.http.queue_error("connection reset");
        test.http.queue_error("connection reset");
        test.http.queue_error("connection reset");
        CHECK(test.round().failed == 1);
    }

    SECTION("the upload") {
        test.http.queue(415, "");
        test.http.queue(200, still_png());
        test.discord.emoji_upload_results.emplace_back(latibot::ports::api_error{.http_status = 400, .message = "Invalid Form Body"});
        CHECK(test.round().failed == 1);
    }

    CHECK(test.plan().fetch.empty());
    test.clock.advance(latibot::events::retry_after(image_state::failed) + 1s);
    CHECK(test.plan().fetch == std::vector<std::string>{"c:1"});
}

TEST_CASE("raising the threshold deletes the copies that no longer qualify", "[db][coro]") {
    fixture test;
    test.react(1, "kekw", 1);
    test.react(2, "pog", 5);
    test.http.queue(415, "");
    test.http.queue(200, still_png('b'));
    test.http.queue(415, "");
    test.http.queue(200, still_png('a'));
    REQUIRE(test.round(1).copied == 2);
    const dpp::snowflake kekw = test.reactions.copy_of("c:1")->id;

    const auto report = test.round(2);
    CHECK(report.pruned == 1);
    CHECK(test.discord.emoji_deletes == std::vector<dpp::snowflake>{kekw});
    CHECK_FALSE(test.reactions.copy_of("c:1").has_value());
    CHECK(test.reactions.copy_of("c:2").has_value());
}

TEST_CASE("with copying off, a round does nothing at all", "[db][coro]") {
    fixture test;
    test.react(1, "kekw", 1);
    CHECK_FALSE(emoji_copier(test.store, test.http, test.discord, test.clock, 0).enabled());
    test.round(0);
    CHECK(test.http.requests.empty());
    CHECK(test.discord.emoji_uploads.empty());
}

TEST_CASE("the duplicates menus show each emote's picture once the bot has a copy", "[db]") {
    fixture test;
    test.react(1, "kekw", 2);
    test.react(2, "KEKW", 1);
    test.store.record_image("c:1", image_state::fetched, "hash-one", false, day_one);
    test.store.add_copy({.image_sha256 = "hash-one", .copy_id = dpp::snowflake{901}, .name = "kekw"}, false, day_one);

    const dpp::message shown = latibot::commands::render_similar(test.reactions, guild, 0, {}, /*can_merge=*/true);
    REQUIRE(shown.components.size() == 1);
    const auto& options = shown.components[0].components[0].options;
    REQUIRE(options.size() == 2);
    CHECK(options[0].emoji.id == dpp::snowflake{901});
    CHECK(options[0].emoji.name == "kekw");
    // No copy of the other yet: its name only, as before.
    CHECK(options[1].emoji.id.empty());
    CHECK(shown.content.find("<:kekw:901>") != std::string::npos);
}
