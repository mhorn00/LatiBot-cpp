#include "core/events/bot_allowlist.hpp"

#include "core/db/database.hpp"

#include <cstdint>

namespace latibot::events {

auto bot_allowlist::allow(dpp::snowflake guild_id, dpp::snowflake bot_id) -> bool {
    const auto guard = db_->lock();

    db_->prepare("INSERT OR IGNORE INTO allowed_bots (guild_id, bot_id) VALUES (?, ?)", guild_id, bot_id).run();

    return db_->changes() > 0;
}

auto bot_allowlist::deny(dpp::snowflake guild_id, dpp::snowflake bot_id) -> bool {
    const auto guard = db_->lock();

    db_->prepare("DELETE FROM allowed_bots WHERE guild_id = ? AND bot_id = ?", guild_id, bot_id).run();

    return db_->changes() > 0;
}

auto bot_allowlist::contains(dpp::snowflake guild_id, dpp::snowflake bot_id) const -> bool {
    const auto guard = db_->lock();

    auto query = db_->prepare("SELECT 1 FROM allowed_bots WHERE guild_id = ? AND bot_id = ?", guild_id, bot_id);
    return query.step();
}

auto bot_allowlist::for_guild(dpp::snowflake guild_id) const -> std::vector<dpp::snowflake> {
    const auto guard = db_->lock();

    std::vector<dpp::snowflake> listed;
    auto query = db_->prepare("SELECT bot_id FROM allowed_bots WHERE guild_id = ? ORDER BY bot_id", guild_id);
    while (query.step()) {
        listed.push_back(query.get<dpp::snowflake>(0));
    }
    return listed;
}

} // namespace latibot::events
