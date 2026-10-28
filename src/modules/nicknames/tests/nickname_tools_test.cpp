// The language model's nickname_history (src/modules/nicknames/docs/Nicknames.md
// §2.4): the history, with the nicknames hidden from the model.

#include "nickname_tools.hpp"
#include "core/db/database.hpp"
#include "nicknames.hpp"

#include "mocks/mock_model_tools.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <utility>

using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{1500};
constexpr dpp::snowflake member{3000};
constexpr dpp::snowflake moderator{4000};
const auto noon = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::September / 23} + 12h;

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::events::nickname_store store{db};
    latibot::testing::mock_model_tools tools;

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::events::nicknames_schema());
        latibot::events::add_nickname_tools(tools, store);
        tools.aliases = {{"umember", member}, {"umoder", moderator}};
    }

    auto record(std::optional<std::string> nickname, std::chrono::system_clock::time_point at,
                std::optional<dpp::snowflake> by = std::nullopt) -> void {
        store.record({.guild_id = guild, .user_id = member, .nickname = std::move(nickname), .changed_at = at, .changed_by = by});
    }
};

} // namespace

TEST_CASE("nickname_history shows the history with every nickname hidden, and who changed it", "[nicknames]") {
    fixture test;
    test.record("Bobby", noon - 48h, member);
    test.record("Sir Robert", noon - 24h, moderator);
    test.record(std::nullopt, noon);

    const auto result = test.tools.run("nickname_history", {{"user", "umember"}}, test.tools.context(guild, channel, moderator));
    CHECK_FALSE(result.is_error);
    CHECK(result.content.contains("umember has had 3 nickname(s)"));
    CHECK(result.content.contains("<hidden:Bobby> on 2026-09-21 12:00 UTC, by themselves"));
    CHECK(result.content.contains("<hidden:Sir Robert> on 2026-09-22 12:00 UTC, by umoder"));
    CHECK(result.content.contains("(cleared) on 2026-09-23 12:00 UTC, by someone unknown"));
    // Newest first.
    CHECK(result.content.find("(cleared)") < result.content.find("Bobby"));
}

TEST_CASE("nickname_history is the asker's own by default, and refuses an alias nobody has", "[nicknames]") {
    fixture test;
    test.record("Bobby", noon);

    CHECK(test.tools.run("nickname_history", nlohmann::json::object(), test.tools.context(guild, channel, member))
              .content.contains("<hidden:Bobby>"));
    CHECK(test.tools.run("nickname_history", nlohmann::json::object(), test.tools.context(guild, channel, moderator))
              .content.contains("no nickname"));
    CHECK(test.tools.run("nickname_history", {{"user", "unobody"}}, test.tools.context(guild, channel, member)).is_error);
}

TEST_CASE("nickname_history shows as many as asked for, and says how many more", "[nicknames]") {
    fixture test;
    for (int day = 0; day < 5; ++day) {
        test.record(std::format("nick {}", day), noon + std::chrono::days{day});
    }
    const auto result = test.tools.run("nickname_history", {{"limit", 2}}, test.tools.context(guild, channel, member));
    CHECK(result.content.contains("<hidden:nick 4>"));
    CHECK(result.content.contains("<hidden:nick 3>"));
    CHECK_FALSE(result.content.contains("<hidden:nick 2>"));
    CHECK(result.content.contains("and 3 older"));
}
