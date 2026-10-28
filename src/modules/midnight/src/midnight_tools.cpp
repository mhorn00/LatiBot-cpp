#include "midnight_tools.hpp"

#include "core/capabilities/model_tools.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "midnight.hpp"

#include <dpp/permissions.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace latibot::events {
namespace {

using capabilities::model_tool_context;
using capabilities::model_tool_result;
using capabilities::tool_done;
using capabilities::tool_refused;

/// Discord's limit on a message.
constexpr std::size_t message_limit = 2000;

auto may_manage(const model_tool_context& context) -> bool {
    return (context.author_permissions & (dpp::p_manage_guild | dpp::p_administrator)) != 0;
}

auto as_time_point(std::chrono::sys_seconds at) -> std::chrono::system_clock::time_point {
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(at);
}

/// The channel the model named, or the one being answered in.
auto channel_in(const nlohmann::json& input, const model_tool_context& context) -> std::optional<dpp::snowflake> {
    const auto named = capabilities::tool_text(input, "channel");
    return named ? context.channel_for(*named) : std::optional(context.channel_id);
}

auto list_midnight(const midnight_store& store, const model_tool_context& context) -> model_tool_result {
    const std::vector<midnight_entry> all = store.for_guild(context.guild_id);
    if (all.empty()) return tool_done("nothing posts at midnight here");
    std::string text = std::format("{} midnight message(s), each posted once a day just after midnight in its time zone:\n", all.size());
    for (const midnight_entry& entry : all) {
        text += std::format("- {}: in {}, at midnight in {}{}{}: {}\n", entry.id, context.sanitize(std::format("<#{}>", entry.channel_id)),
                            entry.timezone, entry.enabled ? "" : ", off",
                            entry.last_fired_date.empty() ? std::string{} : std::format(", last posted {}", entry.last_fired_date),
                            context.sanitize(entry.message));
    }
    return tool_done(text);
}

auto add_midnight(midnight_store& store, const nlohmann::json& input, const model_tool_context& context) -> model_tool_result {
    if (!may_manage(context)) return tool_refused("whoever asked needs Manage Server to add a midnight message, as /midnight does");
    const auto timezone = capabilities::tool_text(input, "timezone");
    if (!timezone || !is_known_timezone(*timezone)) {
        return tool_refused("add_midnight needs a time zone by its IANA name, like America/Chicago or Europe/London");
    }
    const auto channel = channel_in(input, context);
    if (!channel) return tool_refused("there is no such channel here");
    const auto written = capabilities::tool_text(input, "message");
    if (!written) return tool_refused("add_midnight needs the message to post");
    const std::string message = context.restore(*written);
    if (util::character_count(message) > message_limit) return tool_refused("a message is at most 2000 characters");

    // Today counts as posted, so it first posts at the next midnight, as
    // /midnight add does.
    const std::int64_t id = store.add({.id = 0,
                                       .guild_id = context.guild_id,
                                       .channel_id = *channel,
                                       .timezone = *timezone,
                                       .message = message,
                                       .enabled = true,
                                       .last_fired_date = already_posted_today(*timezone, as_time_point(context.now))});
    util::log().info("midnight message {} added in guild {} by the model, asked by {}: {} in channel {}", id, context.guild_id,
                     context.author_id, *timezone, *channel);
    return tool_done(std::format("added midnight message {}: it posts at the next midnight in {}", id, *timezone));
}

auto edit_midnight(midnight_store& store, const nlohmann::json& input, const model_tool_context& context) -> model_tool_result {
    if (!may_manage(context)) return tool_refused("whoever asked needs Manage Server to change a midnight message, as /midnight does");
    const auto id = capabilities::tool_number(input, "id");
    if (!id) return tool_refused("edit_midnight needs the message's id, from list_midnight");
    std::optional<midnight_entry> entry = store.find(*id, context.guild_id);
    if (!entry) return tool_refused(std::format("there is no midnight message {} here", *id));

    if (const auto timezone = capabilities::tool_text(input, "timezone")) {
        if (!is_known_timezone(*timezone))
            return tool_refused(std::format("\"{}\" is not a time zone; use a name like America/Chicago", *timezone));
        entry->timezone = *timezone;
    }
    if (capabilities::tool_text(input, "channel")) {
        const auto channel = channel_in(input, context);
        if (!channel) return tool_refused("there is no such channel here");
        entry->channel_id = *channel;
    }
    if (const auto written = capabilities::tool_text(input, "message")) {
        entry->message = context.restore(*written);
        if (util::character_count(entry->message) > message_limit) return tool_refused("a message is at most 2000 characters");
    }
    entry->enabled = capabilities::tool_flag(input, "enabled").value_or(entry->enabled);

    store.update(*entry);
    util::log().info("midnight message {} in guild {} changed by the model, asked by {}", *id, context.guild_id, context.author_id);
    return tool_done(std::format("midnight message {} is changed{}", *id, entry->enabled ? "" : ", and off"));
}

} // namespace

auto add_midnight_tools(capabilities::model_tools& tools, midnight_store& store) -> void {
    tools.add({.name = "list_midnight",
               .description = "This server's midnight messages (/midnight): each one's id, channel, time zone and text.",
               .input_schema = capabilities::tool_schema(nlohmann::json::object()),
               .changes = false,
               .run = [&store](nlohmann::json, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return list_midnight(store, context);
               }});

    tools.add(
        {.name = "add_midnight",
         .description = "Have the bot post a message every day just after midnight in a time zone, starting at the next one. "
                        "Whoever you are answering needs Manage Server.",
         .input_schema = capabilities::tool_schema({{"timezone", capabilities::tool_property("An IANA time zone, like America/Chicago.")},
                                                    {"message", capabilities::tool_property("What to post, at most 2000 characters.")},
                                                    {"channel", capabilities::tool_property("A channel's name; this one when left out.")}},
                                                   {"timezone", "message"}),
         .changes = true,
         .run = [&store](nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
             co_return add_midnight(store, input, context);
         }});

    tools.add({.name = "edit_midnight",
               .description = "Change a midnight message by its id, from list_midnight: its time zone, channel or text, or turn it on or "
                              "off. What is left out stays. Whoever you are answering needs Manage Server.",
               .input_schema = capabilities::tool_schema(
                   {{"id", capabilities::tool_property("The message's id.", "integer")},
                    {"timezone", capabilities::tool_property("A new IANA time zone.")},
                    {"channel", capabilities::tool_property("A new channel's name.")},
                    {"message", capabilities::tool_property("New text, at most 2000 characters.")},
                    {"enabled", capabilities::tool_property("False to stop it posting, true to start again.", "boolean")}},
                   {"id"}),
               .changes = true,
               .run = [&store](nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return edit_midnight(store, input, context);
               }});
}

} // namespace latibot::events
