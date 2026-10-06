#include "core/util/log.hpp"

#include "support/capture_log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <thread>
#include <utility>
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
        CHECK(message.contains(" line "));
    }
}

// --------------------------------------------------------------------------
// The tap, which the log channel reads from
// --------------------------------------------------------------------------

namespace {

/// Records what a tap is given, and unhooks it however the test ends.
class recording_tap {
public:
    explicit recording_tap(log_level minimum) {
        log().set_tap([this](log_level level, std::string_view message) { lines.emplace_back(level, std::string(message)); }, minimum);
    }
    ~recording_tap() { log().set_tap({}, log_level::off); }

    recording_tap(const recording_tap&) = delete;
    auto operator=(const recording_tap&) -> recording_tap& = delete;

    std::vector<std::pair<log_level, std::string>> lines;
};

} // namespace

TEST_CASE("a tap gets lines below the logger's own level when it asks for them", "[log]") {
    // The console at warn and the channel at debug: each sees its own share.
    const capture_log console(log_level::warn);
    const recording_tap tap(log_level::debug);

    log().trace("never");
    log().debug("details {}", 1);
    log().info("did something");
    log().warn("careful");

    CHECK(console.count() == 1);
    REQUIRE(tap.lines.size() == 3);
    CHECK(tap.lines[0] == std::pair{log_level::debug, std::string("details 1")});
    CHECK(tap.lines[1] == std::pair{log_level::info, std::string("did something")});
    CHECK(tap.lines[2] == std::pair{log_level::warn, std::string("careful")});
}

TEST_CASE("a tap above the logger's level leaves out what it did not ask for", "[log]") {
    const capture_log console(log_level::debug);
    const recording_tap tap(log_level::error);

    log().info("everyday");
    log().error("broken");

    CHECK(console.count() == 2);
    REQUIRE(tap.lines.size() == 1);
    CHECK(tap.lines[0].second == "broken");
}

TEST_CASE("a removed tap gets nothing more", "[log]") {
    const capture_log console(log_level::off);
    std::vector<std::string> seen;
    log().set_tap([&seen](log_level /*level*/, std::string_view message) { seen.emplace_back(message); }, log_level::info);

    log().info("before");
    log().set_tap({}, log_level::off);
    log().info("after");

    CHECK(seen == std::vector<std::string>{"before"});
}

TEST_CASE("colour is stripped and the text kept", "[log]") {
    using latibot::util::strip_colors;

    CHECK(strip_colors("\x1b[92m7\x1b[0m in \x1b[1;91mguild\x1b[0m") == "7 in guild");
    CHECK(strip_colors("no colour at all") == "no colour at all");
    // A lone escape, or a sequence the text cuts off, costs only itself.
    CHECK(strip_colors("a\x1b") == "a\x1b");
    CHECK(strip_colors("a\x1b[1;9") == "a");
}
