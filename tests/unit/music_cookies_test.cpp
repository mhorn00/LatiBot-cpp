// Signing yt-dlp in with a cookies file (docs/features/Music.md §4.9): what
// is counted in one, the copy each run gets, and that the owner's file is
// never written to. Every cookie here is made up.

#include "core/music/cookies.hpp"
#include "core/music/yt_dlp.hpp"
#include "support/temp_directory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using latibot::music::check_cookie_file;
using latibot::music::cookie_copy;
using latibot::music::cookie_source;
using latibot::music::fetch_arguments;
using latibot::music::load_cookies;
using latibot::music::lookup_arguments;
using latibot::music::process_stream;
using latibot::music::ytdlp_resolver;
using latibot::ports::stream_state;
using latibot::testing::temp_directory;
using namespace std::chrono_literals;

namespace {

/// Two YouTube cookies, one of them HttpOnly, and one for another site.
constexpr std::string_view exported =
    "# Netscape HTTP Cookie File\n"
    "# This is a generated file! Do not edit.\n"
    "\n"
    ".youtube.com\tTRUE\t/\tTRUE\t1893456000\tPREF\tnot-a-real-value\n"
    "#HttpOnly_.youtube.com\tTRUE\t/\tTRUE\t1893456000\tLOGIN_INFO\tnot-a-real-value\n"
    ".example.com\tTRUE\t/\tFALSE\t1893456000\tid\tnot-a-real-value\n";

auto write(const std::filesystem::path& path, std::string_view text) -> void {
    std::ofstream(path, std::ios::binary) << text;
}

auto read(const std::filesystem::path& path) -> std::string {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

auto files_in(const std::filesystem::path& directory) -> std::size_t {
    if (!std::filesystem::exists(directory)) return 0;
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}));
}

} // namespace

TEST_CASE("a Netscape cookies file is counted, HttpOnly cookies included", "[music]") {
    const auto found = check_cookie_file(exported);
    CHECK(found.cookies == 3);
    CHECK(found.youtube == 2);
    CHECK(found.malformed == 0);
    CHECK_FALSE(found.json);
}

TEST_CASE("a cookies file saved on Windows, with a byte order mark, reads the same", "[music]") {
    std::string windows = "\xEF\xBB\xBF";
    for (const char letter : exported) {
        if (letter == '\n') windows += '\r';
        windows += letter;
    }
    const auto found = check_cookie_file(windows);
    CHECK(found.cookies == 3);
    CHECK(found.youtube == 2);
    CHECK(found.malformed == 0);
}

TEST_CASE("only youtube.com and its subdomains count as YouTube's", "[music]") {
    const auto found = check_cookie_file(
        "www.YouTube.com\tFALSE\t/\tTRUE\t0\ta\tb\n"
        "notyoutube.com\tTRUE\t/\tTRUE\t0\ta\tb\n"
        "youtube.com.example.org\tTRUE\t/\tTRUE\t0\ta\tb\n");
    CHECK(found.cookies == 3);
    CHECK(found.youtube == 1);
}

TEST_CASE("lines that are not cookies are counted apart", "[music]") {
    const auto found = check_cookie_file(
        ".youtube.com\tTRUE\t/\tTRUE\t0\tPREF\n"
        "youtube.com TRUE / TRUE 0 PREF value\n"
        ".youtube.com\tTRUE\t/\tTRUE\t0\tPREF\tvalue\n");
    CHECK(found.cookies == 1);
    CHECK(found.malformed == 2);
}

TEST_CASE("a JSON export is told apart, since yt-dlp refuses it", "[music]") {
    CHECK(check_cookie_file(R"([{"domain": ".youtube.com", "name": "PREF", "value": "x"}])").json);
    CHECK(check_cookie_file("\n  {\"cookies\": []}").json);
    CHECK_FALSE(check_cookie_file(exported).json);
}

TEST_CASE("loading the cookies the owner named", "[music]") {
    const temp_directory folder;
    const auto copies = folder.file("runs");

    SECTION("none named: signed out, and no problem") {
        const auto status = load_cookies(std::nullopt, copies);
        CHECK_FALSE(status.source.has_value());
        CHECK(status.file.empty());
        CHECK(status.problem.empty());
        CHECK_FALSE(load_cookies(std::filesystem::path{}, copies).source.has_value());
    }
    SECTION("a good file is used, and its path made absolute") {
        write(folder.file("cookies.txt"), exported);
        const auto status = load_cookies(folder.file("cookies.txt"), copies);
        REQUIRE(status.source.has_value());
        CHECK(status.file.is_absolute());
        CHECK(status.found.youtube == 2);
        CHECK(status.problem.empty());
    }
    SECTION("a file that is not there") {
        const auto status = load_cookies(folder.file("missing.txt"), copies);
        CHECK_FALSE(status.source.has_value());
        CHECK(status.problem == "could not be read");
    }
    SECTION("a JSON export") {
        write(folder.file("cookies.json"), R"([{"domain": ".youtube.com"}])");
        const auto status = load_cookies(folder.file("cookies.json"), copies);
        CHECK_FALSE(status.source.has_value());
        CHECK(status.problem.find("Netscape") != std::string::npos);
    }
    SECTION("a file with no cookies") {
        write(folder.file("empty.txt"), "# Netscape HTTP Cookie File\n\n");
        const auto status = load_cookies(folder.file("empty.txt"), copies);
        CHECK_FALSE(status.source.has_value());
        CHECK(status.problem == "has no cookies in it");
    }
}

TEST_CASE("copies an earlier run left behind are cleared at start, and nothing else", "[music]") {
    const temp_directory folder;
    const auto copies = folder.file("runs");
    std::filesystem::create_directories(copies);
    write(copies / "cookies-0123456789abcdef.txt", exported);
    write(copies / "notes.txt", "kept");

    (void)load_cookies(std::nullopt, copies);
    CHECK_FALSE(std::filesystem::exists(copies / "cookies-0123456789abcdef.txt"));
    CHECK(std::filesystem::exists(copies / "notes.txt"));
}

TEST_CASE("each run gets a copy of its own, removed when it is done with", "[music]") {
    const temp_directory folder;
    const auto copies = folder.file("runs");
    write(folder.file("cookies.txt"), exported);
    const cookie_source source(folder.file("cookies.txt"), copies);

    std::optional<cookie_copy> first = source.copy();
    std::optional<cookie_copy> second = source.copy();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->path() != second->path());
    CHECK(read(first->path()) == exported);
    CHECK(files_in(copies) == 2);

    // Moved, the copy is removed once, by whoever holds it last.
    const std::filesystem::path path = first->path();
    std::optional<cookie_copy> moved = std::move(first);
    first.reset();
    CHECK(std::filesystem::exists(path));
    moved.reset();
    CHECK_FALSE(std::filesystem::exists(path));

    second.reset();
    CHECK(files_in(copies) == 0);
    CHECK(read(folder.file("cookies.txt")) == exported);
}

TEST_CASE("a cookies file gone since startup leaves the run signed out", "[music]") {
    const temp_directory folder;
    write(folder.file("cookies.txt"), exported);
    const cookie_source source(folder.file("cookies.txt"), folder.file("runs"));
    std::filesystem::remove(folder.file("cookies.txt"));
    CHECK_FALSE(source.copy().has_value());
}

TEST_CASE("yt-dlp is given the cookies before the --, and the link stays last", "[music]") {
    const std::filesystem::path cookies = R"(C:\bot\data\yt-dlp-runs\cookies-1.txt)";
    for (const auto& arguments : {lookup_arguments("--exec calc", 100, cookies), fetch_arguments("--exec calc", "ff.exe", cookies)}) {
        CHECK(arguments.front() == "--ignore-config");
        const auto flag = std::ranges::find(arguments, "--cookies");
        REQUIRE(flag != arguments.end());
        CHECK(*(flag + 1) == cookies.string());
        CHECK(arguments.back() == "--exec calc");
        CHECK(arguments[arguments.size() - 2] == "--");
    }
    for (const auto& arguments : {lookup_arguments("https://x.com/a", 100), fetch_arguments("https://x.com/a", std::nullopt)}) {
        CHECK(std::ranges::find(arguments, "--cookies") == arguments.end());
    }
}

TEST_CASE("the resolver signs in with a copy, and the owner's file is left as exported", "[music][threads]") {
    const temp_directory folder;
    const auto copies = folder.file("runs");
    write(folder.file("cookies.txt"), exported);

    SECTION("signed in") {
        const ytdlp_resolver resolver(LATIBOT_TEST_CHILD, 5s, 1, cookie_source(folder.file("cookies.txt"), copies));
        // The stand-in writes the copy back as yt-dlp would.
        const auto lookup = resolver.lookup_now("https://203.0.113.5/song", 100);
        REQUIRE(lookup.ok());
        CHECK(lookup.value().items[0].title == "signed in");
        CHECK(read(folder.file("cookies.txt")) == exported);
        CHECK(files_in(copies) == 0);
    }
    SECTION("without cookies, signed out as before") {
        const ytdlp_resolver resolver(LATIBOT_TEST_CHILD, 5s, 1);
        const auto lookup = resolver.lookup_now("https://203.0.113.5/song", 100);
        REQUIRE(lookup.ok());
        CHECK(lookup.value().items[0].title == "A song");
    }
}

TEST_CASE("a track's copy of the cookies lasts until its stream is gone", "[music][threads]") {
    const temp_directory folder;
    const auto copies = folder.file("runs");
    write(folder.file("cookies.txt"), exported);
    const cookie_source source(folder.file("cookies.txt"), copies);

    auto stream = std::make_unique<process_stream>(
        std::vector<latibot::util::program>{{.path = LATIBOT_TEST_CHILD, .arguments = {"samples", "1000"}}}, 10s,
        process_stream::default_buffer, source.copy());
    std::vector<std::int16_t> chunk(4096);
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (stream->state() == stream_state::running && std::chrono::steady_clock::now() < deadline) {
        (void)stream->read(chunk);
        std::this_thread::sleep_for(5ms);
    }
    CHECK(stream->state() == stream_state::finished);
    CHECK(files_in(copies) == 1);

    stream.reset();
    CHECK(files_in(copies) == 0);
}
