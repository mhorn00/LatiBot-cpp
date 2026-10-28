#include "music_tools.hpp"

#include "core/capabilities/model_tools.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "links.hpp"
#include "media.hpp"
#include "music_player.hpp"
#include "music_queue.hpp"
#include "voice/voice_state.hpp"

#include <dpp/permissions.h>

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace latibot::commands {
namespace {

using capabilities::model_tool_context;
using capabilities::model_tool_result;
using capabilities::tool_done;
using capabilities::tool_refused;

/// How much of the queue the model is shown.
constexpr std::size_t shown_upcoming = 10;

auto may_play(const model_tool_context& context) -> bool {
    return (context.author_permissions & (dpp::p_speak | dpp::p_administrator)) != 0;
}

/// A track as the model is told of it: title, length, who queued it.
auto told(const music::track& entry, const model_tool_context& context) -> std::string {
    std::string length;
    if (entry.media.live) {
        length = ", live";
    } else if (entry.media.duration) {
        length = std::format(", {}", format_duration(*entry.media.duration));
    }
    return std::format("\"{}\"{}, queued by {}", context.sanitize(entry.media.title), length, context.shown_as(entry.requested_by));
}

auto music_queue(const music_tool_services& services, const model_tool_context& context) -> model_tool_result {
    const music::music_status status = services.music.player->status(context.guild_id);
    if (!status.current && status.upcoming.empty()) return tool_done("nothing is playing, and nothing is queued");

    std::string text;
    if (status.current) {
        text = std::format("{} {}, {} in.\n", status.paused ? "Paused:" : "Playing:", told(*status.current, context),
                           format_duration(status.elapsed));
    }
    text += std::format("Repeat: {}.\n", music::to_string(status.repeat));
    if (status.upcoming.empty()) {
        text += "Nothing else is queued.";
    } else {
        text += std::format("Up next, {} track(s):\n", status.upcoming.size());
        for (std::size_t index = 0; index < status.upcoming.size() && index < shown_upcoming; ++index) {
            text += std::format("{}. {}\n", index + 1, told(status.upcoming[index], context));
        }
        if (status.upcoming.size() > shown_upcoming) text += std::format("and {} more", status.upcoming.size() - shown_upcoming);
    }
    return tool_done(text);
}

auto play_music(music_tool_services services, nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
    if (!may_play(context)) co_return tool_refused("whoever asked needs Speak to play music, as /music does");
    if (!services.music.unavailable.empty()) co_return tool_refused(services.music.unavailable);
    const music::checked_link link = music::check_link(capabilities::tool_text(input, "link").value_or(""));
    if (!link.ok()) co_return tool_refused(link.refusal);

    const discord::voice_plan plan =
        discord::plan_voice(services.bot_channel(context.guild_id), services.channel_of(context.guild_id, context.author_id));
    if (plan.route == discord::voice_route::nowhere) co_return tool_refused("you are not in a voice channel, and neither is whoever asked");

    const music::queue_position where =
        music::queue_position_from_string(capabilities::tool_text(input, "position").value_or("end")).value_or(music::queue_position::end);
    const auto lookup = co_await services.music.resolver->lookup(link.url, music::max_playlist);
    if (!lookup.has_value()) co_return tool_refused(std::format("it could not be played: {}", util::plain_text(lookup.error().message)));

    play_report report{.lookup = lookup.value(),
                       .added = {},
                       .where = where,
                       .playing = false,
                       .too_long = 0,
                       .limit = track_limit_for(*services.music.settings, context.guild_id)};
    std::vector<music::track> tracks = playable_tracks(report.lookup, report.limit, context.author_id, context.channel_id, report.too_long);
    if (tracks.empty()) co_return tool_refused(describe_play(report));

    if (plan.route == discord::voice_route::join_caller && !services.connect(context.guild_id, plan.channel)) {
        co_return tool_refused("the gateway cannot be reached right now");
    }
    const std::string first_url = tracks.front().media.url;
    const auto before = services.music.player->status(context.guild_id).current;
    report.added = services.music.player->add(context.guild_id, std::move(tracks), where);
    const auto after = services.music.player->status(context.guild_id).current;
    report.playing = after && (!before || before->id != after->id) && after->media.url == first_url;

    util::log().info("the model queued {} track(s) from {} in guild {}, asked by {}", report.added.added, link.url, context.guild_id,
                     context.author_id);
    // The same words /music play answers with, which the model can pass on.
    co_return tool_done(context.sanitize(describe_play(report)));
}

auto music_control(const music_tool_services& services, const nlohmann::json& input, const model_tool_context& context)
    -> model_tool_result {
    if (!may_play(context)) return tool_refused("whoever asked needs Speak to control the music, as /music does");
    music::music_player& player = *services.music.player;
    const dpp::snowflake guild = context.guild_id;
    const std::string action = capabilities::tool_text(input, "action").value_or("");

    if (action == "pause" || action == "resume") {
        const music::music_status status = player.status(guild);
        if (!status.current) return tool_refused("nothing is playing");
        if (status.paused == (action == "pause"))
            return tool_done(std::format("it is already {}d", action == "pause" ? "pause" : "resume"));
        player.toggle_pause(guild);
        return tool_done(std::format("{}d", action));
    }
    if (action == "skip") {
        const auto skipped = player.skip(guild);
        if (!skipped) return tool_refused("nothing is playing");
        const auto next = player.status(guild).current;
        return tool_done(std::format("skipped \"{}\"{}", context.sanitize(skipped->media.title),
                                     next ? std::format("; now playing \"{}\"", context.sanitize(next->media.title)) : std::string{}));
    }
    if (action == "repeat") {
        const auto wanted = capabilities::tool_text(input, "mode");
        const auto mode = player.set_repeat(guild, wanted ? music::repeat_mode_from_string(*wanted) : std::nullopt);
        return tool_done(std::format("repeat is {}", music::to_string(mode)));
    }
    if (action == "shuffle") {
        const std::size_t shuffled = player.shuffle(guild);
        if (shuffled < 2) return tool_refused("there is nothing queued to shuffle");
        return tool_done(std::format("shuffled {} tracks", shuffled));
    }
    if (action == "clear") {
        const std::size_t cleared = player.clear(guild);
        if (cleared == 0) return tool_refused("the queue is already empty");
        return tool_done(std::format("cleared {} track(s) from the queue; the current one plays on", cleared));
    }
    if (action == "stop") {
        if (player.stop(guild) == 0) return tool_refused("nothing is playing");
        return tool_done("stopped the music and emptied the queue");
    }
    return tool_refused("action is pause, resume, skip, repeat, shuffle, clear or stop");
}

} // namespace

auto add_music_tools(capabilities::model_tools& tools, music_tool_services services) -> void {
    const auto shared = std::make_shared<const music_tool_services>(std::move(services));

    tools.add({.name = "music_queue",
               .description = "What music is playing in voice, how far in, the repeat mode, and what is queued after it.",
               .input_schema = capabilities::tool_schema(nlohmann::json::object()),
               .changes = false,
               .run = [shared](nlohmann::json, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return music_queue(*shared, context);
               }});

    tools.add({.name = "play_music",
               .description = "Play music from a link, as /music play does: a video, a song or a playlist, on any site yt-dlp reads. "
                              "Links only; there is no search. It plays where you are in voice, or joins whoever asked. Whoever asked "
                              "needs Speak.",
               .input_schema = capabilities::tool_schema(
                   {{"link", capabilities::tool_property("The https:// link.")},
                    {"position",
                     capabilities::tool_choice("Where in the queue; end when left out. now plays it at once.", {"end", "next", "now"})}},
                   {"link"}),
               .changes = true,
               .run = [shared](nlohmann::json input, model_tool_context context) {
                   return play_music(*shared, std::move(input), std::move(context));
               }});

    tools.add({.name = "music_control",
               .description = "Control the music, as /music does: pause, resume, skip the current track, set the repeat, shuffle or "
                              "clear the queue, or stop and empty it. Whoever asked needs Speak.",
               .input_schema = capabilities::tool_schema(
                   {{"action", capabilities::tool_choice("What to do.", {"pause", "resume", "skip", "repeat", "shuffle", "clear", "stop"})},
                    {"mode", capabilities::tool_choice("For repeat: off, track or queue; the next one in turn when left out.",
                                                       {"off", "track", "queue"})}},
                   {"action"}),
               .changes = true,
               .run = [shared](nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
                   co_return music_control(*shared, input, context);
               }});
}

} // namespace latibot::commands
