#include "core/events/bot_allowlist.hpp"

#include "core/db/database.hpp"

#include <cstdint>

namespace latibot::events {

bool bot_allowlist::allow(dpp::snowflake guild_id, dpp::snowflake bot_id) {
    const auto guard = db_->lock();

    db_->prepare("INSERT OR IGNORE INTO allowed_bots (guild_id, bot_id) VALUES (?, ?)", static_cast<std::uint64_t>(guild_id),
                 static_cast<std::uint64_t>(bot_id))
        .run();

    return db_->changes() > 0;
}

bool bot_allowlist::deny(dpp::snowflake guild_id, dpp::snowflake bot_id) {
    const auto guard = db_->lock();

    db_->prepare("DELETE FROM allowed_bots WHERE guild_id = ? AND bot_id = ?", static_cast<std::uint64_t>(guild_id),
                 static_cast<std::uint64_t>(bot_id))
        .run();

    return db_->changes() > 0;
}

bool bot_allowlist::contains(dpp::snowflake guild_id, dpp::snowflake bot_id) const {
    const auto guard = db_->lock();

    auto query = db_->prepare("SELECT 1 FROM allowed_bots WHERE guild_id = ? AND bot_id = ?", static_cast<std::uint64_t>(guild_id),
                              static_cast<std::uint64_t>(bot_id));
    return query.step();
}

std::vector<dpp::snowflake> bot_allowlist::for_guild(dpp::snowflake guild_id) const {
    const auto guard = db_->lock();

    std::vector<dpp::snowflake> listed;
    auto query = db_->prepare("SELECT bot_id FROM allowed_bots WHERE guild_id = ? ORDER BY bot_id", static_cast<std::uint64_t>(guild_id));
    while (query.step()) {
        listed.emplace_back(static_cast<std::uint64_t>(query.get<std::int64_t>(0)));
    }
    return listed;
}

} // namespace latibot::events
