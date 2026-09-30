#pragma once

#include "core/events/triggers.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::ports {
class clock;
}

namespace latibot::llm {

/// A trigger that asks the model to say something, rather than picking a
/// fixed reply (docs/features/Language_Model.md §2.6).
struct advanced_trigger {
    std::int64_t id = 0;
    dpp::snowflake guild_id;

    /// Matched as the simple triggers match: literal text, whole word or
    /// anywhere, ignoring case.
    std::string pattern;
    events::match_mode mode = events::match_mode::whole_word;

    /// What to say, in a line: "Someone mentioned pineapple pizza. Defend
    /// it with unreasonable passion." How to say it is the guild's
    /// trigger_style document.
    std::string context_prompt;

    /// The chance it fires when it matches, 0 to 1.
    double probability = 1.0;

    /// Per channel.
    std::chrono::seconds cooldown{300};

    bool enabled = true;
    dpp::snowflake created_by;
};

/// A new trigger's cooldown: longer than a simple trigger's, since each one
/// is a model call.
inline constexpr std::chrono::seconds default_advanced_cooldown{300};

/// The longest context prompt: a line, not a document.
inline constexpr std::size_t context_prompt_limit = 500;

/// `llm_triggers`.
class advanced_trigger_store {
public:
    explicit advanced_trigger_store(db::database& db) : db_(&db) {}

    [[nodiscard]] auto for_guild(dpp::snowflake guild_id) const -> std::vector<advanced_trigger>;
    [[nodiscard]] auto find(std::int64_t id, dpp::snowflake guild_id) const -> std::optional<advanced_trigger>;

    /// Returns the new id.
    auto add(const advanced_trigger& entry) -> std::int64_t;

    /// False when it does not exist, or belongs to another guild.
    auto update(const advanced_trigger& entry) -> bool;
    auto remove(std::int64_t id, dpp::snowflake guild_id) -> bool;

private:
    db::database* db_;
};

/// Picks the advanced trigger a message fires, if any: the first enabled one
/// that matches, is off cooldown in the channel, and wins its roll. Only the
/// one that fires starts its cooldown. Thread-safe.
class advanced_trigger_matcher {
public:
    /// `roll` returns a number in [0, 1); the default is a seeded generator,
    /// and tests pass something predictable.
    explicit advanced_trigger_matcher(ports::clock& clock, std::function<double()> roll = {});

    [[nodiscard]] auto fire(const std::vector<advanced_trigger>& triggers, dpp::snowflake channel, std::string_view content)
        -> std::optional<advanced_trigger>;

private:
    ports::clock* clock_;
    std::mutex mutex_;
    std::function<double()> roll_;
    std::map<std::pair<std::int64_t, dpp::snowflake>, std::chrono::steady_clock::time_point> last_fired_;
};

} // namespace latibot::llm
