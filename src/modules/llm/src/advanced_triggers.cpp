#include "advanced_triggers.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/ports/clock.hpp"
#include "core/util/log.hpp"

#include <format>
#include <memory>
#include <random>

namespace latibot::llm {
namespace {

constexpr std::string_view columns = "id, guild_id, pattern, match_mode, context_prompt, probability, cooldown_s, enabled, created_by";

auto read_trigger(db::statement& row) -> advanced_trigger {
    return {.id = row.get<std::int64_t>(0),
            .guild_id = row.get<dpp::snowflake>(1),
            .pattern = row.get<std::string>(2),
            .mode = util::match_mode_from_string(row.get<std::string>(3)).value_or(util::match_mode::whole_word),
            .context_prompt = row.get<std::string>(4),
            .probability = row.get<double>(5),
            .cooldown = std::chrono::seconds{row.get<std::int64_t>(6)},
            .enabled = row.get<bool>(7),
            .created_by = row.get<dpp::snowflake>(8)};
}

auto default_roll() -> std::function<double()> {
    auto engine = std::make_shared<std::mt19937_64>(std::random_device{}());
    return [engine] { return std::uniform_real_distribution<double>(0.0, 1.0)(*engine); };
}

} // namespace

auto advanced_trigger_store::for_guild(dpp::snowflake guild_id) const -> std::vector<advanced_trigger> {
    std::vector<advanced_trigger> found;
    auto query = db_->prepare(std::format("SELECT {} FROM llm_triggers WHERE guild_id = ? ORDER BY id", columns), guild_id);
    while (query.step()) {
        found.push_back(read_trigger(query));
    }
    return found;
}

auto advanced_trigger_store::find(std::int64_t id, dpp::snowflake guild_id) const -> std::optional<advanced_trigger> {
    auto query = db_->prepare(std::format("SELECT {} FROM llm_triggers WHERE id = ? AND guild_id = ?", columns), id, guild_id);
    if (!query.step()) return std::nullopt;
    return read_trigger(query);
}

auto advanced_trigger_store::add(const advanced_trigger& entry) -> std::int64_t {
    const auto guard = db_->lock();
    db_->prepare(
           "INSERT INTO llm_triggers (guild_id, pattern, match_mode, context_prompt, probability, cooldown_s, enabled, created_by) "
           "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
           entry.guild_id, entry.pattern, util::to_string(entry.mode), entry.context_prompt, entry.probability, entry.cooldown.count(),
           entry.enabled, entry.created_by)
        .run();
    return db_->last_insert_rowid();
}

auto advanced_trigger_store::update(const advanced_trigger& entry) -> bool {
    const auto guard = db_->lock();
    db_->prepare(
           "UPDATE llm_triggers SET pattern = ?, match_mode = ?, context_prompt = ?, probability = ?, cooldown_s = ?, enabled = ? "
           "WHERE id = ? AND guild_id = ?",
           entry.pattern, util::to_string(entry.mode), entry.context_prompt, entry.probability, entry.cooldown.count(), entry.enabled,
           entry.id, entry.guild_id)
        .run();
    return db_->changes() > 0;
}

auto advanced_trigger_store::remove(std::int64_t id, dpp::snowflake guild_id) -> bool {
    const auto guard = db_->lock();
    db_->prepare("DELETE FROM llm_triggers WHERE id = ? AND guild_id = ?", id, guild_id).run();
    return db_->changes() > 0;
}

// --------------------------------------------------------------------------

advanced_trigger_matcher::advanced_trigger_matcher(ports::clock& clock, std::function<double()> roll)
    : clock_(&clock), roll_(roll ? std::move(roll) : default_roll()) {}

auto advanced_trigger_matcher::fire(const std::vector<advanced_trigger>& triggers, dpp::snowflake channel, std::string_view content)
    -> std::optional<advanced_trigger> {
    for (const advanced_trigger& entry : triggers) {
        if (!entry.enabled || !util::matches(content, entry.pattern, entry.mode)) continue;

        // Cooldown, roll and claim under one lock, as the simple triggers do,
        // so two messages at once cannot both fire it.
        const std::scoped_lock lock(mutex_);
        const auto now = clock_->steady_now();
        const auto key = std::pair{entry.id, channel};
        const auto seen = last_fired_.find(key);
        const auto last = seen == last_fired_.end() ? std::nullopt : std::optional(seen->second);
        if (!util::off_cooldown(last, now, entry.cooldown)) {
            util::log().debug("advanced trigger {} matched but is on cooldown in channel {}", entry.id, channel);
            continue;
        }
        if (roll_() >= entry.probability) {
            util::log().debug("advanced trigger {} matched but lost its roll ({:.0f}%)", entry.id, entry.probability * 100);
            continue;
        }

        last_fired_[key] = now;
        return entry;
    }
    return std::nullopt;
}

} // namespace latibot::llm
