#pragma once

#include "models.hpp"
#include "provider.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <mutex>
#include <set>
#include <string>

namespace latibot::db {
class database;
}

namespace latibot::llm {

/// What every call to a model cost, in `llm_usage`
/// (docs/features/Language_Model.md §3.6).
class usage_store {
public:
    explicit usage_store(db::database& db) : db_(&db) {}

    /// Writes down one call. Returns what it cost.
    auto record(dpp::snowflake guild_id, const model_info& model, const usage& used, std::chrono::sys_seconds at) -> double;

    /// Dollars spent from `from` up to but not including `to`, across every
    /// guild: the money is one account's, whichever server spent it.
    [[nodiscard]] auto spent_between(std::chrono::sys_seconds from, std::chrono::sys_seconds to) const -> double;

    /// What one guild spent in that time, for `/llm status`.
    [[nodiscard]] auto spent_between(dpp::snowflake guild_id, std::chrono::sys_seconds from, std::chrono::sys_seconds to) const -> double;

private:
    db::database* db_;
};

/// The caps from `config.json`, in dollars
/// (docs/features/Language_Model.md §2.2).
struct spend_caps {
    double daily = 2.0;
    double monthly = 20.0;
};

/// Where spending stands against the caps. Days and months are UTC, as the
/// providers bill them.
struct spend_status {
    double today = 0;
    double this_month = 0;
    bool over_daily = false;
    bool over_monthly = false;

    [[nodiscard]] auto over() const noexcept -> bool { return over_daily || over_monthly; }

    /// The day or month that ran out, as "2026-09-28" or "2026-09", which is
    /// what a notice is sent once per. Empty when neither has.
    std::string period;
};

/// The start of `now`'s UTC day, and of its month.
[[nodiscard]] auto day_start(std::chrono::sys_seconds now) -> std::chrono::sys_seconds;
[[nodiscard]] auto month_start(std::chrono::sys_seconds now) -> std::chrono::sys_seconds;

/// Spending against the caps at `now`. The cap is checked before a call, so
/// one call can take spending past it; the next one is refused.
[[nodiscard]] auto spend_status_at(const usage_store& store, const spend_caps& caps, std::chrono::sys_seconds now) -> spend_status;

/// Remembers who has been told a cap was reached, so it is said once per
/// guild per day or month rather than on every message. Thread-safe.
class spend_notices {
public:
    /// True the first time it is asked about this guild and period.
    auto first(dpp::snowflake guild_id, const std::string& period) -> bool;

private:
    std::mutex mutex_;
    std::set<std::pair<dpp::snowflake, std::string>> told_;
};

} // namespace latibot::llm
