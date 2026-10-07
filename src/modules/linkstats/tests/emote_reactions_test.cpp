// Emotes sent as reactions: a message of nothing but emojis just after a
// post, or a reply to it (src/modules/linkstats/docs/Link_Stats.md §12). What counts as
// one, which messages count, storing them, and counting them as they arrive.

#include "emote_reactions.hpp"
#include "core/db/database.hpp"
#include "legacy_replacements.hpp"
#include "links/replacements.hpp"
#include "reactions.hpp"

#include "links/module.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using latibot::events::emote_message;
using latibot::events::emote_reactions;
using latibot::events::message_emotes;
using latibot::events::reaction_emoji;
using latibot::events::stat_kind;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{2000};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};
constexpr dpp::snowflake carol{13};

const std::chrono::sys_seconds day_one{std::chrono::sys_days{std::chrono::year{2026} / 1 / 1}};

/// The id of a message posted `later` after day one.
auto at(std::chrono::seconds later) -> dpp::snowflake {
    return latibot::events::first_id_at(day_one + later);
}

auto keys_of(const std::vector<latibot::events::emoji_ref>& emojis) -> std::vector<std::string> {
    std::vector<std::string> keys;
    keys.reserve(emojis.size());
    for (const auto& emoji : emojis) {
        keys.push_back(emoji.key);
    }
    return keys;
}

auto said(dpp::snowflake id, dpp::snowflake author, std::string_view text, dpp::snowflake replied_to = {}) -> emote_message {
    return {.id = id, .author_id = author, .from_person = true, .replied_to = replied_to, .emotes = message_emotes(text)};
}

auto from_bot(dpp::snowflake id, std::string_view text) -> emote_message {
    return {.id = id, .author_id = dpp::snowflake{42}, .from_person = false, .replied_to = {}, .emotes = message_emotes(text)};
}

/// Who sent what, from where, as "12 u:💀 <id>".
auto described(const std::vector<latibot::events::emote_reaction>& found) -> std::vector<std::string> {
    std::vector<std::string> lines;
    lines.reserve(found.size());
    for (const auto& emote : found) {
        lines.push_back(std::format("{} {} {}", emote.user_id, emote.emoji.key, emote.source_id));
    }
    return lines;
}

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::events::replacement_store posts{db};
    latibot::events::reaction_store reactions{db};
    latibot::events::emote_tracker tracker{posts, reactions};

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::links::schema());
        latibot::db::apply_schema(db, latibot::events::linkstats_schema());
    }

    /// Alice's image, posted `later` after day one.
    auto post(std::chrono::seconds later) -> dpp::snowflake {
        posts.record_image_post(at(later), guild, channel, alice, day_one + later);
        return at(later);
    }

    [[nodiscard]] auto received(dpp::snowflake who = alice) const -> std::int64_t {
        return reactions.total(guild, {.kind = stat_kind::received, .user_id = who});
    }
};

} // namespace

// --------------------------------------------------------------------------
// What is a message of nothing but emotes
// --------------------------------------------------------------------------

TEST_CASE("a message of nothing but emojis is read as its emojis", "[linkstats]") {
    CHECK(keys_of(message_emotes("💀")) == std::vector<std::string>{"u:💀"});
    CHECK(keys_of(message_emotes("<:kekw:123> <a:party:456>")) == std::vector<std::string>{"c:123", "c:456"});
    CHECK(message_emotes("<a:party:456>")[0].animated);
    CHECK(message_emotes("<:kekw:123>")[0].name == "kekw");

    // Each once, in the order they first appear, spaces and lines or none.
    CHECK(keys_of(message_emotes("💀💀🔥")) == std::vector<std::string>{"u:💀", "u:🔥"});
    CHECK(keys_of(message_emotes("  💀\n🔥 <:kekw:123>")) == std::vector<std::string>{"u:💀", "u:🔥", "c:123"});

    // A heart is the same heart with or without its variation selector, as a
    // reaction's is.
    CHECK(keys_of(message_emotes("❤️")) == std::vector<std::string>{"u:❤"});
    CHECK(keys_of(message_emotes("❤")) == std::vector<std::string>{"u:❤"});
}

TEST_CASE("joined emojis, skin tones, flags and keycaps are one emoji each", "[linkstats]") {
    // A thumbs up with a skin tone; a family joined by U+200D; a flag of two
    // regional indicators; England's flag, made of tags; a keycap.
    const std::string thumbs = "\U0001F44D\U0001F3FD";
    const std::string family = "\U0001F468‍\U0001F469‍\U0001F467";
    const std::string flag = "\U0001F1FA\U0001F1F8";
    const std::string england = "\U0001F3F4\U000E0067\U000E0062\U000E0065\U000E006E\U000E0067\U000E007F";
    const std::string keycap = "1️⃣";

    for (const std::string& emoji : {thumbs, family, flag, england}) {
        INFO(emoji);
        CHECK(keys_of(message_emotes(emoji)) == std::vector<std::string>{"u:" + emoji});
    }
    CHECK(keys_of(message_emotes(keycap)) == std::vector<std::string>{"u:1⃣"});
    CHECK(message_emotes(thumbs + family + flag).size() == 3);
}

TEST_CASE("anything else in a message makes it not a reaction", "[linkstats]") {
    for (const char* text :
         {"lol 💀", "💀 lol", "<@11> 💀", "1", "", "   ", ":kekw:", "<:kekw:>", "<:kekw:123", "<#2000>", "💀!", "\U0001F468‍"}) {
        INFO(text);
        CHECK(message_emotes(text).empty());
    }
}

// --------------------------------------------------------------------------
// Which messages count
// --------------------------------------------------------------------------

TEST_CASE("each person's first message after a post counts, when it is all emotes", "[linkstats]") {
    const dpp::snowflake post{100};
    const std::vector<emote_message> after{
        said(dpp::snowflake{101}, bob, "💀🔥"),      said(dpp::snowflake{102}, alice, "lol"), said(dpp::snowflake{103}, alice, "😂"),
        said(dpp::snowflake{104}, carol, "😂 lmao"), said(dpp::snowflake{105}, bob, "😭"),    said(dpp::snowflake{106}, carol, "👀"),
    };

    // Bob's first message is two emotes, each a reaction. Alice and Carol
    // said something else first, and what anyone said next may be about
    // anything.
    CHECK(described(emote_reactions(post, after, {})) == std::vector<std::string>{"12 u:💀 101", "12 u:🔥 101"});
}

TEST_CASE("only the first 25 messages after a post are looked at", "[linkstats]") {
    const dpp::snowflake post{100};
    std::vector<emote_message> after;
    after.reserve(latibot::events::emote_window + 1);
    for (std::uint64_t index = 0; index < latibot::events::emote_window - 1; ++index) {
        // Bots count towards the window, but are nobody's reaction.
        after.push_back(from_bot(dpp::snowflake{101 + index}, "💀"));
    }
    after.push_back(said(dpp::snowflake{200}, bob, "💀"));
    CHECK(emote_reactions(post, after, {}).size() == 1);

    after.insert(after.begin(), said(dpp::snowflake{199}, carol, "hi"));
    CHECK(emote_reactions(post, after, {}).empty());
}

TEST_CASE("a reply to the post counts wherever it is, and a reply to anything else does not", "[linkstats]") {
    const dpp::snowflake post{100};

    // Bob's first message answers something else, so it is about that, and
    // his next is not his first.
    const std::vector<emote_message> after{said(dpp::snowflake{101}, bob, "💀", dpp::snowflake{99}), said(dpp::snowflake{102}, bob, "🔥")};
    CHECK(emote_reactions(post, after, {}).empty());

    // A reply to the post, however late, counts, and so does one in the
    // window. Each emoji counts once per person, from the first message it
    // was in.
    const std::vector<emote_message> window{said(dpp::snowflake{101}, carol, "😂", post)};
    const std::vector<emote_message> replies{said(dpp::snowflake{500}, carol, "😂😭", post), said(dpp::snowflake{501}, bob, "👀", post),
                                             said(dpp::snowflake{502}, bob, "lol", post)};
    CHECK(described(emote_reactions(post, window, replies)) == std::vector<std::string>{"13 u:😂 101", "13 u:😭 500", "12 u:👀 501"});
}

// --------------------------------------------------------------------------
// Stored
// --------------------------------------------------------------------------

TEST_CASE("an emote sent as a reaction counts as one, and not twice beside the same reaction", "[linkstats]") {
    fixture test;
    const dpp::snowflake post = test.post(0s);
    test.reactions.add(post, bob, reaction_emoji({}, "💀"), day_one + 1s);

    const std::vector<latibot::events::emote_reaction> sent{
        {.user_id = bob, .emoji = reaction_emoji({}, "💀"), .source_id = at(2s)},
        {.user_id = bob, .emoji = reaction_emoji(dpp::snowflake{77}, "kekw"), .source_id = at(2s)},
    };
    CHECK(test.reactions.add_emotes(post, sent) == 2);
    // Again changes nothing.
    CHECK(test.reactions.add_emotes(post, sent) == 0);

    // Bob's skull, as a reaction and as a message, is one skull.
    CHECK(test.received() == 2);
    CHECK(test.reactions.total(guild, {.kind = stat_kind::given, .user_id = bob}) == 2);
    const auto emojis = test.reactions.emoji_breakdown(guild, {.kind = stat_kind::received}, 10);
    REQUIRE(emojis.size() == 2);
    CHECK(emojis[0].count == 1);
    CHECK(emojis[1].count == 1);
    // A custom emoji sent in a message is known by name, as a reaction's is.
    CHECK(test.reactions.describe("c:77").name == "kekw");

    // Taking the reaction back leaves the skull he sent.
    test.reactions.remove(post, bob, "u:💀", day_one + 3s);
    CHECK(test.received() == 2);

    // Only on posts.
    CHECK(test.reactions.add_emotes(at(9s), sent) == 0);
}

TEST_CASE("an emote is dated by the message it was sent in", "[linkstats]") {
    fixture test;
    const dpp::snowflake post = test.post(0s);
    test.reactions.add_emotes(
        post, std::vector<latibot::events::emote_reaction>{{.user_id = bob, .emoji = reaction_emoji({}, "💀"), .source_id = at(48h)}});

    CHECK(test.reactions.total(guild, {.kind = stat_kind::received, .since = day_one + 24h}) == 1);
    CHECK(test.reactions.total(guild, {.kind = stat_kind::received, .until = day_one + 24h}) == 0);
}

TEST_CASE("a recompute's emotes replace what was there, but not what it did not read", "[linkstats]") {
    fixture test;
    const dpp::snowflake post = test.post(0s);
    const auto emote = [](dpp::snowflake who, const char* emoji, std::chrono::seconds sent) {
        return latibot::events::emote_reaction{.user_id = who, .emoji = reaction_emoji({}, emoji), .source_id = at(sent)};
    };
    test.reactions.add_emotes(post, std::vector{emote(bob, "💀", 10s), emote(carol, "😂", 30s), emote(dpp::snowflake{14}, "👀", 15s)});

    // A run that read up to 20s found Bob's skull, a little earlier than
    // counted, and not the 👀: that goes, and Carol's, sent after where it
    // started, stays.
    CHECK(test.reactions.replace_emotes(post, std::vector{emote(bob, "💀", 5s)}, at(20s)) == 1);
    CHECK(test.received() == 2);
    CHECK(test.reactions.remove_emotes_from(at(5s)) == 1);
    CHECK(test.reactions.remove_emotes_from(at(15s)) == 0);
    CHECK(test.received() == 1);

    // One that read everything has the last word.
    test.reactions.replace_emotes(post, {}, std::nullopt);
    CHECK(test.received() == 0);
}

// --------------------------------------------------------------------------
// As they arrive
// --------------------------------------------------------------------------

TEST_CASE("emotes after a post are counted as they arrive", "[linkstats]") {
    fixture test;
    test.tracker.on_message(channel, said(at(0s), carol, "look at this"));
    const dpp::snowflake post = test.post(1s);
    test.tracker.on_message(channel, said(post, alice, "", {}));

    CHECK(test.tracker.on_message(channel, said(at(2s), bob, "💀🔥")) == 2);
    CHECK(test.tracker.on_message(channel, said(at(3s), bob, "😂")) == 0);
    CHECK(test.tracker.on_message(channel, said(at(4s), carol, "nice")) == 0);
    CHECK(test.tracker.on_message(channel, said(at(5s), carol, "😭")) == 0);
    // Her own: a self-reaction, counted as one.
    CHECK(test.tracker.on_message(channel, said(at(6s), alice, "😎")) == 1);

    CHECK(test.received() == 2);
    CHECK(test.reactions.total(guild, {.kind = stat_kind::self, .user_id = alice}) == 1);

    // Another channel's messages are its own.
    CHECK(test.tracker.on_message(dpp::snowflake{2001}, said(at(7s), carol, "💀")) == 0);
}

TEST_CASE("the next post, or 25 messages, ends a post's window", "[linkstats]") {
    fixture test;
    const dpp::snowflake first = test.post(0s);
    test.tracker.on_message(channel, said(first, alice, ""));

    SECTION("the next post") {
        const dpp::snowflake second = test.post(1s);
        test.tracker.on_message(channel, said(second, alice, ""));
        CHECK(test.tracker.on_message(channel, said(at(2s), bob, "💀")) == 1);

        // Counted on the second, so adding it there again changes nothing,
        // and the first never had it.
        const std::vector<latibot::events::emote_reaction> skull{{.user_id = bob, .emoji = reaction_emoji({}, "💀"), .source_id = at(2s)}};
        CHECK(test.reactions.add_emotes(second, skull) == 0);
        CHECK(test.reactions.add_emotes(first, skull) == 1);
    }

    SECTION("25 messages") {
        for (std::int64_t index = 0; std::cmp_less(index, latibot::events::emote_window); ++index) {
            test.tracker.on_message(channel, from_bot(at(std::chrono::seconds{1 + index}), "beep"));
        }
        CHECK(test.tracker.on_message(channel, said(at(100s), bob, "💀")) == 0);
    }
}

TEST_CASE("a reply to a post is counted however late it comes", "[linkstats]") {
    fixture test;
    const dpp::snowflake post = test.post(0s);
    test.tracker.on_message(channel, said(post, alice, ""));
    for (int index = 0; index < 40; ++index) {
        test.tracker.on_message(channel, said(at(std::chrono::seconds{1 + index}), carol, "chatting"));
    }

    CHECK(test.tracker.on_message(channel, said(at(100s), bob, "💀", post)) == 1);
    // A reply to anything else is about that.
    CHECK(test.tracker.on_message(channel, said(at(101s), dpp::snowflake{14}, "💀", at(5s))) == 0);
    CHECK(test.received() == 1);
}

TEST_CASE("a link shown to be an image later counts the emotes sent before that", "[linkstats]") {
    fixture test;
    // Alice's link, which Discord has not shown to be an image yet.
    test.tracker.on_message(channel, said(at(0s), alice, ""));
    CHECK(test.tracker.on_message(channel, said(at(1s), bob, "💀")) == 0);

    test.posts.record_image_post(at(0s), guild, channel, alice, day_one);
    CHECK(test.tracker.on_post(channel, at(0s)) == 1);
    CHECK(test.received() == 1);
}

TEST_CASE("a deleted message is no longer counted as a reaction", "[linkstats]") {
    fixture test;
    const dpp::snowflake post = test.post(0s);
    test.tracker.on_message(channel, said(post, alice, ""));
    test.tracker.on_message(channel, said(at(1s), bob, "💀🔥"));
    REQUIRE(test.received() == 2);

    test.tracker.on_delete(channel, at(1s));
    CHECK(test.received() == 0);
}
