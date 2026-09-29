#include "core/llm/spend.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"

#include <format>

namespace latibot::llm {

auto usage_store::record(dpp::snowflake guild_id, const model_info& model, const usage& used, std::chrono::sys_seconds at) -> double {
    const double cost = cost_usd(model, used);
    db_->prepare(
           "INSERT INTO llm_usage (guild_id, model, input_tokens, output_tokens, cache_write_tokens, cache_read_tokens, cost_usd, at) "
           "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
           guild_id, model.id, used.input_tokens, used.output_tokens, used.cache_write_tokens, used.cache_read_tokens, cost, at)
        .run();
    return cost;
}

auto usage_store::spent_between(std::chrono::sys_seconds from, std::chrono::sys_seconds to) const -> double {
    auto query = db_->prepare("SELECT COALESCE(SUM(cost_usd), 0) FROM llm_usage WHERE at >= ? AND at < ?", from, to);
    return query.step() ? query.get<double>(0) : 0.0;
}

auto usage_store::spent_between(dpp::snowflake guild_id, std::chrono::sys_seconds from, std::chrono::sys_seconds to) const -> double {
    auto query =
        db_->prepare("SELECT COALESCE(SUM(cost_usd), 0) FROM llm_usage WHERE guild_id = ? AND at >= ? AND at < ?", guild_id, from, to);
    return query.step() ? query.get<double>(0) : 0.0;
}

auto day_start(std::chrono::sys_seconds now) -> std::chrono::sys_seconds {
    return std::chrono::floor<std::chrono::days>(now);
}

auto month_start(std::chrono::sys_seconds now) -> std::chrono::sys_seconds {
    const std::chrono::year_month_day date{std::chrono::floor<std::chrono::days>(now)};
    return std::chrono::sys_days{date.year() / date.month() / 1};
}

auto spend_status_at(const usage_store& store, const spend_caps& caps, std::chrono::sys_seconds now) -> spend_status {
    // Up to a second past now, so a call recorded this very second counts.
    const auto until = now + std::chrono::seconds{1};

    spend_status status;
    status.today = store.spent_between(day_start(now), until);
    status.this_month = store.spent_between(month_start(now), until);
    status.over_daily = status.today >= caps.daily;
    status.over_monthly = status.this_month >= caps.monthly;

    // The month first: it is the one that does not come back tomorrow.
    const std::chrono::year_month_day date{std::chrono::floor<std::chrono::days>(now)};
    if (status.over_monthly) {
        status.period = std::format("{:%Y-%m}", date);
    } else if (status.over_daily) {
        status.period = std::format("{:%F}", date);
    }
    return status;
}

auto spend_notices::first(dpp::snowflake guild_id, const std::string& period) -> bool {
    const std::scoped_lock lock(mutex_);
    return told_.emplace(guild_id, period).second;
}

} // namespace latibot::llm
