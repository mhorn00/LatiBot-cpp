#include "sound_tools.hpp"

#include "clips.hpp"
#include "core/capabilities/model_tools.hpp"
#include "core/util/log.hpp"
#include "sounds.hpp"

#include <dpp/permissions.h>

#include <format>
#include <memory>
#include <utility>

namespace latibot::commands {
namespace {

using capabilities::model_tool_context;
using capabilities::model_tool_result;
using capabilities::tool_done;
using capabilities::tool_refused;

auto play_sound(soundboard_services services, nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
    if ((context.author_permissions & (dpp::p_use_soundboard | dpp::p_administrator)) == 0) {
        co_return tool_refused("whoever asked needs Use Soundboard, as /soundboard does");
    }
    const auto wanted = capabilities::tool_text(input, "sound");
    if (!wanted) co_return tool_refused("play_sound needs the sound's name");
    const auto found = services.sounds->find(*wanted, context.guild_id);
    if (!found) {
        if (services.looked_in) services.looked_in(context.guild_id);
        co_return tool_refused(std::format("there is no soundboard sound called \"{}\" that the bot can see", *wanted));
    }

    // In voice, when the bot is there and not told otherwise.
    const dpp::snowflake channel = services.bot_voice_channel ? services.bot_voice_channel(context.guild_id) : dpp::snowflake{};
    if (!channel.empty() && services.play_in_voice && !capabilities::tool_flag(input, "as_message").value_or(false)) {
        const auto played = co_await services.play_in_voice(channel, *found);
        if (!played.has_value()) co_return tool_refused(std::format("Discord would not play it in voice: {}", played.error().message));
        util::log().info("played soundboard sound {} ({}) in voice channel {} for the model in guild {}", found->id, found->name, channel,
                         context.guild_id);
        co_return tool_done(std::format("playing {} in the voice channel", found->name));
    }

    if (services.clips == nullptr || !services.post_voice) co_return tool_refused("voice messages cannot be sent from here");
    auto made = co_await services.clips->make(*found);
    if (!made.has_value()) co_return tool_refused(std::format("it could not be made a voice message: {}", made.error().message));
    const auto sent = co_await services.post_voice(context.channel_id, std::move(made.value()));
    if (!sent.has_value()) co_return tool_refused(std::format("Discord refused the voice message: {}", sent.error().message));
    util::log().info("sent soundboard sound {} ({}) as a voice message for the model in guild {}", found->id, found->name,
                     context.guild_id);
    co_return tool_done(std::format("posted {} as a voice message, as LatiBot", found->name));
}

} // namespace

auto add_sound_tools(capabilities::model_tools& tools, soundboard_services services) -> void {
    const auto shared = std::make_shared<const soundboard_services>(std::move(services));
    tools.add({.name = "play_sound",
               .description = "Play a soundboard sound: Discord's own, or any server's the bot can see, by name. In the voice "
                              "channel when you are in one here, otherwise as a voice message in this channel, posted as LatiBot.",
               .input_schema = capabilities::tool_schema(
                   {{"sound", capabilities::tool_property("The sound's name, like airhorn or quack.")},
                    {"as_message", capabilities::tool_property("True to send a voice message even when you are in voice.", "boolean")}},
                   {"sound"}),
               .changes = true,
               .run = [shared](nlohmann::json input, model_tool_context context) {
                   return play_sound(*shared, std::move(input), std::move(context));
               }});
}

} // namespace latibot::commands
