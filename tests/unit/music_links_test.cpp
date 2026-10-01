// What /music play accepts, and the addresses it will never fetch from
// (docs/features/Music.md §6).

#include "core/music/links.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using latibot::music::check_link;
using latibot::music::host_of;
using latibot::music::is_private;
using latibot::music::resolves_to_private;

TEST_CASE("an http or https link is taken as it is", "[music]") {
    CHECK(check_link("https://www.youtube.com/watch?v=abc").url == "https://www.youtube.com/watch?v=abc");
    CHECK(check_link("  http://example.com/song.mp3 ").url == "http://example.com/song.mp3");
    CHECK(check_link("<https://soundcloud.com/a/b>").url == "https://soundcloud.com/a/b");
}

TEST_CASE("text that is not a link is refused, with the reason", "[music]") {
    CHECK_FALSE(check_link("").ok());
    CHECK_FALSE(check_link("never gonna give you up").ok());
    CHECK_FALSE(check_link("youtube.com/watch?v=abc").ok());
    CHECK_FALSE(check_link("ftp://example.com/a.mp3").ok());
    CHECK_FALSE(check_link("file:///C:/Windows/win.ini").ok());
    CHECK(check_link("search for this").refusal.find("only play links") != std::string::npos);
}

TEST_CASE("a link that looks like an option is still a link or nothing", "[music]") {
    // yt-dlp is given `--` before the link as well; this is the first line.
    CHECK_FALSE(check_link("--exec calc").ok());
    CHECK_FALSE(check_link("-o C:\\evil").ok());
}

TEST_CASE("links into the host's own network are refused", "[music]") {
    for (const char* link : {"http://127.0.0.1/", "http://localhost:8080/admin", "http://app.localhost/", "http://10.0.0.5/a",
                             "http://192.168.1.1/", "http://172.20.0.1/", "http://169.254.169.254/latest/meta-data/", "http://[::1]/",
                             "http://[fe80::1]/", "http://user:pass@127.0.0.1:81/"}) {
        INFO(link);
        CHECK(check_link(link).refusal.find("private network") != std::string::npos);
    }
    CHECK(check_link("http://8.8.8.8/").ok());
    CHECK(check_link("http://[2606:4700:4700::1111]/").ok());
}

TEST_CASE("the host is read without credentials, port or brackets", "[music]") {
    CHECK(host_of("https://user:pw@Example.COM:8443/path") == "example.com");
    CHECK(host_of("http://[::1]:80/") == "::1");
    CHECK(host_of("not a link").empty());
}

TEST_CASE("private IPv4 ranges", "[music]") {
    using v4 = std::array<std::uint8_t, 4>;
    CHECK(is_private(v4{10, 1, 2, 3}));
    CHECK(is_private(v4{127, 0, 0, 1}));
    CHECK(is_private(v4{172, 16, 0, 1}));
    CHECK(is_private(v4{172, 31, 255, 255}));
    CHECK_FALSE(is_private(v4{172, 32, 0, 1}));
    CHECK(is_private(v4{192, 168, 0, 1}));
    CHECK(is_private(v4{100, 64, 0, 1}));
    CHECK_FALSE(is_private(v4{100, 128, 0, 1}));
    CHECK(is_private(v4{169, 254, 1, 1}));
    CHECK(is_private(v4{0, 0, 0, 0}));
    CHECK(is_private(v4{224, 0, 0, 1}));
    CHECK(is_private(v4{255, 255, 255, 255}));
    CHECK_FALSE(is_private(v4{8, 8, 8, 8}));
    CHECK_FALSE(is_private(v4{142, 250, 0, 1}));
}

TEST_CASE("private IPv6 ranges, and IPv4 inside IPv6", "[music]") {
    using v6 = std::array<std::uint8_t, 16>;
    CHECK(is_private(v6{}));                                               // ::
    CHECK(is_private(v6{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1})); // ::1
    CHECK(is_private(v6{0xfd, 0x12}));                                     // unique local
    CHECK(is_private(v6{0xfe, 0x80}));                                     // link-local
    CHECK(is_private(v6{0xff, 0x02}));                                     // multicast
    CHECK(is_private(v6{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 127, 0, 0, 1}));
    CHECK_FALSE(is_private(v6{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 8, 8, 8, 8}));
    CHECK_FALSE(is_private(v6{0x26, 0x06, 0x47, 0x00}));
}

TEST_CASE("names of the machine itself resolve as private without asking DNS", "[music]") {
    CHECK(resolves_to_private("localhost"));
    CHECK(resolves_to_private("127.0.0.1"));
    CHECK(resolves_to_private("::1"));
    CHECK_FALSE(resolves_to_private("8.8.8.8"));
    CHECK_FALSE(resolves_to_private("2606:4700:4700::1111"));
}
