#include "bot_tools.hpp"

#include "core/capabilities/model_tools.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "core/version.hpp"

#include <dpp/permissions.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace latibot::llm {
namespace {

using capabilities::model_tool_context;
using capabilities::model_tool_result;
using capabilities::tool_done;
using capabilities::tool_refused;

/// Discord's limit on a message.
constexpr std::size_t message_limit = 2000;

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
