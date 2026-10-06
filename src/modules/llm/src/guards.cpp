#include "core/llm/guards.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/ports/clock.hpp"

#include <algorithm>
#include <string>

namespace latibot::llm {

auto to_string(block_kind kind) noexcept -> std::string_view {
    return kind == block_kind::role ? "role" : "user";
}

auto blacklist_store::add(dpp::snowflake guild_id, block_kind kind, dpp::snowflake target) -> bool {
    const auto guard = db_->lock();
    db_->prepare("INSERT OR IGNORE INTO llm_blacklist (guild_id, kind, target_id) VALUES (?, ?, ?)", guild_id, to_string(kind), target)
        .run();
    return db_->changes() > 0;
}

auto blacklist_store::remove(dpp::snowflake guild_id, block_kind kind, dpp::snowflake target) -> bool {
    const auto guard = db_->lock();
    db_->prepare("DELETE FROM llm_blacklist WHERE guild_id = ? AND kind = ? AND target_id = ?", guild_id, to_string(kind), target).run();
    return db_->changes() > 0;
}

auto blacklist_store::list(dpp::snowflake guild_id) const -> std::vector<block_entry> {
    std::vector<block_entry> entries;
    auto query = db_->prepare("SELECT kind, target_id FROM llm_blacklist WHERE guild_id = ? ORDER BY kind DESC, target_id", guild_id);
    while (query.step()) {
        entries.push_back(
            {.kind = query.get<std::string>(0) == "role" ? block_kind::role : block_kind::user, .target = query.get<dpp::snowflake>(1)});
    }
    return entries;
}

auto blacklist_store::blocks(dpp::snowflake guild_id, dpp::snowflake user, std::span<const dpp::snowflake> roles) const -> bool {
    // One read per message; a guild's list is a handful of rows.
    return std::ranges::any_of(list(guild_id), [&](const block_entry& entry) {
        return entry.kind == block_kind::user ? entry.target == user : std::ranges::find(roles, entry.target) != roles.end();
    });
}

// --------------------------------------------------------------------------

auto rate_limiter::try_take(key who, int limit) -> bool {
    if (limit <= 0) return false;

    const std::scoped_lock lock(mutex_);
    const auto now = clock_->steady_now();

    // Every key is tidied as it is used, and a key whose window has emptied
    // is dropped, so the map holds only people and channels active lately.
    std::erase_if(taken_, [&](auto& entry) {
        auto& times = entry.second;
        while (!times.empty() && now - times.front() >= window_) {
            times.pop_front();
        }
        return times.empty() && entry.first != who;
    });

    auto& times = taken_[who];
    if (std::cmp_greater_equal(times.size(), limit)) return false;
    times.push_back(now);
    return true;
}

// --------------------------------------------------------------------------

auto bot_pacing::human_spoke(dpp::snowflake channel) -> void {
    const std::scoped_lock lock(mutex_);
    channel_state& state = channels_[channel];
    state.turns = 0;
    state.human_seen = true;
}

auto bot_pacing::claim(dpp::snowflake guild, dpp::snowflake channel, const pacing_rules& rules) -> pacing_decision {
    const std::scoped_lock lock(mutex_);

    const auto today = std::chrono::floor<std::chrono::days>(clock_->now());
    if (today != day_) {
        today_.clear();
        day_ = today;
    }

    channel_state& state = channels_[channel];
    if (rules.turns <= 0) return {.allowed = false, .wait = {}, .reason = "this server's model does not answer bots"};
    if (rules.needs_human && !state.human_seen) {
        return {.allowed = false, .wait = {}, .reason = "no person has spoken in the channel yet"};
    }
    if (state.turns >= rules.turns) return {.allowed = false, .wait = {}, .reason = "too many bot turns in a row; waiting for a person"};
    int& spent = today_[guild];
    if (spent >= rules.daily_cap) return {.allowed = false, .wait = {}, .reason = "the day's replies to bots are used up"};

    const auto now = clock_->steady_now();
    std::chrono::seconds wait{0};
    if (state.last_turn) {
        const auto since = std::chrono::duration_cast<std::chrono::seconds>(now - *state.last_turn);
        if (since < rules.delay) wait = rules.delay - since;
    }

    ++state.turns;
    ++spent;
    // When this turn will be taken, so the next one waits from then.
    state.last_turn = now + wait;
    return {.allowed = true, .wait = wait, .reason = {}};
}

} // namespace latibot::llm
