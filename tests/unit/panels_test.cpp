// Every panel, used end to end as a person would use it in Discord: open it,
// press, pick, fill in a form, and look at what was stored and shown.
//
// Through DPP's own reading and writing of interactions (see
// support/panel_harness.hpp), since that is where the panels once broke:
// each form arrived empty, and nothing short of DPP's own parsing shows it.

#include "core/audio/speech_queue.hpp"
#include "core/audio/voice_store.hpp"
#include "core/commands/voice_lab.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/ui/interaction.hpp"

#include "mocks/mock_clock.hpp"
#include "mocks/mock_tts.hpp"
#include "mocks/mock_voice.hpp"
#include "support/panel_harness.hpp"
#include "support/panel_queries.hpp"
#include "support/schema.hpp"

#include <dpp/cache.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <optional>
#include <string>

using latibot::testing::field_value;
using latibot::testing::panel_harness;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};

struct database_fixture {
    latibot::db::database db{":memory:"};

    database_fixture() { latibot::testing::create_schema(db); }
};

} // namespace

// --------------------------------------------------------------------------
// How a form is read
// --------------------------------------------------------------------------

TEST_CASE("a form's fields are read however DPP lays them out", "[commands]") {
    auto input = [](const std::string& id, std::string value) {
        dpp::component field;
        field.set_type(dpp::cot_text).set_id(id);
        field.value = std::move(value);
        return field;
    };

    SECTION("each field on its own, as DPP 10.1 gives them") {
        const auto fields = latibot::ui::form_fields(std::vector{input("pattern", "hello"), input("mode", "")});
        CHECK(fields.size() == 2);
        CHECK(fields.at("pattern") == "hello");
        CHECK(fields.at("mode").empty());
    }

    SECTION("inside action rows, as older modals had them") {
        dpp::component row;
        row.add_component(input("pattern", "hello"));
        row.add_component(input("cooldown", "30"));
        const auto fields = latibot::ui::form_fields(std::vector{row});
        CHECK(fields.size() == 2);
        CHECK(fields.at("cooldown") == "30");
    }
}

// --------------------------------------------------------------------------
// The voice lab
// --------------------------------------------------------------------------

namespace {

/// A guild in DPP's cache with the tester in a voice channel, which is where
/// the lab's Test looks for somewhere to speak.
class cached_guild {
public:
    cached_guild(dpp::snowflake member, dpp::snowflake voice_channel) {
        // The cache owns it once stored, and frees it after `remove`.
        guild_ = new dpp::guild(); // NOLINT(cppcoreguidelines-owning-memory)
        guild_->id = guild;
        dpp::voicestate state;
        state.guild_id = guild;
        state.channel_id = voice_channel;
        state.user_id = member;
        guild_->voice_members[member] = state;
        dpp::get_guild_cache()->store(guild_);
    }
    ~cached_guild() {
        // Queued for deletion, which allocates; a test that cannot even do
        // that has bigger problems than a guild left in the cache.
        try {
            dpp::get_guild_cache()->remove(guild_);
        } catch (...) { // NOLINT(bugprone-empty-catch)
        }
    }

    cached_guild(const cached_guild&) = delete;
    cached_guild(cached_guild&&) = delete;
    auto operator=(const cached_guild&) -> cached_guild& = delete;
    auto operator=(cached_guild&&) -> cached_guild& = delete;

private:
    dpp::guild* guild_;
};

struct voice_lab_fixture : database_fixture {
    latibot::config::guild_settings settings{db};
    latibot::audio::voice_store voices{db};
    latibot::testing::mock_clock clock;
    latibot::testing::mock_tts tts;
    latibot::testing::mock_voice voice;
    latibot::audio::speech_queue queue{voice};
    latibot::commands::voice_drafts drafts{clock};
    latibot::commands::voice_lab lab{
        drafts, voices, clock, {.engine = &tts, .queue = &queue, .settings = &settings, .bootstrap = nullptr, .voices = &voices}};
    panel_harness discord{
        [this](const auto& event, const auto& state, const std::string& chosen) { return lab.on_component(event, state, chosen); },
        [this](const auto& event, const auto& state) { return lab.on_form(event, state); }};

    voice_lab_fixture() { discord.open(*lab.open(guild, discord.user, "")); }
};

} // namespace

TEST_CASE("the voice lab keeps what its forms set, and the Test says it in that voice", "[commands]") {
    voice_lab_fixture test;

    test.discord.choose("harry");
    const json form = test.discord.choose("0"); // Pitch
    CHECK(field_value(form, "ap").empty());
    REQUIRE(panel_harness::is_update(test.discord.submit(form, {{"ap", "300"}, {"pr", "150"}})));
    CHECK(test.discord.content().contains("**Pitch** ap 300 · pr 150"));
    CHECK(test.discord.content().contains("`[:nh][:dv ap 300 pr 150]`"));

    // The form opens again with what it was given.
    const json again = test.discord.choose("0");
    CHECK(field_value(again, "ap") == "300");
    CHECK(field_value(again, "pr") == "150");

    const cached_guild here(test.discord.user, dpp::snowflake{4000});
    test.discord.press("▶ Test");
    REQUIRE(test.tts.requests.size() == 1);
    CHECK(test.tts.requests[0].voice.voice == "harry");
    CHECK(test.tts.requests[0].voice.custom_params == "ap 300 pr 150");
}

TEST_CASE("the voice lab's text form replaces the whole voice", "[commands]") {
    voice_lab_fixture test;

    const json form = test.discord.choose(latibot::commands::lab_raw_form);
    CHECK(field_value(form, "raw") == "[:np]");
    REQUIRE(panel_harness::is_update(test.discord.submit(form, {{"raw", "[:nk][:dv hs 80]"}})));
    CHECK(test.discord.content().contains("Built on **kit**"));
    CHECK(test.discord.content().contains("`[:nk][:dv hs 80]`"));
}

TEST_CASE("the voice lab saves a voice, says when it has changed since, and opens it again", "[commands]") {
    voice_lab_fixture test;

    test.discord.submit(test.discord.choose("0"), {{"ap", "250"}});
    test.discord.submit(test.discord.press("Save as…"), {{"name", "Robo"}});

    const auto saved = test.voices.find(guild, "robo");
    REQUIRE(saved.has_value());
    CHECK(saved->voice.dv_parameters() == "ap 250");
    CHECK(test.discord.content().contains("editing `robo`, as saved"));

    test.discord.submit(test.discord.choose("0"), {{"ap", "90"}});
    CHECK(test.discord.content().contains("editing `robo`, with **unsaved changes**"));
    CHECK(test.voices.find(guild, "robo")->voice.dv_parameters() == "ap 250");

    // Picking it again throws the changes away.
    test.discord.choose("robo");
    CHECK(test.discord.content().contains("editing `robo`, as saved"));
    CHECK(test.discord.content().contains("ap 250"));

    test.discord.press("New voice");
    CHECK(test.discord.content().contains("editing a new voice, not saved yet"));
}

TEST_CASE("the voice lab will not save over someone else's voice", "[commands]") {
    voice_lab_fixture test;
    test.voices.save(guild, {.name = "theirs", .voice = {}, .created_by = dpp::snowflake{99}, .updated_at = {}});

    test.discord.submit(test.discord.choose("0"), {{"ap", "250"}});
    test.discord.submit(test.discord.press("Save as…"), {{"name", "theirs"}});
    CHECK(test.voices.find(guild, "theirs")->voice.edits.empty());
    CHECK(test.discord.content().contains("only whoever made `theirs`, or an admin, can change it"));

    test.discord.permissions = dpp::p_administrator;
    test.discord.submit(test.discord.press("Save as…"), {{"name", "theirs"}});
    CHECK(test.voices.find(guild, "theirs")->voice.dv_parameters() == "ap 250");
}
