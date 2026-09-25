#include "core/util/log.hpp"

#include "support/capture_log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>
#include <vector>

using latibot::testing::capture_log;
using latibot::util::log;
using latibot::util::log_level;
using latibot::util::log_level_from_string;
using latibot::util::to_string;

TEST_CASE("level names round trip", "[log]") {
    for (const auto level : {log_level::trace, log_level::debug, log_level::info, log_level::warn, log_level::error, log_level::off}) {
        const auto parsed = log_level_from_string(to_string(level));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == level);
    }
}

TEST_CASE("level names are case-insensitive and unknown names are reported", "[log]") {
    CHECK(log_level_from_string("WARN") == log_level::warn);
    CHECK(log_level_from_string("Info") == log_level::info);
    CHECK_FALSE(log_level_from_string("verbose").has_value());
    CHECK_FALSE(log_level_from_string("").has_value());
}

TEST_CASE("messages below the level are dropped", "[log]") {
    const capture_log captured(log_level::warn);

    log().debug("not this one");
    log().info("nor this");
    log().warn("but this");
    log().error("and this");

    REQUIRE(captured.count() == 2);
    CHECK(captured.contains(log_level::warn, "but this"));
    CHECK(captured.contains(log_level::error, "and this"));
}

TEST_CASE("off silences everything", "[log]") {
    const capture_log captured(log_level::off);

    log().error("still quiet");

    CHECK(captured.count() == 0);
}

TEST_CASE("arguments are formatted into the message", "[log]") {
    const capture_log captured;

    log().info("registered {} commands for guild {}", 7, 1234567890123456789ULL);

    CHECK(captured.contains(log_level::info, "registered 7 commands for guild 1234567890123456789"));
}

TEST_CASE("a message is never split between threads", "[log][threads]") {
    const capture_log captured;

    constexpr int thread_count = 4;
    constexpr int per_thread = 50;

    std::vector<std::thread> writers;
    writers.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        writers.emplace_back([t] {
            for (int i = 0; i < per_thread; ++i) {
                log().info("thread {} line {}", t, i);
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }

    const auto lines = captured.lines();
    REQUIRE(lines.size() == static_cast<std::size_t>(thread_count) * per_thread);
    // Every line has to be one complete message: interleaving would leave
    // fragments that do not match the format.
    for (const auto& [level, message] : lines) {
        CHECK(message.starts_with("thread "));
        CHECK(message.find(" line ") != std::string::npos);
    }
}
