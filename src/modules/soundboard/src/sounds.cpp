#include "sounds.hpp"

#include "core/discord/raw_api.hpp"
#include "core/util/text.hpp"

#include <dpp/json.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <tuple>
#include <utility>

namespace latibot::soundboard {
namespace {

/// Discord's limit on a suggestion's label.
constexpr std::size_t max_label = 100;

/// Where a sound is from, as seen from the server someone asks in, in the
/// order suggestions and names prefer them.
enum class nearness : std::uint8_t { this_guild, discord, other_guild };

auto snowflake_of(const nlohmann::json& object, const char* key) -> dpp::snowflake {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) return {};
    return dpp::snowflake(found->get<std::string>());
}

auto string_of(const nlohmann::json& object, const char* key) -> std::string {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

/// One match for `suggest`, with the name of the server it is from.
struct placed_sound {
    const sound* found;
    std::string_view guild_name;
};

} // namespace

auto parse_sounds(const nlohmann::json& listed) -> std::vector<sound> {
    const nlohmann::json* items = &listed;
    if (listed.is_object()) {
        const auto found = listed.find("items");
        if (found == listed.end()) return {};
        items = &*found;
    }
    if (!items->is_array()) return {};

    std::vector<sound> sounds;
    for (const nlohmann::json& item : *items) {
        if (!item.is_object()) continue;
        if (const auto available = item.find("available"); available != item.end() && available->is_boolean() && !available->get<bool>()) {
            continue;
        }
        sound read{.id = snowflake_of(item, "sound_id"),
                   .name = string_of(item, "name"),
                   .guild_id = snowflake_of(item, "guild_id"),
                   .emoji = string_of(item, "emoji_name"),
                   .volume = 1.0};
        if (read.id.empty() || util::is_blank(read.name)) continue;
        if (const auto volume = item.find("volume"); volume != item.end() && volume->is_number()) {
            read.volume = std::clamp(volume->get<double>(), 0.0, 1.0);
        }
        sounds.push_back(std::move(read));
    }
    return sounds;
}

auto sound_url(dpp::snowflake sound_id) -> std::string {
    return std::format("https://cdn.discordapp.com/soundboard-sounds/{}", sound_id.str());
}

// --------------------------------------------------------------------------
// The catalog
// --------------------------------------------------------------------------

auto sound_catalog::set_defaults(std::vector<sound> sounds) -> void {
    const std::scoped_lock lock(mutex_);
    defaults_ = std::move(sounds);
}

auto sound_catalog::set_guild(dpp::snowflake guild, std::string guild_name, std::vector<sound> sounds) -> void {
    const std::scoped_lock lock(mutex_);
    guilds_[guild] = guild_sounds{.name = std::move(guild_name), .sounds = std::move(sounds)};
}

auto sound_catalog::forget_guild(dpp::snowflake guild) -> void {
    const std::scoped_lock lock(mutex_);
    guilds_.erase(guild);
}

auto sound_catalog::guilds() const -> std::map<dpp::snowflake, std::string> {
    const std::scoped_lock lock(mutex_);
    std::map<dpp::snowflake, std::string> named;
    for (const auto& [id, held] : guilds_) {
        named.emplace(id, held.name);
    }
    return named;
}

auto sound_catalog::size() const -> std::size_t {
    const std::scoped_lock lock(mutex_);
    std::size_t count = defaults_.size();
    for (const auto& [id, held] : guilds_) {
        count += held.sounds.size();
    }
    return count;
}

auto sound_catalog::find(std::string_view wanted, dpp::snowflake asking_guild) const -> std::optional<sound> {
    const std::string_view trimmed = util::trim(wanted);
    if (trimmed.empty()) return std::nullopt;

    const std::scoped_lock lock(mutex_);
    const auto each_sound = [&](const auto& visit) {
        for (const sound& each : defaults_) {
            visit(each, nearness::discord);
        }
        for (const auto& [id, held] : guilds_) {
            for (const sound& each : held.sounds) {
                visit(each, id == asking_guild ? nearness::this_guild : nearness::other_guild);
            }
        }
    };

    // An id first: it is what a suggestion gives, and it is never ambiguous.
    const sound* best = nullptr;
    each_sound([&](const sound& each, nearness /*place*/) {
        if (best == nullptr && each.id.str() == trimmed) best = &each;
    });
    if (best != nullptr) return *best;

    nearness best_place = nearness::other_guild;
    each_sound([&](const sound& each, nearness place) {
        if (!util::equals_ignoring_case(each.name, trimmed)) return;
        if (best == nullptr || place < best_place) {
            best = &each;
            best_place = place;
        }
    });
    if (best == nullptr) return std::nullopt;
    return *best;
}

auto sound_catalog::suggest(std::string_view typed, dpp::snowflake asking_guild, std::size_t limit) const -> std::vector<sound_choice> {
    const std::string filter = util::to_lower(util::trim(typed));

    const std::scoped_lock lock(mutex_);
    // Sorted on whether the name starts with what was typed, then where the
    // sound is from, then its name.
    using ranked = std::tuple<bool, nearness, std::string, placed_sound>;
    std::vector<ranked> matches;
    const auto consider = [&](const sound& each, nearness place, std::string_view guild_name) {
        std::string lowered = util::to_lower(each.name);
        const std::size_t at = lowered.find(filter);
        if (at == std::string::npos) return;
        matches.emplace_back(at != 0, place, std::move(lowered), placed_sound{.found = &each, .guild_name = guild_name});
    };
    for (const sound& each : defaults_) {
        consider(each, nearness::discord, {});
    }
    for (const auto& [id, held] : guilds_) {
        for (const sound& each : held.sounds) {
            consider(each, id == asking_guild ? nearness::this_guild : nearness::other_guild, held.name);
        }
    }
    std::ranges::sort(matches, [](const ranked& lhs, const ranked& rhs) {
        return std::tie(std::get<0>(lhs), std::get<1>(lhs), std::get<2>(lhs)) <
               std::tie(std::get<0>(rhs), std::get<1>(rhs), std::get<2>(rhs));
    });

    std::vector<sound_choice> choices;
    for (const auto& [later, place, lowered, placed] : matches) {
        if (choices.size() == limit) break;
        const sound& each = *placed.found;
        std::string label = each.emoji.empty() ? each.name : std::format("{} {}", each.emoji, each.name);
        if (place == nearness::discord) {
            label += " (Discord)";
        } else if (place == nearness::other_guild) {
            label += std::format(" ({})", placed.guild_name.empty() ? std::string_view("another server") : placed.guild_name);
        }
        choices.push_back({.label = util::truncate(label, max_label), .value = each.id.str()});
    }
    return choices;
}

// --------------------------------------------------------------------------
// Reading them from Discord
// --------------------------------------------------------------------------

auto discord_sounds::default_sounds() -> dpp::task<ports::result<std::vector<sound>>> {
    auto listed = co_await raw_->request(ports::http_method::get, "/soundboard-default-sounds");
    if (!listed.has_value()) co_return std::unexpected(std::move(listed.error()));
    co_return parse_sounds(listed.value());
}

auto discord_sounds::guild_sounds(dpp::snowflake guild) -> dpp::task<ports::result<std::vector<sound>>> {
    auto listed = co_await raw_->request(ports::http_method::get, std::format("/guilds/{}/soundboard-sounds", guild.str()));
    if (!listed.has_value()) co_return std::unexpected(std::move(listed.error()));
    std::vector<sound> sounds = parse_sounds(listed.value());
    // Each item names its server, but a missing one must not make a
    // server's sound look like Discord's.
    for (sound& each : sounds) {
        if (each.guild_id.empty()) each.guild_id = guild;
    }
    co_return sounds;
}

} // namespace latibot::soundboard
