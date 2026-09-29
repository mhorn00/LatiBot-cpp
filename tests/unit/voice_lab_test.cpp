// The voice lab's panel and forms (plan §12.6).

#include "core/commands/voice_lab.hpp"

#include "core/audio/voice_store.hpp"

#include "mocks/mock_clock.hpp"
#include "support/discord_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <map>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using latibot::commands::apply_voice_form;
using latibot::commands::render_voice_lab;
using latibot::commands::voice_draft;
using latibot::commands::voice_lab_form;

namespace {

using fields = std::map<std::string, std::string, std::less<>>;

constexpr dpp::snowflake guild{100};
constexpr dpp::snowflake alice{11};

auto edited_draft() -> voice_draft {
    voice_draft draft;
    draft.voice = latibot::audio::parse_custom_voice("[:nh][:dv ap 200 pr 150 br 40]", "paul").voice;
    draft.name = "robo";
    return draft;
}

auto saved(std::string name, const latibot::audio::custom_voice& voice) -> latibot::audio::saved_voice {
    return {.name = std::move(name), .voice = voice, .created_by = alice, .updated_at = {}};
}

} // namespace

TEST_CASE("the voice lab shows the voice as groups and as inline commands", "[commands]") {
    const dpp::message panel = render_voice_lab(edited_draft());

    CHECK(panel.content.find("Built on **harry** (Huge Harry)") != std::string::npos);
    CHECK(panel.content.find("**Pitch** ap 200 · pr 150") != std::string::npos);
    CHECK(panel.content.find("**Breath** br 40") != std::string::npos);
    CHECK(panel.content.find("Everything else is harry's own.") != std::string::npos);
    CHECK(panel.content.find("`[:nh][:dv ap 200 pr 150 br 40]`") != std::string::npos);
    latibot::testing::check_message_fits(panel);
}

TEST_CASE("an untouched voice says so, and a note shows once under it", "[commands]") {
    voice_draft draft;
    draft.note = "saved as `x`";
    const dpp::message panel = render_voice_lab(draft);

    CHECK(panel.content.find("No changes yet: this is paul as DECtalk has them.") != std::string::npos);
    CHECK(panel.content.find("`[:np]`") != std::string::npos);
    CHECK(panel.content.ends_with("\n-# saved as `x`"));
    latibot::testing::check_message_fits(panel);
}

TEST_CASE("the voice lab says which voice it is editing, and whether it still matches what is saved", "[commands]") {
    const voice_draft draft = edited_draft();

    SECTION("a new voice") {
        const dpp::message panel = render_voice_lab(voice_draft{});
        CHECK(panel.content.starts_with("**Voice lab**: editing a new voice, not saved yet\n"));
    }
    SECTION("a saved voice, unchanged") {
        const std::vector<latibot::audio::saved_voice> kept{saved("robo", draft.voice)};
        CHECK(render_voice_lab(draft, kept).content.starts_with("**Voice lab**: editing `robo`, as saved\n"));
    }
    SECTION("a saved voice, changed since") {
        voice_draft changed = draft;
        changed.voice.set("ap", 90);
        const std::vector<latibot::audio::saved_voice> kept{saved("robo", draft.voice)};
        CHECK(render_voice_lab(changed, kept).content.find("editing `robo`, with **unsaved changes**") != std::string::npos);
    }
    SECTION("a saved voice someone has deleted") {
        CHECK(render_voice_lab(draft, {}).content.find("`robo`, which is no longer saved") != std::string::npos);
    }
}

TEST_CASE("the voice lab offers the server's saved voices, the one being edited picked", "[commands]") {
    const voice_draft draft = edited_draft();

    SECTION("none saved, no menu") {
        CHECK(render_voice_lab(draft, {}).components.size() == 3);
    }

    SECTION("some saved") {
        const std::vector<latibot::audio::saved_voice> kept{saved("alto", {}), saved("robo", draft.voice)};
        const dpp::message panel = render_voice_lab(draft, kept);
        latibot::testing::check_message_fits(panel);
        REQUIRE(panel.components.size() == 4);

        const dpp::component& menu = panel.components[0].components[0];
        CHECK(latibot::ui::decode(menu.custom_id)->view == latibot::commands::lab_open_view);
        REQUIRE(menu.options.size() == 2);
        CHECK(menu.options[0].value == "alto");
        CHECK_FALSE(menu.options[0].is_default);
        CHECK(menu.options[1].value == "robo");
        CHECK(menu.options[1].is_default);
        CHECK(menu.options[1].description == "[:nh][:dv ap 200 pr 150 br 40]");
    }

    SECTION("more than a menu holds, the one being edited still among them") {
        std::vector<latibot::audio::saved_voice> kept;
        kept.reserve(41);
        for (int index = 0; index < 40; ++index) {
            kept.push_back(saved(std::format("voice{:02}", index), {}));
        }
        kept.push_back(saved("robo", draft.voice));

        const dpp::message panel = render_voice_lab(draft, kept);
        latibot::testing::check_message_fits(panel);
        const dpp::component& menu = panel.components[0].components[0];
        CHECK(menu.options.size() == latibot::commands::saved_voices_offered);
        CHECK(std::ranges::any_of(menu.options,
                                  [](const dpp::select_option& option) { return option.value == "robo" && option.is_default; }));
        CHECK(menu.placeholder.find("25 of 41") != std::string::npos);
    }
}

TEST_CASE("every voice lab form fits in a modal", "[commands]") {
    const voice_draft draft = edited_draft();
    const auto groups = latibot::audio::voice_parameter_groups();
    for (std::size_t index = 0; index < groups.size(); ++index) {
        const auto form = voice_lab_form(std::to_string(index), draft);
        REQUIRE(form.has_value());
        latibot::testing::check_modal_fits(*form);
    }

    const auto raw = voice_lab_form("raw", draft);
    REQUIRE(raw.has_value());
    latibot::testing::check_modal_fits(*raw);
    latibot::testing::check_modal_fits(latibot::commands::voice_name_form(draft));

    CHECK_FALSE(voice_lab_form("99", draft).has_value());
    CHECK_FALSE(voice_lab_form("nonsense", draft).has_value());
}

TEST_CASE("a group's form sets, clears and clamps its parameters", "[commands]") {
    voice_draft draft = edited_draft();

    apply_voice_form(draft, "0", fields{{"ap", "120"}, {"pr", ""}, {"hr", "999"}, {"sr", "fast"}});

    CHECK(draft.voice.dv_parameters() == "ap 120 hr 100 br 40");
    CHECK(draft.note == "hr goes from 2 to 100, so 999 became 100; sr needs a number, not \"fast\"");
}

TEST_CASE("the raw form replaces the whole voice", "[commands]") {
    voice_draft draft = edited_draft();

    apply_voice_form(draft, "raw", fields{{"raw", "[:nk][:dv hs 80]"}});

    CHECK(draft.voice.base == "kit");
    CHECK(draft.voice.dv_parameters() == "hs 80");
    CHECK(draft.note.empty());
}

TEST_CASE("a raw form that came back without its field leaves the voice alone", "[commands]") {
    // Read as "no edits", it would wipe the voice, which is what an unread
    // form once did.
    voice_draft draft = edited_draft();

    apply_voice_form(draft, "raw", fields{});

    CHECK(draft.voice == edited_draft().voice);
    CHECK_FALSE(draft.note.empty());
}

TEST_CASE("only whoever made a voice, or an admin, may change it", "[commands]") {
    using latibot::commands::voice_change_refusal;
    CHECK(voice_change_refusal(alice, alice, false, "robo") == std::nullopt);
    CHECK(voice_change_refusal(alice, dpp::snowflake{12}, true, "robo") == std::nullopt);
    CHECK(voice_change_refusal(alice, dpp::snowflake{12}, false, "robo") == "only whoever made `robo`, or an admin, can change it");
}

TEST_CASE("a draft is kept per person for half an hour after it was last touched", "[commands]") {
    latibot::testing::mock_clock clock;
    latibot::commands::voice_drafts drafts(clock);

    drafts.put(guild, alice, edited_draft());
    clock.advance(29min);
    CHECK(drafts.get(guild, alice).name == "robo");
    CHECK(drafts.get(guild, dpp::snowflake{12}).name.empty());  // someone else's is their own
    CHECK(drafts.get(dpp::snowflake{200}, alice).name.empty()); // and per guild

    drafts.put(guild, alice, drafts.get(guild, alice)); // touched again
    clock.advance(29min);
    CHECK(drafts.get(guild, alice).name == "robo");

    clock.advance(1min);
    CHECK(drafts.get(guild, alice).name.empty());
}
