#include "trigger_tools.hpp"

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

auto list_triggers(const trigger_store& store, const model_tool_context& context) -> model_tool_result {
    const std::vector<trigger> all = store.for_guild(context.guild_id);
    if (all.empty()) return tool_done("this server has no triggers");

    std::string text = std::format("{} trigger(s), each replying with one of its responses when a message matches:\n", all.size());
    for (const trigger& entry : all) {
        text += std::format("- {}: \"{}\" ({}, every {}{}{})", entry.id, context.sanitize(entry.pattern), to_string(entry.mode),
                            entry.cooldown, entry.respond_to_bots ? ", answers bots" : "", entry.enabled ? "" : ", off");
        for (const weighted_response& response : entry.responses) {
            text += std::format("\n  → {}", util::truncate(context.sanitize(response.text), shown_response));
        }
        text += '\n';
    }
    return tool_done(util::truncate(text, shown_list));
}

auto add_trigger(trigger_store& store, const nlohmann::json& input, const model_tool_context& context) -> model_tool_result {
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
    if (responses.empty()) return tool_refused("add_trigger needs at least one response");
    if (length > responses_limit) return tool_refused(std::format("the responses are at most {} characters together", responses_limit));

    const match_mode mode =
        match_mode_from_string(capabilities::tool_text(input, "mode").value_or("whole_word")).value_or(match_mode::whole_word);
    if (const auto problem = util::pattern_error(pattern, mode)) return tool_refused(std::format("that pattern won't work: {}", *problem));
    const std::int64_t cooldown = std::clamp(capabilities::tool_number(input, "cooldown").value_or(default_trigger_cooldown.count()),
                                             std::int64_t{0}, cooldown_limit);

    const std::int64_t id = store.add({.guild_id = context.guild_id,
                                       .pattern = pattern,
                                       .mode = mode,
                                       .cooldown = std::chrono::seconds{cooldown},
                                       .enabled = true,
                                       .respond_to_bots = false,
                                       .message_flags = trigger{}.message_flags,
                                       .responses = responses});
    util::log().info("trigger {} added in guild {} by the model, asked by {}: pattern=\"{}\" responses={}", id, context.guild_id,
                     context.author_id, pattern, responses.size());
    return tool_done(std::format("added trigger {} with {} response(s)", id, responses.size()));
}

auto remove_trigger(trigger_store& store, const nlohmann::json& input, const model_tool_context& context) -> model_tool_result {
    if (!may_manage(context)) return tool_refused("whoever asked needs Manage Messages here to remove a trigger, as /trigger does");
    const auto id = capabilities::tool_number(input, "id");
    if (!id) return tool_refused("remove_trigger needs the trigger's id, from list_triggers");
    if (!store.remove(*id, context.guild_id)) return tool_refused(std::format("there is no trigger {} here", *id));
    util::log().info("trigger {} removed in guild {} by the model, asked by {}", *id, context.guild_id, context.author_id);
    return tool_done(std::format("removed trigger {}", *id));
}

} // namespace

auto add_trigger_tools(capabilities::model_tools& tools, trigger_store& store) -> void {
    tools.add({.name = "list_triggers",
               .description = "This server's triggers, the bot's automatic replies to phrases (/trigger): each one's id, pattern, "
                              "responses and cooldown.",
               .input_schema = capabilities::tool_schema(nlohmann::json::object()),
               .changes = false,
               .run = [&store](nlohmann::json, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return list_triggers(store, context);
               }});

    tools.add(
        {.name = "add_trigger",
         .description = "Add a trigger: when a message matches the pattern, the bot replies with one of the responses, at most once per "
                        "cooldown in a channel. Whoever you are answering needs Manage Messages.",
         .input_schema = capabilities::tool_schema(
             {{"pattern", capabilities::tool_property(
                              "What to look for, at most 200 characters: literal text, or a PCRE2 regular expression when mode is regex.")},
              {"responses",
               {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "One or more replies, 2000 characters together."}}},
              {"mode", capabilities::tool_choice(
                           "Whether the pattern must be a whole word, may be anywhere, or is a case-insensitive regular expression; "
                           "whole_word when left out.",
                           {"whole_word", "substring", "regex"})},
              {"cooldown", capabilities::tool_property("Seconds between replies in a channel, 0 to 86400; 30 when left out.", "integer")}},
             {"pattern", "responses"}),
         .changes = true,
         .run = [&store](nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
             co_return add_trigger(store, input, context);
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
