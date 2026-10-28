#include "bot_tools.hpp"

#include "advanced_triggers.hpp"
#include "core/capabilities/model_tools.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "core/version.hpp"
#include "personas.hpp"

#include <dpp/permissions.h>

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace latibot::llm {
namespace {

using capabilities::model_tool_context;
using capabilities::model_tool_result;
using capabilities::tool_done;
using capabilities::tool_refused;

/// /llm trigger's limits.
constexpr std::size_t pattern_limit = 200;
constexpr std::int64_t cooldown_limit = 86400;

/// Discord's limit on a message.
constexpr std::size_t message_limit = 2000;

auto may_manage(const model_tool_context& context) -> bool {
    return (context.author_permissions & (dpp::p_manage_guild | dpp::p_administrator)) != 0;
}

/// Whose a trigger is, as the model is told: LatiBot, or a persona by name.
auto answered_by(const advanced_trigger& entry, const std::vector<persona>& personas) -> std::string {
    if (entry.persona_id == 0) return "LatiBot";
    const auto owner = std::ranges::find(personas, entry.persona_id, &persona::id);
    return owner == personas.end() ? std::string("a persona that is gone") : owner->name;
}

auto list_advanced_triggers(const bot_tool_services& services, const model_tool_context& context) -> model_tool_result {
    const std::vector<advanced_trigger> all = services.triggers->for_guild(context.guild_id);
    if (all.empty()) return tool_done("this server has no advanced triggers");
    const std::vector<persona> personas =
        services.personas == nullptr ? std::vector<persona>{} : services.personas->for_guild(context.guild_id);

    std::string text = std::format("{} advanced trigger(s), each one a reply from the model when a message matches:\n", all.size());
    for (const advanced_trigger& entry : all) {
        text += std::format("- {}: \"{}\" ({}, {:.0f}% of matches, every {}{}{}), answered by {}: {}\n", entry.id,
                            context.sanitize(entry.pattern), util::to_string(entry.mode), entry.probability * 100.0, entry.cooldown,
                            entry.respond_to_bots ? ", bots too" : "", entry.enabled ? "" : ", off", answered_by(entry, personas),
                            context.sanitize(entry.context_prompt));
    }
    return tool_done(text);
}

auto add_advanced_trigger(const bot_tool_services& services, const nlohmann::json& input, const model_tool_context& context)
    -> model_tool_result {
    if (!may_manage(context)) return tool_refused("whoever asked needs Manage Server to add an advanced trigger, as /llm trigger does");
    const auto pattern = capabilities::tool_text(input, "pattern");
    const auto prompt = capabilities::tool_text(input, "prompt");
    if (!pattern || !prompt) return tool_refused("add_advanced_trigger needs a pattern and a prompt");

    advanced_trigger entry{
        .id = 0,
        .guild_id = context.guild_id,
        .pattern = context.restore(*pattern),
        .mode = util::match_mode_from_string(capabilities::tool_text(input, "mode").value_or("whole_word"))
                    .value_or(util::match_mode::whole_word),
        .context_prompt = context.restore(*prompt),
        .probability =
            static_cast<double>(std::clamp<std::int64_t>(capabilities::tool_number(input, "chance").value_or(100), 1, 100)) / 100.0,
        .cooldown = std::chrono::seconds{std::clamp<std::int64_t>(
            capabilities::tool_number(input, "cooldown").value_or(default_advanced_cooldown.count()), 0, cooldown_limit)},
        .enabled = true,
        .created_by = context.author_id,
        .persona_id = 0,
        .respond_to_bots = false};
    if (util::character_count(entry.pattern) > pattern_limit)
        return tool_refused(std::format("a pattern is at most {} characters", pattern_limit));
    if (util::character_count(entry.context_prompt) > context_prompt_limit) {
        return tool_refused(std::format("a prompt is at most {} characters: a line, not a document", context_prompt_limit));
    }

    std::string owner = "LatiBot";
    if (const auto named = capabilities::tool_text(input, "persona"); named && !util::equals_ignoring_case(*named, "LatiBot")) {
        const auto found = services.personas == nullptr ? std::nullopt : services.personas->find_named(context.guild_id, *named);
        if (!found) return tool_refused(std::format("there is no persona called {} here", *named));
        entry.persona_id = found->id;
        owner = found->name;
    }

    entry.id = services.triggers->add(entry);
    util::log().info("advanced trigger {} added in guild {} by the model, asked by {}: pattern=\"{}\", answered by {}", entry.id,
                     context.guild_id, context.author_id, entry.pattern, owner);
    return tool_done(std::format("added advanced trigger {}, answered by {}", entry.id, owner));
}

auto post_in_channel(bot_tool_services services, nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
    const auto named = capabilities::tool_text(input, "channel");
    const auto written = capabilities::tool_text(input, "text");
    if (!named || !written) co_return tool_refused("post_in_channel needs the channel and the text");
    const auto channel = context.channel_for(*named);
    if (!channel) co_return tool_refused(std::format("there is no channel called {} here", *named));
    if (*channel == context.channel_id) co_return tool_refused("that is this channel: just answer here");

    // The asker's own right to post there, not only the bot's.
    const std::uint64_t theirs = services.discord->member_permissions(context.guild_id, *channel, context.author_id).value_or(0);
    if ((theirs & (dpp::p_send_messages | dpp::p_administrator)) == 0) {
        co_return tool_refused("whoever asked cannot post in that channel themselves, so neither will you for them");
    }
    const std::string text = context.restore(*written);
    if (util::character_count(text) > message_limit) co_return tool_refused("a message is at most 2000 characters");

    dpp::message message(*channel, text);
    message.set_allowed_mentions(false, false, false, false);
    const auto posted = co_await context.post(std::move(message));
    if (!posted.has_value()) co_return tool_refused(std::format("Discord refused: {}", posted.error().message));
    co_return tool_done(std::format("posted it in {}", context.sanitize(std::format("<#{}>", *channel))));
}

auto bot_info(const bot_tool_services& services, const model_tool_context& context) -> model_tool_result {
    const auto up = std::chrono::floor<std::chrono::minutes>(context.now - services.started_at);
    const auto days = std::chrono::floor<std::chrono::days>(up);
    const auto hours = std::chrono::floor<std::chrono::hours>(up - days);
    const auto minutes = up - days - hours;
    return tool_done(std::format("LatiBot {}, up {}d {}h {}m, in {} server(s), gateway latency {}", version_string(), days.count(),
                                 hours.count(), minutes.count(), services.server_count ? services.server_count() : 0,
                                 services.gateway_ping ? services.gateway_ping() : std::chrono::milliseconds{0}));
}

} // namespace

auto add_bot_tools(capabilities::model_tools& tools, bot_tool_services services) -> void {
    const auto shared = std::make_shared<const bot_tool_services>(std::move(services));

    tools.add({.name = "list_advanced_triggers",
               .description = "This server's advanced triggers (/llm trigger): phrases that have you, or a persona, reply with a "
                              "prompt of their own. Each one's id, pattern, chance, cooldown and prompt.",
               .input_schema = capabilities::tool_schema(nlohmann::json::object()),
               .changes = false,
               .run = [shared](nlohmann::json, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return list_advanced_triggers(*shared, context);
               }});

    tools.add({.name = "add_advanced_trigger",
               .description = "Add an advanced trigger: when a message matches the pattern, you, or a persona, reply following the prompt. "
                              "Whoever you are answering needs Manage Server.",
               .input_schema = capabilities::tool_schema(
                   {{"pattern", capabilities::tool_property("The text to look for, at most 200 characters.")},
                    {"prompt", capabilities::tool_property("What to say about it, in a line, at most 500 characters.")},
                    {"persona", capabilities::tool_property("A persona's name to answer it; you when left out.")},
                    {"chance", capabilities::tool_property("Percent of matches it answers, 1 to 100; 100 when left out.", "integer")},
                    {"cooldown", capabilities::tool_property("Seconds between replies in a channel; 300 when left out.", "integer")},
                    {"mode", capabilities::tool_choice("Whether the pattern must be a whole word; whole_word when left out.",
                                                       {"whole_word", "substring"})}},
                   {"pattern", "prompt"}),
               .changes = true,
               .run = [shared](nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return add_advanced_trigger(*shared, input, context);
               }});

    tools.add({.name = "post_in_channel",
               .description = "Post a message in another channel of this server, as yourself, without mentioning anyone, as /say "
                              "does. Whoever you are answering must be able to post there.",
               .input_schema = capabilities::tool_schema({{"channel", capabilities::tool_property("The channel's name, like general.")},
                                                          {"text", capabilities::tool_property("What to post, at most 2000 characters.")}},
                                                         {"channel", "text"}),
               .changes = true,
               .run = [shared](nlohmann::json input, model_tool_context context) {
                   return post_in_channel(*shared, std::move(input), std::move(context));
               }});

    tools.add({.name = "bot_info",
               .description = "Which version of LatiBot is running, how long it has been up, how many servers it is in, and its "
                              "gateway latency.",
               .input_schema = capabilities::tool_schema(nlohmann::json::object()),
               .changes = false,
               .run = [shared](nlohmann::json, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return bot_info(*shared, context);
               }});
}

} // namespace latibot::llm
