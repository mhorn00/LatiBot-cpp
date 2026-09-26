// The link scanner under URL replacement (plan v4 §9.1). The first tests are
// the Java bot's bugs, each written against the behaviour it got wrong.

#include "core/util/url_scan.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

using latibot::util::find_links;
using latibot::util::found_link;
using latibot::util::rehost;
using latibot::util::rule_host;
using latibot::util::split_url;

namespace {

auto urls_in(std::string_view text) -> std::vector<std::string_view> {
    std::vector<std::string_view> urls;
    for (const found_link& link : find_links(text)) {
        urls.push_back(link.url);
    }
    return urls;
}

auto rehosted(std::string_view url, std::string_view host, std::string_view suffix = {}) -> std::string {
    const auto parts = split_url(url);
    REQUIRE(parts.has_value());
    return rehost(*parts, host, suffix);
}

} // namespace

// --------------------------------------------------------------------------
// The Java bugs
// --------------------------------------------------------------------------

TEST_CASE("every link in a message is found, not just the first", "[util]") {
    // The Java pattern wrapped the URL in greedy (.*) groups, so the first
    // find() consumed the whole message and matched one link.
    const auto urls = urls_in("look https://x.com/a/status/1 and https://x.com/b/status/2 too");

    REQUIRE(urls.size() == 2);
    CHECK(urls[0] == "https://x.com/a/status/1");
    CHECK(urls[1] == "https://x.com/b/status/2");
}

TEST_CASE("a spoiler is an odd number of || before the link", "[util]") {
    // Java split on "||" as a regex, which is empty|empty|empty and splits
    // between every character, so it was really testing whether the text
    // around the link had an even number of characters.
    SECTION("an open spoiler") {
        const auto links = find_links("||https://x.com/a/status/1||");
        REQUIRE(links.size() == 1);
        CHECK(links[0].spoilered);
    }

    SECTION("a closed spoiler before the link") {
        const auto links = find_links("||secret|| https://x.com/a/status/1");
        REQUIRE(links.size() == 1);
        CHECK_FALSE(links[0].spoilered);
    }

    SECTION("even-length text either side, which the Java bot spoilered") {
        const auto links = find_links("hey https://x.com/a/status/1 ok!");
        REQUIRE(links.size() == 1);
        CHECK_FALSE(links[0].spoilered);
    }

    SECTION("two links, one of them inside a spoiler") {
        const auto links = find_links("https://x.com/a/status/1 and ||https://x.com/b/status/2||");
        REQUIRE(links.size() == 2);
        CHECK_FALSE(links[0].spoilered);
        CHECK(links[1].spoilered);
    }

    SECTION("markers inside code are text, not a spoiler") {
        // Discord shows `a || b` as it is written, so nothing after it is
        // hidden, and a replacement spoilered here would hide what was not.
        const auto links = find_links("use `a || b` then https://x.com/a/status/1");
        REQUIRE(links.size() == 1);
        CHECK_FALSE(links[0].spoilered);
    }

    SECTION("code inside a spoiler leaves it open") {
        const auto links = find_links("||see `this` https://x.com/a/status/1||");
        REQUIRE(links.size() == 1);
        CHECK(links[0].spoilered);
    }
}

// --------------------------------------------------------------------------
// What counts as a link
// --------------------------------------------------------------------------

TEST_CASE("trailing punctuation is not part of a link", "[util]") {
    CHECK(urls_in("see https://x.com/a.") == std::vector<std::string_view>{"https://x.com/a"});
    CHECK(urls_in("https://x.com/a, https://x.com/b!") == std::vector<std::string_view>{"https://x.com/a", "https://x.com/b"});
    CHECK(urls_in("**https://x.com/a**") == std::vector<std::string_view>{"https://x.com/a"});
    CHECK(urls_in("\"https://x.com/a\"") == std::vector<std::string_view>{"https://x.com/a"});
}

TEST_CASE("a closing bracket stays only when the link opened one", "[util]") {
    CHECK(urls_in("(see https://x.com/a)") == std::vector<std::string_view>{"https://x.com/a"});
    CHECK(urls_in("https://en.wikipedia.org/wiki/Heat_(film)") ==
          std::vector<std::string_view>{"https://en.wikipedia.org/wiki/Heat_(film)"});
    CHECK(urls_in("[_](https://x.com/a)") == std::vector<std::string_view>{"https://x.com/a"});
}

TEST_CASE("an underscore at the end of a link is kept", "[util]") {
    // Usernames end in underscores, and trimming one would link someone else.
    CHECK(urls_in("https://x.com/somebody_") == std::vector<std::string_view>{"https://x.com/somebody_"});
}

TEST_CASE("a link in angle brackets is marked as having its preview turned off", "[util]") {
    const auto links = find_links("no preview please <https://x.com/a/status/1>");
    REQUIRE(links.size() == 1);
    CHECK(links[0].embed_suppressed);
    CHECK(links[0].url == "https://x.com/a/status/1");

    SECTION("punctuation inside the brackets is part of the link") {
        // Discord takes everything between them. Trimming the full stop first
        // put the link's end on the '.', not the '>', and the bot replaced a
        // link whose author had turned its preview off.
        const auto dotted = find_links("<https://x.com/a/status/1.>");
        REQUIRE(dotted.size() == 1);
        CHECK(dotted[0].embed_suppressed);
        CHECK(dotted[0].url == "https://x.com/a/status/1.");
    }

    SECTION("a bracket on one side only is not suppression") {
        CHECK_FALSE(find_links("<https://x.com/a/status/1 and more").front().embed_suppressed);
        CHECK_FALSE(find_links("https://x.com/a/status/1>").front().embed_suppressed);
    }
}

TEST_CASE("links in code are marked as code", "[util]") {
    SECTION("inline") {
        const auto links = find_links("run `curl https://x.com/a` then https://x.com/b");
        REQUIRE(links.size() == 2);
        CHECK(links[0].in_code);
        CHECK_FALSE(links[1].in_code);
    }

    SECTION("a block") {
        const auto links = find_links("```\nhttps://x.com/a\n```\nhttps://x.com/b");
        REQUIRE(links.size() == 2);
        CHECK(links[0].in_code);
        CHECK_FALSE(links[1].in_code);
    }

    SECTION("an unclosed backtick is only a backtick") {
        const auto links = find_links("it`s https://x.com/a");
        REQUIRE(links.size() == 1);
        CHECK_FALSE(links[0].in_code);
    }
}

TEST_CASE("a code span runs to the next run of backticks as long as its own", "[util]") {
    // A single, a double holding a single, a triple, and one never closed.
    const auto spans = latibot::util::code_spans("a `b` ``c`d`` ```e``` `f");

    REQUIRE(spans.size() == 3);
    CHECK((spans[0].begin == 2 && spans[0].end == 5));
    CHECK((spans[1].begin == 6 && spans[1].end == 13));
    CHECK((spans[2].begin == 14 && spans[2].end == 21));
}

TEST_CASE("a scheme glued to a word is not a link", "[util]") {
    CHECK(urls_in("foohttps://x.com/a").empty());
    CHECK(urls_in("https://").empty());
}

TEST_CASE("the scheme may be in any case", "[util]") {
    CHECK(urls_in("HTTPS://X.com/a") == std::vector<std::string_view>{"HTTPS://X.com/a"});
}

TEST_CASE("offsets point back into the scanned text", "[util]") {
    const std::string_view text = "a https://x.com/b c";
    const auto links = find_links(text);
    REQUIRE(links.size() == 1);
    CHECK(text.substr(links[0].begin, links[0].end - links[0].begin) == "https://x.com/b");
}

// --------------------------------------------------------------------------
// Taking URLs apart and putting them back together
// --------------------------------------------------------------------------

TEST_CASE("split_url separates every part", "[util]") {
    const auto parts = split_url("https://user@www.x.com:8443/a/b?c=d&e#f");
    REQUIRE(parts.has_value());
    CHECK(parts->scheme == "https");
    CHECK(parts->authority == "user@www.x.com:8443");
    CHECK(parts->path == "/a/b");
    CHECK(parts->query == "?c=d&e");
    CHECK(parts->fragment == "#f");

    CHECK_FALSE(split_url("ftp://x.com/a").has_value());
    CHECK_FALSE(split_url("https:///a").has_value());
    CHECK(split_url("https://x.com#top")->fragment == "#top");
}

TEST_CASE("rule_host reduces a host to what a rule is keyed by", "[util]") {
    CHECK(rule_host("x.com") == "x.com");
    CHECK(rule_host("WWW.X.com") == "x.com");
    CHECK(rule_host("user:pass@www.x.com:443") == "x.com");
    CHECK(rule_host("old.reddit.com") == "old.reddit.com");
    CHECK(rule_host("x.com.") == "x.com");
}

TEST_CASE("rehost keeps the path, query and fragment", "[util]") {
    CHECK(rehosted("https://www.x.com/a/status/1?s=20#top", "fxtwitter.com") == "https://fxtwitter.com/a/status/1?s=20#top");
    CHECK(rehosted("http://x.com/a", "fxtwitter.com") == "https://fxtwitter.com/a");
    CHECK(rehosted("https://x.com", "fxtwitter.com") == "https://fxtwitter.com");
}

TEST_CASE("a translation suffix goes on the path, before the query", "[util]") {
    CHECK(rehosted("https://x.com/a/status/1", "fxtwitter.com", "/en") == "https://fxtwitter.com/a/status/1/en");
    CHECK(rehosted("https://x.com/a/status/1?s=20", "fxtwitter.com", "/en") == "https://fxtwitter.com/a/status/1/en?s=20");
    CHECK(rehosted("https://x.com/a/status/1#frag", "fxtwitter.com", "/en") == "https://fxtwitter.com/a/status/1/en#frag");

    SECTION("a trailing slash does not become a double one") {
        CHECK(rehosted("https://x.com/a/status/1/", "fxtwitter.com", "/en") == "https://fxtwitter.com/a/status/1/en");
    }

    SECTION("a link that already asks for the translation is not asked twice") {
        CHECK(rehosted("https://x.com/a/status/1/en", "fxtwitter.com", "/en") == "https://fxtwitter.com/a/status/1/en");
    }

    SECTION("a suffix written without its slash still gets one") {
        CHECK(rehosted("https://x.com/a/status/1", "fxtwitter.com", "en") == "https://fxtwitter.com/a/status/1/en");
    }
}

// --------------------------------------------------------------------------
// Hostile input
// --------------------------------------------------------------------------

TEST_CASE("100 KB of link-shaped junk is scanned quickly", "[util]") {
    // The scanner sees every message, so a message built to be slow must not
    // be. std::regex would recurse on this; CTRE compiles to a loop.
    std::string hostile;
    while (hostile.size() < 100'000) {
        hostile += "https://x.com/(((( ||`` ` https://a https:// <https://x.com/a> ))))]]]] ";
    }

    const auto started = std::chrono::steady_clock::now();
    const auto links = find_links(hostile);
    const auto took = std::chrono::steady_clock::now() - started;

    CHECK_FALSE(links.empty());
    // Generous, because Debug and AddressSanitizer builds are slow; the point
    // is linear rather than fast.
    CHECK(took < std::chrono::seconds(2));
}

TEST_CASE("one link followed by thousands of brackets is still linear", "[util]") {
    const std::string hostile = "https://x.com/a" + std::string(50'000, ')');

    const auto started = std::chrono::steady_clock::now();
    const auto links = find_links(hostile);
    const auto took = std::chrono::steady_clock::now() - started;

    REQUIRE(links.size() == 1);
    CHECK(links[0].url == "https://x.com/a");
    CHECK(took < std::chrono::seconds(2));
}

TEST_CASE("scanning a typical message", "[util][!benchmark][.]") {
    const std::string message =
        "lmao look at this ||https://x.com/somebody/status/1234567890123456789?s=20|| and "
        "https://www.tiktok.com/@someone/video/7234567890123456789 too";
    BENCHMARK("find_links") {
        return find_links(message);
    };
}
