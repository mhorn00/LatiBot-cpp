// The language model's fix_link (src/modules/links/docs/Url_Replacement.md §2.8).

#include "link_tools.hpp"
#include "core/db/database.hpp"
#include "links/url_rules.hpp"

#include "links/module.hpp"
#include "mocks/mock_model_tools.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{2000};
constexpr dpp::snowflake member{3000};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::events::url_rule_store rules{db};
    latibot::testing::mock_model_tools tools;

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::links::schema());
        rules.set(guild,
                  {.domain = "x.com",
                   .mirrors = {{.host = "fxtwitter.com", .translate_suffix = ""}, {.host = "vxtwitter.com", .translate_suffix = ""}}});
        latibot::events::add_link_tools(tools, rules);
    }

    auto run(std::string text) -> latibot::capabilities::model_tool_result {
        return tools.run("fix_link", {{"url", std::move(text)}}, tools.context(guild, channel, member));
    }
};

} // namespace

TEST_CASE("fix_link gives a link on its first mirror, with the rest as fallbacks", "[links]") {
    fixture test;
    const auto result = test.run("https://x.com/someone/status/123");
    CHECK_FALSE(result.is_error);
    CHECK(result.content.contains("becomes https://fxtwitter.com/someone/status/123"));
    CHECK(result.content.contains("or else https://vxtwitter.com/someone/status/123"));
}

TEST_CASE("fix_link says why it leaves a link alone, and needs one", "[links]") {
    fixture test;
    const auto result = test.run("look https://example.com/page and <https://x.com/a/status/1>");
    CHECK(result.content.contains("https://example.com/page: left alone"));
    CHECK(result.content.find("becomes") == std::string::npos);
    CHECK(test.run("no links here").is_error);
}
