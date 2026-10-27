// Channels the bot leaves out, entirely or in part
// (src/core/docs/Message_Pipeline.md §2.4).

#include "commands/channels.hpp"
#include "core/db/database.hpp"
#include "events/ignored_channels.hpp"

#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

using latibot::events::channel_scope;
using latibot::events::ignored_channels;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake other_guild{2000};
constexpr dpp::snowflake general{3000};
constexpr dpp::snowflake logs{3001};
constexpr dpp::snowflake thread{3002};

struct channels_fixture {
    latibot::db::database db{":memory:"};
    ignored_channels ignored{db};

    channels_fixture() { latibot::testing::create_schema(db); }
};

} // namespace

TEST_CASE("every channel is heard until somebody says otherwise", "[db]") {
    const channels_fixture fixture;
    CHECK_FALSE(fixture.ignored.find(guild, general).has_value());
    CHECK(fixture.ignored.for_guild(guild).empty());

    const auto rules = fixture.ignored.rules(guild, general, {});
    CHECK_FALSE(rules.everything);
    CHECK_FALSE(rules.model);
    CHECK_FALSE(rules.bots);
}

TEST_CASE("a channel keeps one rule, replaced by the next, per guild", "[db]") {
    channels_fixture fixture;

    CHECK(fixture.ignored.ignore(guild, general, channel_scope::model));
    CHECK_FALSE(fixture.ignored.ignore(guild, general, channel_scope::model));
    CHECK(fixture.ignored.find(guild, general) == channel_scope::model);
    CHECK_FALSE(fixture.ignored.find(other_guild, general).has_value());

    CHECK(fixture.ignored.ignore(guild, general, channel_scope::everything));
    CHECK(fixture.ignored.find(guild, general) == channel_scope::everything);
    CHECK(fixture.ignored.for_guild(guild).size() == 1);

    CHECK(fixture.ignored.hear(guild, general));
    CHECK_FALSE(fixture.ignored.hear(guild, general));
    CHECK_FALSE(fixture.ignored.find(guild, general).has_value());
}

TEST_CASE("a thread follows its channel's rule, and its own as well", "[db]") {
    channels_fixture fixture;
    fixture.ignored.ignore(guild, logs, channel_scope::bots);
    fixture.ignored.ignore(guild, thread, channel_scope::model);

    const auto in_thread = fixture.ignored.rules(guild, thread, logs);
    CHECK(in_thread.bots);
    CHECK(in_thread.model);
    CHECK_FALSE(in_thread.everything);

    const auto elsewhere = fixture.ignored.rules(guild, general, {});
    CHECK_FALSE(elsewhere.bots);
    CHECK_FALSE(elsewhere.model);

    // Another server's rule for the same id is not this one's, and a DM has
    // no rules at all.
    CHECK_FALSE(fixture.ignored.rules(other_guild, logs, {}).bots);
    CHECK_FALSE(fixture.ignored.rules({}, logs, {}).bots);
}

TEST_CASE("a rule reads back as the command's choice", "[db]") {
    for (const channel_scope scope : {channel_scope::everything, channel_scope::model, channel_scope::bots}) {
        CHECK(latibot::events::channel_scope_from_string(latibot::events::to_string(scope)) == scope);
    }
    CHECK(latibot::events::to_string(channel_scope::model) == "llm");
    CHECK_FALSE(latibot::events::channel_scope_from_string("nope").has_value());
}

TEST_CASE("the channels command says what changed, and lists the log channel as always ignoring bots", "[commands]") {
    using latibot::commands::describe_ignored;
    using latibot::commands::render_ignored_channels;

    CHECK(describe_ignored(general, channel_scope::everything, true) == "ok, i'll ignore everything in <#3000>");
    CHECK(describe_ignored(general, channel_scope::model, false) == "the language model was already leaving <#3000> alone");
    CHECK(describe_ignored(logs, channel_scope::bots, true).contains("every bot in <#3001>"));

    CHECK(render_ignored_channels({}, {}).contains("I hear every channel here"));

    const std::vector<std::pair<dpp::snowflake, channel_scope>> listed{{general, channel_scope::model}};
    const std::string with_log = render_ignored_channels(listed, logs);
    CHECK(with_log.contains("<#3000>: the language model"));
    CHECK(with_log.contains("<#3001>: bots, LatiBot and its personas (my log channel, always)"));

    // Listed by hand as well, it is shown once.
    const std::vector<std::pair<dpp::snowflake, channel_scope>> both{{logs, channel_scope::everything}};
    const std::string once = render_ignored_channels(both, logs);
    CHECK(once.contains("<#3001>: everything (my log channel)"));
    CHECK_FALSE(once.contains("always"));
}
