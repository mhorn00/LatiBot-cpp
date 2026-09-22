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

/// Per-guild settings, stored in the `guild_settings` table (plan v4 §5.1).
///
/// Values are edited at runtime through commands and panels, so they are read
/// on every use rather than cached: a change takes effect on the next
/// message, with no restart. Each getter takes the default to use when a
/// guild has no row, so a brand new server behaves sensibly.
///
/// Values are stored as text. Numbers and booleans are parsed on read, and a
/// value that cannot be parsed falls back to the caller's default rather than
/// throwing, so one bad row cannot take a feature down.
class guild_settings {
public:
    explicit guild_settings(db::database& db) : db_(&db) {}

    [[nodiscard]] std::optional<std::string> find(dpp::snowflake guild_id, std::string_view key) const;

    [[nodiscard]] std::string get(dpp::snowflake guild_id, std::string_view key, std::string_view fallback) const;
    [[nodiscard]] std::int64_t get_int(dpp::snowflake guild_id, std::string_view key, std::int64_t fallback) const;
    [[nodiscard]] bool get_bool(dpp::snowflake guild_id, std::string_view key, bool fallback) const;
    [[nodiscard]] double get_real(dpp::snowflake guild_id, std::string_view key, double fallback) const;

    void set(dpp::snowflake guild_id, std::string_view key, std::string_view value);
    void set_int(dpp::snowflake guild_id, std::string_view key, std::int64_t value);
    void set_bool(dpp::snowflake guild_id, std::string_view key, bool value);
    void set_real(dpp::snowflake guild_id, std::string_view key, double value);

    /// True when a row was removed.
    bool erase(dpp::snowflake guild_id, std::string_view key);

    /// Everything set for one guild, for a settings panel to display.
    [[nodiscard]] std::map<std::string, std::string, std::less<>> all(dpp::snowflake guild_id) const;

private:
    db::database* db_;
};

} // namespace latibot::config
