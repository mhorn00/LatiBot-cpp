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

namespace latibot::capabilities {
class model_replies;
}

namespace latibot::events {

/// The module's tables, `triggers`, `trigger_responses` and
/// `trigger_imports`, version 1 first (docs/modules/Module_Plan_Final.md
/// §7.1). Defined in module.cpp.
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
    std::string text{};
    int weight = 1;
};

/// What sets a trigger off (src/modules/triggers/docs/Triggers.md §2.1).
enum class trigger_condition : std::uint8_t {
    /// Its pattern matches the message's text.
    text,
    /// The message addresses LatiBot: an @mention, or its name first.
    addressed,
    /// The message replies to whoever answers the trigger: LatiBot's
    /// message, or its persona's.
    reply,
};

[[nodiscard]] auto to_string(trigger_condition condition) noexcept -> std::string_view;
[[nodiscard]] auto trigger_condition_from_string(std::string_view name) -> std::optional<trigger_condition>;

/// How a trigger answers (src/modules/triggers/docs/Triggers.md §2.6).
enum class trigger_kind : std::uint8_t {
    /// One of its fixed responses.
    responses,
    /// The model answers: chiming in with its prompt on a text match, or as
    /// spoken to on being addressed or replied to.
    model,
    /// The check model is asked its question first; a yes is answered by
    /// the model as spoken to.
    analysis,
};

struct trigger {
    std::int64_t id = 0;
    dpp::snowflake guild_id{};
    std::string pattern{};
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

    /// Whether it answers as a Discord reply to the message rather than a
    /// plain post (src/modules/triggers/docs/Triggers.md §2.2). A persona
    /// cannot: its webhook has no way to reply.
    bool reply = false;

    /// The fixed replies it picks from. Empty for a model trigger.
    std::vector<weighted_response> responses{};

    /// What a model trigger asks the language model to say, in a line:
    /// "Someone mentioned pineapple pizza. Defend it." Empty for a trigger
    /// with responses (src/modules/triggers/docs/Triggers.md §2.6).
    std::string prompt{};

    /// The chance a model trigger answers a match, 0 to 1.
    double chance = 1.0;

    /// Who answers a model trigger: one of the language model's personas,
    /// or 0 for LatiBot.
    std::int64_t persona_id = 0;

    /// For an analysis: the yes-or-no question the check model is asked
    /// about a message that matched, "Is this message talking to LatiBot?".
    std::string question{};

    trigger_condition condition = trigger_condition::text;

    /// Which built-in trigger this is, such as "latibot-named"; empty for
    /// one someone made. A built-in can be turned off and edited, never
    /// removed, and keeps its kind, condition and persona.
    std::string builtin{};

    /// How it answers: a question makes it an analysis; a prompt, or no
    /// responses on being addressed or replied to, the model.
    [[nodiscard]] auto kind() const noexcept -> trigger_kind {
        if (!question.empty()) return trigger_kind::analysis;
        if (responses.empty() && (!prompt.empty() || condition != trigger_condition::text)) return trigger_kind::model;
        return trigger_kind::responses;
    }

    /// Whether it asks the model rather than picking a response.
    [[nodiscard]] auto asks_model() const noexcept -> bool { return kind() != trigger_kind::responses; }
};

/// The default cooldown for a new trigger.
inline constexpr std::chrono::seconds default_trigger_cooldown{30};

/// The default cooldown for a new model trigger: longer, since each answer
/// is a model call.
inline constexpr std::chrono::seconds default_model_cooldown{300};

/// The longest prompt: a line, not a document.
inline constexpr std::size_t prompt_limit = 500;

/// The longest analysis question.
inline constexpr std::size_t question_limit = 300;

/// Why a trigger cannot be saved as it is, or nothing when it can: a pattern
/// that will not work, nothing to answer with, or an analysis that could run
/// on every message. An analysis needs a pattern of whole words or text
/// anywhere, with at least one letter or digit, so the check model is only
/// ever asked about messages that contain it.
[[nodiscard]] auto trigger_problem(const trigger& entry) -> std::optional<std::string>;

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

    /// False when it does not exist, belongs to another guild, or is built
    /// in, which is never removed.
    auto remove(std::int64_t id, dpp::snowflake guild_id) -> bool;

    /// Adds the built-in triggers `guild` lacks, and removes those of
    /// personas no longer in `personas`, its persona ids. Returns whether
    /// anything changed (src/modules/triggers/docs/Triggers.md §2.8).
    auto ensure_builtins(dpp::snowflake guild, std::span<const std::int64_t> personas) -> bool;

    /// Copies the language model's advanced triggers, from the table it kept
    /// them in before the triggers module took them over, `llm_triggers`,
    /// once: `trigger_imports` remembers it was done. Each becomes a model
    /// trigger with a new id, answering as a reply, as they did. Returns how
    /// many it copied; none without that table.
    auto import_llm_triggers() -> int;

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
/// (src/core/docs/Message_Pipeline.md §2.2). Every trigger with responses
/// that matches replies; then, only when none did, the first model trigger
/// that matches and wins its roll asks the model, through `model`, which is
/// null without the language model. Either marks the message answered,
/// which keeps the model from also joining in.
///
/// It hears the bot's own messages and webhooks' too, when `model` is
/// there, since LatiBot and its personas set off each other's model
/// triggers; only a model trigger that answers bots answers them, and never
/// its own writer's (src/modules/triggers/docs/Triggers.md §2.6).
///
/// Safe to call from several threads at once, which is how DPP delivers
/// messages.
class trigger_responder {
public:
    /// `roll` supplies the randomness for weighted responses and model
    /// triggers' chances. The default is a seeded generator; tests pass
    /// something predictable.
    trigger_responder(trigger_store& store, ports::clock& clock, std::function<std::uint64_t()> roll = {},
                      capabilities::model_replies* model = nullptr);

    auto operator()(const incoming_message& message) -> stage_result;

    /// What `entry` does now that it fired on `message`: picks a response,
    /// or asks the model. Also how a trigger is fired by hand, `forced`,
    /// when an analysis skips its check and is answered as spoken to
    /// (src/modules/triggers/docs/Triggers.md §2.9).
    auto fire(const trigger& entry, const incoming_message& message, stage_result& result, bool forced = false) -> void;

private:
    /// The first trigger in `triggers` that asks the model to fire, as the
    /// model's answer: one answered as spoken to first, then an analysis,
    /// then a comment. `writer` is who of LatiBot's own wrote the message.
    auto ask_model(const incoming_message& message, const std::vector<trigger>& triggers, std::optional<std::int64_t> writer,
                   stage_result& result) -> void;

    /// Whether `entry`'s condition holds for the message.
    [[nodiscard]] auto condition_holds(const trigger& entry, const incoming_message& message) const -> bool;

    /// The guild's triggers, its built-ins made first when the model is there
    /// and its personas changed since last time.
    auto triggers_of(dpp::snowflake guild) -> std::vector<trigger>;

    /// Claims the trigger's cooldown in the channel if it is off it, as one
    /// step under the lock.
    auto claim(const trigger& entry, dpp::snowflake channel) -> bool;

    trigger_store* store_;
    ports::clock* clock_;
    capabilities::model_replies* model_;

    /// Guards `roll_` and `last_fired_`.
    std::mutex mutex_;
    std::function<std::uint64_t()> roll_;

    /// Cooldowns are per (trigger, channel) and kept here rather than in the
    /// database: forgetting them across a restart costs one extra reply.
    std::map<std::pair<std::int64_t, dpp::snowflake>, std::chrono::steady_clock::time_point> last_fired_;

    /// Each guild's personas when its built-ins were last made; guarded by
    /// `mutex_`.
    std::map<dpp::snowflake, std::vector<std::int64_t>> seeded_;
};

/// Makes sure `guild` has its built-in triggers, with `model`, the language
/// model, for its personas; nothing without it. For the command and the
/// panel, which may show a guild before any message there.
auto ensure_builtins(trigger_store& store, const capabilities::model_replies* model, dpp::snowflake guild) -> void;

} // namespace latibot::events
