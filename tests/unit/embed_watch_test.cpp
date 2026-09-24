// Posting a replacement and following it until its previews appear or its
// mirrors run out (plan v4 §9.2-§9.4).

#include "core/events/embed_watch.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/replacements.hpp"
#include "core/events/triggers.hpp"
#include "core/events/url_replacer.hpp"
#include "core/events/url_rules.hpp"
#include "core/ui/paginator.hpp"

#include "mocks/mock_clock.hpp"
#include "mocks/mock_discord.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <variant>
#include <vector>

using namespace std::chrono_literals;
using latibot::events::edit_replacement;
using latibot::events::embed_action;
using latibot::events::embed_tracker;
using latibot::events::planned_link;
using latibot::events::replacement_state;
using latibot::events::set_original_embeds;
using latibot::events::watch_request;
using latibot::events::watched_link;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{2000};
constexpr dpp::snowflake original{3000};
constexpr dpp::snowflake ours{4000};
constexpr dpp::snowflake author{5000};

planned_link x_link(std::string path = "/a/status/1", bool spoilered = false) {
    return {.original_url = "https://x.com" + path,
            .domain = "x.com",
            .spoilered = spoilered,
            .mirrors = {{.host = "fxtwitter.com", .translate_suffix = ""}, {.host = "vxtwitter.com", .translate_suffix = ""}}};
}

planned_link tiktok_link() {
    return {.original_url = "https://tiktok.com/@a/video/9",
            .domain = "tiktok.com",
            .spoilered = false,
            .mirrors = {{.host = "tfxktok.com", .translate_suffix = ""}}};
}

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::events::replacement_store replacements{db};
    latibot::events::url_rule_store rules{db};
    latibot::testing::mock_clock clock{std::chrono::system_clock::time_point{std::chrono::sys_days{std::chrono::year{2026} / 9 / 24}}};
    embed_tracker tracker{replacements, clock};

    fixture() { latibot::db::migrate(db); }

    void record(std::vector<planned_link> links, replacement_state state = replacement_state::pending) {
        replacements.record({.message_id = ours,
                             .guild_id = guild,
                             .channel_id = channel,
                             .original_message_id = original,
                             .original_author_id = author,
                             .state = state,
                             .created_at = std::chrono::floor<std::chrono::seconds>(clock.now()),
                             .retried_at = std::nullopt,
                             .links = std::move(links)});
    }

    watch_request request(std::vector<planned_link> links, bool retry = false) const {
        return {.guild_id = guild,
                .channel_id = channel,
                .message_id = ours,
                .original_message_id = original,
                .links = std::move(links),
                .per_mirror = retry ? latibot::events::retry_attempts_per_mirror : latibot::events::attempts_per_mirror,
                .retry = retry};
    }

    [[nodiscard]] replacement_state state() const { return replacements.find(ours)->state; }

    /// Lets one attempt's time run out.
    std::vector<embed_action> time_out() {
        clock.advance(latibot::events::embed_timeout + 1s);
        return tracker.tick();
    }
};

std::vector<std::string> embeds(std::initializer_list<const char*> urls) {
    return {urls.begin(), urls.end()};
}

const edit_replacement& only_edit(const std::vector<embed_action>& actions) {
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<edit_replacement>(actions[0]));
    return std::get<edit_replacement>(actions[0]);
}

} // namespace

// --------------------------------------------------------------------------
// What gets posted
// --------------------------------------------------------------------------

TEST_CASE("a replacement is one link line per link", "[events]") {
    const std::vector<watched_link> links{
        {.link = x_link(), .attempt = 0, .progress = latibot::events::link_progress::waiting},
        {.link = x_link("/b/status/2", true), .attempt = 2, .progress = latibot::events::link_progress::waiting}};

    CHECK(latibot::events::render_replacement(links, 2) ==
          "🔗 [_](https://fxtwitter.com/a/status/1)\n"
          "🔗 ||[_](https://vxtwitter.com/b/status/2)||");
}

TEST_CASE("the failure note names the mirrors that were tried", "[events]") {
    const std::vector<planned_link> one{x_link()};
    CHECK(latibot::events::render_failure(one).starts_with("🔗 couldn't get a preview for that link from fxtwitter.com or vxtwitter.com."));

    const std::vector<planned_link> two{x_link(), tiktok_link()};
    CHECK(latibot::events::render_failure(two).find("those links from fxtwitter.com, vxtwitter.com or tfxktok.com") != std::string::npos);
}

TEST_CASE("a failure note turns its own previews off and carries Retry", "[events]") {
    const dpp::message edit = latibot::events::build_edit({.channel_id = channel, .message_id = ours, .content = "nope", .failed = true});

    CHECK((edit.flags & dpp::m_suppress_embeds) != 0);
    REQUIRE(edit.components.size() == 1);
    REQUIRE(edit.components[0].components.size() == 1);

    const std::string& id = edit.components[0].components[0].custom_id;
    CHECK(id.size() <= latibot::ui::custom_id_limit);
    const auto state = latibot::ui::decode(id);
    REQUIRE(state.has_value());
    CHECK(state->view == latibot::events::url_retry_view);
    CHECK(state->argument == ours.str());
}

TEST_CASE("a working replacement has no button and its previews on", "[events]") {
    const dpp::message edit = latibot::events::build_edit({.channel_id = channel, .message_id = ours, .content = "ok", .failed = false});
    CHECK(edit.flags == 0);
    CHECK(edit.components.empty());
}

// --------------------------------------------------------------------------
// Matching previews to links
// --------------------------------------------------------------------------

TEST_CASE("with one link, any preview counts", "[events]") {
    const std::vector<watched_link> links{{.link = tiktok_link(), .attempt = 0, .progress = latibot::events::link_progress::waiting}};
    CHECK(latibot::events::embedded_links(links, embeds({"https://www.tiktok.com/@someone/video/123"})) == std::vector<bool>{true});
    CHECK(latibot::events::embedded_links(links, {}) == std::vector<bool>{false});
}

TEST_CASE("previews are matched to links by path", "[events]") {
    const std::vector<watched_link> links{
        {.link = x_link("/a/status/1"), .attempt = 0, .progress = latibot::events::link_progress::waiting},
        {.link = x_link("/b/status/2"), .attempt = 0, .progress = latibot::events::link_progress::waiting}};

    // Only the second link embedded; mirrors report the original site.
    CHECK(latibot::events::embedded_links(links, embeds({"https://twitter.com/b/status/2"})) == std::vector<bool>{false, true});
}

TEST_CASE("several previews of one post do not spill onto the next link", "[events]") {
    const std::vector<watched_link> links{
        {.link = x_link("/a/status/1"), .attempt = 0, .progress = latibot::events::link_progress::waiting},
        {.link = x_link("/b/status/2"), .attempt = 0, .progress = latibot::events::link_progress::waiting}};

    // A post with four images: four previews, all the first link's.
    const auto urls =
        embeds({"https://x.com/a/status/1", "https://x.com/a/status/1", "https://x.com/a/status/1", "https://x.com/a/status/1"});
    CHECK(latibot::events::embedded_links(links, urls) == std::vector<bool>{true, false});
}

TEST_CASE("a preview that matches nothing goes to the first link still waiting", "[events]") {
    const std::vector<watched_link> links{
        {.link = x_link("/a/status/1"), .attempt = 0, .progress = latibot::events::link_progress::waiting},
        {.link = tiktok_link(), .attempt = 0, .progress = latibot::events::link_progress::waiting}};

    CHECK(latibot::events::embedded_links(links, embeds({"https://x.com/a/status/1", "https://www.tiktok.com/@a/video/123"})) ==
          std::vector<bool>{true, true});
}

// --------------------------------------------------------------------------
// The tracker
// --------------------------------------------------------------------------

TEST_CASE("a preview on the first try settles the replacement", "[events]") {
    fixture test;
    test.record({x_link()});

    CHECK(test.tracker.watch(test.request({x_link()})).empty());
    CHECK(test.tracker.watching(ours));

    CHECK(test.tracker.on_embeds(ours, embeds({"https://x.com/a/status/1"})).empty());
    CHECK_FALSE(test.tracker.watching(ours));
    CHECK(test.state() == replacement_state::ok);

    // Nothing is left to time out.
    CHECK(test.time_out().empty());
}

TEST_CASE("each mirror gets two tries, then the next one", "[events]") {
    fixture test;
    test.record({x_link()});
    CHECK(test.tracker.watch(test.request({x_link()})).empty());

    SECTION("nothing happens before the time is up") {
        test.clock.advance(latibot::events::embed_timeout - 1s);
        CHECK(test.tracker.tick().empty());
    }

    SECTION("alt1, alt1, alt2, alt2, then give up") {
        // The first try was the post itself.
        CHECK(only_edit(test.time_out()).content == "🔗 [_](https://fxtwitter.com/a/status/1)");
        CHECK(only_edit(test.time_out()).content == "🔗 [_](https://vxtwitter.com/a/status/1)");
        CHECK(only_edit(test.time_out()).content == "🔗 [_](https://vxtwitter.com/a/status/1)");

        const auto final_actions = test.time_out();
        REQUIRE(final_actions.size() == 2);

        // The original's preview comes back, and ours becomes the note.
        REQUIRE(std::holds_alternative<set_original_embeds>(final_actions[0]));
        CHECK(std::get<set_original_embeds>(final_actions[0]).message_id == original);
        CHECK_FALSE(std::get<set_original_embeds>(final_actions[0]).suppressed);

        REQUIRE(std::holds_alternative<edit_replacement>(final_actions[1]));
        CHECK(std::get<edit_replacement>(final_actions[1]).failed);

        CHECK(test.state() == replacement_state::failed);
        CHECK_FALSE(test.tracker.watching(ours));
    }

    SECTION("a preview after a retry still counts") {
        CHECK(only_edit(test.time_out()).content == "🔗 [_](https://fxtwitter.com/a/status/1)");
        CHECK(test.tracker.on_embeds(ours, embeds({"https://x.com/a/status/1"})).empty());
        CHECK(test.state() == replacement_state::ok);
    }
}

TEST_CASE("each link in a message is tracked on its own", "[events]") {
    fixture test;
    const std::vector<planned_link> links{x_link("/a/status/1"), x_link("/b/status/2")};
    test.record(links);
    CHECK(test.tracker.watch(test.request(links)).empty());

    // The first link embeds; the second never does.
    CHECK(test.tracker.on_embeds(ours, embeds({"https://x.com/a/status/1"})).empty());

    // Only the second link moves on; the first keeps the mirror that worked.
    CHECK(only_edit(test.time_out()).content ==
          "🔗 [_](https://fxtwitter.com/a/status/1)\n"
          "🔗 [_](https://fxtwitter.com/b/status/2)");
    CHECK(only_edit(test.time_out()).content ==
          "🔗 [_](https://fxtwitter.com/a/status/1)\n"
          "🔗 [_](https://vxtwitter.com/b/status/2)");
    CHECK(only_edit(test.time_out()).content ==
          "🔗 [_](https://fxtwitter.com/a/status/1)\n"
          "🔗 [_](https://vxtwitter.com/b/status/2)");

    // One link working is a working replacement: no note, no button.
    CHECK(test.time_out().empty());
    CHECK(test.state() == replacement_state::ok);
}

TEST_CASE("a preview that arrives before the watch starts is not lost", "[events]") {
    // The update can overtake the reply to the post that created the message.
    fixture test;
    test.record({x_link()});

    CHECK(test.tracker.on_embeds(ours, embeds({"https://x.com/a/status/1"})).empty());
    CHECK(test.tracker.watch(test.request({x_link()})).empty());

    CHECK_FALSE(test.tracker.watching(ours));
    CHECK(test.state() == replacement_state::ok);
}

TEST_CASE("an early preview is forgotten after a while", "[events]") {
    fixture test;
    test.record({x_link()});

    CHECK(test.tracker.on_embeds(ours, embeds({"https://x.com/a/status/1"})).empty());
    test.clock.advance(1min);
    CHECK(test.tracker.watch(test.request({x_link()})).empty());
    CHECK(test.tracker.watching(ours));
}

TEST_CASE("previews already on the posted message count", "[events]") {
    fixture test;
    test.record({x_link()});
    const auto urls = embeds({"https://x.com/a/status/1"});

    CHECK(test.tracker.watch(test.request({x_link()}), urls).empty());
    CHECK(test.state() == replacement_state::ok);
}

TEST_CASE("a deleted replacement is no longer followed", "[events]") {
    fixture test;
    test.record({x_link()});
    CHECK(test.tracker.watch(test.request({x_link()})).empty());

    test.tracker.forget(ours);
    CHECK_FALSE(test.tracker.watching(ours));
    CHECK(test.time_out().empty());
}

// --------------------------------------------------------------------------
// Retry
// --------------------------------------------------------------------------

TEST_CASE("Retry uses the rule as it is now, one try per mirror", "[events]") {
    fixture test;
    test.record({x_link()}, replacement_state::failed);
    test.rules.set(guild, {.domain = "x.com",
                           .mirrors = {{.host = "fixupx.com", .translate_suffix = ""}, {.host = "vxtwitter.com", .translate_suffix = ""}}});

    auto planned = latibot::events::plan_retry(test.replacements, test.rules, ours, guild);
    REQUIRE(std::holds_alternative<latibot::events::retry_plan>(planned));
    auto& plan = std::get<latibot::events::retry_plan>(planned);

    CHECK(plan.first.content == "🔗 [_](https://fixupx.com/a/status/1)");
    CHECK_FALSE(plan.first.failed);
    CHECK(plan.request.retry);
    CHECK(plan.request.per_mirror == 1);

    SECTION("success puts the original's preview back off") {
        CHECK(test.tracker.watch(plan.request).empty());
        const auto actions = test.tracker.on_embeds(ours, embeds({"https://x.com/a/status/1"}));

        REQUIRE(actions.size() == 1);
        REQUIRE(std::holds_alternative<set_original_embeds>(actions[0]));
        CHECK(std::get<set_original_embeds>(actions[0]).suppressed);

        const auto stored = test.replacements.find(ours);
        CHECK(stored->state == replacement_state::ok);
        CHECK(stored->retried_at.has_value());
    }

    SECTION("failure keeps the button and records when it ran") {
        CHECK(test.tracker.watch(plan.request).empty());
        CHECK(only_edit(test.time_out()).content == "🔗 [_](https://vxtwitter.com/a/status/1)");

        // The original's preview is already on, so only the note changes.
        CHECK(only_edit(test.time_out()).failed);

        const auto stored = test.replacements.find(ours);
        CHECK(stored->state == replacement_state::failed);
        CHECK(stored->retried_at.has_value());
    }
}

TEST_CASE("Retry says why there is nothing to retry", "[events]") {
    fixture test;
    test.rules.set(guild, {.domain = "x.com", .mirrors = {{.host = "fxtwitter.com", .translate_suffix = ""}}});

    SECTION("a replacement that is working") {
        test.record({x_link()}, replacement_state::ok);
        CHECK(std::holds_alternative<std::string>(latibot::events::plan_retry(test.replacements, test.rules, ours, guild)));
    }

    SECTION("one already retrying") {
        test.record({x_link()}, replacement_state::retrying);
        CHECK(std::holds_alternative<std::string>(latibot::events::plan_retry(test.replacements, test.rules, ours, guild)));
    }

    SECTION("a button from another guild") {
        test.record({x_link()}, replacement_state::failed);
        CHECK(std::holds_alternative<std::string>(latibot::events::plan_retry(test.replacements, test.rules, ours, dpp::snowflake{9})));
    }

    SECTION("a rule that has since been removed") {
        test.record({tiktok_link()}, replacement_state::failed);
        CHECK(std::holds_alternative<std::string>(latibot::events::plan_retry(test.replacements, test.rules, ours, guild)));
    }
}

// --------------------------------------------------------------------------
// Posting, through the Discord mock
// --------------------------------------------------------------------------

TEST_CASE("posting sends the replacement, records it, and turns the original's preview off", "[events][coro]") {
    fixture test;
    latibot::testing::mock_discord discord;

    latibot::events::post_replacement(
        discord, test.replacements, test.tracker, test.clock,
        {.guild_id = guild, .channel_id = channel, .message_id = original, .author_id = author, .links = {x_link()}})
        .sync_wait_for(2s);

    REQUIRE(discord.sent.size() == 1);
    CHECK(discord.sent[0].content == "🔗 [_](https://fxtwitter.com/a/status/1)");
    CHECK((discord.sent[0].flags & dpp::m_suppress_notifications) != 0);
    // A plain message, never a reply (plan v4 §9.2).
    CHECK(discord.sent[0].message_reference.message_id.empty());

    REQUIRE(discord.suppressions.size() == 1);
    CHECK(discord.suppressions[0].message_id == original);
    CHECK(discord.suppressions[0].suppressed);

    // The mock hands out ids from 1001.
    const auto stored = test.replacements.find(dpp::snowflake{1001});
    REQUIRE(stored.has_value());
    CHECK(stored->original_author_id == author);
    CHECK(stored->original_message_id == original);
    CHECK(stored->state == replacement_state::pending);
    REQUIRE(stored->links.size() == 1);
    CHECK(stored->links[0].original_url == "https://x.com/a/status/1");

    CHECK(test.tracker.watching(dpp::snowflake{1001}));
}

TEST_CASE("a replacement that cannot be posted leaves the original alone", "[events][coro]") {
    fixture test;
    latibot::testing::mock_discord discord;
    discord.send_results.emplace_back(latibot::api_error{.http_status = 403, .message = "Missing Permissions"});

    latibot::events::post_replacement(
        discord, test.replacements, test.tracker, test.clock,
        {.guild_id = guild, .channel_id = channel, .message_id = original, .author_id = author, .links = {x_link()}})
        .sync_wait_for(2s);

    CHECK(discord.suppressions.empty());
    CHECK_FALSE(test.replacements.contains(dpp::snowflake{1001}));
}

TEST_CASE("a failure's actions reach Discord", "[events][coro]") {
    latibot::testing::mock_discord discord;
    const std::vector<embed_action> actions{set_original_embeds{.channel_id = channel, .message_id = original, .suppressed = false},
                                            edit_replacement{.channel_id = channel, .message_id = ours, .content = "note", .failed = true}};

    latibot::events::carry_out_embed_actions(discord, actions).sync_wait_for(2s);

    REQUIRE(discord.suppressions.size() == 1);
    CHECK_FALSE(discord.suppressions[0].suppressed);
    REQUIRE(discord.edited.size() == 1);
    CHECK(discord.edited[0].id == ours);
    CHECK((discord.edited[0].flags & dpp::m_suppress_embeds) != 0);
}

// --------------------------------------------------------------------------
// The stage
// --------------------------------------------------------------------------

namespace {

latibot::events::incoming_message link_message(std::string content) {
    latibot::events::incoming_message message;
    message.guild_id = guild;
    message.channel_id = channel;
    message.message_id = original;
    message.author_id = author;
    message.content = std::move(content);
    return message;
}

} // namespace

TEST_CASE("the stage asks for a replacement and lets the message carry on", "[events]") {
    fixture test;
    test.rules.set(guild, {.domain = "x.com", .mirrors = {{.host = "fxtwitter.com", .translate_suffix = ""}}});
    const latibot::events::url_replacer stage(test.rules);

    const auto result = stage(link_message("look https://x.com/a/status/1"));
    CHECK_FALSE(result.consumed);
    REQUIRE(result.actions.size() == 1);

    const auto& wanted = std::get<latibot::events::replace_links>(result.actions[0]);
    CHECK(wanted.message_id == original);
    CHECK(wanted.author_id == author);
    REQUIRE(wanted.links.size() == 1);
    CHECK(wanted.links[0].original_url == "https://x.com/a/status/1");
}

TEST_CASE("the stage leaves some messages alone", "[events]") {
    fixture test;
    test.rules.set(guild, {.domain = "x.com", .mirrors = {{.host = "fxtwitter.com", .translate_suffix = ""}}});
    const latibot::events::url_replacer stage(test.rules);

    SECTION("a bot's") {
        auto message = link_message("https://x.com/a/status/1");
        message.from_bot = true;
        message.author_is_allowed_bot = true;
        CHECK(stage(message).actions.empty());
    }

    SECTION("one whose author turned previews off") {
        auto message = link_message("https://x.com/a/status/1");
        message.embeds_suppressed = true;
        CHECK(stage(message).actions.empty());
    }

    SECTION("an author who opted out") {
        test.rules.toggle_opt_out(guild, author);
        CHECK(stage(link_message("https://x.com/a/status/1")).actions.empty());
    }

    SECTION("a link to a site with no rule") {
        CHECK(stage(link_message("https://example.com/a")).actions.empty());
    }

    SECTION("a direct message") {
        auto message = link_message("https://x.com/a/status/1");
        message.guild_id = dpp::snowflake{};
        CHECK(stage(message).actions.empty());
    }
}

TEST_CASE("a link too long to post is dropped rather than failing the post", "[events]") {
    fixture test;
    test.rules.set(guild, {.domain = "x.com", .mirrors = {{.host = "fxtwitter.com", .translate_suffix = ""}}});
    const latibot::events::url_replacer stage(test.rules);

    const std::string long_link = "https://x.com/a/" + std::string(latibot::events::message_length_limit, 'b');
    const auto result = stage(link_message("https://x.com/a/status/1 " + long_link));

    REQUIRE(result.actions.size() == 1);
    CHECK(std::get<latibot::events::replace_links>(result.actions[0]).links.size() == 1);
}

TEST_CASE("a message with a joke and a link gets both", "[events]") {
    // The Java bot's early returns answered "nice" and skipped the link.
    fixture test;
    test.rules.set(guild, {.domain = "x.com", .mirrors = {{.host = "fxtwitter.com", .translate_suffix = ""}}});

    latibot::events::trigger_store triggers(test.db);
    triggers.seed_defaults(guild);
    latibot::events::trigger_responder responder(triggers, test.clock, [] { return std::uint64_t{0}; });

    latibot::events::pipeline stages;
    stages.add("url replacement", latibot::events::url_replacer(test.rules));
    stages.add("triggers", [&](const latibot::events::incoming_message& message) { return responder(message); });

    const auto actions = stages.run(link_message("420 https://x.com/a/status/1"));
    REQUIRE(actions.size() == 2);
    CHECK(std::holds_alternative<latibot::events::replace_links>(actions[0]));
    CHECK(std::holds_alternative<latibot::events::send_message>(actions[1]));
}
