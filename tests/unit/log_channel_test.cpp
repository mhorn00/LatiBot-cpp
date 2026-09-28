#include "core/events/log_channel.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/util/text.hpp"

#include "mocks/mock_clock.hpp"
#include "mocks/mock_discord.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using latibot::events::log_buffer;
using latibot::events::log_channel;
using latibot::events::log_destination;
using latibot::util::log;
using latibot::util::log_level;
using namespace std::chrono_literals;

namespace {

/// 12:34:56 UTC on some day, so a line's time is known.
constexpr std::chrono::sys_seconds stamp{std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} + 12h + 34min + 56s};

constexpr log_destination destination{.guild_id = dpp::snowflake{1}, .channel_id = dpp::snowflake{2}, .level = log_level::info};

/// Every line the messages hold, in order, without the fences.
auto lines_in(const std::vector<std::string>& messages) -> std::vector<std::string> {
    std::vector<std::string> lines;
    for (const std::string& message : messages) {
        REQUIRE(message.starts_with("```\n"));
        REQUIRE(message.ends_with("```"));
        for (const std::string_view line : latibot::util::lines(std::string_view(message).substr(4, message.size() - 7))) {
            if (!line.empty()) lines.emplace_back(line);
        }
    }
    return lines;
}

} // namespace

// --------------------------------------------------------------------------
// The buffer
// --------------------------------------------------------------------------

TEST_CASE("a waiting line has its time and level, in a code block", "[events]") {
    log_buffer buffer;
    buffer.push(stamp, log_level::info, "connected");

    CHECK(buffer.take(1) == std::vector<std::string>{"```\n12:34:56 [info] connected\n```"});
    CHECK(buffer.waiting() == 0);
}

TEST_CASE("lines are packed into as few messages as fit, in order", "[events]") {
    log_buffer buffer;
    for (int i = 0; i < 100; ++i) {
        buffer.push(stamp, log_level::debug, std::format("line {:03} {}", i, std::string(80, 'x')));
    }

    const std::vector<std::string> messages = buffer.take(100);

    CHECK(messages.size() == 6); // 107 characters a line with its newline, 18 to a message
    for (const std::string& message : messages) {
        CHECK(latibot::util::character_count(message) <= 2000);
    }
    const std::vector<std::string> lines = lines_in(messages);
    REQUIRE(lines.size() == 100);
    CHECK(lines.front().find("line 000") != std::string::npos);
    CHECK(lines.back().find("line 099") != std::string::npos);
}

TEST_CASE("what does not fit this time keeps waiting", "[events]") {
    log_buffer buffer;
    for (int i = 0; i < 100; ++i) {
        buffer.push(stamp, log_level::info, std::string(100, 'x'));
    }

    CHECK(buffer.take(2).size() == 2);
    CHECK(buffer.waiting() > 0);
    CHECK(buffer.waiting() < 100);
}

TEST_CASE("secrets are masked wherever they appear, and short ones left alone", "[events]") {
    log_buffer buffer({"s3cret-token-value", "abc"});
    buffer.push(stamp, log_level::error, "sent s3cret-token-value twice: s3cret-token-value, abc");

    const std::string message = buffer.take(1).at(0);

    CHECK(message.find("s3cret") == std::string::npos);
    CHECK(latibot::util::count_occurrences(message, "*****") == 2);
    CHECK(message.find("abc") != std::string::npos);
}

TEST_CASE("nothing a line holds can close its code block", "[events]") {
    log_buffer buffer;
    buffer.push(stamp, log_level::info, "```\n@everyone ``````` ``x`");

    const std::string message = buffer.take(1).at(0);

    // The opening and closing fences, and no others: any run of backticks,
    // however long, is broken up.
    CHECK(latibot::util::count_occurrences(message, "```") == 2);
    CHECK(message.find("``x`") != std::string::npos);
    CHECK(message.find("@everyone") != std::string::npos);
}

TEST_CASE("a very long line is cut to fit one message", "[events]") {
    log_buffer buffer;
    buffer.push(stamp, log_level::info, std::string(5000, 'x'));

    const std::vector<std::string> messages = buffer.take(5);

    REQUIRE(messages.size() == 1);
    CHECK(latibot::util::character_count(messages[0]) <= 2000);
    CHECK(messages[0].ends_with("…\n```"));
}

TEST_CASE("a flood keeps its start and says how much was dropped", "[events]") {
    log_buffer buffer;
    for (std::size_t i = 0; i < log_buffer::max_lines + 5; ++i) {
        buffer.push(stamp, log_level::info, std::format("line {}", i));
    }
    CHECK(buffer.waiting() == log_buffer::max_lines);

    const std::vector<std::string> messages = buffer.take(1000);

    REQUIRE_FALSE(messages.empty());
    CHECK(messages[0].starts_with("```\n… 5 line(s) dropped"));
    CHECK(messages[0].find("line 0\n") != std::string::npos);
    CHECK(buffer.take(1).empty());
}

// --------------------------------------------------------------------------
// The stored destination
// --------------------------------------------------------------------------

TEST_CASE("the log channel is kept bot-wide and can be cleared", "[events]") {
    latibot::db::database db(":memory:");
    latibot::db::migrate(db);
    latibot::config::guild_settings settings(db);
    latibot::events::log_destination_store store(settings);

    CHECK_FALSE(store.find().has_value());

    store.save({.guild_id = dpp::snowflake{10}, .channel_id = dpp::snowflake{20}, .level = log_level::warn});
    CHECK(store.find() == log_destination{.guild_id = dpp::snowflake{10}, .channel_id = dpp::snowflake{20}, .level = log_level::warn});
    CHECK(settings.find(latibot::config::bot_wide, "log_channel") == "20");

    CHECK(store.clear());
    CHECK_FALSE(store.find().has_value());
    CHECK_FALSE(store.clear());
}

TEST_CASE("a stored level that cannot be read is info, and the channel is kept", "[events]") {
    latibot::db::database db(":memory:");
    latibot::db::migrate(db);
    latibot::config::guild_settings settings(db);
    latibot::events::log_destination_store store(settings);

    store.save(destination);
    settings.set(latibot::config::bot_wide, "log_channel_level", "loud");

    REQUIRE(store.find().has_value());
    CHECK(store.find()->level == log_level::info);
    CHECK(store.find()->channel_id == destination.channel_id);
}

// --------------------------------------------------------------------------
// Posting
// --------------------------------------------------------------------------

TEST_CASE("the log is posted to its channel, silently, from its level up", "[events][coro]") {
    latibot::testing::mock_discord discord;
    latibot::testing::mock_clock clock;
    log_channel channel(discord, clock);
    channel.start(destination);

    log().debug("too quiet");
    log().info("hello {}", 7);
    channel.flush().sync_wait_for(2s);

    REQUIRE(discord.sent.size() == 1);
    const dpp::message& posted = discord.sent[0];
    CHECK(posted.channel_id == destination.channel_id);
    CHECK(posted.content.find("[info] hello 7") != std::string::npos);
    CHECK(posted.content.find("too quiet") == std::string::npos);
    CHECK((posted.flags & dpp::m_suppress_notifications) != 0);
    CHECK((posted.flags & dpp::m_suppress_embeds) != 0);

    // Nothing waiting, nothing posted.
    channel.flush().sync_wait_for(2s);
    CHECK(discord.sent.size() == 1);
}

TEST_CASE("the level can change without moving the channel", "[events][coro]") {
    latibot::testing::mock_discord discord;
    latibot::testing::mock_clock clock;
    log_channel channel(discord, clock);
    channel.start(destination);

    channel.start({.guild_id = destination.guild_id, .channel_id = destination.channel_id, .level = log_level::debug});
    log().debug("now wanted");
    channel.flush().sync_wait_for(2s);

    REQUIRE(discord.sent.size() == 1);
    CHECK(discord.sent[0].content.find("now wanted") != std::string::npos);
}

TEST_CASE("a failed post waits before trying again, longer each time", "[events][coro]") {
    latibot::testing::mock_discord discord;
    latibot::testing::mock_clock clock;
    log_channel channel(discord, clock);
    channel.start(destination);

    discord.send_results.emplace_back(latibot::ports::api_error{.http_status = 403, .message = "Missing Access"});
    log().info("first");
    channel.flush().sync_wait_for(2s);

    REQUIRE(discord.sent.size() == 1);
    CHECK(channel.status().failure == "Missing Access");
    CHECK(channel.status().retry_in == log_channel::first_backoff);

    // Waiting out the backoff, nothing is tried, and lines keep waiting.
    log().info("second");
    channel.flush().sync_wait_for(2s);
    CHECK(discord.sent.size() == 1);
    CHECK(channel.status().waiting > 0);

    clock.advance(log_channel::first_backoff);
    discord.send_results.emplace_back(latibot::ports::api_error{.http_status = 403, .message = "Missing Access"});
    channel.flush().sync_wait_for(2s);
    CHECK(discord.sent.size() == 2);
    CHECK(channel.status().retry_in == log_channel::first_backoff * 2);

    // Recovering posts what waited, and then says it recovered.
    clock.advance(log_channel::first_backoff * 2);
    log().info("third");
    channel.flush().sync_wait_for(2s);
    REQUIRE(discord.sent.size() == 3);
    CHECK_FALSE(channel.status().failure.has_value());
    CHECK(discord.sent[2].content.find("third") != std::string::npos);

    channel.flush().sync_wait_for(2s);
    REQUIRE(discord.sent.size() == 4);
    CHECK(discord.sent[3].content.find("posting the log to its channel again") != std::string::npos);
}

TEST_CASE("the first failure is logged, and the lines it lost are counted", "[events][coro]") {
    latibot::testing::mock_discord discord;
    latibot::testing::mock_clock clock;
    log_channel channel(discord, clock);
    channel.start(destination);

    discord.send_results.emplace_back(latibot::ports::api_error{.http_status = 404, .message = "Unknown Channel"});
    log().info("lost");
    channel.flush().sync_wait_for(2s);

    clock.advance(log_channel::first_backoff);
    channel.flush().sync_wait_for(2s);

    REQUIRE(discord.sent.size() == 2);
    CHECK(discord.sent[1].content.find("could not post the log to channel 2: Unknown Channel; 1 message(s) of it lost") !=
          std::string::npos);
}

TEST_CASE("the backoff stops growing at its longest", "[events][coro]") {
    latibot::testing::mock_discord discord;
    latibot::testing::mock_clock clock;
    log_channel channel(discord, clock);
    channel.start(destination);

    for (int attempt = 0; attempt < 10; ++attempt) {
        discord.send_results.emplace_back(latibot::ports::api_error{.http_status = 500, .message = "down"});
        log().info("attempt {}", attempt);
        channel.flush().sync_wait_for(2s);
        clock.advance(log_channel::longest_backoff);
    }

    CHECK(discord.sent.size() == 10);
    clock.advance(-log_channel::longest_backoff);
    CHECK(channel.status().retry_in == log_channel::longest_backoff);
}

TEST_CASE("stopping throws away what was waiting and stops taking lines", "[events][coro]") {
    latibot::testing::mock_discord discord;
    latibot::testing::mock_clock clock;
    log_channel channel(discord, clock);
    channel.start(destination);

    log().info("never posted");
    channel.stop();
    log().info("nor this");
    channel.flush().sync_wait_for(2s);

    CHECK(discord.sent.empty());
    CHECK(channel.status().waiting == 0);
    CHECK_FALSE(channel.status().destination.has_value());
}

TEST_CASE("a log channel unhooks itself from the logger when it goes", "[events]") {
    latibot::testing::mock_discord discord;
    latibot::testing::mock_clock clock;
    {
        log_channel channel(discord, clock);
        channel.start(destination);
    }

    // A tap left behind would reach into the destroyed channel here.
    log().info("after");
    CHECK_FALSE(log().enabled(log_level::info));
}

TEST_CASE("the first message says what the channel will get", "[events]") {
    CHECK(latibot::events::log_channel_greeting(log_level::warn).find("warn and above") != std::string::npos);
    CHECK(latibot::events::log_channel_greeting(log_level::trace).find("everything") != std::string::npos);
}
