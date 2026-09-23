#include "core/events/triggers.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/ports/clock.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <numeric>
#include <random>
#include <utility>

namespace latibot::events {
namespace {

char lower(char letter) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
}

std::string lowercased(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), lower);
    return result;
}

/// Word characters for the purposes of whole-word matching: what sits either
/// side of "420" in "4200" but not in "it's 420 somewhere".
bool is_word_character(char letter) {
    const auto byte = static_cast<unsigned char>(letter);
    return byte == '_' || std::isalnum(byte) != 0;
}

/// A generator per responder, seeded once. Sharing one across guilds is fine:
/// the only thing riding on it is which of several jokes gets picked.
std::function<std::uint64_t()> default_roll() {
    auto engine = std::make_shared<std::mt19937_64>(std::random_device{}());
    return [engine] { return (*engine)(); };
}

} // namespace

std::string_view to_string(match_mode mode) noexcept {
    return mode == match_mode::substring ? "substring" : "whole_word";
}

std::optional<match_mode> match_mode_from_string(std::string_view name) {
    const std::string key = lowercased(name);
    if (key == "whole_word" || key == "word") {
        return match_mode::whole_word;
    }
    if (key == "substring" || key == "anywhere") {
        return match_mode::substring;
    }
    return std::nullopt;
}

bool matches(std::string_view content, std::string_view pattern, match_mode mode) {
    if (pattern.empty()) {
        return false;
    }

    const std::string haystack = lowercased(content);
    const std::string needle = lowercased(pattern);

    for (std::size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
        if (mode == match_mode::substring) {
            return true;
        }

        // Every occurrence is checked, not just the first: "4200 and 420"
        // should fire even though the first hit is inside a longer word.
        const bool open_left = at == 0 || !is_word_character(haystack[at - 1]);
        const std::size_t after = at + needle.size();
        const bool open_right = after >= haystack.size() || !is_word_character(haystack[after]);
        if (open_left && open_right) {
            return true;
        }
    }

    return false;
}

const weighted_response* choose(std::span<const weighted_response> responses, std::uint64_t roll) {
    std::uint64_t total = 0;
    for (const weighted_response& option : responses) {
        if (option.weight > 0) {
            total += static_cast<std::uint64_t>(option.weight);
        }
    }
    if (total == 0) {
        return nullptr;
    }

    std::uint64_t remaining = roll % total;
    for (const weighted_response& option : responses) {
        if (option.weight <= 0) {
            continue;
        }
        const auto weight = static_cast<std::uint64_t>(option.weight);
        if (remaining < weight) {
            return &option;
        }
        remaining -= weight;
    }

    // Unreachable while the weights above add up to `total`, but returning
    // the last positive option beats reaching the end of the function.
    return nullptr;
}

bool off_cooldown(std::optional<std::chrono::steady_clock::time_point> last_fired, std::chrono::steady_clock::time_point now,
                  std::chrono::seconds cooldown) {
    if (cooldown <= std::chrono::seconds::zero() || !last_fired) {
        return true;
    }
    return now - *last_fired >= cooldown;
}

// --------------------------------------------------------------------------

std::vector<trigger> trigger_store::for_guild(dpp::snowflake guild_id) const {
    const auto guard = db_->lock();

    std::vector<trigger> found;
    {
        auto query = db_->prepare(
            "SELECT id, pattern, match_mode, cooldown_s, enabled FROM triggers "
            "WHERE guild_id = ? ORDER BY id",
            static_cast<std::uint64_t>(guild_id));
        while (query.step()) {
            trigger entry;
            entry.id = query.get<std::int64_t>(0);
            entry.guild_id = guild_id;
            entry.pattern = query.get<std::string>(1);
            entry.mode = match_mode_from_string(query.get<std::string>(2)).value_or(match_mode::whole_word);
            entry.cooldown = std::chrono::seconds(query.get<std::int64_t>(3));
            entry.enabled = query.get<bool>(4);
            found.push_back(std::move(entry));
        }
    }

    for (trigger& entry : found) {
        auto query = db_->prepare("SELECT response, weight FROM trigger_responses WHERE trigger_id = ? ORDER BY rowid", entry.id);
        while (query.step()) {
            entry.responses.push_back({.text = query.get<std::string>(0), .weight = query.get<int>(1)});
        }
    }

    return found;
}

std::optional<trigger> trigger_store::find(std::int64_t id, dpp::snowflake guild_id) const {
    for (trigger& entry : for_guild(guild_id)) {
        if (entry.id == id) {
            return std::move(entry);
        }
    }
    return std::nullopt;
}

void trigger_store::replace_responses(std::int64_t trigger_id, std::span<const weighted_response> responses) {
    db_->prepare("DELETE FROM trigger_responses WHERE trigger_id = ?", trigger_id).run();
    for (const weighted_response& option : responses) {
        db_->prepare("INSERT INTO trigger_responses (trigger_id, response, weight) VALUES (?, ?, ?)", trigger_id, option.text,
                     option.weight)
            .run();
    }
}

std::int64_t trigger_store::add(const trigger& entry) {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    db_->prepare(
           "INSERT INTO triggers (guild_id, pattern, match_mode, cooldown_s, enabled) "
           "VALUES (?, ?, ?, ?, ?)",
           static_cast<std::uint64_t>(entry.guild_id), entry.pattern, to_string(entry.mode), entry.cooldown.count(), entry.enabled)
        .run();

    const std::int64_t id = db_->last_insert_rowid();
    replace_responses(id, entry.responses);

    tx.commit();
    return id;
}

bool trigger_store::update(const trigger& entry) {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    db_->prepare(
           "UPDATE triggers SET pattern = ?, match_mode = ?, cooldown_s = ?, enabled = ? "
           "WHERE id = ? AND guild_id = ?",
           entry.pattern, to_string(entry.mode), entry.cooldown.count(), entry.enabled, entry.id,
           static_cast<std::uint64_t>(entry.guild_id))
        .run();

    if (db_->changes() == 0) {
        return false;
    }

    replace_responses(entry.id, entry.responses);
    tx.commit();
    return true;
}

bool trigger_store::remove(std::int64_t id, dpp::snowflake guild_id) {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    db_->prepare("DELETE FROM triggers WHERE id = ? AND guild_id = ?", id, static_cast<std::uint64_t>(guild_id)).run();
    if (db_->changes() == 0) {
        return false;
    }

    // The foreign key cascades, but only with foreign_keys=ON; deleting here
    // as well keeps this correct if that pragma ever changes.
    db_->prepare("DELETE FROM trigger_responses WHERE trigger_id = ?", id).run();

    tx.commit();
    return true;
}

int trigger_store::seed_defaults(dpp::snowflake guild_id) {
    const auto guard = db_->lock();

    auto count = db_->prepare("SELECT COUNT(*) FROM triggers WHERE guild_id = ?", static_cast<std::uint64_t>(guild_id));
    if (!count.step() || count.get<std::int64_t>(0) > 0) {
        return 0;
    }

    // The Java bot's three, which were a regular expression there and are
    // three literal patterns here (plan v4 §11).
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

stage_result trigger_responder::operator()(const incoming_message& message) {
    stage_result result;

    const auto now = clock_->steady_now();
    for (const trigger& entry : store_->for_guild(message.guild_id)) {
        if (!entry.enabled || !matches(message.content, entry.pattern, entry.mode)) {
            continue;
        }

        const auto key = std::pair{entry.id, message.channel_id};
        const auto seen = last_fired_.find(key);
        const auto last = seen == last_fired_.end() ? std::nullopt : std::optional<std::chrono::steady_clock::time_point>(seen->second);
        if (!off_cooldown(last, now, entry.cooldown)) {
            continue;
        }

        const weighted_response* reply = choose(entry.responses, roll_());
        if (reply == nullptr) {
            continue;
        }

        last_fired_[key] = now;
        result.actions.emplace_back(send_message{.channel_id = message.channel_id, .content = reply->text});
    }

    return result;
}

void trigger_responder::forget_cooldowns() {
    last_fired_.clear();
}

} // namespace latibot::events
