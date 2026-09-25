// Deciding which links in a message get replaced, and with what (plan v4 §9.1).

#include "core/events/url_rules.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using latibot::events::mirror;
using latibot::events::mirror_url;
using latibot::events::normalise_domain;
using latibot::events::parse_legacy_rules;
using latibot::events::parse_mirror;
using latibot::events::plan_replacements;
using latibot::events::url_rule;

namespace {

std::vector<url_rule> sample_rules() {
    return {
        {.domain = "x.com",
         .mirrors = {{.host = "fxtwitter.com", .translate_suffix = "/en"}, {.host = "vxtwitter.com", .translate_suffix = ""}}},
        {.domain = "tiktok.com", .mirrors = {{.host = "tfxktok.com", .translate_suffix = ""}}},
    };
}

} // namespace

TEST_CASE("a link to a site without a rule does not stop the others", "[events]") {
    // The Java bot returned null from the whole message the moment it met a
    // domain it had no rule for.
    const auto planned = plan_replacements("https://example.com/a then https://x.com/b/status/1", sample_rules());

    REQUIRE(planned.size() == 1);
    CHECK(planned[0].original_url == "https://x.com/b/status/1");
    CHECK(planned[0].domain == "x.com");
}

TEST_CASE("each link picks its mirror on its own", "[events]") {
    // The Java loop reassigned one shared index as it went, so the second
    // link's mirror depended on how many the first one had.
    const auto planned = plan_replacements("https://tiktok.com/@a/video/1 https://x.com/b/status/2", sample_rules());
    REQUIRE(planned.size() == 2);

    CHECK(mirror_url(planned[0], 0) == "https://tfxktok.com/@a/video/1");
    CHECK(mirror_url(planned[1], 0) == "https://fxtwitter.com/b/status/2/en");
    CHECK(mirror_url(planned[1], 1) == "https://vxtwitter.com/b/status/2");
}

TEST_CASE("a mirror index past the end uses the last mirror", "[events]") {
    const auto planned = plan_replacements("https://tiktok.com/@a/video/1", sample_rules());
    REQUIRE(planned.size() == 1);
    CHECK(mirror_url(planned[0], 7) == "https://tfxktok.com/@a/video/1");
}

TEST_CASE("www and letter case do not hide a link from its rule", "[events]") {
    const auto planned = plan_replacements("https://WWW.X.com/b/status/1", sample_rules());
    REQUIRE(planned.size() == 1);
    CHECK(mirror_url(planned[0], 1) == "https://vxtwitter.com/b/status/1");
}

TEST_CASE("the spoiler survives into the plan", "[events]") {
    const auto planned = plan_replacements("||https://x.com/b/status/1||", sample_rules());
    REQUIRE(planned.size() == 1);
    CHECK(planned[0].spoilered);
}

TEST_CASE("links Discord would not have embedded are left alone", "[events]") {
    CHECK(plan_replacements("<https://x.com/b/status/1>", sample_rules()).empty());
    CHECK(plan_replacements("`https://x.com/b/status/1`", sample_rules()).empty());
}

TEST_CASE("the same link twice is replaced once", "[events]") {
    const auto planned = plan_replacements("https://x.com/b/status/1 https://x.com/b/status/1", sample_rules());
    CHECK(planned.size() == 1);
}

TEST_CASE("a message of links is capped", "[events]") {
    std::string content;
    for (int index = 0; index < 20; ++index) {
        content += "https://x.com/a/status/" + std::to_string(index) + " ";
    }
    CHECK(plan_replacements(content, sample_rules()).size() == latibot::events::max_links_per_message);
}

TEST_CASE("every link gets a verdict, and the plan is the replaced ones", "[events]") {
    using latibot::events::link_decision;

    std::string content = "https://x.com/a/status/1 https://x.com/a/status/1 https://example.com/b <https://x.com/c> `https://x.com/d` ";
    for (int index = 2; index < 8; ++index) {
        content += "https://x.com/a/status/" + std::to_string(index) + " ";
    }

    const auto verdicts = latibot::events::explain_links(content, sample_rules());
    REQUIRE(verdicts.size() == 11);
    CHECK(verdicts[0].decision == link_decision::replaced);
    CHECK(verdicts[1].decision == link_decision::duplicate);
    CHECK(verdicts[2].decision == link_decision::no_rule);
    CHECK(verdicts[2].link.domain == "example.com");
    CHECK(verdicts[3].decision == link_decision::preview_off);
    CHECK(verdicts[4].decision == link_decision::in_code);
    // The first link plus four more make five; the rest are over the limit.
    CHECK(verdicts[9].decision == link_decision::over_limit);
    CHECK(verdicts[10].decision == link_decision::over_limit);

    CHECK(plan_replacements(content, sample_rules()).size() == latibot::events::max_links_per_message);
}

TEST_CASE("no rules means no plan", "[events]") {
    CHECK(plan_replacements("https://x.com/b/status/1", {}).empty());
}

TEST_CASE("a mirror is its host plus an optional suffix", "[events]") {
    CHECK(parse_mirror("fxtwitter.com") == mirror{.host = "fxtwitter.com", .translate_suffix = ""});
    CHECK(parse_mirror("fxtwitter.com/en") == mirror{.host = "fxtwitter.com", .translate_suffix = "/en"});
    CHECK(parse_mirror("  https://www.FXtwitter.com/en/ ") == mirror{.host = "fxtwitter.com", .translate_suffix = "/en"});
    CHECK_FALSE(parse_mirror("").has_value());
    CHECK_FALSE(parse_mirror("https:///en").has_value());
}

TEST_CASE("a typed domain is reduced to its rule host", "[events]") {
    CHECK(normalise_domain("x.com") == "x.com");
    CHECK(normalise_domain("https://www.X.com/some/path") == "x.com");
    CHECK_FALSE(normalise_domain("   ").has_value());
}

TEST_CASE("the Java rule file is read line by line", "[events]") {
    const auto parsed = parse_legacy_rules(
        "tiktok.com|tfxktok.com^vxtiktok.com\n"
        "x.com|fxtwitter.com\r\n"
        "\n"
        "not a rule\n"
        "|nothing.com\n"
        "empty.com|\n"
        "x.com|vxtwitter.com^fxtwitter.com\n");

    REQUIRE(parsed.rules.size() == 2);
    CHECK(parsed.rules[0].domain == "tiktok.com");
    CHECK(parsed.rules[0].mirrors.size() == 2);

    // The last line for a domain wins, as it did in the Java map.
    CHECK(parsed.rules[1].domain == "x.com");
    REQUIRE(parsed.rules[1].mirrors.size() == 2);
    CHECK(parsed.rules[1].mirrors[0].host == "vxtwitter.com");

    // Bad lines are named, with their numbers.
    REQUIRE(parsed.problems.size() == 3);
    CHECK(parsed.problems[0].starts_with("line 4:"));
    CHECK(parsed.problems[1].starts_with("line 5:"));
    CHECK(parsed.problems[2].starts_with("line 6:"));
}
