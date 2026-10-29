#include "talk_tool.hpp"

#include "aliases.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "personas.hpp"
#include "prompt.hpp"

#include <algorithm>
#include <format>
#include <optional>

namespace latibot::llm {
namespace {

using json = nlohmann::json;

/// The longest message one model sends another: one Discord message.
constexpr std::size_t talk_length_limit = 2000;

auto refuse(std::string reason) -> tool_outcome {
    return {.content = std::move(reason), .is_error = true};
}

auto joined(const std::vector<std::string>& names) -> std::string {
    std::string text;
    for (const std::string& name : names) {
        if (!text.empty()) text += ", ";
        text += name;
    }
    return text.empty() ? std::string("nobody") : text;
}

auto talk_to(const talk_services& services, json input, tool_context context) -> dpp::task<tool_outcome> {
    const auto to = input.find("to");
    const auto said = input.find("message");
    if (to == input.end() || !to->is_string() || said == input.end() || !said->is_string() || util::is_blank(said->get<std::string>())) {
        co_return refuse("talk_to needs whom to message, by name, and the message");
    }

    const std::vector<persona> personas = services.personas->for_guild(context.guild_id);
    const std::vector<std::string> reachable = reachable_by(context.persona_id, personas);
    const std::string wanted(util::trim(to->get<std::string>()));
    const auto name = std::ranges::find_if(reachable, [&](const std::string& each) { return util::equals_ignoring_case(each, wanted); });
    if (name == reachable.end()) co_return refuse(std::format("you can't reach \"{}\"; you can reach {}", wanted, joined(reachable)));

    std::int64_t target = 0;
    if (!util::equals_ignoring_case(*name, model_name)) {
        target = std::ranges::find_if(personas, [&](const persona& each) { return each.name == *name; })->id;
    }
    std::string sender(model_name);
    if (context.persona_id != 0) {
        sender = std::ranges::find(personas, context.persona_id, &persona::id)->name;
    }

    const std::string text(
        util::trim(context.cast == nullptr ? said->get<std::string>() : context.cast->restore(said->get<std::string>())));
    if (util::character_count(text) > talk_length_limit) {
        co_return refuse(std::format("a message is at most {} characters", talk_length_limit));
    }

    const auto turn = services.claim_turn(context.guild_id, context.channel_id);
    if (!turn) co_return refuse(std::format("not now: {}", turn.error()));

    // Noted first: Discord's echo of it can come back before the post does.
    services.expected->expect(context.channel_id, text);
    dpp::message message(context.channel_id, text);
    message.set_allowed_mentions(false, false, false, false);
    const auto posted = co_await services.post(std::move(message), context.persona_id, context.guild_id);
    if (!posted.has_value()) {
        (void)services.expected->take(context.channel_id, text);
        co_return refuse(std::format("could not post it: {}", posted.error().message));
    }

    util::log().info("{} messaged {} in channel {} with talk_to, answering {}", sender, *name, context.channel_id, context.author_id);
    services.hand_off({.guild_id = context.guild_id,
                       .channel_id = context.channel_id,
                       .message_id = posted.value().id,
                       .author_id = posted.value().author.id,
                       .author_name = sender,
                       .content = text,
                       .author_is_bot = true,
                       .reply_to = {},
                       .how = approach::addressed,
                       .check_first = false,
                       .trigger_id = 0,
                       .context_prompt = {},
                       .speak = false,
                       .wait = *turn,
                       .persona_id = target,
                       .speaker = sender});
    co_return tool_outcome{.content = std::format("posted; {} will answer in the channel", *name), .is_error = false};
}

} // namespace

auto handoffs::expect(dpp::snowflake channel, std::string content) -> void {
    const std::scoped_lock lock(mutex_);
    expected_.emplace_back(channel, std::move(content));
    while (expected_.size() > kept) {
        expected_.pop_front();
    }
}

auto handoffs::take(dpp::snowflake channel, std::string_view content) -> bool {
    const std::scoped_lock lock(mutex_);
    const auto found = std::ranges::find_if(expected_, [&](const auto& each) { return each.first == channel && each.second == content; });
    if (found == expected_.end()) return false;
    expected_.erase(found);
    return true;
}

auto handoffs::expects(dpp::snowflake channel, std::string_view content) const -> bool {
    const std::scoped_lock lock(mutex_);
    return std::ranges::any_of(expected_, [&](const auto& each) { return each.first == channel && each.second == content; });
}

auto reachable_by(std::int64_t persona_id, const std::vector<persona>& personas) -> std::vector<std::string> {
    std::vector<std::string> names;
    if (persona_id != 0) {
        const auto self = std::ranges::find(personas, persona_id, &persona::id);
        if (self == personas.end() || !self->talks || !self->enabled) return names;
        names.emplace_back(model_name);
    }
    for (const persona& each : personas) {
        if (each.enabled && each.talks && each.id != persona_id) names.push_back(each.name);
    }
    return names;
}

auto add_talk_tool(tool_registry& tools, talk_services services) -> void {
    tools.add_async(
        {.name = std::string(talk_tool_name),
         .description = "Send a message to LatiBot or another persona, posted in this channel as you; they answer it there, and may "
                        "carry on the conversation. Use it to bring them in, not to talk past the people here. Who you can reach is "
                        "listed with your memories.",
         .input_schema = {{"type", "object"},
                          {"properties",
                           {{"to", {{"type", "string"}, {"description", "LatiBot, or a persona's name."}}},
                            {"message", {{"type", "string"}, {"description", "What to say to them, as a Discord message."}}}}},
                          {"required", json::array({"to", "message"})}}},
        [services](json input, tool_context context) { return talk_to(services, std::move(input), std::move(context)); });
}

} // namespace latibot::llm
