#pragma once

#include <dpp/snowflake.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace latibot::db {
class database;
}

namespace latibot::config {

/// The guild id settings that belong to the bot itself are kept under, such
/// as its `/status`. No guild has the id 0.
inline constexpr dpp::snowflake bot_wide{};

/// Per-guild settings, stored in the `guild_settings` table (plan §5.1).
///
/// Values are edited at runtime through commands and panels, so they are read
/// on every use rather than cached: a change takes effect on the next
/// message, with no restart. Each getter takes the default to use when a
/// guild has no row, so a brand new server behaves sensibly.
///
/// Values are stored as text. Numbers and booleans are parsed on read, and a
/// value that cannot be parsed falls back to the caller's default rather than
/// throwing, so one bad row cannot take a feature down.
///
/// The bot uses the text and boolean accessors today. The numeric ones,
/// `erase` and `all` are tested but wait for `/llm settings` (plan §14),
/// the panel of numbers they were written for.
class guild_settings {
public:
    explicit guild_settings(db::database& db) : db_(&db) {}

    [[nodiscard]] auto find(dpp::snowflake guild_id, std::string_view key) const -> std::optional<std::string>;

    [[nodiscard]] auto get(dpp::snowflake guild_id, std::string_view key, std::string_view fallback) const -> std::string;
    [[nodiscard]] auto get_int(dpp::snowflake guild_id, std::string_view key, std::int64_t fallback) const -> std::int64_t;
    [[nodiscard]] auto get_bool(dpp::snowflake guild_id, std::string_view key, bool fallback) const -> bool;
    [[nodiscard]] auto get_real(dpp::snowflake guild_id, std::string_view key, double fallback) const -> double;

    auto set(dpp::snowflake guild_id, std::string_view key, std::string_view value) -> void;
    auto set_int(dpp::snowflake guild_id, std::string_view key, std::int64_t value) -> void;
    auto set_bool(dpp::snowflake guild_id, std::string_view key, bool value) -> void;
    auto set_real(dpp::snowflake guild_id, std::string_view key, double value) -> void;

    /// True when a row was removed.
    auto erase(dpp::snowflake guild_id, std::string_view key) -> bool;

    /// Everything set for one guild, for a settings panel to display.
    [[nodiscard]] auto all(dpp::snowflake guild_id) const -> std::map<std::string, std::string, std::less<>>;

private:
    db::database* db_;
};

} // namespace latibot::config
