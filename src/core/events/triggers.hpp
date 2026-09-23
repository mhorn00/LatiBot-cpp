#pragma once

#include "core/events/message_pipeline.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::ports {
class clock;
}

namespace latibot::events {

/// How a trigger's pattern is compared against a message (plan v4 §11).
///
/// Users do not write regular expressions: a pattern is literal text, and the
/// only choice is whether it has to stand alone as a word.
enum class match_mode : std::uint8_t {
    /// "420" fires on "420" but not on "4200".
    whole_word,
    /// "420" fires on "4200" too.
    substring,
};

[[nodiscard]] std::string_view to_string(match_mode mode) noexcept;
[[nodiscard]] std::optional<match_mode> match_mode_from_string(std::string_view name);

struct weighted_response {
    std::string text;
    int weight = 1;
};

struct trigger {
    std::int64_t id = 0;
    dpp::snowflake guild_id;
    std::string pattern;
    match_mode mode = match_mode::whole_word;

    /// Per channel, and may be zero (plan v4 §11).
    std::chrono::seconds cooldown{30};
    bool enabled = true;

    std::vector<weighted_response> responses;
};

/// The default cooldown for a new trigger.
inline constexpr std::chrono::seconds default_trigger_cooldown{30};

// --------------------------------------------------------------------------
// Decisions
// --------------------------------------------------------------------------

/// Whether `content` fires `pattern`. Case-insensitive, and the pattern is
/// literal text rather than a regular expression.
[[nodiscard]] bool matches(std::string_view content, std::string_view pattern, match_mode mode);

/// Picks a response, weighted.
///
/// `roll` is any number; the caller owns the randomness, which is what makes
/// the distribution testable. Returns nullptr when there is nothing to pick:
/// no responses, or every weight zero or negative.
[[nodiscard]] const weighted_response* choose(std::span<const weighted_response> responses, std::uint64_t roll);

/// Whether a trigger may fire again in a channel.
///
/// `last_fired` is empty when it has not fired there yet. A zero cooldown
/// always allows it, which is the documented way to turn cooldowns off.
[[nodiscard]] bool off_cooldown(std::optional<std::chrono::steady_clock::time_point> last_fired, std::chrono::steady_clock::time_point now,
                                std::chrono::seconds cooldown);

// --------------------------------------------------------------------------
// Storage
// --------------------------------------------------------------------------

/// Triggers and their responses, in SQLite.
class trigger_store {
public:
    explicit trigger_store(db::database& db) : db_(&db) {}

    [[nodiscard]] std::vector<trigger> for_guild(dpp::snowflake guild_id) const;
    [[nodiscard]] std::optional<trigger> find(std::int64_t id, dpp::snowflake guild_id) const;

    /// Returns the new id. Responses are replaced wholesale, which is how the
    /// command and the panel both edit them.
    std::int64_t add(const trigger& entry);

    /// False when the trigger does not exist, or belongs to another guild.
    bool update(const trigger& entry);
    bool remove(std::int64_t id, dpp::snowflake guild_id);

    /// The three the Java bot had, added only when the guild has none
    /// (plan v4 §11). Returns how many were added.
    int seed_defaults(dpp::snowflake guild_id);

private:
    void replace_responses(std::int64_t trigger_id, std::span<const weighted_response> responses);

    db::database* db_;
};

// --------------------------------------------------------------------------
// The pipeline stage
// --------------------------------------------------------------------------

/// Answers messages that match a guild's triggers.
///
/// Does not consume the message: a message with both "420" and a link should
/// get the reply and the replacement (plan v4 §5.4). It does suppress the
/// advanced LLM triggers, which is a decision for the stage that adds them.
class trigger_responder {
public:
    /// `roll` supplies the randomness for weighted responses. The default is
    /// a seeded generator; tests pass something predictable.
    trigger_responder(const trigger_store& store, ports::clock& clock, std::function<std::uint64_t()> roll = {});

    stage_result operator()(const incoming_message& message);

    /// Cooldowns are per (trigger, channel) and kept here rather than in the
    /// database: forgetting them across a restart costs one extra reply.
    void forget_cooldowns();

private:
    const trigger_store* store_;
    ports::clock* clock_;
    std::function<std::uint64_t()> roll_;

    std::map<std::pair<std::int64_t, dpp::snowflake>, std::chrono::steady_clock::time_point> last_fired_;
};

} // namespace latibot::events
