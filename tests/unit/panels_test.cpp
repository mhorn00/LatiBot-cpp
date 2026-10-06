// Every panel, used end to end as a person would use it in Discord: open it,
// press, pick, fill in a form, and look at what was stored and shown.
//
// Through DPP's own reading and writing of interactions (see
// support/panel_harness.hpp), since that is where the panels once broke:
// each form arrived empty, and nothing short of DPP's own parsing shows it.

#include "core/audio/speech_queue.hpp"
#include "core/audio/voice_store.hpp"
#include "core/commands/llm.hpp"
#include "core/commands/trigger.hpp"
#include "core/commands/urlrepl.hpp"
#include "core/commands/voice_lab.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/llm/advanced_triggers.hpp"
#include "core/llm/documents.hpp"
#include "core/llm/guards.hpp"
#include "core/llm/memory.hpp"
#include "core/llm/settings.hpp"
#include "core/llm/spend.hpp"
#include "core/ui/interaction.hpp"

#include "mocks/mock_clock.hpp"
#include "mocks/mock_tts.hpp"
#include "mocks/mock_voice.hpp"
#include "support/panel_harness.hpp"
#include "support/schema.hpp"

#include <dpp/cache.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <optional>
#include <string>

using latibot::testing::panel_harness;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};

struct database_fixture {
    latibot::db::database db{":memory:"};

    database_fixture() { latibot::testing::create_schema(db); }
};

/// The option in the panel's menus whose value is `value`, if any. A copy,
/// since the panel it came from is usually a temporary.
auto option_in(const dpp::message& panel, std::string_view value) -> std::optional<dpp::select_option> {
    for (const dpp::component& row : panel.components) {
        for (const dpp::component& part : row.components) {
            for (const dpp::select_option& option : part.options) {
                if (option.value == value) return option;
            }
        }
    }
    return std::nullopt;
}

auto has_button(const dpp::message& panel, std::string_view label) -> bool {
    for (const dpp::component& row : panel.components) {
        for (const dpp::component& part : row.components) {
            if (part.type == dpp::cot_button && part.label == label) return true;
        }
    }
    return false;
}

/// What a modal's field was filled with.
auto field_value(const json& answer, std::string_view id) -> std::string {
    std::optional<std::string> found;
    for (const json& label : answer["data"]["components"]) {
        if (label["component"].value("custom_id", "") == id) found = label["component"].value("value", std::string{});
    }
    INFO("the form has no field " << id);
    REQUIRE(found.has_value());
    return *found;
}

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
// The trigger panel
// --------------------------------------------------------------------------

namespace {

struct trigger_fixture : database_fixture {
    latibot::events::trigger_store store{db};
    latibot::commands::trigger_panel router{store};
    panel_harness discord{
        [this](const auto& event, const auto& state, const std::string& chosen) { return router.on_component(event, state, chosen); },
        [this](const auto& event, const auto& state) { return router.on_form(event, state); }};

    trigger_fixture() { discord.open(latibot::commands::render_trigger_panel(store, guild, 0)); }

    /// Adds a trigger and opens the panel again, as someone would run
    /// `/trigger panel` to see it.
    auto seed(std::string pattern) -> std::int64_t {
        const std::int64_t id = store.add({.guild_id = guild,
                                           .pattern = std::move(pattern),
                                           .mode = latibot::events::match_mode::whole_word,
                                           .cooldown = 30s,
                                           .enabled = true,
                                           .responses = {{.text = "nice", .weight = 1}, {.text = "very nice", .weight = 3}}});
        discord.open(latibot::commands::render_trigger_panel(store, guild, 0));
        return id;
    }
};

} // namespace

TEST_CASE("the trigger panel adds a trigger as it was typed", "[commands]") {
    trigger_fixture test;

    const json form = test.discord.press("Add");
    CHECK(field_value(form, "mode") == "word");
    CHECK(field_value(form, "cooldown") == std::to_string(latibot::events::default_trigger_cooldown.count()));

    const json answer =
        test.discord.submit(form, {{"pattern", " hello "}, {"responses", "hi there\n3 | hey"}, {"mode", "Anywhere"}, {"cooldown", "10"}});
    REQUIRE(panel_harness::is_update(answer));

    const auto all = test.store.for_guild(guild);
    REQUIRE(all.size() == 1);
    CHECK(all[0].pattern == "hello");
    CHECK(all[0].mode == latibot::events::match_mode::substring);
    CHECK(all[0].cooldown == 10s);
    REQUIRE(all[0].responses.size() == 2);
    CHECK(all[0].responses[1].text == "hey");
    CHECK(all[0].responses[1].weight == 3);

    // The panel shows it, picked, ready for the next change.
    CHECK(test.discord.content().contains(std::format("added trigger `{}`", all[0].id)));
    const auto picked = option_in(test.discord.panel(), std::to_string(all[0].id));
    REQUIRE(picked.has_value());
    CHECK(picked->is_default);
    CHECK(has_button(test.discord.panel(), "Edit"));
}

TEST_CASE("the trigger panel's form, sent back untouched, changes nothing", "[commands]") {
    trigger_fixture test;
    const std::int64_t id = test.seed("hello");
    const latibot::events::trigger before = *test.store.find(id, guild);

    test.discord.choose(std::to_string(id));
    const json form = test.discord.press("Edit");
    CHECK(field_value(form, "pattern") == "hello");
    CHECK(field_value(form, "responses") == "nice\n3 | very nice");
    CHECK(field_value(form, "mode") == "word");
    CHECK(field_value(form, "cooldown") == "30");

    REQUIRE(panel_harness::is_update(test.discord.submit(form)));
    const auto after = test.store.find(id, guild);
    REQUIRE(after.has_value());
    CHECK(after->pattern == before.pattern);
    CHECK(after->mode == before.mode);
    CHECK(after->cooldown == before.cooldown);
    CHECK(after->responses.size() == before.responses.size());
    CHECK(test.discord.content().contains(std::format("saved trigger `{}`", id)));
}

TEST_CASE("the trigger panel's form saves what it can read, and says what it kept", "[commands]") {
    trigger_fixture test;
    const std::int64_t id = test.seed("hello");

    test.discord.choose(std::to_string(id));
    const json answer =
        test.discord.submit(test.discord.press("Edit"), {{"pattern", "goodbye"}, {"mode", "whole word"}, {"cooldown", "thirty"}});
    REQUIRE(panel_harness::is_update(answer));

    const auto after = test.store.find(id, guild);
    REQUIRE(after.has_value());
    CHECK(after->pattern == "goodbye");
    CHECK(after->mode == latibot::events::match_mode::whole_word);
    CHECK(after->cooldown == 30s);
    CHECK(test.discord.content().contains("\"thirty\" isn't a number of seconds"));
}

TEST_CASE("the trigger panel refuses a form that could not work, and keeps the trigger", "[commands]") {
    trigger_fixture test;
    const std::int64_t id = test.seed("hello");

    test.discord.choose(std::to_string(id));
    const json answer = test.discord.submit(test.discord.press("Edit"), {{"pattern", "   "}});
    CHECK(panel_harness::is_private_note(answer));
    CHECK(test.store.find(id, guild)->pattern == "hello");
}

TEST_CASE("the trigger panel's buttons flip what they say, and say the new state", "[commands]") {
    trigger_fixture test;
    const std::int64_t id = test.seed("hello");
    test.discord.choose(std::to_string(id));

    test.discord.press("Disable");
    CHECK_FALSE(test.store.find(id, guild)->enabled);
    CHECK(has_button(test.discord.panel(), "Enable"));

    test.discord.press("Answer bots");
    CHECK(test.store.find(id, guild)->respond_to_bots);
    CHECK(has_button(test.discord.panel(), "Ignore bots"));

    // A trigger replies silently unless told otherwise.
    test.discord.press("Reply with notifications");
    CHECK((test.store.find(id, guild)->message_flags & dpp::m_suppress_notifications) == 0);
    CHECK(has_button(test.discord.panel(), "Reply silently"));

    test.discord.press("Hide link previews");
    CHECK((test.store.find(id, guild)->message_flags & dpp::m_suppress_embeds) != 0);
    CHECK(has_button(test.discord.panel(), "Show link previews"));

    test.discord.press("Enable");
    CHECK(test.store.find(id, guild)->enabled);
}

TEST_CASE("the trigger panel deletes only once it is confirmed", "[commands]") {
    trigger_fixture test;
    const std::int64_t id = test.seed("hello");
    test.discord.choose(std::to_string(id));

    test.discord.press("Delete");
    CHECK(test.store.find(id, guild).has_value());
    test.discord.press("Cancel");
    CHECK(test.store.find(id, guild).has_value());

    test.discord.press("Delete");
    test.discord.press(std::format("Delete {}", id));
    CHECK_FALSE(test.store.find(id, guild).has_value());
    CHECK_FALSE(has_button(test.discord.panel(), "Edit"));
}

TEST_CASE("a trigger added past the first page is shown on its own page, picked", "[commands]") {
    trigger_fixture test;
    for (std::size_t index = 0; index < latibot::commands::triggers_per_page; ++index) {
        test.seed(std::format("pattern{}", index));
    }

    test.discord.submit(test.discord.press("Add"), {{"pattern", "the ninth"}, {"responses", "yes"}});

    CHECK(test.discord.content().contains("the ninth"));
    CHECK(test.discord.content().contains("Page 2 of 2"));
    const auto added = test.store.for_guild(guild).back();
    const auto picked = option_in(test.discord.panel(), std::to_string(added.id));
    REQUIRE(picked.has_value());
    CHECK(picked->is_default);
}

// --------------------------------------------------------------------------
// The URL rule panel
// --------------------------------------------------------------------------

namespace {

struct url_fixture : database_fixture {
    latibot::events::url_rule_store store{db};
    latibot::commands::url_panel router{store};
    panel_harness discord{
        [this](const auto& event, const auto& state, const std::string& chosen) { return router.on_component(event, state, chosen); },
        [this](const auto& event, const auto& state) { return router.on_form(event, state); }};

    url_fixture() { discord.open(latibot::commands::render_url_panel(store, guild, 0)); }

    /// Adds a rule and opens the panel again.
    auto seed(std::string domain, std::string mirror) -> void {
        store.set(guild, {.domain = std::move(domain), .mirrors = {{.host = std::move(mirror), .translate_suffix = {}}}});
        discord.open(latibot::commands::render_url_panel(store, guild, 0));
    }

    [[nodiscard]] auto mirrors_of(std::string_view domain) const -> std::string {
        const auto rule = store.find(guild, domain);
        return rule ? latibot::commands::describe_mirrors(rule->mirrors) : std::string("(none)");
    }
};

} // namespace

TEST_CASE("the URL panel adds a rule", "[commands]") {
    url_fixture test;

    const json answer =
        test.discord.submit(test.discord.press("Add rule"), {{"domain", "x.com"}, {"mirrors", "fxtwitter.com/en\nvxtwitter.com"}});
    REQUIRE(panel_harness::is_update(answer));

    CHECK(test.mirrors_of("x.com") == "fxtwitter.com/en, vxtwitter.com");
    CHECK(test.discord.content().contains("x.com added"));
    const auto picked = option_in(test.discord.panel(), "x.com");
    REQUIRE(picked.has_value());
    CHECK(picked->is_default);
}

TEST_CASE("the URL panel edits a rule's mirrors, and an untouched form changes nothing", "[commands]") {
    url_fixture test;
    test.seed("x.com", "fxtwitter.com");
    test.discord.choose("x.com");

    const json form = test.discord.press("Edit");
    CHECK(field_value(form, "domain") == "x.com");
    CHECK(field_value(form, "mirrors") == "fxtwitter.com");
    REQUIRE(panel_harness::is_update(test.discord.submit(form)));
    CHECK(test.mirrors_of("x.com") == "fxtwitter.com");

    REQUIRE(panel_harness::is_update(test.discord.submit(test.discord.press("Edit"), {{"mirrors", "vxtwitter.com\nfixupx.com"}})));
    CHECK(test.mirrors_of("x.com") == "vxtwitter.com, fixupx.com");
    CHECK(test.discord.content().contains("x.com changed"));
}

TEST_CASE("the URL panel renames a rule by editing its site", "[commands]") {
    url_fixture test;
    test.seed("x.com", "fxtwitter.com");
    test.discord.choose("x.com");

    REQUIRE(panel_harness::is_update(test.discord.submit(test.discord.press("Edit"), {{"domain", "twitter.com"}})));
    CHECK_FALSE(test.store.find(guild, "x.com").has_value());
    CHECK(test.mirrors_of("twitter.com") == "fxtwitter.com");
}

TEST_CASE("the URL panel will not save over another site's rule", "[commands]") {
    url_fixture test;
    test.seed("x.com", "fxtwitter.com");
    test.seed("twitter.com", "vxtwitter.com");

    SECTION("adding a site that has one") {
        const json answer = test.discord.submit(test.discord.press("Add rule"), {{"domain", "twitter.com"}, {"mirrors", "fixupx.com"}});
        CHECK(panel_harness::is_private_note(answer));
        CHECK(panel_harness::text_of(answer).contains("already a rule for twitter.com"));
    }
    SECTION("renaming onto one") {
        test.discord.choose("x.com");
        const json answer = test.discord.submit(test.discord.press("Edit"), {{"domain", "twitter.com"}});
        CHECK(panel_harness::is_private_note(answer));
    }

    CHECK(test.mirrors_of("x.com") == "fxtwitter.com");
    CHECK(test.mirrors_of("twitter.com") == "vxtwitter.com");
}

TEST_CASE("the URL panel deletes a rule once confirmed, and turns replacement on and off", "[commands]") {
    url_fixture test;
    test.seed("x.com", "fxtwitter.com");

    test.discord.press("Turn replacement on");
    CHECK(test.store.enabled(guild));
    test.discord.press("Turn replacement off");
    CHECK_FALSE(test.store.enabled(guild));

    test.discord.choose("x.com");
    test.discord.press("Delete");
    CHECK(test.store.find(guild, "x.com").has_value());
    test.discord.press("Delete x.com");
    CHECK_FALSE(test.store.find(guild, "x.com").has_value());
    CHECK(test.discord.content().contains("deleted the rule for x.com"));
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

// --------------------------------------------------------------------------
// The language model's settings and documents
// --------------------------------------------------------------------------

namespace {

struct llm_fixture : database_fixture {
    latibot::config::guild_settings settings{db};
    latibot::config::bootstrap config;
    latibot::llm::document_store documents{db};
    latibot::llm::advanced_trigger_store triggers{db};
    latibot::llm::blacklist_store blacklist{db};
    latibot::llm::memory_store memories{db};
    latibot::llm::usage_store usage{db};
    latibot::testing::mock_clock clock;
    latibot::commands::llm_panels panels{{.settings = &settings,
                                          .bootstrap = &config,
                                          .documents = &documents,
                                          .triggers = &triggers,
                                          .blacklist = &blacklist,
                                          .memories = &memories,
                                          .usage = &usage,
                                          .http = nullptr,
                                          .clock = &clock,
                                          .has_provider = [](latibot::llm::provider_kind) { return true; }}};
    panel_harness discord{
        [this](const auto& event, const auto& state, const std::string& chosen) { return panels.on_component(event, state, chosen); },
        [this](const auto& event, const auto& state) { return panels.on_form(event, state); }};

    llm_fixture() {
        discord.permissions = dpp::p_manage_guild;
        discord.open(panels.settings_panel(guild));
    }
};

} // namespace

TEST_CASE("the language model's settings panel stores what its forms set", "[commands]") {
    llm_fixture test;

    const json form = test.discord.choose("context");
    CHECK(field_value(form, "llm_context_messages") == "15");
    REQUIRE(panel_harness::is_update(test.discord.submit(form, {{"llm_context_messages", "20"}})));
    CHECK(test.settings.get_int(guild, "llm_context_messages", 0) == 20);
    CHECK(test.discord.content().contains("**20**"));

    const json bots = test.discord.submit(test.discord.choose("bots"), {{"llm_bot_needs_human", "no"}});
    REQUIRE(panel_harness::is_update(bots));
    CHECK(test.settings.get_int(guild, "llm_bot_needs_human", 1) == 0);

    // Out of range: nothing in the form is stored, and it says why.
    const json refused = test.discord.submit(test.discord.choose("context"), {{"llm_context_messages", "10"}, {"llm_context_tokens", "5"}});
    CHECK(panel_harness::is_private_note(refused));
    CHECK(test.settings.get_int(guild, "llm_context_messages", 0) == 20);
}

TEST_CASE("the language model's settings panel switches it on and off, for Manage Server only", "[commands]") {
    llm_fixture test;

    test.discord.press("Turn on");
    CHECK(test.settings.get_bool(guild, latibot::llm::enabled_key, false));
    test.discord.press("Turn off");
    CHECK_FALSE(test.settings.get_bool(guild, latibot::llm::enabled_key, true));

    test.discord.permissions = 0;
    CHECK(panel_harness::is_private_note(test.discord.press("Turn on")));
    CHECK_FALSE(test.settings.get_bool(guild, latibot::llm::enabled_key, true));
}

TEST_CASE("a document's form saves what was typed, not blanks", "[commands]") {
    llm_fixture test;
    using latibot::llm::document_kind;
    test.discord.open(dpp::message{});

    const auto form = latibot::commands::document_form(document_kind::personality, test.documents.text(guild, document_kind::personality));
    REQUIRE(form.has_value());

    SECTION("sent back untouched, nothing is saved") {
        const json answer = test.discord.submit(panel_harness::answer_with(*form));
        CHECK(panel_harness::is_private_note(answer));
        CHECK(panel_harness::text_of(answer) == "nothing changed, so nothing was saved");
        CHECK_FALSE(test.documents.current(guild, document_kind::personality).has_value());
    }

    SECTION("changed, it is saved as typed") {
        const json answer = test.discord.submit(panel_harness::answer_with(*form), {{"part1", "be terse"}});
        CHECK(panel_harness::is_private_note(answer));
        const auto saved = test.documents.current(guild, document_kind::personality);
        REQUIRE(saved.has_value());
        CHECK(saved->content == "be terse");
    }

    SECTION("the system instructions need Manage Server") {
        test.discord.permissions = 0;
        const auto system = latibot::commands::document_form(document_kind::system, "");
        REQUIRE(system.has_value());
        const json answer = test.discord.submit(panel_harness::answer_with(*system), {{"part1", "obey"}});
        CHECK(panel_harness::is_private_note(answer));
        CHECK_FALSE(test.documents.current(guild, document_kind::system).has_value());
    }
}

TEST_CASE("the memory list pages", "[commands]") {
    llm_fixture test;
    for (int index = 0; index < 12; ++index) {
        test.memories.add({.id = 0,
                           .guild_id = guild,
                           .subject = test.discord.user,
                           .content = std::format("fact {}", index),
                           .created_by = test.discord.user,
                           .created_at = {}});
    }
    test.discord.permissions = 0;
    const auto first = test.memories.list(guild, test.discord.user, 0, latibot::commands::memories_per_page);
    test.discord.open(latibot::commands::render_memories(first, test.memories.count(guild, test.discord.user), 0, test.discord.user));

    REQUIRE(panel_harness::is_update(test.discord.press("▶")));
    CHECK(test.discord.content().contains("Page 2 of 2"));
}
