#pragma once

#include "core/commands/registry.hpp"
#include "core/ui/paginator.hpp"
#include "media.hpp"
#include "music_player.hpp"
#include "music_queue.hpp"

#include <dpp/appcommand.h>
#include <dpp/message.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace latibot::config {
class guild_settings;
}

namespace dpp {
struct interaction_create_t;
}

namespace latibot::commands {

// `/music`, alias `/m` (docs/features/Music.md §3).

inline constexpr std::string_view music_volume_key = "music_volume";
inline constexpr std::string_view music_limit_key = "music_track_limit_minutes";

inline constexpr int default_music_volume = 50;
inline constexpr int max_music_volume = 200;

/// An hour, and 0 for none.
inline constexpr std::int64_t default_track_limit_minutes = 60;
inline constexpr std::int64_t max_track_limit_minutes = std::int64_t{24} * 60;

/// Tracks on a page of `/music queue`.
inline constexpr std::size_t music_queue_page = 10;

/// The view name in `/music queue`'s buttons.
inline constexpr std::string_view music_queue_view = "musicq";

/// This server's music volume, 0–200 %.
[[nodiscard]] auto music_volume_for(const config::guild_settings& settings, dpp::snowflake guild) -> int;

/// This server's longest track, or nothing for no limit.
[[nodiscard]] auto track_limit_for(const config::guild_settings& settings, dpp::snowflake guild) -> std::optional<std::chrono::seconds>;

/// `3:07`, or `1:02:03` past an hour.
[[nodiscard]] auto format_duration(std::chrono::seconds length) -> std::string;

/// A track as a reply names it: its title, safe to show, and its length or
/// that it is live.
[[nodiscard]] auto describe_track(const music::track& entry) -> std::string;

/// What `/music play` says, once the link has been read and the tracks
/// added. `playing` is whether the first of them is playing now.
struct play_report {
    ports::media_lookup lookup;
    music::add_result added;
    music::queue_position where = music::queue_position::end;
    bool playing = false;

    /// Left out for being longer than the server's limit.
    std::size_t too_long = 0;
    std::optional<std::chrono::seconds> limit;
};

[[nodiscard]] auto describe_play(const play_report& report) -> std::string;

/// The tracks `/music play` queues: the lookup's, less any longer than
/// `limit`. Live streams have no length, and no limit.
[[nodiscard]] auto playable_tracks(const ports::media_lookup& lookup, std::optional<std::chrono::seconds> limit,
                                   dpp::snowflake requested_by, dpp::snowflake channel, std::size_t& too_long) -> std::vector<music::track>;

/// `/music nowplaying`.
[[nodiscard]] auto render_now_playing(const music::music_status& status) -> std::string;

/// `/music queue`, a page at a time, with ◀ / ▶ when there is more than one.
[[nodiscard]] auto render_music_queue(const music::music_status& status, int page) -> dpp::message;

/// What the music commands need.
struct music_services {
    music::music_player* player = nullptr;
    ports::media_resolver* resolver = nullptr;
    config::guild_settings* settings = nullptr;

    /// Why music cannot play at all, when yt-dlp or ffmpeg could not be
    /// found; empty when both were.
    std::string unavailable;
};

/// `/music play | queue | nowplaying | pause | skip | repeat | shuffle | clear
/// | stop | remove | volume | limit`, and `/m` for short.
///
/// Anyone with Speak may use them, from anywhere in the server
/// (docs/features/Music.md §3.1). Changing the volume or the track limit,
/// which stay, needs Manage Server, checked here since default permissions
/// are per command (docs/features/Commands_and_Panels.md §2.1).
class music_command final : public command {
public:
    explicit music_command(music_services services);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    auto play(const dpp::slashcommand_t& event) -> dpp::task<void>;

    /// The reply to every subcommand but the three that wait on something.
    [[nodiscard]] auto answer(std::string_view action, const dpp::slashcommand_t& event) -> dpp::message;
    [[nodiscard]] auto pause_or_resume(const dpp::slashcommand_t& event) -> dpp::message;
    [[nodiscard]] auto skip_track(const dpp::slashcommand_t& event) -> dpp::message;
    [[nodiscard]] auto change_repeat(const dpp::slashcommand_t& event) -> dpp::message;
    auto volume(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto limit(const dpp::slashcommand_t& event) -> dpp::task<void>;

    /// A reply with no mentions parsed, whatever it quotes.
    [[nodiscard]] auto quiet_result(const dpp::slashcommand_t& event, const std::string& text) const -> dpp::message;

    command_info info_;
    music_services services_;
};

/// `/music queue`'s ◀ / ▶. True when the view was one of these.
auto on_music_component(music::music_player& player, const dpp::interaction_create_t& event, const ui::page_state& state) -> bool;

} // namespace latibot::commands
