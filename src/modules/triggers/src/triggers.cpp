#include "triggers.hpp"

#include "core/capabilities/model_replies.hpp"
#include "core/db/database.hpp"
#include "core/db/error.hpp"
#include "core/db/statement.hpp"
#include "core/ports/clock.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <numeric>
#include <random>
#include <set>
#include <utility>

namespace latibot::events {
namespace {

/// A generator per responder, seeded once. Sharing one across guilds is fine:
/// the only thing riding on it is which of several jokes gets picked.
auto default_roll() -> std::function<std::uint64_t()> {
    auto engine = std::make_shared<std::mt19937_64>(std::random_device{}());
    return [engine] { return (*engine)(); };
}

/// The built-in triggers' keys. A persona's is this prefix and its id.
constexpr std::string_view addressed_key = "latibot-addressed";
constexpr std::string_view reply_key = "latibot-reply";
constexpr std::string_view named_key = "latibot-named";
constexpr std::string_view persona_reply_prefix = "persona-reply:";

/// The built-in triggers a guild has, with these personas
/// (src/modules/triggers/docs/Triggers.md §2.8): what LatiBot answered on its
/// own before the triggers took over, as triggers a server can turn off or
/// change.
auto builtins(dpp::snowflake guild, std::span<const std::int64_t> personas) -> std::vector<trigger> {
    // Answering as spoken to, as a Discord reply, to people and the bots the
    // server allowed; with no cooldown, as being addressed never had one.
    const auto spoken = [guild](trigger_condition condition, std::string_view key) {
        trigger entry;
        entry.guild_id = guild;
        entry.cooldown = std::chrono::seconds{0};
        entry.respond_to_bots = true;
        entry.reply = true;
        entry.condition = condition;
        entry.builtin = std::string(key);
        return entry;
    };

    std::vector<trigger> made;
    made.push_back(spoken(trigger_condition::addressed, addressed_key));
    made.push_back(spoken(trigger_condition::reply, reply_key));

    trigger named = spoken(trigger_condition::text, named_key);
    named.pattern = "latibot";
    named.mode = match_mode::whole_word;
    named.question = "Is this message talking to LatiBot, or asking it something, rather than only talking about it?";
    made.push_back(std::move(named));

    // A reply to a persona's message is for the persona, from people only: a
    // persona never answers a bot by a reply.
    for (const std::int64_t persona : personas) {
        trigger entry = spoken(trigger_condition::reply, std::format("{}{}", persona_reply_prefix, persona));
        entry.persona_id = persona;
        entry.respond_to_bots = false;
        entry.reply = false;
        made.push_back(std::move(entry));
    }
    return made;
}

} // namespace

auto to_string(trigger_condition condition) noexcept -> std::string_view {
    switch (condition) {
    case trigger_condition::addressed:
        return "addressed";
    case trigger_condition::reply:
        return "reply";
    case trigger_condition::text:
        break;
    }
    return "text";
}

auto trigger_condition_from_string(std::string_view name) -> std::optional<trigger_condition> {
    const std::string key = util::to_lower(util::trim(name));
    if (key == "text" || key.empty()) return trigger_condition::text;
    if (key == "addressed" || key == "mention") return trigger_condition::addressed;
    if (key == "reply") return trigger_condition::reply;
    return std::nullopt;
}

auto trigger_problem(const trigger& entry) -> std::optional<std::string> {
    if (entry.condition == trigger_condition::text) {
        if (util::is_blank(entry.pattern)) return std::string("a pattern of only whitespace would match everything");
        if (const auto problem = util::pattern_error(entry.pattern, entry.mode))
            return std::format("that pattern won't work: {}", *problem);
    }
    if (util::character_count(entry.prompt) > prompt_limit) {
        return std::format("a prompt is at most {} characters: a line, not a document", prompt_limit);
    }

    switch (entry.kind()) {
    case trigger_kind::responses:
        if (entry.responses.empty()) return std::string("that leaves no responses to pick from");
        break;
    case trigger_kind::model:
        if (entry.condition == trigger_condition::text && util::is_blank(entry.prompt)) {
            return std::string("a trigger that asks the model on a pattern needs a prompt saying what to say");
        }
        break;
    case trigger_kind::analysis: {
        if (util::character_count(entry.question) > question_limit) {
            return std::format("a question is at most {} characters", question_limit);
        }
        // The check is a model call: it must only ever be asked about
        // messages that contain a word, never every message.
        const bool has_word = std::ranges::any_of(entry.pattern, [](char letter) {
            const auto byte = static_cast<unsigned char>(letter);
            return byte >= 128 || std::isalnum(byte) != 0;
        });
        if (entry.condition != trigger_condition::text || entry.mode == match_mode::regex || !has_word) {
            return std::string(
                "an analysis needs a pattern of words, matched as whole words or anywhere, so it is not run on every message");
        }
        if (!entry.responses.empty()) return std::string("an analysis is answered by the model, not with responses");
        break;
    }
    }
    return std::nullopt;
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

// --------------------------------------------------------------------------

auto trigger_store::for_guild(dpp::snowflake guild_id) const -> std::vector<trigger> {
    const auto guard = db_->lock();

    std::vector<trigger> found;
    {
        auto query = db_->prepare(
            "SELECT id, pattern, match_mode, cooldown_s, enabled, respond_to_bots, message_flags, reply, prompt, chance, persona_id, "
            "question, condition, COALESCE(builtin, '') FROM triggers WHERE guild_id = ? ORDER BY id",
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
            entry.reply = query.get<bool>(7);
            entry.prompt = query.get<std::string>(8);
            entry.chance = query.get<double>(9);
            entry.persona_id = query.get<std::int64_t>(10);
            entry.question = query.get<std::string>(11);
            entry.condition = trigger_condition_from_string(query.get<std::string>(12)).value_or(trigger_condition::text);
            entry.builtin = query.get<std::string>(13);
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
           "INSERT INTO triggers (guild_id, pattern, match_mode, cooldown_s, enabled, respond_to_bots, message_flags, reply, prompt, "
           "chance, persona_id, question, condition, builtin) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULLIF(?, ''))",
           entry.guild_id, entry.pattern, to_string(entry.mode), entry.cooldown.count(), entry.enabled, entry.respond_to_bots,
           std::int64_t{discord::channel_flags(entry.message_flags)}, entry.reply, entry.prompt, entry.chance, entry.persona_id,
           entry.question, to_string(entry.condition), entry.builtin)
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
           "UPDATE triggers SET pattern = ?, match_mode = ?, cooldown_s = ?, enabled = ?, respond_to_bots = ?, message_flags = ?, "
           "reply = ?, prompt = ?, chance = ?, persona_id = ?, question = ?, condition = ? WHERE id = ? AND guild_id = ?",
           entry.pattern, to_string(entry.mode), entry.cooldown.count(), entry.enabled, entry.respond_to_bots,
           std::int64_t{discord::channel_flags(entry.message_flags)}, entry.reply, entry.prompt, entry.chance, entry.persona_id,
           entry.question, to_string(entry.condition), entry.id, entry.guild_id)
        .run();

    if (db_->changes() == 0) return false;

    replace_responses(entry.id, entry.responses);
    tx.commit();
    return true;
}

auto trigger_store::remove(std::int64_t id, dpp::snowflake guild_id) -> bool {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    db_->prepare("DELETE FROM triggers WHERE id = ? AND guild_id = ? AND builtin IS NULL", id, guild_id).run();
    if (db_->changes() == 0) return false;

    // The foreign key cascades, but only with foreign_keys=ON; deleting here
    // as well keeps this correct if that pragma ever changes.
    db_->prepare("DELETE FROM trigger_responses WHERE trigger_id = ?", id).run();

    tx.commit();
    return true;
}

auto trigger_store::ensure_builtins(dpp::snowflake guild, std::span<const std::int64_t> personas) -> bool {
    std::set<std::string, std::less<>> have;
    {
        const auto guard = db_->lock();
        auto query = db_->prepare("SELECT builtin FROM triggers WHERE guild_id = ? AND builtin IS NOT NULL", guild);
        while (query.step()) {
            have.insert(query.get<std::string>(0));
        }
    }

    bool changed = false;
    std::set<std::string, std::less<>> wanted;
    for (trigger& entry : builtins(guild, personas)) {
        wanted.insert(entry.builtin);
        if (have.contains(entry.builtin)) continue;
        // The unique index makes a second startup, or two threads at once,
        // add nothing twice.
        try {
            add(entry);
            changed = true;
            util::log().info("built-in trigger {} added in guild {}", entry.builtin, guild);
        } catch (const db::db_error& error) {
            util::log().debug("built-in trigger {} in guild {} was added meanwhile: {}", entry.builtin, guild, error.what());
        }
    }

    // A persona's own goes with the persona.
    for (const std::string& key : have) {
        if (!key.starts_with(persona_reply_prefix) || wanted.contains(key)) continue;
        const auto guard = db_->lock();
        db::transaction tx(*db_);
        db_->prepare("DELETE FROM trigger_responses WHERE trigger_id IN (SELECT id FROM triggers WHERE guild_id = ? AND builtin = ?)",
                     guild, key)
            .run();
        db_->prepare("DELETE FROM triggers WHERE guild_id = ? AND builtin = ?", guild, key).run();
        tx.commit();
        changed = true;
        util::log().info("built-in trigger {} removed from guild {}, with its persona", key, guild);
    }
    return changed;
}

auto ensure_builtins(trigger_store& store, const capabilities::model_replies* model, dpp::snowflake guild) -> void {
    if (model == nullptr || guild.empty()) return;
    std::vector<std::int64_t> personas;
    for (const capabilities::persona_name& each : model->personas(guild)) {
        personas.push_back(each.id);
    }
    store.ensure_builtins(guild, personas);
}

auto trigger_store::import_llm_triggers() -> int {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    {
        auto done = db_->prepare("SELECT 1 FROM trigger_imports WHERE source = 'llm_triggers'");
        if (done.step()) return 0;
        auto exists = db_->prepare("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'llm_triggers'");
        if (!exists.step()) return 0;
    }

    // They answered as LatiBot's replies, silently, with previews; a persona
    // was NULL for LatiBot then.
    db_->prepare(
           "INSERT INTO triggers (guild_id, pattern, match_mode, cooldown_s, enabled, respond_to_bots, message_flags, reply, prompt, "
           "chance, persona_id) "
           "SELECT guild_id, pattern, match_mode, cooldown_s, enabled, respond_to_bots, ?, 1, context_prompt, probability, "
           "COALESCE(persona_id, 0) FROM llm_triggers ORDER BY id",
           std::int64_t{dpp::m_suppress_notifications})
        .run();
    const int copied = db_->changes();
    db_->prepare("INSERT INTO trigger_imports (source) VALUES ('llm_triggers')").run();

    tx.commit();
    return copied;
}

// --------------------------------------------------------------------------

trigger_responder::trigger_responder(trigger_store& store, ports::clock& clock, std::function<std::uint64_t()> roll,
                                     capabilities::model_replies* model)
    : store_(&store), clock_(&clock), model_(model), roll_(roll ? std::move(roll) : default_roll()) {}

auto trigger_responder::triggers_of(dpp::snowflake guild) -> std::vector<trigger> {
    if (model_ != nullptr && !guild.empty()) {
        // The personas are read for every message anyway; the built-ins are
        // only looked at when they changed, or the guild is new here.
        std::vector<std::int64_t> personas;
        for (const capabilities::persona_name& each : model_->personas(guild)) {
            personas.push_back(each.id);
        }
        bool fresh = false;
        {
            const std::scoped_lock lock(mutex_);
            const auto seen = seeded_.find(guild);
            fresh = seen == seeded_.end() || seen->second != personas;
        }
        if (fresh) {
            store_->ensure_builtins(guild, personas);
            const std::scoped_lock lock(mutex_);
            seeded_[guild] = std::move(personas);
        }
    }
    return store_->for_guild(guild);
}

auto trigger_responder::condition_holds(const trigger& entry, const incoming_message& message) const -> bool {
    switch (entry.condition) {
    case trigger_condition::text:
        return matches(message.content, entry.pattern, entry.mode);
    case trigger_condition::addressed:
        return model_ != nullptr && model_->addresses(message);
    case trigger_condition::reply:
        return model_ != nullptr && model_->replied_to(message) == entry.persona_id;
    }
    return false;
}

auto trigger_responder::claim(const trigger& entry, dpp::snowflake channel) -> bool {
    // Checking the cooldown and claiming it are one step under the lock, so
    // two messages at once cannot both find the trigger ready.
    const auto now = clock_->steady_now();
    const auto key = std::pair{entry.id, channel};
    const auto seen = last_fired_.find(key);
    const auto last = seen == last_fired_.end() ? std::nullopt : std::optional<std::chrono::steady_clock::time_point>(seen->second);
    if (!off_cooldown(last, now, entry.cooldown)) {
        const auto waited = std::chrono::duration_cast<std::chrono::seconds>(now - *last);
        util::log().debug("trigger {} matched but is on cooldown in channel {}: {} of {}", entry.id, channel, waited, entry.cooldown);
        return false;
    }
    last_fired_[key] = now;
    return true;
}

auto trigger_responder::operator()(const incoming_message& message) -> stage_result {
    stage_result result;

    // The bot's own messages, webhooks' and those of bots nobody allowed
    // reach this stage only for the model triggers, which LatiBot and its
    // personas set off for each other. Anyone else's is left alone, and none
    // of them gets a fixed reply.
    std::optional<std::int64_t> writer;
    const bool own = message.from_self || (message.from_bot && !message.author_is_allowed_bot);
    if (own) {
        if (model_ == nullptr) return result;
        writer = model_->own_writer(message);
        if (!writer) return result;
    }

    // Read before any lock is taken, so no message waits on SQLite while
    // holding up another's cooldown check.
    const std::vector<trigger> triggers = triggers_of(message.guild_id);
    for (const trigger& entry : triggers) {
        if (own || entry.asks_model() || !condition_holds(entry, message)) continue;

        // Past this point the pattern matched, so every way out is a reason
        // the bot stayed quiet — which is the question being asked whenever
        // somebody reports that a trigger "stopped working".
        if (!entry.enabled) {
            util::log().debug("trigger {} matched but is disabled", entry.id);
            continue;
        }

        // The allowlist decided this bot may be heard; this decides whether
        // this particular trigger answers it
        // (src/core/docs/Message_Pipeline.md §2.1).
        if (message.from_bot && !entry.respond_to_bots) {
            util::log().debug("trigger {} matched a bot's message but does not answer bots", entry.id);
            continue;
        }

        // DPP hands messages to several threads at once, and the moment this
        // exists for, 4:20, is several people posting at once. The cooldown
        // and the pick are one step under the lock, so the generator is
        // never called from two threads.
        const std::scoped_lock lock(mutex_);
        if (!claim(entry, message.channel_id)) continue;

        const weighted_response* reply = choose(entry.responses, roll_());
        if (reply == nullptr) {
            util::log().debug("trigger {} matched but has no response worth picking", entry.id);
            continue;
        }

        util::log().debug("trigger {} (\"{}\") fired in channel {}", entry.id, entry.pattern, message.channel_id);
        result.answered = true;
        result.actions.emplace_back(send_message{.channel_id = message.channel_id,
                                                 .content = reply->text,
                                                 .flags = entry.message_flags,
                                                 .reply_to = entry.reply ? message.message_id : dpp::snowflake{},
                                                 .what = std::format("trigger {}'s reply", entry.id)});
    }

    // A fixed reply wins: the model is not asked as well.
    if (!result.answered && model_ != nullptr) ask_model(message, triggers, writer, result);
    return result;
}

auto trigger_responder::ask_model(const incoming_message& message, const std::vector<trigger>& triggers, std::optional<std::int64_t> writer,
                                  stage_result& result) -> void {
    // Spoken to first, then an analysis, then a comment: being addressed or
    // replied to is answered as such even when a word in the message also
    // matches something.
    const auto rank = [](const trigger& entry) {
        if (entry.kind() == trigger_kind::analysis) return 1;
        return entry.condition == trigger_condition::text ? 2 : 0;
    };
    std::vector<const trigger*> candidates;
    for (const trigger& entry : triggers) {
        if (entry.asks_model()) candidates.push_back(&entry);
    }
    std::ranges::stable_sort(candidates, {}, [&](const trigger* entry) { return rank(*entry); });

    for (const trigger* candidate : candidates) {
        const trigger& entry = *candidate;
        if (!condition_holds(entry, message)) continue;
        if (!entry.enabled) {
            util::log().debug("model trigger {} matched but is disabled", entry.id);
            continue;
        }

        // A bot sets one off only when it says it answers bots, and never
        // one its own writer answers: no persona answers itself, and LatiBot
        // does not answer LatiBot (src/modules/triggers/docs/Triggers.md §2.6).
        if ((message.from_bot || writer) && !entry.respond_to_bots) {
            util::log().debug("model trigger {} matched a bot's message but does not answer bots", entry.id);
            continue;
        }
        if (writer && *writer == entry.persona_id) {
            util::log().debug("model trigger {} matched a message from whoever answers it", entry.id);
            continue;
        }

        {
            const std::scoped_lock lock(mutex_);
            // The roll first: a match that loses it does not start the
            // cooldown. 53 bits make an even double in [0, 1).
            const double rolled = static_cast<double>(roll_() >> 11U) * 0x1.0p-53;
            if (rolled >= entry.chance) {
                util::log().debug("model trigger {} matched but lost its roll ({:.2f} against {:.2f})", entry.id, rolled, entry.chance);
                continue;
            }
            if (!claim(entry, message.channel_id)) continue;
        }

        // The first to fire is the only one: one model call per message.
        util::log().debug("model trigger {} fired in channel {}", entry.id, message.channel_id);
        fire(entry, message, result);
        return;
    }
}

auto trigger_responder::fire(const trigger& entry, const incoming_message& message, stage_result& result, bool forced) -> void {
    if (!entry.asks_model()) {
        const weighted_response* reply = [&] {
            const std::scoped_lock lock(mutex_);
            return choose(entry.responses, roll_());
        }();
        if (reply == nullptr) {
            util::log().debug("trigger {} fired but has no response worth picking", entry.id);
            return;
        }
        result.answered = true;
        result.actions.emplace_back(send_message{.channel_id = message.channel_id,
                                                 .content = reply->text,
                                                 .flags = entry.message_flags,
                                                 .reply_to = entry.reply ? message.message_id : dpp::snowflake{},
                                                 .what = std::format("trigger {}'s reply", entry.id)});
        return;
    }
    if (model_ == nullptr) {
        util::log().debug("trigger {} asks the model, which is not built in", entry.id);
        return;
    }

    capabilities::model_answer how = capabilities::model_answer::comment;
    if (entry.kind() == trigger_kind::analysis && !forced) {
        how = capabilities::model_answer::analysis;
    } else if (entry.condition != trigger_condition::text || entry.kind() == trigger_kind::analysis) {
        how = capabilities::model_answer::spoken_to;
    }
    std::vector<action> answer = model_->answer(message, {.trigger_id = entry.id,
                                                          .prompt = entry.prompt,
                                                          .persona_id = entry.persona_id,
                                                          .reply = entry.reply && entry.persona_id == 0,
                                                          .how = how,
                                                          .question = entry.question});
    if (answer.empty()) return;
    result.answered = true;
    for (action& step : answer) {
        result.actions.push_back(std::move(step));
    }
}

} // namespace latibot::events
