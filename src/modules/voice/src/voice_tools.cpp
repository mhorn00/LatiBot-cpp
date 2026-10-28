#include "voice_tools.hpp"

#include "core/capabilities/model_tools.hpp"
#include "join_command.hpp"

#include <dpp/permissions.h>

#include <format>
#include <utility>

namespace latibot::commands {
namespace {

using capabilities::model_tool_context;
using capabilities::model_tool_result;
using capabilities::tool_done;
using capabilities::tool_refused;

auto may_speak(const model_tool_context& context) -> bool {
    return (context.author_permissions & (dpp::p_speak | dpp::p_administrator)) != 0;
}

auto join_voice(const voice_tool_services& services, const nlohmann::json& input, const model_tool_context& context) -> model_tool_result {
    if (!may_speak(context)) return tool_refused("whoever asked needs Speak to move the bot around voice, as /join does");
    dpp::snowflake target = context.author_id;
    if (const auto alias = capabilities::tool_text(input, "user")) {
        const auto found = context.user_for(*alias);
        if (!found) return tool_refused("there is nobody here with that alias");
        target = *found;
    }

    const join_decision decision = plan_join(services.channel_of(context.guild_id, target), services.bot_channel(context.guild_id));
    switch (decision.action) {
    case join_action::target_not_in_voice:
        return tool_refused(std::format("{} is not in a voice channel", context.shown_as(target)));
    case join_action::already_there:
        return tool_done("you are already in that voice channel");
    case join_action::connect:
    case join_action::move:
        if (!services.connect(context.guild_id, decision.channel_id)) return tool_refused("the gateway cannot be reached right now");
        return tool_done(
            std::format("{} {}'s voice channel", decision.action == join_action::move ? "moving to" : "joining", context.shown_as(target)));
    }
    return tool_refused("that cannot be done");
}

auto leave_voice(const voice_tool_services& services, const model_tool_context& context) -> model_tool_result {
    if (!may_speak(context)) return tool_refused("whoever asked needs Speak to make the bot leave voice, as /leave does");
    if (services.bot_channel(context.guild_id).empty()) return tool_refused("you are not in a voice channel");
    if (!services.disconnect(context.guild_id)) return tool_refused("the gateway cannot be reached right now");
    return tool_done("leaving the voice channel");
}

} // namespace

auto add_voice_tools(capabilities::model_tools& tools, voice_tool_services services) -> void {
    tools.add({.name = "join_voice",
               .description = "Join, or move to, the voice channel whoever you are answering is in, or someone else's. Whoever you "
                              "are answering needs Speak.",
               .input_schema = capabilities::tool_schema(
                   {{"user", capabilities::tool_property("The alias of whose channel to join; whoever you are answering when left out.")}}),
               .changes = true,
               .run = [services](nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return join_voice(services, input, context);
               }});

    tools.add({.name = "leave_voice",
               .description = "Leave the voice channel you are in here. Whoever you are answering needs Speak.",
               .input_schema = capabilities::tool_schema(nlohmann::json::object()),
               .changes = true,
               .run = [services = std::move(services)](nlohmann::json, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return leave_voice(services, context);
               }});
}

} // namespace latibot::commands
