#include "llm_command.hpp"
#include "core/commands/registry.hpp"
#include "settings.hpp"

#include "support/discord_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

using latibot::commands::document_parts;
using latibot::commands::join_document_parts;
using latibot::llm::document_kind;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};

auto default_values() -> std::map<std::string, std::int64_t, std::less<>> {
    std::map<std::string, std::int64_t, std::less<>> values;
    for (const auto& spec : latibot::llm::setting_specs()) {
        values.emplace(spec.key, spec.fallback);
    }
    return values;
}

/// Every name, description and choice a command's options hold, counted as
/// Discord counts them against a command's total. Recursive, and only three
/// deep: a group, a subcommand, then plain options.
// NOLINTNEXTLINE(misc-no-recursion)
auto characters_in(const std::vector<dpp::command_option>& options) -> std::size_t {
    std::size_t total = 0;
    for (const dpp::command_option& option : options) {
        total += latibot::util::character_count(option.name) + latibot::util::character_count(option.description);
        for (const auto& choice : option.choices) {
            total += latibot::util::character_count(choice.name);
            if (const auto* text = std::get_if<std::string>(&choice.value)) total += latibot::util::character_count(*text);
        }
        total += characters_in(option.options);
    }
    return total;
}

// NOLINTNEXTLINE(misc-no-recursion)
auto walk_options(const std::vector<dpp::command_option>& options, int depth, int& count) -> void {
    CHECK(options.size() <= 25);
    for (const dpp::command_option& option : options) {
        ++count;
        INFO(option.name);
        CHECK(option.name.size() <= 32);
        CHECK(latibot::util::character_count(option.description) <= 100);
        CHECK(option.choices.size() <= 25);
        for (const auto& choice : option.choices) {
            CHECK(latibot::util::character_count(choice.name) <= 100);
        }
        CHECK(depth <= 2);
        walk_options(option.options, depth + 1, count);
    }
}

} // namespace

TEST_CASE("the personality is open to everyone until an admin narrows it to a role", "[llm]") {
    constexpr dpp::snowflake editors{55};
    const std::vector<dpp::snowflake> none;
    const std::vector<dpp::snowflake> with_role{editors};

    CHECK(latibot::commands::may_edit_personality(false, guild, guild, none));
    CHECK_FALSE(latibot::commands::may_edit_personality(false, guild, editors, none));
    CHECK(latibot::commands::may_edit_personality(false, guild, editors, with_role));
    CHECK(latibot::commands::may_edit_personality(true, guild, editors, none));
}

TEST_CASE("a document is cut into form parts between lines, and joins back the same", "[llm]") {
    std::string text;
    for (int line = 0; line < 300; ++line) {
        text += std::format("line {} of a long personality, which goes on a while\n", line);
    }
    text += "the end";

    const auto parts = document_parts(text);
    REQUIRE(parts.has_value());
    CHECK(parts->size() > 1);
    CHECK(parts->size() <= 5);
    for (const std::string& part : *parts) {
        CHECK(latibot::util::character_count(part) <= 4000);
    }
    CHECK(join_document_parts(*parts) == text);

    // An emptied part in the middle leaves no blank line behind.
    CHECK(join_document_parts(std::vector<std::string>{"one", "", "three"}) == "one\nthree");
}

TEST_CASE("a document too long for a form has no form, and a line longer than a part is cut", "[llm]") {
    CHECK_FALSE(document_parts(std::string(25000, 'x')).has_value());
    CHECK_FALSE(latibot::commands::document_form(document_kind::personality, std::string(25000, 'x')).has_value());

    const auto parts = document_parts(std::string(9000, 'y'));
    REQUIRE(parts.has_value());
    CHECK(parts->size() == 3);
    CHECK(parts->at(0).size() == 4000);
    CHECK(parts->at(2).size() == 1000);
}

TEST_CASE("the document form fits a modal and is filled with the current text", "[llm]") {
    const auto form = latibot::commands::document_form(document_kind::system, "be nice");
    REQUIRE(form.has_value());
    latibot::testing::check_modal_fits(*form);
    CHECK(form->components.size() == 5);
    const auto* filled = std::get_if<std::string>(&form->components[0][0].value);
    REQUIRE(filled != nullptr);
    CHECK(*filled == "be nice");
    CHECK(form->custom_id == "llmdoc:0:system");
}

TEST_CASE("a document is shown inline when short, and attached when not", "[llm]") {
    const latibot::llm::document_version short_one{.version = 3, .content = "be nice", .edited_by = {}, .edited_at = {}, .note = {}};
    const dpp::message inline_message = latibot::commands::render_document(document_kind::personality, short_one, false);
    CHECK(inline_message.content == "The personality, version 3:\n```\nbe nice\n```");
    CHECK(inline_message.file_data.empty());
    latibot::testing::check_message_fits(inline_message);

    const latibot::llm::document_version long_one{
        .version = 4, .content = std::string(3000, 'z'), .edited_by = {}, .edited_at = {}, .note = {}};
    const dpp::message attached = latibot::commands::render_document(document_kind::personality, long_one, false);
    CHECK(attached.file_data.size() == 1);
    latibot::testing::check_message_fits(attached);
}

TEST_CASE("saving a large document warns that it is sent with every message", "[llm]") {
    CHECK(latibot::commands::describe_saved(document_kind::personality, 2, "short") ==
          "saved the personality as version 2 (about 2 tokens)");
    CHECK(latibot::commands::describe_saved(document_kind::system, 5, std::string(8000, 'a')).contains("sent with every message"));
}

TEST_CASE("history lists the newest versions first, with who and when", "[llm]") {
    const std::vector<latibot::llm::document_version> versions{
        {.version = 2,
         .content = "b",
         .edited_by = dpp::snowflake{7},
         .edited_at = std::chrono::sys_seconds{100s},
         .note = "reverted to version 0"},
        {.version = 1, .content = "a", .edited_by = dpp::snowflake{8}, .edited_at = std::chrono::sys_seconds{50s}, .note = {}}};
    const std::string text = latibot::commands::render_history(document_kind::personality, versions);
    CHECK(text.contains("`v2` <t:100:f> by <@7>, 1 characters (reverted to version 0)"));
    CHECK(text.find("`v2`") < text.find("`v1`"));
    CHECK(latibot::commands::render_history(document_kind::personality, {}).contains("default"));
}

TEST_CASE("the settings panel shows every setting and fits a message", "[llm]") {
    const dpp::message panel = latibot::commands::render_llm_settings(default_values(), true, false);
    latibot::testing::check_message_fits(panel);
    for (const auto& spec : latibot::llm::setting_specs()) {
        CHECK(panel.content.contains(spec.label));
    }
    CHECK(panel.content.contains("the model is **on** here, and conversation mode is **off**"));
}

TEST_CASE("each settings form fits a modal and is filled with the current values", "[llm]") {
    for (const std::string_view group : latibot::llm::setting_groups()) {
        const auto form = latibot::commands::llm_settings_form(group, default_values());
        REQUIRE(form.has_value());
        latibot::testing::check_modal_fits(*form);
    }
    CHECK_FALSE(latibot::commands::llm_settings_form("nope", default_values()).has_value());
}

TEST_CASE("a settings form is stored whole or not at all, naming what was out of range", "[llm]") {
    const std::map<std::string, std::string, std::less<>> good{
        {"llm_context_messages", "20"}, {"llm_context_tokens", "4000"}, {"llm_trigger_context", "3"}};
    const auto stored = latibot::commands::read_llm_settings_form("context", good);
    const auto* changes = std::get_if<std::vector<std::pair<std::string_view, std::int64_t>>>(&stored);
    REQUIRE(changes != nullptr);
    CHECK(changes->size() == 3);

    std::map<std::string, std::string, std::less<>> bad = good;
    bad["llm_context_tokens"] = "5";
    const auto refused = latibot::commands::read_llm_settings_form("context", bad);
    const auto* reason = std::get_if<std::string>(&refused);
    REQUIRE(reason != nullptr);
    CHECK(*reason == "\"Token budget for them\" takes a whole number from 200 to 20000; nothing was changed");
}

TEST_CASE("the memory list pages ten at a time, carrying whose list it is", "[llm]") {
    std::vector<latibot::llm::memory> page;
    page.reserve(10);
    for (int index = 0; index < 10; ++index) {
        page.push_back(
            {.id = index + 1, .guild_id = guild, .subject = dpp::snowflake{7}, .content = "likes tea", .created_by = {}, .created_at = {}});
    }
    const dpp::message shown = latibot::commands::render_memories(page, 25, 0, dpp::snowflake{7});
    latibot::testing::check_message_fits(shown);
    REQUIRE(shown.components.size() == 1);
    CHECK(shown.components[0].components[1].custom_id == "memlist:1:7");

    const dpp::message everyone = latibot::commands::render_memories(page, 10, 0, std::nullopt);
    CHECK(everyone.components.empty());
    CHECK(everyone.content.contains("(<@7>)"));
}

TEST_CASE("the status says what was spent against the caps", "[llm]") {
    latibot::commands::llm_overview overview;
    overview.enabled = true;
    overview.model = "claude-haiku-4-5";
    overview.has_key = true;
    overview.spend = {.today = 0.5, .this_month = 3.25, .over_daily = false, .over_monthly = false, .period = {}};
    overview.caps = {.daily = 2.0, .monthly = 20.0};
    overview.guild_this_month = 1.0;
    CHECK(latibot::commands::render_llm_status(overview) ==
          "The language model is **on** here, using Claude Haiku 4.5.\n"
          "Spent today: $0.50 of $2.00. This month: $3.25 of $20.00, $1.00 of it here.");

    overview.conversation = true;
    CHECK(latibot::commands::render_llm_status(overview).contains("Conversation mode is **on**"));
}

TEST_CASE("the llm and memory commands register, within Discord's limits", "[llm]") {
    latibot::commands::registry commands;
    const latibot::commands::llm_command_services none{};
    REQUIRE_NOTHROW(commands.add(std::make_unique<latibot::commands::llm_command>(none)));
    REQUIRE_NOTHROW(commands.add(std::make_unique<latibot::commands::memory_command>(none)));

    for (const dpp::slashcommand& payload : commands.build_all(dpp::snowflake{1})) {
        INFO(payload.name);
        int count = 0;
        walk_options(payload.options, 0, count);
        CHECK(latibot::util::character_count(payload.description) <= 100);

        // Discord refuses a command whose names, descriptions and choices
        // add up to more than this, and /llm is the biggest there is.
        const std::size_t total = latibot::util::character_count(payload.name) + latibot::util::character_count(payload.description) +
                                  characters_in(payload.options);
        INFO("characters: " << total);
        CHECK(total <= 8000);
    }
}
