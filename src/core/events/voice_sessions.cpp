#include "core/events/voice_sessions.hpp"

namespace latibot::events {

auto voice_sessions::start(const voice_session& session) -> void {
    const std::scoped_lock lock(mutex_);
    sessions_.insert_or_assign(session.guild_id, session);
}

auto voice_sessions::end(dpp::snowflake guild) -> std::optional<voice_session> {
    const std::scoped_lock lock(mutex_);
    const auto found = sessions_.find(guild);
    if (found == sessions_.end()) return std::nullopt;
    voice_session ended = found->second;
    sessions_.erase(found);
    return ended;
}

auto voice_sessions::find(dpp::snowflake guild) const -> std::optional<voice_session> {
    const std::scoped_lock lock(mutex_);
    const auto found = sessions_.find(guild);
    if (found == sessions_.end()) return std::nullopt;
    return found->second;
}

auto voice_sessions::moved(dpp::snowflake guild, dpp::snowflake voice_channel) -> void {
    const std::scoped_lock lock(mutex_);
    const auto found = sessions_.find(guild);
    if (found != sessions_.end()) found->second.voice_channel = voice_channel;
}

auto_leave::auto_leave(ports::clock& clock) : clock_(&clock) {}

auto auto_leave::observe(dpp::snowflake guild, bool in_voice, int humans) -> void {
    const std::scoped_lock lock(mutex_);
    if (!in_voice || humans > 0) {
        alone_since_.erase(guild);
        return;
    }
    // Only the first sighting counts: being alone again a moment later must
    // not restart the wait.
    alone_since_.try_emplace(guild, clock_->steady_now());
}

auto auto_leave::due(const std::function<std::chrono::seconds(dpp::snowflake)>& grace_for) -> std::vector<dpp::snowflake> {
    const std::scoped_lock lock(mutex_);
    const auto now = clock_->steady_now();

    std::vector<dpp::snowflake> leaving;
    for (auto at = alone_since_.begin(); at != alone_since_.end();) {
        if (now - at->second >= grace_for(at->first)) {
            leaving.push_back(at->first);
            at = alone_since_.erase(at);
        } else {
            ++at;
        }
    }
    return leaving;
}

auto auto_leave::forget(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    alone_since_.erase(guild);
}

} // namespace latibot::events
