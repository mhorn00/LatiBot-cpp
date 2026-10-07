// Signing yt-dlp in with a cookies file (src/modules/music/docs/Music.md §4.9): what
// is counted in one, the copy each run gets, that the owner's file is never
// written to, and that yt-dlp goes signed out until it is told to sign in.
// Every cookie here is made up.

#include "cookies.hpp"
#include "core/db/database.hpp"
#include "support/capture_log.hpp"
#include "support/temp_directory.hpp"
#include "yt_dlp.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using latibot::music::check_cookie_file;
using latibot::music::check_firefox_profile;
using latibot::music::cookie_copy;
using latibot::music::cookie_source;
using latibot::music::fetch_arguments;
using latibot::music::load_cookies;
using latibot::music::lookup_arguments;
using latibot::music::needs_sign_in;
using latibot::music::process_stream;
using latibot::music::sign_in_retry;
using latibot::music::ytdlp_extras;
using latibot::music::ytdlp_resolver;
using latibot::ports::pcm_stream;
using latibot::ports::stream_state;
using latibot::testing::capture_log;
using latibot::testing::temp_directory;
using latibot::util::program;
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

/// YouTube's refusal of an age-restricted video, as yt-dlp tells it.
constexpr const char* age_check =
    "ERROR: [youtube] abc: Sign in to confirm your age. This video may be inappropriate for some users. "
    "Use --cookies-from-browser or --cookies for the authentication.";

auto child_stream(std::vector<std::string> arguments) -> std::unique_ptr<pcm_stream> {
    return std::make_unique<process_stream>(std::vector<program>{{.path = LATIBOT_TEST_CHILD, .arguments = std::move(arguments)}}, 10s);
}

/// Reads a stream until it is no longer running, or `limit` passes.
auto drain(pcm_stream& stream, std::chrono::milliseconds limit = 10s) -> std::vector<std::int16_t> {
    std::vector<std::int16_t> all;
    std::vector<std::int16_t> chunk(4096);
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        const std::size_t got = stream.read(chunk);
        all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
        if (got == 0) {
            if (stream.state() != stream_state::running) break;
            std::this_thread::sleep_for(5ms);
        }
    }
    return all;
}

/// A stream that gives a little audio, then fails asking to sign in.
class fails_after_audio final : public pcm_stream {
public:
    auto read(std::span<std::int16_t> into) -> std::size_t override {
        if (given_ || into.empty()) return 0;
        given_ = true;
        into[0] = 1;
        return 1;
    }
    [[nodiscard]] auto state() const -> stream_state override { return given_ ? stream_state::failed : stream_state::running; }
    [[nodiscard]] auto error() const -> std::string override { return age_check; }

private:
    bool given_ = false;
};

/// A Firefox profile whose cookies.sqlite holds these cookies, by where they
/// are for and their name; their values are made up.
auto make_profile(const std::filesystem::path& profile, const std::vector<std::pair<std::string, std::string>>& cookies) -> void {
    std::filesystem::create_directories(profile);
    latibot::db::database database(profile / "cookies.sqlite");
    database.execute("CREATE TABLE moz_cookies (id INTEGER PRIMARY KEY, host TEXT, name TEXT, value TEXT)");
    for (const auto& [host, name] : cookies) {
        auto insert = database.prepare("INSERT INTO moz_cookies (host, name, value) VALUES (?, ?, 'not-a-real-value')", host, name);
        (void)insert.step();
    }
}

auto retried(const std::vector<std::pair<latibot::util::log_level, std::string>>& lines) -> bool {
    return std::ranges::any_of(lines, [](const auto& line) { return line.second.contains("tries again signed in"); });
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

TEST_CASE("yt-dlp is told where Deno and the PO token provider are, before the --, and the link stays last", "[music]") {
    const std::filesystem::path deno = R"(C:\bot\deno.exe)";
    const ytdlp_extras out{.deno = deno, .pot_provider = "http://127.0.0.1:4416"};
    ytdlp_extras in = out;
    in.sign_in = {"--cookies", R"(C:\bot\data\yt-dlp-runs\cookies-1.txt)"};
    for (const auto& arguments : {lookup_arguments("--exec calc", 100, out), fetch_arguments("--exec calc", "ff.exe", out),
                                  lookup_arguments("--exec calc", 100, in), fetch_arguments("--exec calc", "ff.exe", in)}) {
        CHECK(arguments.front() == "--ignore-config");
        const auto runtime = std::ranges::find(arguments, "--js-runtimes");
        REQUIRE(runtime != arguments.end());
        CHECK(*(runtime + 1) == "deno:" + deno.string());
        const auto provider = std::ranges::find(arguments, "--extractor-args");
        REQUIRE(provider != arguments.end());
        CHECK(*(provider + 1) == "youtubepot-bgutilhttp:base_url=http://127.0.0.1:4416");
        CHECK(arguments.back() == "--exec calc");
        CHECK(arguments[arguments.size() - 2] == "--");
    }
    for (const auto& arguments : {lookup_arguments("https://x.com/a", 100), fetch_arguments("https://x.com/a", std::nullopt)}) {
        CHECK(std::ranges::find(arguments, "--js-runtimes") == arguments.end());
        CHECK(std::ranges::find(arguments, "--extractor-args") == arguments.end());
    }
}

TEST_CASE("the resolver hands Deno on, signed in or not", "[music][threads]") {
    const temp_directory folder;
    write(folder.file("cookies.txt"), exported);
    const ytdlp_resolver resolver(LATIBOT_TEST_CHILD, 5s, 1, cookie_source(folder.file("cookies.txt"), folder.file("runs")),
                                  ytdlp_extras{.deno = std::filesystem::path(R"(C:\bot\deno.exe)")});
    // The stand-in finds --cookies wherever it is, so the retry still signs in.
    const auto lookup = resolver.lookup_now("https://203.0.113.5/adult", 100);
    REQUIRE(lookup.has_value());
    CHECK(lookup.value().items[0].title == "signed in");
}

TEST_CASE("a file yt-dlp can sign in with has a youtube.com SAPISID", "[music]") {
    CHECK_FALSE(check_cookie_file(exported).youtube_sign_in);
    CHECK(check_cookie_file(".youtube.com\tTRUE\t/\tTRUE\t0\tSAPISID\tnot-a-real-value\n").youtube_sign_in);
    CHECK(check_cookie_file("#HttpOnly_.youtube.com\tTRUE\t/\tTRUE\t0\t__Secure-3PAPISID\tnot-a-real-value\n").youtube_sign_in);
    // Google's own, or one named alike elsewhere, is not YouTube's.
    CHECK_FALSE(check_cookie_file(".google.com\tTRUE\t/\tTRUE\t0\tSAPISID\tnot-a-real-value\n").youtube_sign_in);
    CHECK_FALSE(check_cookie_file(".youtube.com\tTRUE\t/\tTRUE\t0\tSAPISIDX\tnot-a-real-value\n").youtube_sign_in);
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
        const auto status = load_cookies(std::nullopt, std::nullopt, copies);
        CHECK_FALSE(status.source.has_value());
        CHECK(status.file.empty());
        CHECK(status.problem.empty());
        CHECK_FALSE(load_cookies(std::filesystem::path{}, std::filesystem::path{}, copies).source.has_value());
    }
    SECTION("a good file is used, and its path made absolute") {
        write(folder.file("cookies.txt"), exported);
        const auto status = load_cookies(folder.file("cookies.txt"), std::nullopt, copies);
        REQUIRE(status.source.has_value());
        CHECK(status.file.is_absolute());
        CHECK(status.found.youtube == 2);
        CHECK(status.problem.empty());
    }
    SECTION("a file that is not there") {
        const auto status = load_cookies(folder.file("missing.txt"), std::nullopt, copies);
        CHECK_FALSE(status.source.has_value());
        CHECK(status.problem == "could not be read");
    }
    SECTION("a JSON export") {
        write(folder.file("cookies.json"), R"([{"domain": ".youtube.com"}])");
        const auto status = load_cookies(folder.file("cookies.json"), std::nullopt, copies);
        CHECK_FALSE(status.source.has_value());
        CHECK(status.problem.contains("Netscape"));
    }
    SECTION("a file with no cookies") {
        write(folder.file("empty.txt"), "# Netscape HTTP Cookie File\n\n");
        const auto status = load_cookies(folder.file("empty.txt"), std::nullopt, copies);
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

    (void)load_cookies(std::nullopt, std::nullopt, copies);
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
    const ytdlp_extras signed_in{.sign_in = {"--cookies", cookies.string()}};
    for (const auto& arguments : {lookup_arguments("--exec calc", 100, signed_in), fetch_arguments("--exec calc", "ff.exe", signed_in)}) {
        CHECK(arguments.front() == "--ignore-config");
        const auto flag = std::ranges::find(arguments, "--cookies");
        REQUIRE(flag != arguments.end());
        CHECK(*(flag + 1) == cookies.string());
        CHECK(arguments.back() == "--exec calc");
        CHECK(arguments[arguments.size() - 2] == "--");
        // Signed in, its warnings say whether the cookies still work.
        CHECK(std::ranges::find(arguments, "--no-warnings") == arguments.end());
    }
    for (const auto& arguments : {lookup_arguments("https://x.com/a", 100), fetch_arguments("https://x.com/a", std::nullopt)}) {
        CHECK(std::ranges::find(arguments, "--cookies") == arguments.end());
        CHECK(std::ranges::find(arguments, "--no-warnings") != arguments.end());
    }
}

TEST_CASE("what yt-dlp says when signing in would help", "[music]") {
    CHECK(needs_sign_in(age_check));
    CHECK(
        needs_sign_in("ERROR: [youtube] abc: Sign in to confirm you\u2019re not a bot. Use --cookies-from-browser or --cookies for the "
                      "authentication."));
    CHECK(needs_sign_in("ERROR: [youtube] abc: Private video. Sign in if you've been granted access to this video"));
    CHECK(needs_sign_in("ERROR: [vimeo] 1: This video is only available for registered users. Use --cookies for the authentication."));
    CHECK_FALSE(needs_sign_in("ERROR: [generic] Unable to download webpage: HTTP Error 404: Not Found"));
    CHECK_FALSE(needs_sign_in("ERROR: Unsupported URL: https://example.com/a"));
    CHECK_FALSE(needs_sign_in("ERROR: [youtube] abc: Video unavailable"));
    CHECK_FALSE(needs_sign_in(""));
}

TEST_CASE("the links that needed signing in are shared, and kept to a limit", "[music]") {
    const temp_directory folder;
    const cookie_source source(folder.file("cookies.txt"), folder.file("runs"));
    const cookie_source copied = source; // NOLINT(performance-unnecessary-copy-initialization): the copy is the point
    source.remember("https://example.com/0");
    CHECK(copied.needed_for("https://example.com/0"));
    CHECK_FALSE(copied.needed_for("https://example.com/1"));

    for (std::size_t index = 1; index <= cookie_source::remembered_links; ++index) {
        source.remember("https://example.com/" + std::to_string(index));
    }
    CHECK_FALSE(source.needed_for("https://example.com/0"));
    CHECK(source.needed_for("https://example.com/" + std::to_string(cookie_source::remembered_links)));
}

TEST_CASE("the resolver reads signed out, and signs in only when yt-dlp asks to", "[music][threads]") {
    const temp_directory folder;
    const auto copies = folder.file("runs");
    write(folder.file("cookies.txt"), exported);
    const cookie_source source(folder.file("cookies.txt"), copies);
    const ytdlp_resolver resolver(LATIBOT_TEST_CHILD, 5s, 1, source);
    const capture_log log;

    SECTION("an ordinary link, signed out") {
        const auto lookup = resolver.lookup_now("https://203.0.113.5/song", 100);
        REQUIRE(lookup.has_value());
        CHECK(lookup.value().items[0].title == "A song");
        CHECK_FALSE(source.needed_for("https://203.0.113.5/song"));
    }
    SECTION("an age-restricted one, read again signed in, without a word") {
        const auto lookup = resolver.lookup_now("https://203.0.113.5/adult", 100);
        REQUIRE(lookup.has_value());
        CHECK(lookup.value().items[0].title == "signed in");
        CHECK(retried(log.lines()));
        // So the track is fetched signed in, not refused first.
        CHECK(source.needed_for("https://203.0.113.5/adult"));
        // The stand-in wrote its copy back, as yt-dlp does.
        CHECK(read(folder.file("cookies.txt")) == exported);
        CHECK(files_in(copies) == 0);
    }
    SECTION("a link that needed signing in before, signed in at once") {
        source.remember("https://203.0.113.5/song");
        const auto lookup = resolver.lookup_now("https://203.0.113.5/song", 100);
        REQUIRE(lookup.has_value());
        CHECK(lookup.value().items[0].title == "signed in");
        CHECK_FALSE(retried(log.lines()));
    }
    SECTION("a failure signing in would not help, told without a retry") {
        const auto lookup = resolver.lookup_now("https://203.0.113.5/fail", 100);
        REQUIRE_FALSE(lookup.has_value());
        CHECK(lookup.error().message.contains("HTTP Error 404"));
        CHECK_FALSE(retried(log.lines()));
    }
}

TEST_CASE("refused signed in as well, the second refusal is what is told", "[music][threads]") {
    const temp_directory folder;
    // Cookies, but none for YouTube: the stand-in is not signed in by them.
    write(folder.file("cookies.txt"), ".example.com\tTRUE\t/\tFALSE\t1893456000\tid\tnot-a-real-value\n");
    const ytdlp_resolver resolver(LATIBOT_TEST_CHILD, 5s, 1, cookie_source(folder.file("cookies.txt"), folder.file("runs")));
    const auto lookup = resolver.lookup_now("https://203.0.113.5/adult", 100);
    REQUIRE_FALSE(lookup.has_value());
    CHECK(lookup.error().message.starts_with("[youtube] abc: Sign in to confirm your age"));
}

TEST_CASE("without cookies, an age-restricted link is refused as YouTube refused it", "[music][threads]") {
    const ytdlp_resolver resolver(LATIBOT_TEST_CHILD, 5s, 1);
    const auto lookup = resolver.lookup_now("https://203.0.113.5/adult", 100);
    REQUIRE_FALSE(lookup.has_value());
    CHECK(lookup.error().message.contains("Sign in to confirm your age"));
}

TEST_CASE("a track refused for want of signing in is fetched again, signed in", "[music][threads]") {
    int opened = 0;
    auto signed_in = [&opened] {
        ++opened;
        return child_stream({"samples", "1000"});
    };

    SECTION("asked to sign in: the second try plays, and nothing is told") {
        sign_in_retry stream(child_stream({"fail", "1", age_check}), signed_in);
        CHECK(drain(stream).size() == 1000);
        CHECK(stream.state() == stream_state::finished);
        CHECK(stream.error().empty());
        CHECK(opened == 1);
    }
    SECTION("any other failure is told, with no second try") {
        sign_in_retry stream(child_stream({"fail", "1", "ERROR: [youtube] abc: Video unavailable"}), signed_in);
        (void)drain(stream);
        CHECK(stream.state() == stream_state::failed);
        CHECK(stream.error() == "[youtube] abc: Video unavailable");
        CHECK(opened == 0);
    }
    SECTION("refused again signed in: that refusal is told, and there is no third try") {
        int tries = 0;
        sign_in_retry stream(child_stream({"fail", "1", age_check}), [&tries] {
            ++tries;
            return child_stream({"fail", "1", age_check});
        });
        (void)drain(stream);
        CHECK(stream.state() == stream_state::failed);
        CHECK(stream.error().contains("Sign in to confirm your age"));
        CHECK(tries == 1);
    }
    SECTION("a track that failed after some of it played is not started over") {
        sign_in_retry stream(std::make_unique<fails_after_audio>(), signed_in);
        (void)drain(stream);
        CHECK(stream.state() == stream_state::failed);
        CHECK(opened == 0);
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

TEST_CASE("a cookies file signs a run in with a copy, and a Firefox profile with itself", "[music]") {
    const temp_directory folder;
    write(folder.file("cookies.txt"), exported);
    const cookie_source file(folder.file("cookies.txt"), folder.file("runs"));
    const auto from_file = file.sign_in();
    REQUIRE(from_file.has_value());
    REQUIRE(from_file->copy.has_value());
    CHECK(from_file->arguments == std::vector<std::string>{"--cookies", from_file->copy->path().string()});

    const cookie_source firefox = cookie_source::firefox(folder.file("profile"));
    const auto from_profile = firefox.sign_in();
    REQUIRE(from_profile.has_value());
    CHECK(from_profile->arguments == std::vector<std::string>{"--cookies-from-browser", "firefox:" + folder.file("profile").string()});
    CHECK_FALSE(from_profile->copy.has_value());
    CHECK_FALSE(firefox.copy().has_value());
    CHECK(firefox.file().empty());
}

TEST_CASE("a Firefox profile's cookies are counted by name, from a copy", "[music]") {
    const temp_directory folder;
    const auto profile = folder.file("profile");
    make_profile(profile, {{".youtube.com", "PREF"}, {".youtube.com", "SAPISID"}, {".google.com", "SID"}});
    const auto check = check_firefox_profile(profile, folder.file("runs"));
    CHECK(check.problem.empty());
    CHECK(check.found.cookies == 3);
    CHECK(check.found.youtube == 2);
    CHECK(check.found.youtube_sign_in);
    CHECK_FALSE(check.unsaved);
    // The copy it read is gone.
    CHECK(files_in(folder.file("runs")) == 0);
}

TEST_CASE("a Firefox profile signed out, or never opened, is told apart", "[music]") {
    const temp_directory folder;

    SECTION("signed out: YouTube's cookies, but not the sign-in") {
        make_profile(folder.file("profile"), {{".youtube.com", "PREF"}, {".youtube.com", "VISITOR_INFO1_LIVE"}});
        const auto check = check_firefox_profile(folder.file("profile"), folder.file("runs"));
        CHECK(check.found.youtube == 2);
        CHECK_FALSE(check.found.youtube_sign_in);
    }
    SECTION("not a folder") {
        CHECK(check_firefox_profile(folder.file("missing"), folder.file("runs")).problem == "is not a folder");
    }
    SECTION("never opened in Firefox") {
        std::filesystem::create_directories(folder.file("empty"));
        CHECK(check_firefox_profile(folder.file("empty"), folder.file("runs")).problem.starts_with("has no cookies.sqlite"));
    }
    SECTION("not a database") {
        std::filesystem::create_directories(folder.file("broken"));
        write(folder.file("broken") / "cookies.sqlite", "not a database");
        CHECK(check_firefox_profile(folder.file("broken"), folder.file("runs")).problem.starts_with("has a cookies.sqlite that could not"));
    }
}

TEST_CASE("cookies Firefox has not yet saved are noticed", "[music]") {
    const temp_directory folder;
    make_profile(folder.file("profile"), {{".youtube.com", "SAPISID"}});
    write(folder.file("profile") / "cookies.sqlite-wal", "pending");
    CHECK(check_firefox_profile(folder.file("profile"), folder.file("runs")).unsaved);
}

TEST_CASE("a Firefox profile is used rather than a cookies file", "[music]") {
    const temp_directory folder;
    const auto copies = folder.file("runs");
    write(folder.file("cookies.txt"), exported);
    make_profile(folder.file("profile"), {{".youtube.com", "SAPISID"}});

    SECTION("both named: the profile") {
        const auto status = load_cookies(folder.file("cookies.txt"), folder.file("profile"), copies);
        REQUIRE(status.source.has_value());
        CHECK(status.source->profile() == status.profile);
        CHECK(status.both_named);
        CHECK(status.named() == status.profile);
        CHECK(status.found.youtube_sign_in);
    }
    SECTION("a profile with no cookies yet is not used") {
        make_profile(folder.file("new"), {});
        const auto status = load_cookies(std::nullopt, folder.file("new"), copies);
        CHECK_FALSE(status.source.has_value());
        CHECK(status.problem.starts_with("has no cookies in it yet"));
    }
}

TEST_CASE("the resolver signs in from a Firefox profile when it must", "[music][threads]") {
    const temp_directory folder;
    make_profile(folder.file("profile"), {{".youtube.com", "SAPISID"}});
    const ytdlp_resolver resolver(LATIBOT_TEST_CHILD, 5s, 1, cookie_source::firefox(folder.file("profile")));
    const auto lookup = resolver.lookup_now("https://203.0.113.5/adult", 100);
    REQUIRE(lookup.has_value());
    CHECK(lookup.value().items[0].title == "signed in");
}
