#include "core/llm/advanced_triggers.hpp"
#include "core/llm/documents.hpp"
#include "core/llm/guards.hpp"
#include "core/llm/memory.hpp"
#include "core/llm/settings.hpp"

#include "mocks/mock_clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <set>
#include <string>
#include <vector>

using latibot::llm::advanced_trigger;
using latibot::llm::pacing_rules;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{3000};
constexpr dpp::snowflake other_channel{4000};

auto pizza_trigger(std::int64_t id = 1) -> advanced_trigger {
    return {.id = id,
            .guild_id = guild,
            .pattern = "pizza",
            .mode = latibot::util::match_mode::whole_word,
            .context_prompt = "Talk about pizza.",
            .probability = 0.5,
            .cooldown = 60s,
            .enabled = true,
            .created_by = dpp::snowflake{1}};
}

} // namespace

// --------------------------------------------------------------------------
// Rate limits
// --------------------------------------------------------------------------

TEST_CASE("a rate limit allows so many per window, then frees up as they age", "[llm]") {
    latibot::testing::mock_clock clock;
    latibot::llm::rate_limiter limiter(clock, 60s);
    const latibot::llm::rate_limiter::key alice{guild, dpp::snowflake{1}};
    const latibot::llm::rate_limiter::key bob{guild, dpp::snowflake{2}};

    CHECK(limiter.try_take(alice, 2));
    clock.advance(10s);
    CHECK(limiter.try_take(alice, 2));
    CHECK_FALSE(limiter.try_take(alice, 2));
    CHECK(limiter.try_take(bob, 2));

    // The first one ages out at 60 s; the second at 70 s.
    clock.advance(50s);
    CHECK(limiter.try_take(alice, 2));
    CHECK_FALSE(limiter.try_take(alice, 2));
    CHECK_FALSE(limiter.try_take(alice, 0));
}

// --------------------------------------------------------------------------
// Bot-to-bot pacing
// --------------------------------------------------------------------------

TEST_CASE("bot turns in a row stop at the limit until a person speaks", "[llm]") {
    latibot::testing::mock_clock clock;
    latibot::llm::bot_pacing pacing(clock);
    const pacing_rules rules{.turns = 2, .delay = 0s, .daily_cap = 100, .needs_human = false};

    CHECK(pacing.claim(guild, channel, rules).allowed);
    CHECK(pacing.claim(guild, channel, rules).allowed);
    CHECK_FALSE(pacing.claim(guild, channel, rules).allowed);
    // Per channel.
    CHECK(pacing.claim(guild, other_channel, rules).allowed);

    pacing.human_spoke(channel);
    CHECK(pacing.claim(guild, channel, rules).allowed);
}

TEST_CASE("a bot turn soon after the last one waits out the delay", "[llm]") {
    latibot::testing::mock_clock clock;
    latibot::llm::bot_pacing pacing(clock);
    const pacing_rules rules{.turns = 10, .delay = 10s, .daily_cap = 100, .needs_human = false};

    CHECK(pacing.claim(guild, channel, rules).wait == 0s);
    clock.advance(3s);
    CHECK(pacing.claim(guild, channel, rules).wait == 7s);
    // The one after that waits from when the delayed turn will be taken.
    CHECK(pacing.claim(guild, channel, rules).wait == 17s);
}

TEST_CASE("the day's bot turns are capped per guild, and come back the next day", "[llm]") {
    latibot::testing::mock_clock clock;
    latibot::llm::bot_pacing pacing(clock);
    const pacing_rules rules{.turns = 10, .delay = 0s, .daily_cap = 1, .needs_human = false};

    CHECK(pacing.claim(guild, channel, rules).allowed);
    CHECK_FALSE(pacing.claim(guild, other_channel, rules).allowed);
    CHECK(pacing.claim(dpp::snowflake{9}, channel, rules).allowed);

    clock.advance(24h);
    CHECK(pacing.claim(guild, other_channel, rules).allowed);
}

TEST_CASE("pacing can insist on a person first, or refuse bots entirely", "[llm]") {
    latibot::testing::mock_clock clock;
    latibot::llm::bot_pacing pacing(clock);

    const pacing_rules human_first{.turns = 6, .delay = 0s, .daily_cap = 100, .needs_human = true};
    CHECK_FALSE(pacing.claim(guild, channel, human_first).allowed);
    pacing.human_spoke(channel);
    CHECK(pacing.claim(guild, channel, human_first).allowed);

    const pacing_rules never{.turns = 0, .delay = 0s, .daily_cap = 100, .needs_human = false};
    CHECK_FALSE(pacing.claim(guild, other_channel, never).allowed);
}

// --------------------------------------------------------------------------
// Advanced triggers
// --------------------------------------------------------------------------

TEST_CASE("an advanced trigger fires on its roll, then waits out its cooldown in that channel", "[llm]") {
    latibot::testing::mock_clock clock;
    double next_roll = 0.9;
    latibot::llm::advanced_trigger_matcher matcher(clock, [&] { return next_roll; });
    const std::vector<advanced_trigger> triggers{pizza_trigger()};

    // 0.9 is not under 0.5: no reply, and no cooldown either.
    CHECK_FALSE(matcher.fire(triggers, channel, "who wants pizza").has_value());
    next_roll = 0.1;
    CHECK(matcher.fire(triggers, channel, "who wants pizza").has_value());
    CHECK_FALSE(matcher.fire(triggers, channel, "more pizza").has_value());
    CHECK(matcher.fire(triggers, other_channel, "pizza?").has_value());

    clock.advance(60s);
    CHECK(matcher.fire(triggers, channel, "pizza again").has_value());
}

TEST_CASE("an advanced trigger needs its pattern, and to be on", "[llm]") {
    latibot::testing::mock_clock clock;
    latibot::llm::advanced_trigger_matcher matcher(clock, [] { return 0.0; });
    advanced_trigger off = pizza_trigger();
    off.enabled = false;

    CHECK_FALSE(matcher.fire({pizza_trigger()}, channel, "pizzas").has_value());
    CHECK_FALSE(matcher.fire({off}, channel, "pizza").has_value());
}

// --------------------------------------------------------------------------
// Settings, documents and memory search, without a database
// --------------------------------------------------------------------------

TEST_CASE("a setting out of its range is refused with the range", "[llm]") {
    const auto* messages = latibot::llm::find_setting("llm_context_messages");
    REQUIRE(messages != nullptr);

    std::string reason;
    CHECK(latibot::llm::parse_setting(*messages, " 20 ", reason) == 20);
    CHECK_FALSE(latibot::llm::parse_setting(*messages, "51", reason).has_value());
    CHECK(reason == "\"Recent messages it reads\" takes a whole number from 0 to 50");
    CHECK_FALSE(latibot::llm::parse_setting(*messages, "twenty", reason).has_value());
    CHECK_FALSE(latibot::llm::parse_setting(*messages, "", reason).has_value());

    const auto* human = latibot::llm::find_setting("llm_bot_needs_human");
    REQUIRE(human != nullptr);
    CHECK(latibot::llm::parse_setting(*human, "Yes", reason) == 1);
    CHECK(latibot::llm::parse_setting(*human, "off", reason) == 0);
    CHECK_FALSE(latibot::llm::parse_setting(*human, "maybe", reason).has_value());
    CHECK(latibot::llm::describe_setting(*human, 1) == "yes");
}

TEST_CASE("every setting fits a modal: labels short enough, and at most five to a form", "[llm]") {
    std::set<std::string_view> keys;
    for (const std::string_view group : latibot::llm::setting_groups()) {
        int in_group = 0;
        for (const auto& spec : latibot::llm::setting_specs()) {
            if (spec.group != group) continue;
            ++in_group;
            CHECK(spec.label.size() <= 45);
            CHECK(spec.fallback >= spec.min);
            CHECK(spec.fallback <= spec.max);
            CHECK(keys.insert(spec.key).second);
        }
        CHECK(in_group >= 1);
        CHECK(in_group <= 5);
    }
    CHECK(keys.size() == latibot::llm::setting_specs().size());
}

TEST_CASE("a diff shows removed and added lines, and only the unchanged lines near them", "[llm]") {
    CHECK(latibot::llm::diff_lines("same", "same").empty());
    CHECK(latibot::llm::diff_lines("a\nb\nc\nd\ne\nf", "a\nb\nc\nd\nE\nf") == "  …\n  d\n- e\n+ E\n  f\n");
    CHECK(latibot::llm::diff_lines("", "new") == "- \n+ new\n");
}

TEST_CASE("a memory search is made of the message's words, quoted, and never of FTS syntax", "[llm]") {
    CHECK(latibot::llm::search_query("What's Alice's favourite pizza?") == R"("alice" OR "favourite" OR "pizza")");
    CHECK(latibot::llm::search_query("the and you it's, don't").empty());
    CHECK(latibot::llm::search_query("pizza PIZZA pizza") == R"("pizza")");
    CHECK(latibot::llm::search_query(R"(" OR NEAR( x*)") == R"("near")");
}

TEST_CASE("token estimates are a quarter of the characters, rounded up", "[llm]") {
    CHECK(latibot::llm::estimate_tokens("") == 0);
    CHECK(latibot::llm::estimate_tokens("abcde") == 2);
    // Characters, not bytes.
    CHECK(latibot::llm::estimate_tokens("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9") == 1);
}
