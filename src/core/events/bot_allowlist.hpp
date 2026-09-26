#pragma once

#include <dpp/snowflake.h>

#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::events {

/// The bots a guild lets LatiBot hear (plan §5.4, §14.4).
///
/// The pipeline drops every message from a bot that is not listed here, which
/// is what stops two bots triggering each other forever. Being heard is not
/// the same as being answered: each feature still decides whether it wants
/// bot messages, and for triggers that is `trigger::respond_to_bots`.
///
/// The list is per guild and starts empty, so the default is the safe one.
class bot_allowlist {
public:
    explicit bot_allowlist(db::database& db) : db_(&db) {}

    /// False when it was already listed, which the command reports rather
    /// than claiming to have changed something.
    auto allow(dpp::snowflake guild_id, dpp::snowflake bot_id) -> bool;
    auto deny(dpp::snowflake guild_id, dpp::snowflake bot_id) -> bool;

    [[nodiscard]] auto contains(dpp::snowflake guild_id, dpp::snowflake bot_id) const -> bool;
    [[nodiscard]] auto for_guild(dpp::snowflake guild_id) const -> std::vector<dpp::snowflake>;

private:
    db::database* db_;
};

} // namespace latibot::events
