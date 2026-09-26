#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/triggers.hpp"
#include "core/ports/clock.hpp"

#include "mocks/mock_clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <format>
#include <latch>
#include <string>
#include <thread>
#include <vector>

using latibot::events::match_mode;
using latibot::events::trigger;
using latibot::events::trigger_responder;
using latibot::events::trigger_store;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake other_guild{2000};
constexpr dpp::snowflake channel{3000};
constexpr dpp::snowflake other_channel{4000};

struct store_fixture {
    latibot::db::database db{":memory:"};
    trigger_store store{db};

    store_fixture() { latibot::db::migrate(db); }
};

auto nice_trigger(dpp::snowflake in = guild) -> trigger {
    return {.guild_id = in,
            .pattern = "420",
            .mode = match_mode::whole_word,
            .cooldown = 30s,
            .enabled = true,
            .responses = {{.text = "nice", .weight = 1}}};
}

auto message_saying(std::string content, dpp::snowflake in_channel = channel) -> latibot::events::incoming_message {
    return {.guild_id = guild, .channel_id = in_channel, .author_id = dpp::snowflake{9}, .content = std::move(content)};
}

/// A message from a bot this guild has already allowed, so the only question
/// left is whether the trigger itself answers bots.
auto message_from_allowed_bot(std::string content) -> latibot::events::incoming_message {
    auto message = message_saying(std::move(content));
    message.from_bot = true;
    message.author_is_allowed_bot = true;
    return message;
}

} // namespace

TEST_CASE("a trigger survives a round trip with its responses", "[db]") {
    store_fixture fixture;

    trigger saved = nice_trigger();
    saved.responses = {{.text = "nice", .weight = 2}, {.text = "very nice", .weight = 1}};
    const std::int64_t id = fixture.store.add(saved);

    const auto loaded = fixture.store.find(id, guild);
    REQUIRE(loaded.has_value());
    CHECK(loaded->pattern == "420");
    CHECK(loaded->mode == match_mode::whole_word);
    CHECK(loaded->cooldown == 30s);
    CHECK(loaded->enabled);
    REQUIRE(loaded->responses.size() == 2);
    CHECK(loaded->responses[0].text == "nice");
    CHECK(loaded->responses[0].weight == 2);
    CHECK(loaded->responses[1].text == "very nice");
}

TEST_CASE("each trigger in a guild gets its own responses, in order", "[db]") {
    // They are read for the whole guild in one query and sorted out after.
    store_fixture fixture;
    trigger first = nice_trigger();
    first.responses = {{.text = "a", .weight = 1}, {.text = "b", .weight = 2}};
    trigger second = nice_trigger();
    second.pattern = "69";
    second.responses = {{.text = "c", .weight = 3}};
    trigger silent = nice_trigger();
    silent.pattern = "1337";
    silent.responses = {};
    fixture.store.add(first);
    fixture.store.add(second);
    fixture.store.add(silent);
    fixture.store.add(nice_trigger(other_guild));

    const auto all = fixture.store.for_guild(guild);
    REQUIRE(all.size() == 3);
    REQUIRE(all[0].responses.size() == 2);
    CHECK(all[0].responses[0].text == "a");
    CHECK(all[0].responses[1].text == "b");
    CHECK(all[0].responses[1].weight == 2);
    REQUIRE(all[1].responses.size() == 1);
    CHECK(all[1].responses[0].text == "c");
    CHECK(all[2].responses.empty());
}

TEST_CASE("guilds cannot see or change each other's triggers", "[db]") {
    store_fixture fixture;
    const std::int64_t mine = fixture.store.add(nice_trigger(guild));
    fixture.store.add(nice_trigger(other_guild));

    CHECK(fixture.store.for_guild(guild).size() == 1);
    CHECK_FALSE(fixture.store.find(mine, other_guild).has_value());
    CHECK_FALSE(fixture.store.remove(mine, other_guild));

    trigger stolen = nice_trigger(other_guild);
    stolen.id = mine;
    CHECK_FALSE(fixture.store.update(stolen));

    // Still there, still ours.
    CHECK(fixture.store.find(mine, guild).has_value());
}

TEST_CASE("updating a trigger replaces its responses rather than adding to them", "[db]") {
    store_fixture fixture;
    trigger saved = nice_trigger();
    saved.responses = {{.text = "one", .weight = 1}, {.text = "two", .weight = 1}};
    saved.id = fixture.store.add(saved);

    saved.responses = {{.text = "only", .weight = 5}};
    REQUIRE(fixture.store.update(saved));

    const auto loaded = fixture.store.find(saved.id, guild);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->responses.size() == 1);
    CHECK(loaded->responses[0].text == "only");
    CHECK(loaded->responses[0].weight == 5);
}

TEST_CASE("removing a trigger takes its responses with it", "[db]") {
    store_fixture fixture;
    const std::int64_t id = fixture.store.add(nice_trigger());

    REQUIRE(fixture.store.remove(id, guild));
    CHECK(fixture.store.for_guild(guild).empty());

    auto orphans = fixture.db.prepare("SELECT COUNT(*) FROM trigger_responses WHERE trigger_id = ?", id);
    REQUIRE(orphans.step());
    CHECK(orphans.get<std::int64_t>(0) == 0);
}

TEST_CASE("the defaults are seeded once per guild", "[db]") {
    store_fixture fixture;

    CHECK(fixture.store.seed_defaults(guild) == 3);
    CHECK(fixture.store.for_guild(guild).size() == 3);

    // Seeding again would give a server that deliberately deleted them back.
    CHECK(fixture.store.seed_defaults(guild) == 0);
    CHECK(fixture.store.for_guild(guild).size() == 3);
}

TEST_CASE("a matching message gets one of the trigger's responses", "[db]") {
    store_fixture fixture;
    fixture.store.add(nice_trigger());

    latibot::testing::mock_clock clock;
    trigger_responder responder(fixture.store, clock, [] { return 0; });

    const auto result = responder(message_saying("it is 420 somewhere"));

    REQUIRE(result.actions.size() == 1);
    const auto* post = std::get_if<latibot::events::send_message>(&result.actions.front());
    REQUIRE(post != nullptr);
    CHECK(post->content == "nice");
    CHECK(post->channel_id == channel);

    SECTION("and the message does not stop here") {
        // A message with both "420" and a link should get the reply and the
        // URL replacement (plan v4 §5.4).
        CHECK_FALSE(result.consumed);
    }
}

TEST_CASE("a trigger is quiet until its cooldown has passed", "[db]") {
    store_fixture fixture;
    fixture.store.add(nice_trigger());

    latibot::testing::mock_clock clock;
    trigger_responder responder(fixture.store, clock, [] { return 0; });

    CHECK(responder(message_saying("420")).actions.size() == 1);
    CHECK(responder(message_saying("420")).actions.empty());

    clock.advance(29s);
    CHECK(responder(message_saying("420")).actions.empty());

    clock.advance(1s);
    CHECK(responder(message_saying("420")).actions.size() == 1);
}

TEST_CASE("cooldowns are per channel", "[db]") {
    // One busy channel should not silence the trigger everywhere else.
    store_fixture fixture;
    fixture.store.add(nice_trigger());

    latibot::testing::mock_clock clock;
    trigger_responder responder(fixture.store, clock, [] { return 0; });

    CHECK(responder(message_saying("420", channel)).actions.size() == 1);
    CHECK(responder(message_saying("420", other_channel)).actions.size() == 1);
    CHECK(responder(message_saying("420", channel)).actions.empty());
}

TEST_CASE("messages arriving at once still get one reply per cooldown", "[db][threads]") {
    // DPP runs message handlers on several threads, and a trigger's busiest
    // moment is several people saying "420" together. The roll comes between
    // finding the trigger ready and claiming its cooldown, so a slow one holds
    // that gap open: a responder that did those in separate steps would reply
    // more than once here.
    store_fixture fixture;
    fixture.store.add(nice_trigger());

    latibot::testing::mock_clock clock;
    trigger_responder responder(fixture.store, clock, [] {
        std::this_thread::sleep_for(5ms);
        return std::uint64_t{0};
    });

    constexpr int thread_count = 8;
    constexpr int per_thread = 25;

    std::atomic<int> replies = 0;
    std::latch start(thread_count);
    std::vector<std::thread> senders;
    senders.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        senders.emplace_back([&] {
            start.arrive_and_wait();
            for (int i = 0; i < per_thread; ++i) {
                replies += static_cast<int>(responder(message_saying("420")).actions.size());
            }
        });
    }
    for (std::thread& sender : senders) {
        sender.join();
    }

    // The clock never moved, so the first reply started a cooldown that
    // every other message fell inside.
    CHECK(replies == 1);
}

TEST_CASE("a disabled trigger says nothing", "[db]") {
    store_fixture fixture;
    trigger off = nice_trigger();
    off.enabled = false;
    fixture.store.add(off);

    latibot::testing::mock_clock clock;
    trigger_responder responder(fixture.store, clock, [] { return 0; });

    CHECK(responder(message_saying("420")).actions.empty());
}

TEST_CASE("two triggers on one message both answer", "[db]") {
    store_fixture fixture;
    fixture.store.add(nice_trigger());

    trigger second = nice_trigger();
    second.pattern = "69";
    second.responses = {{.text = "also nice", .weight = 1}};
    fixture.store.add(second);

    latibot::testing::mock_clock clock;
    trigger_responder responder(fixture.store, clock, [] { return 0; });

    CHECK(responder(message_saying("420 and 69")).actions.size() == 2);
}

TEST_CASE("respond_to_bots survives a round trip and defaults to off", "[db]") {
    store_fixture fixture;

    const std::int64_t quiet = fixture.store.add(nice_trigger());
    REQUIRE(fixture.store.find(quiet, guild).has_value());
    CHECK_FALSE(fixture.store.find(quiet, guild)->respond_to_bots);

    trigger chatty = nice_trigger();
    chatty.respond_to_bots = true;
    const std::int64_t loud = fixture.store.add(chatty);
    CHECK(fixture.store.find(loud, guild)->respond_to_bots);

    // And it is editable, which is what the panel button does.
    auto entry = fixture.store.find(loud, guild);
    entry->respond_to_bots = false;
    REQUIRE(fixture.store.update(*entry));
    CHECK_FALSE(fixture.store.find(loud, guild)->respond_to_bots);
}

TEST_CASE("a trigger only answers an allowed bot when it opts in", "[db]") {
    // The allowlist got the message this far (plan v4 14.4); this is the
    // second, per-trigger decision.
    store_fixture fixture;
    fixture.store.add(nice_trigger());

    latibot::testing::mock_clock clock;
    trigger_responder responder(fixture.store, clock, [] { return 0; });

    CHECK(responder(message_from_allowed_bot("420")).actions.empty());

    trigger chatty = nice_trigger();
    chatty.respond_to_bots = true;
    fixture.store.add(chatty);

    CHECK(responder(message_from_allowed_bot("420")).actions.size() == 1);
}

TEST_CASE("a trigger that answers bots still answers humans", "[db]") {
    store_fixture fixture;

    trigger chatty = nice_trigger();
    chatty.respond_to_bots = true;
    fixture.store.add(chatty);

    latibot::testing::mock_clock clock;
    trigger_responder responder(fixture.store, clock, [] { return 0; });

    CHECK(responder(message_saying("420")).actions.size() == 1);
}

TEST_CASE("a trigger's reply flags survive a round trip and default to silent", "[db]") {
    store_fixture fixture;

    const std::int64_t quiet = fixture.store.add(nice_trigger());
    CHECK(fixture.store.find(quiet, guild)->message_flags == dpp::m_suppress_notifications);

    trigger loud = nice_trigger();
    loud.message_flags = dpp::m_suppress_embeds;
    const std::int64_t id = fixture.store.add(loud);
    CHECK(fixture.store.find(id, guild)->message_flags == dpp::m_suppress_embeds);

    // Editable, which is what the panel's buttons and /trigger edit do.
    auto entry = fixture.store.find(id, guild);
    entry->message_flags = dpp::m_suppress_notifications | dpp::m_suppress_embeds;
    REQUIRE(fixture.store.update(*entry));
    CHECK(fixture.store.find(id, guild)->message_flags == (dpp::m_suppress_notifications | dpp::m_suppress_embeds));

    // Only the flags a channel message can carry are kept.
    entry->message_flags = dpp::m_ephemeral;
    REQUIRE(fixture.store.update(*entry));
    CHECK(fixture.store.find(id, guild)->message_flags == 0);
}

TEST_CASE("triggers from before reply flags existed stay silent", "[db]") {
    latibot::db::database db{":memory:"};

    // Stop at the schema before message_flags, add a trigger the old way,
    // then let the migration add the column.
    const auto all = latibot::db::schema();
    latibot::db::migrate(db, all.first(all.size() - 1));
    db.prepare("INSERT INTO triggers (guild_id, pattern, match_mode, cooldown_s, enabled) VALUES (?, '420', 'whole_word', 30, 1)",
               static_cast<std::uint64_t>(guild))
        .run();
    latibot::db::migrate(db);

    const auto found = trigger_store(db).for_guild(guild);
    REQUIRE(found.size() == 1);
    CHECK(found.front().message_flags == dpp::m_suppress_notifications);
}

TEST_CASE("a trigger's reply carries its flags", "[db]") {
    store_fixture fixture;
    trigger loud = nice_trigger();
    loud.message_flags = dpp::m_suppress_embeds;
    fixture.store.add(loud);

    latibot::testing::mock_clock clock;
    trigger_responder responder(fixture.store, clock, [] { return 0; });

    const auto result = responder(message_saying("420"));
    REQUIRE(result.actions.size() == 1);
    CHECK(std::get<latibot::events::send_message>(result.actions.front()).flags == dpp::m_suppress_embeds);
}

TEST_CASE("a trigger's reply says which trigger it is from", "[db]") {
    // For the line saying whether it was posted.
    store_fixture fixture;
    const std::int64_t id = fixture.store.add(nice_trigger());

    latibot::testing::mock_clock clock;
    trigger_responder responder(fixture.store, clock, [] { return 0; });

    const auto result = responder(message_saying("420"));
    REQUIRE(result.actions.size() == 1);
    CHECK(std::get<latibot::events::send_message>(result.actions.front()).what == std::format("trigger {}'s reply", id));
}
