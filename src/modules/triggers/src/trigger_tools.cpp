#include "trigger_tools.hpp"

#include "core/capabilities/model_replies.hpp"
#include "core/capabilities/model_tools.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "triggers.hpp"

#include <dpp/permissions.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace latibot::events {
namespace {

using capabilities::model_tool_context;
using capabilities::model_tool_result;
using capabilities::tool_done;
using capabilities::tool_refused;

/// The limits /trigger add has.
constexpr std::size_t pattern_limit = 200;
constexpr std::size_t responses_limit = 2000;
constexpr std::int64_t cooldown_limit = 86400;

/// How much of each response the list shows, and of the whole list.
constexpr std::size_t shown_response = 100;
constexpr std::size_t shown_list = 6000;

auto may_manage(const model_tool_context& context) -> bool {
    return (context.author_permissions & (dpp::p_manage_messages | dpp::p_administrator)) != 0;
}

/// Who answers a model trigger, as the model is told: LatiBot, or a persona
/// by name.
auto answered_by(const trigger& entry, const std::vector<capabilities::persona_name>& personas) -> std::string {
    if (entry.persona_id == 0) return "LatiBot";
    const auto owner = std::ranges::find(personas, entry.persona_id, &capabilities::persona_name::id);
    return owner == personas.end() ? std::string("a persona that is gone") : owner->name;
}

auto list_triggers(const trigger_store& store, const capabilities::model_replies* model, const model_tool_context& context)
    -> model_tool_result {
    const std::vector<trigger> all = store.for_guild(context.guild_id);
    if (all.empty()) return tool_done("this server has no triggers");
    const std::vector<capabilities::persona_name> personas =
        model == nullptr ? std::vector<capabilities::persona_name>{} : model->personas(context.guild_id);

    std::string text = std::format(
        "{} trigger(s), each replying with one of its responses, or by asking the model to answer its prompt, when a message "
        "matches:\n",
        all.size());
    for (const trigger& entry : all) {
        text += std::format("- {}: \"{}\" ({}, every {}{}{}{})", entry.id, context.sanitize(entry.pattern), to_string(entry.mode),
                            entry.cooldown, entry.respond_to_bots ? ", answers bots" : "", entry.reply ? ", as a reply" : "",
                            entry.enabled ? "" : ", off");
        if (entry.asks_model()) {
            text += std::format(" asks the model, {:.0f}% of matches, answered by {}: {}", entry.chance * 100.0,
                                answered_by(entry, personas), context.sanitize(entry.prompt));
        }
        for (const weighted_response& response : entry.responses) {
            text += std::format("\n  → {}", util::truncate(context.sanitize(response.text), shown_response));
        }
        text += '\n';
    }
    return tool_done(util::truncate(text, shown_list));
}

auto add_trigger(trigger_store& store, const capabilities::model_replies* model, const nlohmann::json& input,
                 const model_tool_context& context) -> model_tool_result {
    if (!may_manage(context)) return tool_refused("whoever asked needs Manage Messages here to add a trigger, as /trigger does");
    const auto written = capabilities::tool_text(input, "pattern");
    if (!written) return tool_refused("add_trigger needs a pattern");
    const std::string pattern = context.restore(*written);
    if (util::character_count(pattern) > pattern_limit)
        return tool_refused(std::format("a pattern is at most {} characters", pattern_limit));

    std::vector<weighted_response> responses;
    std::size_t length = 0;
    if (const auto given = input.find("responses"); given != input.end() && given->is_array()) {
        for (const nlohmann::json& each : *given) {
            if (!each.is_string() || util::is_blank(each.get<std::string>())) continue;
            responses.push_back({.text = context.restore(each.get<std::string>()), .weight = 1});
            length += util::character_count(responses.back().text) + 1;
        }
    }
    const std::string prompt =
        model == nullptr ? std::string{} : std::string(util::trim(context.restore(capabilities::tool_text(input, "prompt").value_or(""))));
    const std::string question = model == nullptr
                                     ? std::string{}
                                     : std::string(util::trim(context.restore(capabilities::tool_text(input, "question").value_or(""))));
    const bool asks_model = !prompt.empty() || !question.empty();
    if (!responses.empty() && asks_model) return tool_refused("a trigger has responses or asks the model, not both");
    if (responses.empty() && !asks_model) return tool_refused("add_trigger needs at least one response, or a prompt or question");
    if (length > responses_limit) return tool_refused(std::format("the responses are at most {} characters together", responses_limit));

    const match_mode mode =
        match_mode_from_string(capabilities::tool_text(input, "mode").value_or("whole_word")).value_or(match_mode::whole_word);
    std::chrono::seconds fallback = default_trigger_cooldown;
    if (!question.empty()) {
        fallback = std::chrono::seconds{0};
    } else if (!prompt.empty()) {
        fallback = default_model_cooldown;
    }
    const std::int64_t cooldown =
        std::clamp(capabilities::tool_number(input, "cooldown").value_or(fallback.count()), std::int64_t{0}, cooldown_limit);
    const std::int64_t chance = std::clamp<std::int64_t>(capabilities::tool_number(input, "chance").value_or(100), 1, 100);

    trigger entry;
    entry.guild_id = context.guild_id;
    entry.pattern = pattern;
    entry.mode = mode;
    entry.cooldown = std::chrono::seconds{cooldown};
    entry.responses = responses;
    entry.prompt = prompt;
    entry.question = question;
    entry.chance = static_cast<double>(chance) / 100.0;
    // The model's answers have always been replies.
    entry.reply = asks_model;
    if (const auto problem = trigger_problem(entry)) return tool_refused(*problem);

    std::string owner = "LatiBot";
    const std::string named = capabilities::tool_text(input, "persona").value_or("");
    if (model != nullptr && !named.empty() && asks_model && !util::equals_ignoring_case(named, "LatiBot")) {
        for (const capabilities::persona_name& each : model->personas(context.guild_id)) {
            if (!util::equals_ignoring_case(each.name, named)) continue;
            entry.persona_id = each.id;
            owner = each.name;
        }
        if (entry.persona_id == 0) return tool_refused(std::format("there is no persona called {} here", named));
    }

    const std::int64_t id = store.add(entry);
    if (asks_model) {
        util::log().info("trigger {} added in guild {} by the model, asked by {}: pattern=\"{}\", asks the model, answered by {}", id,
                         context.guild_id, context.author_id, pattern, owner);
        return tool_done(std::format("added trigger {}, which asks the model, answered by {}", id, owner));
    }
    util::log().info("trigger {} added in guild {} by the model, asked by {}: pattern=\"{}\" responses={}", id, context.guild_id,
                     context.author_id, pattern, responses.size());
    return tool_done(std::format("added trigger {} with {} response(s)", id, responses.size()));
}

auto remove_trigger(trigger_store& store, const nlohmann::json& input, const model_tool_context& context) -> model_tool_result {
    if (!may_manage(context)) return tool_refused("whoever asked needs Manage Messages here to remove a trigger, as /trigger does");
    const auto id = capabilities::tool_number(input, "id");
    if (!id) return tool_refused("remove_trigger needs the trigger's id, from list_triggers");
    const auto entry = store.find(*id, context.guild_id);
    if (entry && !entry->builtin.empty())
        return tool_refused(std::format("trigger {} is built in: it can be turned off, not removed", *id));
    if (!entry || !store.remove(*id, context.guild_id)) return tool_refused(std::format("there is no trigger {} here", *id));
    util::log().info("trigger {} removed in guild {} by the model, asked by {}", *id, context.guild_id, context.author_id);
    return tool_done(std::format("removed trigger {}", *id));
}

} // namespace

auto add_trigger_tools(capabilities::model_tools& tools, trigger_store& store, const capabilities::model_replies* model) -> void {
    tools.add({.name = "list_triggers",
               .description = "This server's triggers, the bot's automatic replies to phrases (/trigger): each one's id, pattern, "
                              "cooldown, and its responses, or the prompt it asks you or a persona to answer.",
               .input_schema = capabilities::tool_schema(nlohmann::json::object()),
               .changes = false,
               .run = [&store, model](nlohmann::json, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return list_triggers(store, model, context);
               }});

    tools.add(
        {.name = "add_trigger",
         .description = "Add a trigger: when a message matches the pattern, the bot replies with one of the responses, or, given a "
                        "prompt instead, you or a persona answer following it; at most once per cooldown in a channel. Whoever you are "
                        "answering needs Manage Messages.",
         .input_schema = capabilities::tool_schema(
             {{"pattern", capabilities::tool_property(
                              "What to look for, at most 200 characters: literal text, or a PCRE2 regular expression when mode is regex.")},
              {"responses",
               {{"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "One or more replies, 2000 characters together. Or a prompt instead."}}},
              {"prompt", capabilities::tool_property("What you or a persona should say about it, in a line, at most 500 characters. "
                                                     "Or responses instead.")},
              {"question", capabilities::tool_property("Instead of a prompt or responses, for an analysis: a yes-or-no question the check "
                                                       "model is asked about each message the pattern matched; a yes is answered by you "
                                                       "or the persona. The pattern must be words, not a regular expression.")},
              {"persona", capabilities::tool_property("With a prompt or question: a persona's name to answer it; you when left out.")},
              {"chance",
               capabilities::tool_property("With a prompt: percent of matches answered, 1 to 100; 100 when left out.", "integer")},
              {"mode", capabilities::tool_choice(
                           "Whether the pattern must be a whole word, may be anywhere, or is a case-insensitive regular expression; "
                           "whole_word when left out.",
                           {"whole_word", "substring", "regex"})},
              {"cooldown", capabilities::tool_property(
                               "Seconds between replies in a channel, 0 to 86400; 30 when left out, 300 with a prompt.", "integer")}},
             {"pattern"}),
         .changes = true,
         .run = [&store, model](nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
             co_return add_trigger(store, model, input, context);
         }});

    tools.add({.name = "remove_trigger",
               .description = "Delete a trigger by its id, from list_triggers. Whoever you are answering needs Manage Messages.",
               .input_schema = capabilities::tool_schema({{"id", capabilities::tool_property("The trigger's id.", "integer")}}, {"id"}),
               .changes = true,
               .run = [&store](nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return remove_trigger(store, input, context);
               }});
}

} // namespace latibot::events
