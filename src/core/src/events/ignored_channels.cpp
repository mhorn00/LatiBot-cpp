#include "events/ignored_channels.hpp"

#include "core/db/database.hpp"

#include <string>

namespace latibot::events {

auto to_string(channel_scope scope) noexcept -> std::string_view {
    switch (scope) {
    case channel_scope::everything:
        return "everything";
    case channel_scope::model:
        return "llm";
    case channel_scope::bots:
        return "bots";
    }
    return "everything";
}

auto channel_scope_from_string(std::string_view text) noexcept -> std::optional<channel_scope> {
    for (const channel_scope scope : {channel_scope::everything, channel_scope::model, channel_scope::bots}) {
        if (to_string(scope) == text) return scope;
    }
    return std::nullopt;
}

auto apply(channel_rules& rules, channel_scope scope) noexcept -> void {
    switch (scope) {
    case channel_scope::everything:
        rules.everything = true;
        break;
    case channel_scope::model:
        rules.model = true;
        break;
    case channel_scope::bots:
        rules.bots = true;
        break;
    }
}

auto ignored_channels::ignore(dpp::snowflake guild_id, dpp::snowflake channel_id, channel_scope scope) -> bool {
    const auto guard = db_->lock();

    db_->prepare(
           "INSERT INTO ignored_channels (guild_id, channel_id, what) VALUES (?, ?, ?) "
           "ON CONFLICT (guild_id, channel_id) DO UPDATE SET what = excluded.what WHERE what != excluded.what",
           guild_id, channel_id, std::string(to_string(scope)))
        .run();

    return db_->changes() > 0;
}

auto ignored_channels::hear(dpp::snowflake guild_id, dpp::snowflake channel_id) -> bool {
    const auto guard = db_->lock();

    db_->prepare("DELETE FROM ignored_channels WHERE guild_id = ? AND channel_id = ?", guild_id, channel_id).run();

    return db_->changes() > 0;
}

auto ignored_channels::find(dpp::snowflake guild_id, dpp::snowflake channel_id) const -> std::optional<channel_scope> {
    const auto guard = db_->lock();

    auto query = db_->prepare("SELECT what FROM ignored_channels WHERE guild_id = ? AND channel_id = ?", guild_id, channel_id);
    if (!query.step()) return std::nullopt;
    return channel_scope_from_string(query.get<std::string>(0));
}

auto ignored_channels::for_guild(dpp::snowflake guild_id) const -> std::vector<std::pair<dpp::snowflake, channel_scope>> {
    const auto guard = db_->lock();

    std::vector<std::pair<dpp::snowflake, channel_scope>> listed;
    auto query = db_->prepare("SELECT channel_id, what FROM ignored_channels WHERE guild_id = ? ORDER BY channel_id", guild_id);
    while (query.step()) {
        if (const auto scope = channel_scope_from_string(query.get<std::string>(1)))
            listed.emplace_back(query.get<dpp::snowflake>(0), *scope);
    }
    return listed;
}

auto ignored_channels::rules(dpp::snowflake guild_id, dpp::snowflake channel_id, dpp::snowflake parent) const -> channel_rules {
    channel_rules found;
    if (guild_id.empty()) return found;

    const auto guard = db_->lock();

    auto query =
        db_->prepare("SELECT what FROM ignored_channels WHERE guild_id = ? AND channel_id IN (?, ?)", guild_id, channel_id, parent);
    while (query.step()) {
        if (const auto scope = channel_scope_from_string(query.get<std::string>(0))) apply(found, *scope);
    }
    return found;
}

} // namespace latibot::events
