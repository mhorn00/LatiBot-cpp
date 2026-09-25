#include "core/config/guild_settings.hpp"

#include "core/db/database.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <string>

namespace latibot::config {
namespace {

std::string lowercase(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

std::optional<std::string> guild_settings::find(dpp::snowflake guild_id, std::string_view key) const {
    auto query = db_->prepare("SELECT value FROM guild_settings WHERE guild_id = ? AND key = ?", static_cast<std::uint64_t>(guild_id), key);
    if (!query.step()) {
        return std::nullopt;
    }
    return query.get<std::string>(0);
}

std::string guild_settings::get(dpp::snowflake guild_id, std::string_view key, std::string_view fallback) const {
    return find(guild_id, key).value_or(std::string(fallback));
}

std::int64_t guild_settings::get_int(dpp::snowflake guild_id, std::string_view key, std::int64_t fallback) const {
    const auto stored = find(guild_id, key);
    if (!stored) {
        return fallback;
    }

    std::int64_t parsed = 0;
    const char* begin = stored->data();
    const char* end = begin + stored->size();
    const auto [stop, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || stop != end) {
        return fallback;
    }
    return parsed;
}

bool guild_settings::get_bool(dpp::snowflake guild_id, std::string_view key, bool fallback) const {
    const auto stored = find(guild_id, key);
    if (!stored) {
        return fallback;
    }

    const std::string text = lowercase(*stored);
    static constexpr std::array truthy{"1", "true", "yes", "on"};
    static constexpr std::array falsy{"0", "false", "no", "off"};

    if (std::ranges::find(truthy, text) != truthy.end()) {
        return true;
    }
    if (std::ranges::find(falsy, text) != falsy.end()) {
        return false;
    }
    return fallback;
}

double guild_settings::get_real(dpp::snowflake guild_id, std::string_view key, double fallback) const {
    const auto stored = find(guild_id, key);
    if (!stored) {
        return fallback;
    }

    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(*stored, &consumed);
        return consumed == stored->size() ? parsed : fallback;
    } catch (const std::exception&) {
        // MSVC's from_chars handles doubles, but stod keeps this readable and
        // the values here are hand-entered, not hot-path.
        return fallback;
    }
}

void guild_settings::set(dpp::snowflake guild_id, std::string_view key, std::string_view value) {
    db_->prepare(
           "INSERT INTO guild_settings (guild_id, key, value) VALUES (?, ?, ?) "
           "ON CONFLICT(guild_id, key) DO UPDATE SET value = excluded.value",
           static_cast<std::uint64_t>(guild_id), key, value)
        .run();
}

void guild_settings::set_int(dpp::snowflake guild_id, std::string_view key, std::int64_t value) {
    set(guild_id, key, std::to_string(value));
}

void guild_settings::set_bool(dpp::snowflake guild_id, std::string_view key, bool value) {
    set(guild_id, key, value ? "1" : "0");
}

void guild_settings::set_real(dpp::snowflake guild_id, std::string_view key, double value) {
    set(guild_id, key, std::to_string(value));
}

bool guild_settings::erase(dpp::snowflake guild_id, std::string_view key) {
    const auto guard = db_->lock();

    db_->prepare("DELETE FROM guild_settings WHERE guild_id = ? AND key = ?", static_cast<std::uint64_t>(guild_id), key).run();
    return db_->changes() > 0;
}

std::map<std::string, std::string, std::less<>> guild_settings::all(dpp::snowflake guild_id) const {
    std::map<std::string, std::string, std::less<>> settings;

    auto query = db_->prepare("SELECT key, value FROM guild_settings WHERE guild_id = ?", static_cast<std::uint64_t>(guild_id));
    while (query.step()) {
        settings.emplace(query.get<std::string>(0), query.get<std::string>(1));
    }
    return settings;
}

} // namespace latibot::config
