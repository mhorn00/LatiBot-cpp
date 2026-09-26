#pragma once

#include "core/ports/clock.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

namespace latibot::events {

/// A voice session: the bot in a voice channel on someone's request, tied
/// to the text channel it was started from (plan §13).
///
/// While one is active, `/speak` from anywhere in the guild goes to its
/// voice channel, and the language model's replies in its text channel will
/// be spoken as well as posted (phase 5).
struct voice_session {
    dpp::snowflake guild_id;
    dpp::snowflake voice_channel;
    dpp::snowflake text_channel;
    dpp::snowflake started_by;
};

/// The sessions in progress, one per guild at most. Thread-safe.
class voice_sessions {
public:
    /// Starts one, replacing any the guild had.
    auto start(const voice_session& session) -> void;

    /// Ends the guild's session, returning it if there was one.
    auto end(dpp::snowflake guild) -> std::optional<voice_session>;

    [[nodiscard]] auto find(dpp::snowflake guild) const -> std::optional<voice_session>;

    /// The bot was moved to another channel in the guild; the session goes
    /// with it.
    auto moved(dpp::snowflake guild, dpp::snowflake voice_channel) -> void;

private:
    mutable std::mutex mutex_;
    std::map<dpp::snowflake, voice_session> sessions_;
};

/// How long the bot stays in a voice channel with nobody else in it, per
/// guild (plan §13, §20). Long enough that someone dropping out and
/// rejoining does not lose it.
inline constexpr std::string_view voice_grace_key = "voice_grace_seconds";
inline constexpr std::chrono::seconds default_voice_grace{30};

/// How often the shell checks.
inline constexpr std::chrono::seconds auto_leave_tick{5};

/// Notices when the bot has been left on its own in a voice channel, so it
/// can leave rather than sit there for ever. Applies whether it came with
/// `/join` or `/voice start`. Thread-safe.
class auto_leave {
public:
    explicit auto_leave(ports::clock& clock);

    /// What the bot's voice channel in `guild` holds now: `humans` people
    /// other than bots. Called whenever that may have changed. A guild whose
    /// bot is not in voice is forgotten.
    auto observe(dpp::snowflake guild, bool in_voice, int humans) -> void;

    /// The guilds whose bot has been alone for at least `grace_for(guild)`.
    /// Each is returned once and forgotten; the caller leaves.
    [[nodiscard]] auto due(const std::function<std::chrono::seconds(dpp::snowflake)>& grace_for) -> std::vector<dpp::snowflake>;

    /// The bot left, for any reason.
    auto forget(dpp::snowflake guild) -> void;

private:
    ports::clock* clock_;

    std::mutex mutex_;

    /// When the bot was last seen alone, for the guilds where it still is.
    std::map<dpp::snowflake, std::chrono::steady_clock::time_point> alone_since_;
};

} // namespace latibot::events
