// The voice lab's panel and forms (plan §12.6).

#include "core/commands/voice_lab.hpp"

#include "mocks/mock_clock.hpp"
#include "support/discord_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <map>
#include <string>

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

} // namespace

TEST_CASE("the voice lab shows the voice as groups and as inline commands", "[commands]") {
    const dpp::message panel = render_voice_lab(edited_draft());

    CHECK(panel.content.find("built on harry (Huge Harry), saved as `robo`") != std::string::npos);
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
