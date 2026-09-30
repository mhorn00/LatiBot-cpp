// Reactions on the images and videos people post (docs/features/Link_Stats.md
// §9): what counts as one, and recording them as they are posted.

#include "core/events/media_posts.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/legacy_replacements.hpp"
#include "core/events/reactions.hpp"
#include "core/events/replacements.hpp"

#include "mocks/mock_clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using latibot::events::has_media;
using latibot::events::is_direct_media_embed;
using latibot::events::is_media_attachment;
using latibot::events::media_tracker;
using latibot::events::reaction_emoji;
using latibot::events::stat_kind;
using latibot::events::stat_source;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{2000};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

const std::chrono::sys_seconds day_one{std::chrono::sys_days{std::chrono::year{2026} / 1 / 1}};

auto posted_at(std::chrono::seconds later) -> dpp::snowflake {
    return latibot::events::first_id_at(day_one + later);
}

auto attachment(std::string content_type, std::string filename) -> dpp::attachment {
    dpp::attachment made(nullptr);
    made.content_type = std::move(content_type);
    made.filename = std::move(filename);
    return made;
}

auto embed(std::string type, bool with_provider) -> dpp::embed {
    dpp::embed made;
    made.type = std::move(type);
    if (with_provider) {
        made.provider.emplace();
        made.provider->name = "YouTube";
    }
    return made;
}

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::config::guild_settings settings{db};
    latibot::events::replacement_store posts{db};
    latibot::events::reaction_store reactions{db};
    latibot::testing::mock_clock clock{day_one};
    media_tracker tracker{posts, settings, clock};

    fixture() {
        latibot::db::migrate(db);
        latibot::events::set_images_enabled(settings, guild, true);
    }

    static auto post(dpp::snowflake id, bool media, bool links, bool person = true) -> media_tracker::posted {
        return {.message_id = id,
                .guild_id = guild,
                .channel_id = channel,
                .author_id = alice,
                .from_person = person,
                .has_media = media,
                .has_links = links};
    }
};

} // namespace

// --------------------------------------------------------------------------
// What counts
// --------------------------------------------------------------------------

TEST_CASE("an image or a video counts, whatever its kind", "[events]") {
    CHECK(is_media_attachment("image/png", "cat.png"));
    CHECK(is_media_attachment("image/gif", "dance.gif"));
    CHECK(is_media_attachment("video/mp4", "clip.mp4"));
    CHECK_FALSE(is_media_attachment("application/pdf", "notes.pdf"));
    CHECK_FALSE(is_media_attachment("text/plain", "notes.png"));

    // No type from Discord: the file's name says.
    CHECK(is_media_attachment("", "Holiday.JPG"));
    CHECK(is_media_attachment("", "clip.webm"));
    CHECK_FALSE(is_media_attachment("", "archive.zip"));
}

TEST_CASE("only a link's own picture or video counts, not a site's preview", "[events]") {
    CHECK(is_direct_media_embed("image", false));
    CHECK(is_direct_media_embed("video", false));

    // YouTube's player names its provider.
    CHECK_FALSE(is_direct_media_embed("video", true));
    // Tenor and Giphy, left out on purpose.
    CHECK_FALSE(is_direct_media_embed("gifv", false));
    CHECK_FALSE(is_direct_media_embed("rich", false));
    CHECK_FALSE(is_direct_media_embed("article", false));
}

TEST_CASE("a message has media when something attached or embedded is", "[events]") {
    dpp::message message;
    CHECK_FALSE(has_media(message));

    message.attachments.push_back(attachment("application/pdf", "notes.pdf"));
    CHECK_FALSE(has_media(message));
    message.attachments.push_back(attachment("image/webp", "sticker.webp"));
    CHECK(has_media(message));

    dpp::message linked;
    linked.embeds.push_back(embed("video", true));
    CHECK_FALSE(has_media(linked));
    linked.embeds.push_back(embed("image", false));
    CHECK(has_media(linked));
}

// --------------------------------------------------------------------------
// Recording them as they are posted
// --------------------------------------------------------------------------

TEST_CASE("an upload is counted from the moment it is posted", "[db]") {
    fixture test;
    const dpp::snowflake id = posted_at(5s);

    CHECK(test.tracker.on_message(fixture::post(id, /*media=*/true, /*links=*/false)));

    const auto stored = test.posts.find(id);
    REQUIRE(stored.has_value());
    CHECK(stored->original_author_id == alice);
    CHECK(stored->original_message_id == id);
    CHECK(stored->created_at == day_one + 5s);
    CHECK(stored->links.empty());

    // Reactions on it now count, credited to whoever posted it.
    CHECK(test.reactions.add(id, bob, reaction_emoji({}, "💀"), day_one + 10s));
    CHECK(test.reactions.add(id, alice, reaction_emoji({}, "😂"), day_one + 10s));
    CHECK(test.reactions.total(guild, {.kind = stat_kind::received, .user_id = alice}) == 1);
    CHECK(test.reactions.total(guild, {.kind = stat_kind::self, .user_id = alice}) == 1);

    // Posted again, as a gateway replay would, nothing changes.
    CHECK_FALSE(test.tracker.on_message(fixture::post(id, true, false)));
}

TEST_CASE("a link waits for its preview to show whether it was an image", "[db]") {
    fixture test;
    const dpp::snowflake id = posted_at(5s);

    CHECK_FALSE(test.tracker.on_message(fixture::post(id, /*media=*/false, /*links=*/true)));
    CHECK(test.tracker.waiting() == 1);
    CHECK_FALSE(test.posts.find(id).has_value());

    SECTION("a preview that is an image counts it") {
        CHECK(test.tracker.on_update(id, /*has_media=*/true));
        CHECK(test.posts.find(id)->original_author_id == alice);
        CHECK(test.tracker.waiting() == 0);
    }

    SECTION("one that is not keeps it waiting, in case another is") {
        CHECK_FALSE(test.tracker.on_update(id, false));
        CHECK(test.tracker.waiting() == 1);
        CHECK(test.tracker.on_update(id, true));
    }

    SECTION("and after a minute it is forgotten") {
        test.clock.advance(latibot::events::media_preview_wait + 1s);
        CHECK_FALSE(test.tracker.on_update(id, true));
        CHECK_FALSE(test.posts.find(id).has_value());
    }

    SECTION("an update to a message nobody was waiting for is nothing") {
        CHECK_FALSE(test.tracker.on_update(posted_at(9s), true));
    }
}

TEST_CASE("nothing is counted where images are off, or from bots", "[db]") {
    fixture test;

    CHECK_FALSE(test.tracker.on_message(fixture::post(posted_at(1s), true, false, /*person=*/false)));

    latibot::events::set_images_enabled(test.settings, guild, false);
    CHECK_FALSE(latibot::events::images_enabled(test.settings, guild));
    CHECK_FALSE(test.tracker.on_message(fixture::post(posted_at(2s), true, false)));
    CHECK_FALSE(test.tracker.on_message(fixture::post(posted_at(3s), false, true)));
    CHECK(test.tracker.waiting() == 0);

    // Off unless turned on.
    CHECK_FALSE(latibot::events::images_enabled(test.settings, dpp::snowflake{77}));
}

// --------------------------------------------------------------------------
// Statistics by kind of post
// --------------------------------------------------------------------------

TEST_CASE("statistics count links, images, or both", "[db]") {
    fixture test;

    // Bob's replaced link, and Alice's image.
    test.posts.record({.message_id = posted_at(1s),
                       .guild_id = guild,
                       .channel_id = channel,
                       .original_message_id = std::nullopt,
                       .original_author_id = bob,
                       .state = latibot::events::replacement_state::ok,
                       .created_at = day_one + 1s,
                       .retried_at = std::nullopt,
                       .links = {{.original_url = "https://x.com/b/status/1", .domain = "x.com", .spoilered = false, .mirrors = {}}}});
    test.tracker.on_message(fixture::post(posted_at(2s), true, false));

    test.reactions.add(posted_at(1s), alice, reaction_emoji({}, "💀"), day_one + 3s);
    test.reactions.add(posted_at(2s), bob, reaction_emoji({}, "🔥"), day_one + 3s);
    test.reactions.add(posted_at(2s), dpp::snowflake{13}, reaction_emoji({}, "🔥"), day_one + 3s);

    const auto count = [&](stat_source source) { return test.reactions.total(guild, {.kind = stat_kind::received, .source = source}); };
    CHECK(count(stat_source::both) == 3);
    CHECK(count(stat_source::links) == 1);
    CHECK(count(stat_source::images) == 2);

    // A site is a link's, so it leaves images out.
    CHECK(test.reactions.total(guild, {.kind = stat_kind::received, .domain = "x.com", .source = stat_source::both}) == 1);

    // Leaderboards and emoji lists follow the same filter.
    const auto images = test.reactions.leaderboard(guild, {.kind = stat_kind::received, .source = stat_source::images}, 10);
    REQUIRE(images.size() == 1);
    CHECK(images[0].user_id == alice);
    CHECK(images[0].count == 2);
    CHECK(test.reactions.emojis(guild, {.kind = stat_kind::received, .source = stat_source::links}) == 1);
}
