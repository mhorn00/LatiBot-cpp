// Running other programs, as music runs yt-dlp and ffmpeg
// (docs/features/Music.md §4.6), against a stand-in program the tests build
// (tests/support/test_child.cpp).

#include "core/util/process.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using latibot::util::command_line;
using latibot::util::pipeline;
using latibot::util::program;
using latibot::util::quote_argument;
using latibot::util::run;
using namespace std::chrono_literals;

namespace {

auto child(std::vector<std::string> arguments) -> program {
    return {.path = LATIBOT_TEST_CHILD, .arguments = std::move(arguments)};
}

auto lines_of(const std::string& text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t end = text.find('\n'); end != std::string::npos; end = text.find('\n', start)) {
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

} // namespace

TEST_CASE("an argument with nothing special is left as it is", "[util]") {
    CHECK(quote_argument("--ignore-config") == "--ignore-config");
    CHECK(quote_argument("https://x.com/a?b=c&d=e") == "https://x.com/a?b=c&d=e");
    CHECK(quote_argument("C:\\path\\to") == "C:\\path\\to");
}

TEST_CASE("spaces, quotes and backslashes before them are quoted", "[util]") {
    CHECK(quote_argument("") == "\"\"");
    CHECK(quote_argument("a b") == "\"a b\"");
    CHECK(quote_argument("say \"hi\"") == R"("say \"hi\"")");
    CHECK(quote_argument("ends in \\") == R"("ends in \\")");
    CHECK(quote_argument("a\\\"b c") == R"("a\\\"b c")");
}

TEST_CASE("the command line starts with the program, quoted", "[util]") {
    const program to_run{.path = "C:\\Program Files\\yt-dlp.exe", .arguments = {"--", "a b"}};
    CHECK(command_line(to_run) == R"("C:\Program Files\yt-dlp.exe" -- "a b")");
}

TEST_CASE("a program reads back exactly the arguments it was given", "[util][threads]") {
    // What a link typed in Discord can hold: spaces, quotes, backslashes,
    // things that look like options, and text that is not ASCII.
    const std::vector<std::string> arguments{
        "--exec calc", "a b", R"(say "hi")", R"(trailing \)", R"(\\server\share\)", "", "&|<>^%PATH%", "caf\xc3\xa9 \xe2\x99\xab",
    };
    std::vector<std::string> call{"echo-args"};
    call.insert(call.end(), arguments.begin(), arguments.end());

    const auto result = run(child(call), 10s);
    CHECK(result.exit_code == 0);
    CHECK(lines_of(result.output) == arguments);
}

TEST_CASE("run collects stdout, stderr and the exit code", "[util][threads]") {
    SECTION("stdout, in full") {
        const auto result = run(child({"bytes", "100000"}), 10s);
        REQUIRE(result.output.size() == 100000);
        CHECK(static_cast<unsigned char>(result.output[300]) == 300 % 251);
        CHECK_FALSE(result.timed_out);
    }
    SECTION("stderr, a line at a time") {
        const auto result = run(child({"stderr", "ERROR: first", "second"}), 10s);
        CHECK(result.errors == "ERROR: first\nsecond\n");
        CHECK(result.output.empty());
    }
    SECTION("the exit code") {
        CHECK(run(child({"exit", "3"}), 10s).exit_code == 3);
    }
}

TEST_CASE("a program that runs too long is killed", "[util][threads]") {
    const auto started = std::chrono::steady_clock::now();
    const auto result = run(child({"hang"}), 300ms);
    CHECK(result.timed_out);
    CHECK(std::chrono::steady_clock::now() - started < 10s);
}

TEST_CASE("output past the limit kills the program", "[util][threads]") {
    const auto result = run(child({"bytes", "1000000"}), 10s, 1000);
    CHECK(result.output.size() <= 1000);
}

TEST_CASE("a program that cannot be found is refused", "[util]") {
    CHECK_THROWS_AS(run({.path = "C:\\no\\such\\program.exe", .arguments = {}}, 1s), latibot::util::process_error);
}

TEST_CASE("one program's output is the next one's input", "[util][threads]") {
    const std::array programs{child({"samples", "48000"}), child({"cat"}), child({"cat"})};
    pipeline chain(programs);

    std::vector<std::int16_t> samples;
    std::array<std::byte, 4096> buffer{};
    std::string partial;
    while (const std::size_t got = chain.read(buffer)) {
        partial.append(reinterpret_cast<const char*>(buffer.data()), got);
    }
    REQUIRE(partial.size() == 48000 * sizeof(std::int16_t));
    samples.resize(48000);
    std::memcpy(samples.data(), partial.data(), partial.size());
    CHECK(samples[1234] == 234);
    CHECK(chain.wait() == std::vector<int>{0, 0, 0});
}

TEST_CASE("stderr lines say which program in the pipeline wrote them", "[util][threads]") {
    std::mutex mutex;
    std::vector<std::pair<std::size_t, std::string>> heard;
    {
        const std::array programs{child({"stderr", "from the first"}), child({"cat"})};
        pipeline chain(programs, [&](std::size_t index, std::string_view line) {
            const std::scoped_lock lock(mutex);
            heard.emplace_back(index, std::string(line));
        });
        std::array<std::byte, 64> buffer{};
        while (chain.read(buffer) > 0) {
        }
        (void)chain.wait();
    } // the stderr readers are finished once it is gone
    REQUIRE(heard.size() == 1);
    CHECK(heard[0].first == 0);
    CHECK(heard[0].second == "from the first");
}

TEST_CASE("killing a pipeline ends its programs and what they started", "[util][threads]") {
    const std::array programs{child({"spawn-hang"})};
    pipeline chain(programs);

    // Its first line is the id of the program it started.
    std::string first;
    std::array<std::byte, 1> byte{};
    while (chain.read(byte) == 1 && static_cast<char>(byte[0]) != '\n') {
        first += static_cast<char>(byte[0]);
    }
    const auto grandchild = static_cast<DWORD>(std::stoul(first));
    HANDLE watched = OpenProcess(SYNCHRONIZE, FALSE, grandchild);
    REQUIRE(watched != nullptr);

    chain.kill();
    CHECK(WaitForSingleObject(watched, 10000) == WAIT_OBJECT_0);
    CloseHandle(watched);

    std::array<std::byte, 64> rest{};
    CHECK(chain.read(rest) == 0);
}
