#pragma once

#include "core/db/schema_versions.hpp"
#include "core/events/message_pipeline.hpp"
#include "core/util/match.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
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

/// The module's tables, `triggers` and `trigger_responses`, version 1 first
/// (docs/modules/Module_Plan_Final.md §7.1). Defined in module.cpp.
[[nodiscard]] auto triggers_schema() noexcept -> db::module_schema;

// How a pattern matches and when a trigger may fire again are shared with
// the language model's advanced triggers, so they live in util/match; the
// names are kept here for the triggers' own code.
using util::match_mode;
using util::match_mode_from_string;
using util::matches;
using util::off_cooldown;
using util::to_string;

struct weighted_response {
    std::string text;
    int weight = 1;
};

struct trigger {
    std::int64_t id = 0;
    dpp::snowflake guild_id;
    std::string pattern;
    match_mode mode = match_mode::whole_word;

    /// Per channel, and may be zero (src/modules/triggers/docs/Triggers.md §2.2).
    std::chrono::seconds cooldown{30};
    bool enabled = true;

    /// Whether this trigger answers messages from other bots.
    ///
    /// Off by default, and only reachable at all for bots this guild has
    /// allowed (src/core/docs/Message_Pipeline.md §2.1): the allowlist
    /// decides who is heard, this decides who is answered.
    bool respond_to_bots = false;

    /// How its replies are posted: silent, and whether with link previews.
    /// Only `discord::channel_message_flags` are kept. Silent by default, as
    /// every trigger was before this could be chosen.
    discord::message_flags message_flags = dpp::m_suppress_notifications;

    std::vector<weighted_response> responses;
};

/// The default cooldown for a new trigger.
inline constexpr std::chrono::seconds default_trigger_cooldown{30};

// --------------------------------------------------------------------------
// Decisions
// --------------------------------------------------------------------------

/// Picks a response, weighted.
///
/// `roll` is any number; the caller owns the randomness, which is what makes
/// the distribution testable. Returns nullptr when there is nothing to pick:
/// no responses, or every weight zero or negative.
[[nodiscard]] auto choose(std::span<const weighted_response> responses, std::uint64_t roll) -> const weighted_response*;

// --------------------------------------------------------------------------
// Storage
// --------------------------------------------------------------------------

/// Triggers and their responses, in SQLite.
class trigger_store {
public:
    explicit trigger_store(db::database& db) : db_(&db) {}

    [[nodiscard]] auto for_guild(dpp::snowflake guild_id) const -> std::vector<trigger>;
    [[nodiscard]] auto find(std::int64_t id, dpp::snowflake guild_id) const -> std::optional<trigger>;

    /// Returns the new id. Responses are replaced wholesale, which is how the
    /// command and the panel both edit them.
    auto add(const trigger& entry) -> std::int64_t;

    /// False when the trigger does not exist, or belongs to another guild.
    auto update(const trigger& entry) -> bool;
    auto remove(std::int64_t id, dpp::snowflake guild_id) -> bool;

private:
    auto replace_responses(std::int64_t trigger_id, std::span<const weighted_response> responses) -> void;

    db::database* db_;
};

// --------------------------------------------------------------------------
// The pipeline stage
// --------------------------------------------------------------------------

/// Answers messages that match a guild's triggers.
///
/// Does not consume the message: a message with both "420" and a link should
/// get the reply and the replacement
/// (src/core/docs/Message_Pipeline.md §2.2). A reply marks the message
/// answered, which keeps the advanced triggers quiet: the simple one wins
/// (src/core/docs/Message_Pipeline.md §2.2).
///
/// Safe to call from several threads at once, which is how DPP delivers
/// messages.
class trigger_responder {
public:
    /// `roll` supplies the randomness for weighted responses. The default is
    /// a seeded generator; tests pass something predictable.
    trigger_responder(const trigger_store& store, ports::clock& clock, std::function<std::uint64_t()> roll = {});

    auto operator()(const incoming_message& message) -> stage_result;

private:
    const trigger_store* store_;
    ports::clock* clock_;

    /// Guards `roll_` and `last_fired_`.
    std::mutex mutex_;
    std::function<std::uint64_t()> roll_;

    /// Cooldowns are per (trigger, channel) and kept here rather than in the
    /// database: forgetting them across a restart costs one extra reply.
    std::map<std::pair<std::int64_t, dpp::snowflake>, std::chrono::steady_clock::time_point> last_fired_;
};

} // namespace latibot::events
