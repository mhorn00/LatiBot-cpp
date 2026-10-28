#pragma once

#include "core/ports/result.hpp"

#include <dpp/coro/task.h>
#include <dpp/json_fwd.h>
#include <dpp/snowflake.h>

#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::discord {
class raw_api;
}

namespace latibot::soundboard {

// The soundboard sounds the bot can see, and where they come from
// (src/modules/soundboard/docs/Soundboard.md §2).

/// One soundboard sound.
struct sound {
    dpp::snowflake id;
    std::string name;

    /// The server it belongs to; 0 for Discord's own, which every server
    /// has.
    dpp::snowflake guild_id;

    /// The standard emoji shown beside it, or empty. A server's own emoji
    /// cannot be drawn in a command's suggestions, so those are left out.
    std::string emoji;

    /// How loud the soundboard plays it, 0 to 1.
    double volume = 1.0;
};

/// The body that asks Discord to play `played` in a voice channel: its id,
/// and the server it is from, for one that is not Discord's own.
[[nodiscard]] auto play_request(const sound& played) -> nlohmann::json;

/// Reads Discord's list of sounds: `{"items": [...]}` from a server, a bare
/// array for Discord's own. Sounds Discord marks unavailable, which a server
/// that lost its boosts can have, are left out, and so is anything without
/// an id or a name.
[[nodiscard]] auto parse_sounds(const nlohmann::json& listed) -> std::vector<sound>;

/// Where the soundboard's file is: Discord's CDN, as MP3 or Ogg.
[[nodiscard]] auto sound_url(dpp::snowflake sound_id) -> std::string;

/// One suggestion for the `sound` option: what is shown, and what the
/// command is given when it is picked.
struct sound_choice {
    std::string label;
    std::string value;

    auto operator==(const sound_choice&) const -> bool = default;
};

/// How many suggestions Discord shows at most.
inline constexpr std::size_t max_choices = 25;

/// Every sound the bot can see: Discord's own, and each server's, kept by
/// server. Safe from any thread: DPP's events and the command arrive on
/// several.
class sound_catalog {
public:
    auto set_defaults(std::vector<sound> sounds) -> void;

    /// Replaces a server's sounds, and remembers its name, for suggesting
    /// its sounds elsewhere.
    auto set_guild(dpp::snowflake guild, std::string guild_name, std::vector<sound> sounds) -> void;

    /// A server the bot left, or that went away.
    auto forget_guild(dpp::snowflake guild) -> void;

    /// The servers whose sounds it holds, and what each is called.
    [[nodiscard]] auto guilds() const -> std::map<dpp::snowflake, std::string>;

    /// How many sounds it holds in all.
    [[nodiscard]] auto size() const -> std::size_t;

    /// The sound `wanted` names, for someone in `asking_guild`: a sound's id,
    /// which is what a suggestion gives, or its name, ignoring case. A name
    /// several sounds share is the asking server's first, then Discord's,
    /// then another server's.
    [[nodiscard]] auto find(std::string_view wanted, dpp::snowflake asking_guild) const -> std::optional<sound>;

    /// Suggestions for what someone in `asking_guild` has typed, at most
    /// `limit` of them: names starting with it before names only containing
    /// it, and within each, the asking server's sounds, then Discord's, then
    /// other servers', by name. A sound from somewhere else says where.
    [[nodiscard]] auto suggest(std::string_view typed, dpp::snowflake asking_guild, std::size_t limit = max_choices) const
        -> std::vector<sound_choice>;

private:
    struct guild_sounds {
        std::string name;
        std::vector<sound> sounds;
    };

    mutable std::mutex mutex_;
    std::vector<sound> defaults_;
    std::map<dpp::snowflake, guild_sounds> guilds_;
};

/// Where the sounds are read from: Discord in the bot, a mock in tests.
class sound_source {
public:
    virtual ~sound_source() = default;

    sound_source() = default;
    sound_source(const sound_source&) = delete;
    auto operator=(const sound_source&) -> sound_source& = delete;

    /// Discord's own sounds, which every server has.
    virtual auto default_sounds() -> dpp::task<ports::result<std::vector<sound>>> = 0;

    /// One server's sounds. Any member can list them; the bot needs no
    /// permission.
    virtual auto guild_sounds(dpp::snowflake guild) -> dpp::task<ports::result<std::vector<sound>>> = 0;
};

/// `sound_source` through Discord's API, which DPP does not wrap.
class discord_sounds final : public sound_source {
public:
    explicit discord_sounds(discord::raw_api& raw) : raw_(&raw) {}

    auto default_sounds() -> dpp::task<ports::result<std::vector<sound>>> override;
    auto guild_sounds(dpp::snowflake guild) -> dpp::task<ports::result<std::vector<sound>>> override;

private:
    discord::raw_api* raw_;
};

} // namespace latibot::soundboard
