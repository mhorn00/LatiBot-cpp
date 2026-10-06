#include "core/commands/music.hpp"

#include "core/commands/options.hpp"
#include "core/commands/speak.hpp"
#include "core/config/guild_settings.hpp"
#include "core/discord/voice_state.hpp"
#include "core/music/links.hpp"
#include "core/ui/interaction.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/cluster.h>
#include <dpp/discordclient.h>
#include <dpp/dispatcher.h>

#include <algorithm>
#include <format>
#include <utility>

namespace latibot::commands {
namespace {

/// Longest title shown in a list, so a page of long ones still fits.
constexpr std::size_t list_title_limit = 80;

auto plural(std::size_t count, std::string_view one, std::string_view many) -> std::string {
    return std::format("{} {}", count, count == 1 ? one : many);
}

/// Who queued a track, as a mention that pings nobody: every music reply
/// is sent with mentions off.
auto queued_by(const music::track& entry) -> std::string {
    return entry.requested_by.empty() ? std::string{} : std::format(", queued by <@{}>", entry.requested_by);
}

auto repeat_note(music::repeat_mode mode) -> std::string {
    switch (mode) {
    case music::repeat_mode::off:
        return {};
    case music::repeat_mode::track:
        return " · repeating this track";
    case music::repeat_mode::queue:
        return " · repeating the queue";
    }
    return {};
}

/// The current track's line: title, where it is, and how it is playing.
auto now_line(const music::music_status& status) -> std::string {
    const music::track& current = *status.current;
    std::string position = format_duration(status.elapsed);
    if (current.media.live) {
        position += " (live)";
    } else if (current.media.duration) {
        position += " / " + format_duration(*current.media.duration);
    }
    return std::format("**Now playing:** {} — {}{}{}{}", util::plain_text(util::truncate(current.media.title, list_title_limit)), position,
                       status.paused ? " · paused" : "", repeat_note(status.repeat), queued_by(current));
}

} // namespace

// --------------------------------------------------------------------------
// Settings
// --------------------------------------------------------------------------

auto music_volume_for(const config::guild_settings& settings, dpp::snowflake guild) -> int {
    const std::int64_t volume = settings.get_int(guild, music_volume_key, default_music_volume);
    return static_cast<int>(std::clamp<std::int64_t>(volume, 0, max_music_volume));
}

auto track_limit_for(const config::guild_settings& settings, dpp::snowflake guild) -> std::optional<std::chrono::seconds> {
    const std::int64_t minutes =
        std::clamp<std::int64_t>(settings.get_int(guild, music_limit_key, default_track_limit_minutes), 0, max_track_limit_minutes);
    if (minutes == 0) return std::nullopt;
    return std::chrono::minutes{minutes};
}

// --------------------------------------------------------------------------
// Rendering
// --------------------------------------------------------------------------

auto format_duration(std::chrono::seconds length) -> std::string {
    const auto total = std::max<std::int64_t>(length.count(), 0);
    const auto hours = total / 3600;
    const auto minutes = (total % 3600) / 60;
    const auto seconds = total % 60;
    if (hours > 0) return std::format("{}:{:02}:{:02}", hours, minutes, seconds);
    return std::format("{}:{:02}", minutes, seconds);
}

auto describe_track(const music::track& entry) -> std::string {
    std::string length;
    if (entry.media.live) {
        length = " (live)";
    } else if (entry.media.duration) {
        length = std::format(" ({})", format_duration(*entry.media.duration));
    }
    return std::format("**{}**{}", util::plain_text(entry.media.title), length);
}

auto playable_tracks(const ports::media_lookup& lookup, std::optional<std::chrono::seconds> limit, dpp::snowflake requested_by,
                     dpp::snowflake channel, std::size_t& too_long) -> std::vector<music::track> {
    too_long = 0;
    std::vector<music::track> tracks;
    for (const ports::media_item& item : lookup.items) {
        if (limit && !item.live && item.duration && *item.duration > *limit) {
            ++too_long;
            continue;
        }
        tracks.push_back({.id = 0, .media = item, .requested_by = requested_by, .channel = channel});
    }
    return tracks;
}

namespace {

/// What happened to a single track.
auto describe_single(const play_report& report) -> std::string {
    const music::track first{.id = 0, .media = report.lookup.items.front(), .requested_by = {}, .channel = {}};
    if (report.playing) {
        return std::format("playing {}{}", describe_track(first), report.where == music::queue_position::now ? " now" : "");
    }
    if (report.where == music::queue_position::next) return std::format("playing {} next", describe_track(first));
    return std::format("queued {}", describe_track(first));
}

/// What happened to a playlist, and how much of it was read.
auto describe_playlist(const play_report& report) -> std::string {
    const std::size_t added = report.added.added;
    const std::string title = util::plain_text(report.lookup.playlist_title);
    std::string text;
    if (added == 0) {
        text = std::format("nothing from **{}** was queued", title);
    } else {
        text = std::format("queued {} from **{}**", plural(added, "track", "tracks"), title);
        if (report.playing) text += report.where == music::queue_position::now ? ", playing now" : ", playing";
        if (!report.playing && report.where == music::queue_position::next) text += ", to play next";
    }
    if (report.lookup.playlist_size > report.lookup.items.size()) {
        text += std::format("; the other {} were left out, since a playlist adds at most {}",
                            report.lookup.playlist_size - report.lookup.items.size(), music::max_playlist);
    }
    return text;
}

} // namespace

auto describe_play(const play_report& report) -> std::string {
    std::string text = report.lookup.is_playlist() ? describe_playlist(report) : describe_single(report);

    if (report.too_long > 0 && report.limit) {
        text += std::format("; {} longer than this server's {}-minute limit {} left out", plural(report.too_long, "track", "tracks"),
                            std::chrono::duration_cast<std::chrono::minutes>(*report.limit).count(), report.too_long == 1 ? "was" : "were");
    }
    if (report.added.over_limit > 0) {
        text += std::format("; the queue is full at {}, so {} didn't fit", music::max_queue,
                            plural(report.added.over_limit, "track", "tracks"));
    }
    return text;
}

auto render_now_playing(const music::music_status& status) -> std::string {
    if (!status.current) return "nothing is playing";
    const music::track& current = *status.current;
    std::string text = now_line(status);
    text += std::format("\n<{}>", current.media.url);
    if (!current.media.uploader.empty()) text += std::format("\nby {}", util::plain_text(current.media.uploader));
    if (!status.upcoming.empty()) text += std::format("\nnext: {}", describe_track(status.upcoming.front()));
    return text;
}

auto render_music_queue(const music::music_status& status, int page) -> dpp::message {
    dpp::message message;
    message.set_allowed_mentions();

    if (!status.current && status.upcoming.empty()) {
        message.set_content("nothing is playing, and the queue is empty");
        return message;
    }

    const std::size_t total = status.upcoming.size();
    const int shown = ui::clamp_page(page, total, music_queue_page);
    const ui::page_range range = ui::range_for(shown, total, music_queue_page);

    std::vector<std::string> lines;
    for (std::size_t index = range.begin; index < range.end; ++index) {
        const music::track& entry = status.upcoming[index];
        std::string length;
        if (entry.media.live) {
            length = " (live)";
        } else if (entry.media.duration) {
            length = std::format(" ({})", format_duration(*entry.media.duration));
        }
        lines.push_back(std::format("`{}.` {}{}{}", index + 1, util::plain_text(util::truncate(entry.media.title, list_title_limit)),
                                    length, queued_by(entry)));
    }

    std::string content = status.current ? now_line(status) : std::string("Nothing is playing.");
    content += "\n\n";
    if (total == 0) {
        content += "Nothing else is queued.";
    } else {
        const std::deque<music::track> upcoming(status.upcoming.begin(), status.upcoming.end());
        const music::running_time time = music::total_time(upcoming);
        content += util::fit_lines(lines, 1500);
        content += std::format("\n\n{} queued, {}", plural(total, "track", "tracks"), format_duration(time.known));
        if (time.unknown > 0) content += std::format(" and {} of unknown length", time.unknown);
        if (total > music_queue_page) content += " · " + ui::page_label(shown, total, music_queue_page);
    }
    message.set_content(content);

    if (auto controls = ui::controls({.view = std::string(music_queue_view), .page = shown, .argument = {}}, total, music_queue_page)) {
        message.add_component(*controls);
    }
    return message;
}

// --------------------------------------------------------------------------
// The command
// --------------------------------------------------------------------------

music_command::music_command(music_services services)
    : info_{.name = "music",
            .description = "Play music in the voice channel.",
            .aliases = {"m"},
            .required_bot_permissions = dpp::p_connect | dpp::p_speak,
            .default_member_permissions = dpp::permission(dpp::p_speak),
            .guild_only = true,
            // What the room hears, the room is told, without a ping
            // (docs/features/Music.md §3.1).
            .responses = {.result = dpp::m_suppress_notifications, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      services_(std::move(services)) {}

auto music_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    dpp::slashcommand payload = command::build(name, application_id);

    dpp::command_option play(dpp::co_sub_command, "play", "Play a link: a song, a video, or a playlist.");
    play.add_option(dpp::command_option(dpp::co_string, "link", "What to play, starting with https://", true).set_max_length(1000));
    play.add_option(dpp::command_option(dpp::co_string, "position", "Where in the queue it goes.", false)
                        .add_choice(dpp::command_option_choice("At the end", std::string("end")))
                        .add_choice(dpp::command_option_choice("Next", std::string("next")))
                        .add_choice(dpp::command_option_choice("Now", std::string("now"))));
    payload.add_option(play);

    payload.add_option(dpp::command_option(dpp::co_sub_command, "queue", "What is playing, and what is queued after it."));
    payload.add_option(dpp::command_option(dpp::co_sub_command, "nowplaying", "What is playing now."));
    payload.add_option(dpp::command_option(dpp::co_sub_command, "pause", "Pause the music, or carry on."));
    payload.add_option(dpp::command_option(dpp::co_sub_command, "skip", "Skip to the next track."));

    dpp::command_option repeat(dpp::co_sub_command, "repeat", "Repeat this track, the whole queue, or nothing.");
    repeat.add_option(dpp::command_option(dpp::co_string, "mode", "Leave it out to go to the next mode.", false)
                          .add_choice(dpp::command_option_choice("Off", std::string("off")))
                          .add_choice(dpp::command_option_choice("This track", std::string("track")))
                          .add_choice(dpp::command_option_choice("The whole queue", std::string("queue"))));
    payload.add_option(repeat);

    payload.add_option(dpp::command_option(dpp::co_sub_command, "shuffle", "Shuffle the queue."));
    payload.add_option(dpp::command_option(dpp::co_sub_command, "clear", "Empty the queue; the current track plays on."));
    payload.add_option(dpp::command_option(dpp::co_sub_command, "stop", "Stop the music and empty the queue."));

    dpp::command_option remove(dpp::co_sub_command, "remove", "Take a track out of the queue.");
    remove.add_option(dpp::command_option(dpp::co_integer, "position", "Its number in /music queue.", true).set_min_value(1));
    payload.add_option(remove);

    dpp::command_option volume(dpp::co_sub_command, "volume", "Show the music's volume, or change it (Manage Server).");
    volume.add_option(dpp::command_option(dpp::co_integer, "percent", "0 to 200.", false).set_min_value(0).set_max_value(max_music_volume));
    payload.add_option(volume);

    dpp::command_option limit(dpp::co_sub_command, "limit", "Show the longest a track may play, or change it (Manage Server).");
    limit.add_option(dpp::command_option(dpp::co_integer, "minutes", "0 for no limit. Live streams have none.", false)
                         .set_min_value(0)
                         .set_max_value(max_track_limit_minutes));
    payload.add_option(limit);

    return payload;
}

auto music_command::quiet_result(const dpp::slashcommand_t& event, const std::string& text) const -> dpp::message {
    dpp::message message(text);
    message.set_allowed_mentions();
    return result(event, std::move(message));
}

auto music_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const std::string action = subcommand_path(event.command.get_command_interaction());
    if (action == "play") {
        co_await play(event);
    } else if (action == "volume") {
        co_await volume(event);
    } else if (action == "limit") {
        co_await limit(event);
    } else {
        co_await event.co_reply(answer(action, event));
    }
}

auto music_command::answer(std::string_view action, const dpp::slashcommand_t& event) -> dpp::message {
    const dpp::snowflake guild = event.command.guild_id;
    music::music_player& player = *services_.player;

    if (action == "queue") return result(event, render_music_queue(player.status(guild), 0));
    if (action == "nowplaying") return quiet_result(event, render_now_playing(player.status(guild)));
    if (action == "pause") return pause_or_resume(event);
    if (action == "skip") return skip_track(event);
    if (action == "repeat") return change_repeat(event);
    if (action == "shuffle") {
        const std::size_t shuffled = player.shuffle(guild);
        if (shuffled < 2) return refusal(event, "there's nothing queued to shuffle");
        return quiet_result(event, std::format("shuffled {}", plural(shuffled, "track", "tracks")));
    }
    if (action == "clear") {
        const std::size_t cleared = player.clear(guild);
        if (cleared == 0) return refusal(event, "the queue is already empty");
        return quiet_result(event, std::format("cleared {} from the queue", plural(cleared, "track", "tracks")));
    }
    if (action == "stop") {
        if (player.stop(guild) == 0) return refusal(event, "nothing is playing");
        util::log().info("{} stopped the music in guild {}", describe_user(event.command.get_issuing_user()), guild);
        return quiet_result(event, "stopped the music and emptied the queue");
    }
    if (action == "remove") {
        const auto position = int_option(event, "position").value_or(0);
        const auto removed = player.remove(guild, static_cast<std::size_t>(std::max<std::int64_t>(position, 0)));
        if (!removed) return refusal(event, std::format("there's no track {} in the queue", position));
        return quiet_result(event, std::format("removed {} from the queue", describe_track(*removed)));
    }
    return refusal(event, "i don't know that subcommand");
}

auto music_command::pause_or_resume(const dpp::slashcommand_t& event) -> dpp::message {
    const dpp::snowflake guild = event.command.guild_id;
    const auto current = services_.player->status(guild).current;
    const auto paused = services_.player->toggle_pause(guild);
    if (!paused || !current) return refusal(event, "nothing is playing");

    const std::string_view done = *paused ? "paused" : "resumed";
    util::log().info("{} {} the music in guild {}", describe_user(event.command.get_issuing_user()), done, guild);
    return quiet_result(event, std::format("{} {}", done, describe_track(*current)));
}

auto music_command::skip_track(const dpp::slashcommand_t& event) -> dpp::message {
    const dpp::snowflake guild = event.command.guild_id;
    const auto skipped = services_.player->skip(guild);
    if (!skipped) return refusal(event, "nothing is playing");

    util::log().info("{} skipped \"{}\" in guild {}", describe_user(event.command.get_issuing_user()), skipped->media.title, guild);
    std::string text = std::format("skipped {}", describe_track(*skipped));
    if (const auto next = services_.player->status(guild).current) text += std::format("; now playing {}", describe_track(*next));
    return quiet_result(event, text);
}

auto music_command::change_repeat(const dpp::slashcommand_t& event) -> dpp::message {
    const dpp::snowflake guild = event.command.guild_id;
    const std::string wanted = string_option(event, "mode");
    const auto mode = services_.player->set_repeat(guild, wanted.empty() ? std::nullopt : music::repeat_mode_from_string(wanted));
    switch (mode) {
    case music::repeat_mode::off:
        return quiet_result(event, "repeat is off");
    case music::repeat_mode::track:
        if (const auto current = services_.player->status(guild).current) {
            return quiet_result(event, std::format("repeating {}", describe_track(*current)));
        }
        return quiet_result(event, "repeating each track");
    case music::repeat_mode::queue:
        return quiet_result(event, "repeating the whole queue");
    }
    return quiet_result(event, "repeat is off");
}

auto music_command::play(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const dpp::snowflake caller = event.command.get_issuing_user().id;

    if (!services_.unavailable.empty()) {
        co_await event.co_reply(refusal(event, services_.unavailable));
        co_return;
    }

    const music::checked_link link = music::check_link(string_option(event, "link"));
    if (!link.ok()) {
        co_await event.co_reply(refusal(event, link.refusal));
        co_return;
    }

    // Where it plays: wherever the bot is, or else the caller's channel.
    // Anyone may queue from anywhere once the bot is in one.
    dpp::discord_client* shard = event.from();
    const speak_plan plan = plan_speak(discord::bot_voice_channel(shard, guild), discord::voice_channel_of(guild, caller));
    if (plan.route == speak_route::nowhere) {
        co_await event.co_reply(refusal(event, "i'm not in a voice channel, and neither are you"));
        co_return;
    }
    if (plan.route == speak_route::join_caller && shard == nullptr) {
        co_await event.co_reply(refusal(event, "i can't reach the gateway right now"));
        co_return;
    }

    const music::queue_position where =
        music::queue_position_from_string(string_option(event, "position")).value_or(music::queue_position::end);

    // Reading a link takes seconds, far past Discord's three.
    co_await defer(event);
    const auto lookup = co_await services_.resolver->lookup(link.url, music::max_playlist);
    if (!lookup.has_value()) {
        util::log().info("/music play from {} in guild {}: couldn't read {}: {}", describe_user(event.command.get_issuing_user()), guild,
                         link.url, lookup.error().message);
        co_await answer_deferred(event,
                                 quiet_result(event, std::format("couldn't play that: {}", util::plain_text(lookup.error().message))));
        co_return;
    }

    play_report report{.lookup = lookup.value(),
                       .added = {},
                       .where = where,
                       .playing = false,
                       .too_long = 0,
                       .limit = track_limit_for(*services_.settings, guild)};
    std::vector<music::track> tracks = playable_tracks(report.lookup, report.limit, caller, event.command.channel_id, report.too_long);
    if (tracks.empty()) {
        co_await answer_deferred(event, quiet_result(event, describe_play(report)));
        co_return;
    }

    if (plan.route == speak_route::join_caller) {
        shard->connect_voice(guild, plan.channel);
        util::log().info("joined voice channel {} in guild {} to play music for {}", plan.channel, guild,
                         describe_user(event.command.get_issuing_user()));
    }

    // Playing now when the first of these became the current track.
    const std::string first_url = tracks.front().media.url;
    const auto before = services_.player->status(guild).current;
    report.added = services_.player->add(guild, std::move(tracks), where);
    const auto after = services_.player->status(guild).current;
    report.playing = after && (!before || before->id != after->id) && after->media.url == first_url;

    util::log().info("{} queued {} track(s) from {} in guild {} ({})", describe_user(event.command.get_issuing_user()), report.added.added,
                     link.url, guild, report.lookup.is_playlist() ? report.lookup.playlist_title : report.lookup.items.front().title);
    co_await answer_deferred(event, quiet_result(event, describe_play(report)));
}

auto music_command::volume(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const auto wanted = int_option(event, "percent");
    if (!wanted) {
        co_await event.co_reply(quiet_result(event, std::format("music plays at {}% here", music_volume_for(*services_.settings, guild))));
        co_return;
    }
    if (!invoker_permissions(event).can(dpp::p_manage_guild)) {
        co_await event.co_reply(refusal(event, "changing the volume needs Manage Server"));
        co_return;
    }
    const auto percent = std::clamp<std::int64_t>(*wanted, 0, max_music_volume);
    services_.settings->set_int(guild, music_volume_key, percent);
    services_.player->volume_changed(guild);
    util::log().info("{} set the music volume in guild {} to {}%", describe_user(event.command.get_issuing_user()), guild, percent);
    co_await event.co_reply(quiet_result(event, std::format("music plays at {}% here now", percent)));
}

auto music_command::limit(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const dpp::snowflake guild = event.command.guild_id;
    const auto wanted = int_option(event, "minutes");
    const auto describe = [](std::optional<std::chrono::seconds> limit) {
        if (!limit) return std::string("tracks can be any length here");
        return std::format("tracks can play for up to {} minutes here; live streams have no limit",
                           std::chrono::duration_cast<std::chrono::minutes>(*limit).count());
    };
    if (!wanted) {
        co_await event.co_reply(quiet_result(event, describe(track_limit_for(*services_.settings, guild))));
        co_return;
    }
    if (!invoker_permissions(event).can(dpp::p_manage_guild)) {
        co_await event.co_reply(refusal(event, "changing the track limit needs Manage Server"));
        co_return;
    }
    services_.settings->set_int(guild, music_limit_key, std::clamp<std::int64_t>(*wanted, 0, max_track_limit_minutes));
    util::log().info("{} set the music track limit in guild {} to {} minute(s)", describe_user(event.command.get_issuing_user()), guild,
                     *wanted);
    co_await event.co_reply(quiet_result(event, describe(track_limit_for(*services_.settings, guild)) + " from the next track on"));
}

auto on_music_component(music::music_player& player, const dpp::interaction_create_t& event, const ui::page_state& state) -> bool {
    if (state.view != music_queue_view) return false;
    ui::update_panel(event, render_music_queue(player.status(event.command.guild_id), state.page));
    return true;
}

} // namespace latibot::commands
