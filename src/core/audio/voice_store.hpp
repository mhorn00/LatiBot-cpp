#pragma once

#include "core/audio/voice_params.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::audio {

/// A custom voice a guild has kept (plan §12.6).
struct saved_voice {
    std::string name;
    custom_voice voice;
    dpp::snowflake created_by;
    std::chrono::sys_seconds updated_at;
};

/// The most voices one guild may keep.
inline constexpr std::size_t max_saved_voices = 100;

/// Why `name` cannot name a custom voice, or nothing when it can. Names are
/// 1 to 32 lowercase letters, digits, '-' and '_', and not a built-in
/// voice's, so `/speak voice:` means one thing.
[[nodiscard]] auto voice_name_refusal(std::string_view name) -> std::optional<std::string>;

/// A name as it is stored: trimmed and lowercase.
[[nodiscard]] auto normalise_voice_name(std::string_view name) -> std::string;

/// Custom voices, per guild. Thread-safe through the database's lock.
class voice_store {
public:
    explicit voice_store(db::database& db) : db_(&db) {}

    /// Keeps `voice` under `name`, replacing one of that name.
    auto save(dpp::snowflake guild, const saved_voice& voice) -> void;

    [[nodiscard]] auto find(dpp::snowflake guild, std::string_view name) const -> std::optional<saved_voice>;

    /// Every voice the guild has, by name.
    [[nodiscard]] auto list(dpp::snowflake guild) const -> std::vector<saved_voice>;

    [[nodiscard]] auto count(dpp::snowflake guild) const -> std::size_t;

    /// False when there was no such voice.
    auto remove(dpp::snowflake guild, std::string_view name) -> bool;

private:
    db::database* db_;
};

} // namespace latibot::audio
