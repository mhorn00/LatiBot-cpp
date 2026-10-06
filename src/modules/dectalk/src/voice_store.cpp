#include "core/audio/voice_store.hpp"

#include "core/db/database.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <format>

namespace latibot::audio {
namespace {

constexpr std::size_t max_name_length = 32;

auto is_name_character(char letter) -> bool {
    return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '-' || letter == '_';
}

auto read_voice(db::statement& row) -> saved_voice {
    saved_voice found;
    found.name = row.get<std::string>(0);
    found.voice = parse_custom_voice(row.get<std::string>(2), row.get<std::string>(1)).voice;
    found.created_by = row.get<dpp::snowflake>(3);
    found.updated_at = row.get<std::chrono::sys_seconds>(4);
    return found;
}

} // namespace

auto normalise_voice_name(std::string_view name) -> std::string {
    return util::to_lower(util::trim(name));
}

auto voice_name_refusal(std::string_view name) -> std::optional<std::string> {
    const std::string normal = normalise_voice_name(name);
    if (normal.empty()) return "a voice needs a name";
    if (normal.size() > max_name_length) return std::format("a voice's name can be at most {} characters", max_name_length);
    if (!std::ranges::all_of(normal, is_name_character)) return "a voice's name can only have letters, digits, - and _";
    if (find_builtin_voice(normal) != nullptr) return std::format("{} is already one of the built-in voices", normal);
    return std::nullopt;
}

auto voice_store::save(dpp::snowflake guild, const saved_voice& voice) -> void {
    const auto guard = db_->lock();
    db_->prepare(
           "INSERT INTO tts_voices (guild_id, name, base_voice, params, created_by, updated_at) VALUES (?, ?, ?, ?, ?, ?) "
           "ON CONFLICT (guild_id, name) DO UPDATE SET base_voice = excluded.base_voice, params = excluded.params, "
           "updated_at = excluded.updated_at",
           guild, normalise_voice_name(voice.name), voice.voice.base, voice.voice.dv_parameters(), voice.created_by, voice.updated_at)
        .run();
}

auto voice_store::find(dpp::snowflake guild, std::string_view name) const -> std::optional<saved_voice> {
    const auto guard = db_->lock();
    auto query = db_->prepare("SELECT name, base_voice, params, created_by, updated_at FROM tts_voices WHERE guild_id = ? AND name = ?",
                              guild, normalise_voice_name(name));
    if (!query.step()) return std::nullopt;
    return read_voice(query);
}

auto voice_store::list(dpp::snowflake guild) const -> std::vector<saved_voice> {
    const auto guard = db_->lock();
    std::vector<saved_voice> voices;
    auto query =
        db_->prepare("SELECT name, base_voice, params, created_by, updated_at FROM tts_voices WHERE guild_id = ? ORDER BY name", guild);
    while (query.step()) {
        voices.push_back(read_voice(query));
    }
    return voices;
}

auto voice_store::count(dpp::snowflake guild) const -> std::size_t {
    const auto guard = db_->lock();
    auto query = db_->prepare("SELECT COUNT(*) FROM tts_voices WHERE guild_id = ?", guild);
    return query.step() ? static_cast<std::size_t>(query.get<std::int64_t>(0)) : 0;
}

auto voice_store::remove(dpp::snowflake guild, std::string_view name) -> bool {
    const auto guard = db_->lock();
    db_->prepare("DELETE FROM tts_voices WHERE guild_id = ? AND name = ?", guild, normalise_voice_name(name)).run();
    return db_->changes() > 0;
}

} // namespace latibot::audio
