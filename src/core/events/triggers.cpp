#include "core/events/triggers.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/ports/clock.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <numeric>
#include <random>
#include <utility>

namespace latibot::events {
namespace {

/// Word characters for the purposes of whole-word matching: what sits either
/// side of "420" in "4200" but not in "it's 420 somewhere".
auto is_word_character(char letter) -> bool {
    const auto byte = static_cast<unsigned char>(letter);
    return byte == '_' || std::isalnum(byte) != 0;
}

/// A generator per responder, seeded once. Sharing one across guilds is fine:
/// the only thing riding on it is which of several jokes gets picked.
auto default_roll() -> std::function<std::uint64_t()> {
    auto engine = std::make_shared<std::mt19937_64>(std::random_device{}());
    return [engine] { return (*engine)(); };
}

} // namespace

auto to_string(match_mode mode) noexcept -> std::string_view {
    return mode == match_mode::substring ? "substring" : "whole_word";
}

auto match_mode_from_string(std::string_view name) -> std::optional<match_mode> {
    const std::string key = util::to_lower(name);
    if (key == "whole_word" || key == "word") return match_mode::whole_word;
    if (key == "substring" || key == "anywhere") return match_mode::substring;
    return std::nullopt;
}

auto matches(std::string_view content, std::string_view pattern, match_mode mode) -> bool {
    if (pattern.empty()) return false;

    const std::string haystack = util::to_lower(content);
    const std::string needle = util::to_lower(pattern);

    for (std::size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
        if (mode == match_mode::substring) return true;

        // Every occurrence is checked, not just the first: "4200 and 420"
        // should fire even though the first hit is inside a longer word.
        const bool open_left = at == 0 || !is_word_character(haystack[at - 1]);
        const std::size_t after = at + needle.size();
        const bool open_right = after >= haystack.size() || !is_word_character(haystack[after]);
        if (open_left && open_right) return true;
    }

    return false;
}

auto choose(std::span<const weighted_response> responses, std::uint64_t roll) -> const weighted_response* {
    std::uint64_t total = 0;
    for (const weighted_response& option : responses) {
        if (option.weight > 0) total += static_cast<std::uint64_t>(option.weight);
    }
    if (total == 0) return nullptr;

    std::uint64_t remaining = roll % total;
    for (const weighted_response& option : responses) {
        if (option.weight <= 0) continue;
        const auto weight = static_cast<std::uint64_t>(option.weight);
        if (remaining < weight) return &option;
        remaining -= weight;
    }

    // Unreachable while the weights above add up to `total`, but returning
    // the last positive option beats reaching the end of the function.
    return nullptr;
}

auto off_cooldown(std::optional<std::chrono::steady_clock::time_point> last_fired, std::chrono::steady_clock::time_point now,
                  std::chrono::seconds cooldown) -> bool {
    if (cooldown <= std::chrono::seconds::zero() || !last_fired) return true;
    return now - *last_fired >= cooldown;
}

// --------------------------------------------------------------------------

auto trigger_store::for_guild(dpp::snowflake guild_id) const -> std::vector<trigger> {
    const auto guard = db_->lock();

    std::vector<trigger> found;
    {
        auto query = db_->prepare(
            "SELECT id, pattern, match_mode, cooldown_s, enabled, respond_to_bots, message_flags FROM triggers "
            "WHERE guild_id = ? ORDER BY id",
            guild_id);
        while (query.step()) {
            trigger entry;
            entry.id = query.get<std::int64_t>(0);
            entry.guild_id = guild_id;
            entry.pattern = query.get<std::string>(1);
            entry.mode = match_mode_from_string(query.get<std::string>(2)).value_or(match_mode::whole_word);
            entry.cooldown = std::chrono::seconds(query.get<std::int64_t>(3));
            entry.enabled = query.get<bool>(4);
            entry.respond_to_bots = query.get<bool>(5);
            entry.message_flags = discord::channel_flags(query.get<std::int64_t>(6));
            found.push_back(std::move(entry));
        }
    }

    // Every response in the guild in one query, rather than one per trigger:
    // this runs for every message. `found` is ordered by id, so each
    // response's trigger is found by binary search.
    auto query = db_->prepare(
        "SELECT trigger_id, response, weight FROM trigger_responses "
        "WHERE trigger_id IN (SELECT id FROM triggers WHERE guild_id = ?) ORDER BY trigger_id, rowid",
        guild_id);
    while (query.step()) {
        const auto owner = query.get<std::int64_t>(0);
        const auto entry = std::ranges::lower_bound(found, owner, {}, &trigger::id);
        if (entry != found.end() && entry->id == owner) {
            entry->responses.push_back({.text = query.get<std::string>(1), .weight = query.get<int>(2)});
        }
    }

    return found;
}

auto trigger_store::find(std::int64_t id, dpp::snowflake guild_id) const -> std::optional<trigger> {
    for (trigger& entry : for_guild(guild_id)) {
        if (entry.id == id) return std::move(entry);
    }
    return std::nullopt;
}

auto trigger_store::replace_responses(std::int64_t trigger_id, std::span<const weighted_response> responses) -> void {
    db_->prepare("DELETE FROM trigger_responses WHERE trigger_id = ?", trigger_id).run();
    for (const weighted_response& option : responses) {
        db_->prepare("INSERT INTO trigger_responses (trigger_id, response, weight) VALUES (?, ?, ?)", trigger_id, option.text,
                     option.weight)
            .run();
    }
}

auto trigger_store::add(const trigger& entry) -> std::int64_t {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    db_->prepare(
           "INSERT INTO triggers (guild_id, pattern, match_mode, cooldown_s, enabled, respond_to_bots, message_flags) "
           "VALUES (?, ?, ?, ?, ?, ?, ?)",
           entry.guild_id, entry.pattern, to_string(entry.mode), entry.cooldown.count(), entry.enabled, entry.respond_to_bots,
           std::int64_t{discord::channel_flags(entry.message_flags)})
        .run();

    const std::int64_t id = db_->last_insert_rowid();
    replace_responses(id, entry.responses);

    tx.commit();
    return id;
}

auto trigger_store::update(const trigger& entry) -> bool {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    db_->prepare(
           "UPDATE triggers SET pattern = ?, match_mode = ?, cooldown_s = ?, enabled = ?, respond_to_bots = ?, message_flags = ? "
           "WHERE id = ? AND guild_id = ?",
           entry.pattern, to_string(entry.mode), entry.cooldown.count(), entry.enabled, entry.respond_to_bots,
           std::int64_t{discord::channel_flags(entry.message_flags)}, entry.id, entry.guild_id)
        .run();

    if (db_->changes() == 0) return false;

    replace_responses(entry.id, entry.responses);
    tx.commit();
    return true;
}

auto trigger_store::remove(std::int64_t id, dpp::snowflake guild_id) -> bool {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    db_->prepare("DELETE FROM triggers WHERE id = ? AND guild_id = ?", id, guild_id).run();
    if (db_->changes() == 0) return false;

    // The foreign key cascades, but only with foreign_keys=ON; deleting here
    // as well keeps this correct if that pragma ever changes.
    db_->prepare("DELETE FROM trigger_responses WHERE trigger_id = ?", id).run();

    tx.commit();
    return true;
}

auto trigger_store::seed_defaults(dpp::snowflake guild_id) -> int {
    const auto guard = db_->lock();

    auto count = db_->prepare("SELECT COUNT(*) FROM triggers WHERE guild_id = ?", guild_id);
    if (!count.step() || count.get<std::int64_t>(0) > 0) return 0;

    // The Java bot's three, which were a regular expression there and are
    // three literal patterns here (plan §11).
    static constexpr std::array patterns{"420", "4:20", "69"};
    for (const char* pattern : patterns) {
        add({.guild_id = guild_id,
             .pattern = pattern,
             .mode = match_mode::whole_word,
             .cooldown = default_trigger_cooldown,
             .enabled = true,
             .responses = {{.text = "nice", .weight = 1}}});
    }
    return static_cast<int>(patterns.size());
}

// --------------------------------------------------------------------------

trigger_responder::trigger_responder(const trigger_store& store, ports::clock& clock, std::function<std::uint64_t()> roll)
    : store_(&store), clock_(&clock), roll_(roll ? std::move(roll) : default_roll()) {}

auto trigger_responder::operator()(const incoming_message& message) -> stage_result {
    stage_result result;

    // Read before any lock is taken, so no message waits on SQLite while
    // holding up another's cooldown check.
    const std::vector<trigger> triggers = store_->for_guild(message.guild_id);
    for (const trigger& entry : triggers) {
        if (!matches(message.content, entry.pattern, entry.mode)) continue;

        // Past this point the pattern matched, so every way out is a reason
        // the bot stayed quiet — which is the question being asked whenever
        // somebody reports that a trigger "stopped working".
        if (!entry.enabled) {
            util::log().debug("trigger {} matched but is disabled", entry.id);
            continue;
        }

        // The allowlist decided this bot may be heard; this decides whether
        // this particular trigger answers it (plan §14.4).
        if (message.from_bot && !entry.respond_to_bots) {
            util::log().debug("trigger {} matched a bot's message but does not answer bots", entry.id);
            continue;
        }

        // DPP hands messages to several threads at once, and the moment this
        // exists for, 4:20, is several people posting at once. Checking the
        // cooldown, picking a response and claiming the cooldown are one
        // step under the lock, so two messages cannot both find the trigger
        // ready, and the generator is never called from two threads.
        const std::scoped_lock lock(mutex_);
        const auto now = clock_->steady_now();

        const auto key = std::pair{entry.id, message.channel_id};
        const auto seen = last_fired_.find(key);
        const auto last = seen == last_fired_.end() ? std::nullopt : std::optional<std::chrono::steady_clock::time_point>(seen->second);
        if (!off_cooldown(last, now, entry.cooldown)) {
            const auto waited = std::chrono::duration_cast<std::chrono::seconds>(now - *last);
            util::log().debug("trigger {} matched but is on cooldown in channel {}: {} of {}", entry.id, message.channel_id, waited,
                              entry.cooldown);
            continue;
        }

        const weighted_response* reply = choose(entry.responses, roll_());
        if (reply == nullptr) {
            util::log().debug("trigger {} matched but has no response worth picking", entry.id);
            continue;
        }

        util::log().debug("trigger {} (\"{}\") fired in channel {}", entry.id, entry.pattern, message.channel_id);
        last_fired_[key] = now;
        result.actions.emplace_back(send_message{.channel_id = message.channel_id,
                                                 .content = reply->text,
                                                 .flags = entry.message_flags,
                                                 .what = std::format("trigger {}'s reply", entry.id)});
    }

    return result;
}

} // namespace latibot::events
