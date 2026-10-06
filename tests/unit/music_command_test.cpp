// /music and /m (docs/features/Music.md §3.1): what the replies say, the
// queue's pages against Discord's limits, and the settings.

#include "core/commands/music.hpp"
#include "core/commands/registry.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/util/text.hpp"

#include "support/discord_limits.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

using namespace latibot::commands;
using latibot::music::music_status;
using latibot::music::queue_position;
using latibot::music::track;
using latibot::ports::media_item;
using latibot::ports::media_lookup;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{100};
constexpr dpp::snowflake someone{42};
constexpr dpp::snowflake channel{300};

auto item(std::string title, std::optional<std::chrono::seconds> length = 200s) -> media_item {
    return {.title = std::move(title), .url = "https://x.com/a", .uploader = "Artist", .duration = length, .live = false};
}

auto song(std::string title, std::optional<std::chrono::seconds> length = 200s) -> track {
    return {.id = 1, .media = item(std::move(title), length), .requested_by = someone, .channel = channel};
}

auto single(media_item entry) -> media_lookup {
    return {.items = {std::move(entry)}, .playlist_title = {}, .playlist_size = 1};
}

auto added(std::size_t count, std::size_t over = 0) -> latibot::music::add_result {
    return {.added = count, .over_limit = over, .replaces_current = false};
}

} // namespace

TEST_CASE("durations read as minutes and seconds, and hours past an hour", "[music]") {
    CHECK(format_duration(0s) == "0:00");
    CHECK(format_duration(7s) == "0:07");
    CHECK(format_duration(187s) == "3:07");
    CHECK(format_duration(3723s) == "1:02:03");
}

TEST_CASE("a track is named safely, with its length or that it is live", "[music]") {
    CHECK(describe_track(song("Song")) == "**Song** (3:20)");
    CHECK(describe_track(song("Mystery", std::nullopt)) == "**Mystery**");

    track radio = song("Radio");
    radio.media.live = true;
    CHECK(describe_track(radio) == "**Radio** (live)");

    // Markdown and mentions from a site's title are shown, not obeyed.
    const std::string shown = describe_track(song("**bold** @everyone"));
    CHECK(shown.contains("\\*\\*bold\\*\\*"));
    CHECK_FALSE(shown.contains("@everyone"));
}

TEST_CASE("text from outside is made plain", "[util]") {
    CHECK(latibot::util::plain_text("a_b*c") == "a\\_b\\*c");
    CHECK(latibot::util::plain_text("line\nbreak") == "line break");
    CHECK(latibot::util::plain_text("<@123>") ==
          "\\<@\xE2\x80\x8B"
          "123\\>");
    CHECK(latibot::util::plain_text("plain words") == "plain words");
}

TEST_CASE("tracks over the limit are left out, and live streams never are", "[music]") {
    media_item radio = item("Radio", std::nullopt);
    radio.live = true;
    const media_lookup lookup{.items = {item("short", 200s), item("long", 7200s), radio, item("unknown", std::nullopt)},
                              .playlist_title = "Mix",
                              .playlist_size = 4};
    std::size_t too_long = 0;
    const auto tracks = playable_tracks(lookup, 3600s, someone, channel, too_long);
    CHECK(too_long == 1);
    REQUIRE(tracks.size() == 3);
    CHECK(tracks[0].requested_by == someone);
    CHECK(tracks[0].channel == channel);

    CHECK(playable_tracks(lookup, std::nullopt, someone, channel, too_long).size() == 4);
    CHECK(too_long == 0);
}

TEST_CASE("the reply to /music play says what happened", "[music]") {
    SECTION("one track, playing") {
        CHECK(describe_play({.lookup = single(item("Song")),
                             .added = added(1),
                             .where = queue_position::end,
                             .playing = true,
                             .too_long = 0,
                             .limit = std::nullopt}) == "playing **Song** (3:20)");
    }
    SECTION("one track, queued") {
        CHECK(describe_play({.lookup = single(item("Song")),
                             .added = added(1),
                             .where = queue_position::end,
                             .playing = false,
                             .too_long = 0,
                             .limit = std::nullopt}) == "queued **Song** (3:20)");
    }
    SECTION("next and now") {
        CHECK(describe_play({.lookup = single(item("Song")),
                             .added = added(1),
                             .where = queue_position::next,
                             .playing = false,
                             .too_long = 0,
                             .limit = std::nullopt}) == "playing **Song** (3:20) next");
        CHECK(describe_play({.lookup = single(item("Song")),
                             .added = added(1),
                             .where = queue_position::now,
                             .playing = true,
                             .too_long = 0,
                             .limit = std::nullopt}) == "playing **Song** (3:20) now");
    }
    SECTION("a playlist, with what was left out and why") {
        media_lookup lookup{.items = {}, .playlist_title = "Big Mix", .playlist_size = 250};
        for (int i = 0; i < 100; ++i) {
            lookup.items.push_back(item("t"));
        }
        const std::string text = describe_play(
            {.lookup = lookup, .added = added(95, 3), .where = queue_position::end, .playing = false, .too_long = 2, .limit = 3600s});
        CHECK(text.starts_with("queued 95 tracks from **Big Mix**"));
        CHECK(text.contains("the other 150 were left out"));
        CHECK(text.contains("2 tracks longer than this server's 60-minute limit were left out"));
        CHECK(text.contains("3 tracks didn't fit"));
    }
    SECTION("a single track over the limit") {
        CHECK(describe_play({.lookup = single(item("Epic", 7200s)),
                             .added = added(0),
                             .where = queue_position::end,
                             .playing = false,
                             .too_long = 1,
                             .limit = 3600s})
                  .contains("longer than this server's 60-minute limit"));
    }
}

TEST_CASE("now playing shows the track, where it is, and who queued it", "[music]") {
    CHECK(render_now_playing({}) == "nothing is playing");

    music_status status;
    status.current = song("Song");
    status.elapsed = 65s;
    status.paused = true;
    status.repeat = latibot::music::repeat_mode::track;
    status.upcoming = {song("Next")};
    const std::string text = render_now_playing(status);
    CHECK(text.find("**Now playing:** Song — 1:05 / 3:20 · paused · repeating this track, queued by <@42>") == 0);
    CHECK(text.contains("<https://x.com/a>"));
    CHECK(text.contains("by Artist"));
    CHECK(text.contains("next: **Next**"));
}

TEST_CASE("the queue pages ten at a time, within Discord's limits, pinging nobody", "[music]") {
    music_status status;
    status.current = song("Current");
    for (int i = 1; i <= 25; ++i) {
        status.upcoming.push_back(song(std::string(200, 'x'), 100s));
    }

    const dpp::message first = render_music_queue(status, 0);
    latibot::testing::check_message_fits(first);
    CHECK(first.content.contains("`1.`"));
    CHECK(first.content.contains("`10.`"));
    CHECK_FALSE(first.content.contains("`11.`"));
    CHECK(first.content.contains("25 tracks queued, 41:40"));
    CHECK(first.content.contains("Page 1 of 3"));
    CHECK_FALSE(first.components.empty());
    CHECK(first.allowed_mentions.parse_users == false);

    const dpp::message last = render_music_queue(status, 99);
    latibot::testing::check_message_fits(last);
    CHECK(last.content.contains("`25.`"));
    CHECK(last.content.contains("Page 3 of 3"));
}

TEST_CASE("an empty queue says so, with no buttons", "[music]") {
    CHECK(render_music_queue({}, 0).content == "nothing is playing, and the queue is empty");

    music_status alone;
    alone.current = song("Only");
    const dpp::message message = render_music_queue(alone, 0);
    CHECK(message.content.contains("Nothing else is queued."));
    CHECK(message.components.empty());
}

TEST_CASE("volume and track limit have defaults, and stored values are kept in range", "[music]") {
    latibot::db::database db(":memory:");
    latibot::testing::create_schema(db);
    latibot::config::guild_settings settings(db);

    CHECK(music_volume_for(settings, guild) == 50);
    CHECK(track_limit_for(settings, guild) == std::chrono::seconds{3600});

    settings.set_int(guild, music_volume_key, 500);
    CHECK(music_volume_for(settings, guild) == 200);
    settings.set_int(guild, music_limit_key, 0);
    CHECK_FALSE(track_limit_for(settings, guild).has_value());
    settings.set_int(guild, music_limit_key, 90);
    CHECK(track_limit_for(settings, guild) == std::chrono::seconds{5400});
}

TEST_CASE("the music command registers, with m as its alias", "[music]") {
    registry commands;
    REQUIRE_NOTHROW(commands.add(std::make_unique<music_command>(music_services{})));
    CHECK(commands.find("music") != nullptr);
    CHECK(commands.find("m") != nullptr);

    const auto payloads = commands.build_all(dpp::snowflake{1});
    REQUIRE(payloads.size() == 2);
    for (const dpp::slashcommand& payload : payloads) {
        CHECK(payload.options.size() == 12);
        CHECK(payload.options.size() <= 25);
        for (const dpp::command_option& option : payload.options) {
            CHECK(option.description.size() <= 100);
        }
    }
}
