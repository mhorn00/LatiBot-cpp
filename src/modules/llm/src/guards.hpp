#pragma once

#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::ports {
class clock;
}

namespace latibot::llm {

// What stands between a message and a model call
// (src/modules/llm/docs/Language_Model.md §2.2): who is not answered, and how often
// anyone is.

enum class block_kind : std::uint8_t { user, role };

[[nodiscard]] auto to_string(block_kind kind) noexcept -> std::string_view;

struct block_entry {
    block_kind kind = block_kind::user;
    dpp::snowflake target;
};

/// The users and roles the model does not answer in each guild.
class blacklist_store {
public:
    explicit blacklist_store(db::database& db) : db_(&db) {}

    /// False when it was already there.
    auto add(dpp::snowflake guild_id, block_kind kind, dpp::snowflake target) -> bool;

    /// False when it was not there.
    auto remove(dpp::snowflake guild_id, block_kind kind, dpp::snowflake target) -> bool;

    [[nodiscard]] auto list(dpp::snowflake guild_id) const -> std::vector<block_entry>;

    /// Whether this user, or any of these roles, is on the list.
    [[nodiscard]] auto blocks(dpp::snowflake guild_id, dpp::snowflake user, std::span<const dpp::snowflake> roles) const -> bool;

private:
    db::database* db_;
};

/// At most so many of something per window, per key. Thread-safe.
///
/// Kept in memory: forgetting it across a restart costs a few extra replies.
class rate_limiter {
public:
    rate_limiter(ports::clock& clock, std::chrono::seconds window) : clock_(&clock), window_(window) {}

    using key = std::pair<dpp::snowflake, dpp::snowflake>;

    /// Takes one if fewer than `limit` were taken in the last window. A
    /// limit of zero or less takes nothing.
    auto try_take(key who, int limit) -> bool;

private:
    ports::clock* clock_;
    std::chrono::seconds window_;
    std::mutex mutex_;
    std::map<key, std::deque<std::chrono::steady_clock::time_point>> taken_;
};

/// The pacing settings, from `llm_settings`
/// (src/modules/llm/docs/Language_Model.md §2.7).
struct pacing_rules {
    /// Replies to bots in a row in one channel before waiting for a human.
    /// Zero never answers a bot.
    int turns = 6;

    /// The least time between two of those replies in one channel.
    std::chrono::seconds delay{5};

    /// Replies to bots per guild per UTC day.
    int daily_cap = 50;

    /// Answer a bot only once a person has spoken in the channel since the
    /// bot started.
    bool needs_human = false;
};

/// Whether to answer a bot now, later, or not at all.
struct pacing_decision {
    bool allowed = false;

    /// How long to wait first, when allowed.
    std::chrono::seconds wait{0};

    /// Why not, for the log.
    std::string_view reason;
};

/// Keeps two bots from talking each other into a bill
/// (src/modules/llm/docs/Language_Model.md §2.7). Thread-safe.
///
/// A human speaking in a channel resets its count, which is what lets a
/// conversation between people and bots carry on while a conversation
/// between bots alone runs down.
class bot_pacing {
public:
    explicit bot_pacing(ports::clock& clock) : clock_(&clock) {}

    /// A person said something in this channel.
    auto human_spoke(dpp::snowflake channel) -> void;

    /// Whether the model may answer a bot in this channel now, and if so
    /// claims the turn: two bot messages arriving at once cannot both take
    /// the last one.
    auto claim(dpp::snowflake guild, dpp::snowflake channel, const pacing_rules& rules) -> pacing_decision;

private:
    struct channel_state {
        int turns = 0;
        bool human_seen = false;
        std::optional<std::chrono::steady_clock::time_point> last_turn;
    };

    ports::clock* clock_;
    std::mutex mutex_;
    std::map<dpp::snowflake, channel_state> channels_;

    /// Replies to bots per guild on `day_`.
    std::map<dpp::snowflake, int> today_;
    std::chrono::sys_days day_;
};

} // namespace latibot::llm
