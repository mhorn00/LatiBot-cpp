#pragma once

#include "advanced_triggers.hpp"
#include "ask.hpp"
#include "conversation.hpp"
#include "core/events/message_pipeline.hpp"
#include "guards.hpp"
#include "llm_config.hpp"
#include "models.hpp"
#include "personas.hpp"
#include "responder.hpp"
#include "settings.hpp"
#include "spend.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::config {
class guild_settings;
} // namespace latibot::config

namespace latibot::events {}

namespace latibot::ports {
class clock;
}

namespace latibot::capabilities {
class link_replacements;
class speech;
} // namespace latibot::capabilities

namespace latibot::llm {

/// What the stage decides: notes it posts itself, and the `ask_llm` the
/// pipeline hands to the responder (src/core/docs/Message_Pipeline.md §3).
using stage_result = events::own_stage_result<ask_llm>;

/// Whether a message starts with the bot's name followed by anything but a
/// letter: "latibot, what's up", but not "latibots are great".
[[nodiscard]] auto starts_with_name(std::string_view content, std::string_view bot_name) -> bool;

/// Whether a message addresses the bot: an @mention, a reply to one of its
/// messages, or a message that starts with its name, "LatiBot" or
/// `bot_name`, its Discord username
/// (src/modules/llm/docs/Language_Model.md §2.1). `reply_counts` is false
/// for a reply that comments on something rather than talking to the bot,
/// such as a reply to a link replacement.
[[nodiscard]] auto addresses_bot(const events::incoming_message& message, std::string_view bot_name, bool reply_counts = true) -> bool;

/// The persona a message starts with the name of, as LatiBot is addressed
/// by its name: the longest name that fits, so "Claude Opus, hi" is for
/// Claude Opus rather than Claude. Null when it names none of them
/// (src/modules/llm/docs/Language_Model.md §2.11).
[[nodiscard]] auto addressed_persona(std::string_view content, std::span<const persona> personas) -> const persona*;

/// The persona a message names anywhere, as conversation mode hears LatiBot
/// named: "thanks claude" as much as "claude, ...". The longest name that
/// fits wins; null when it names none of them
/// (src/modules/llm/docs/Language_Model.md §2.10).
[[nodiscard]] auto named_persona(std::string_view content, std::span<const persona> personas) -> const persona*;

/// What the bot says, once per guild per day or month, when a spend cap
/// stops it answering (src/modules/llm/docs/Language_Model.md §2.2).
[[nodiscard]] auto spend_cap_reply(const spend_status& status) -> std::string;

struct stage_services {
    const config::guild_settings* settings = nullptr;
    /// config.json's llm section.
    const llm_config* section = nullptr;
    const blacklist_store* blacklist = nullptr;
    const advanced_trigger_store* triggers = nullptr;
    const usage_store* usage = nullptr;

    /// The guild's personas, and which of the messages they posted. Null
    /// leaves them out (src/modules/llm/docs/Language_Model.md §2.11).
    const persona_store* personas = nullptr;
    /// Whether a channel's replies are spoken, when DECtalk is built in;
    /// null otherwise, and then none is.
    const capabilities::speech* speech = nullptr;

    /// Which of the bot's messages are link replacements, when the links
    /// module is built in; null otherwise, and then none is.
    const capabilities::link_replacements* replacements = nullptr;

    /// Conversation mode (src/modules/llm/docs/Language_Model.md §2.10):
    /// where a conversation is open, and who last said what. Null leaves it
    /// off.
    const conversation_windows* windows = nullptr;
    channel_activity* activity = nullptr;

    /// Whether a key is set for this provider.
    std::function<bool(provider_kind)> has_provider;

    std::function<bot_identity()> me;

    /// Whether a webhook is one the personas post through, so that a message
    /// from it is a persona's. Null: none is.
    std::function<bool(dpp::snowflake)> persona_webhook;
};

/// The pipeline's last stage: decides whether the model answers a message,
/// and if so hands the shell an `ask_llm`
/// (src/core/docs/Message_Pipeline.md §2.2,
/// src/modules/llm/docs/Language_Model.md §2.1).
///
/// Everything that can refuse runs here, before any money is spent, in this
/// order: the guild's switch; being addressed, or else an advanced trigger
/// firing on a message nothing else answered; the blacklist; the spend caps;
/// the rate limits; and for a bot, the pacing
/// (src/modules/llm/docs/Language_Model.md §2.2, §2.7). Consumes what it answers,
/// and what it refuses when addressed, so nothing after it sees a message
/// meant for the model.
///
/// It hears LatiBot's own messages and webhooks' too
/// (`events::hears::own_too`), and of those answers only LatiBot's and its
/// personas', and only with an advanced trigger that answers bots and that
/// their writer does not answer (src/modules/llm/docs/Language_Model.md
/// §2.11).
///
/// Safe to call from several threads at once.
class llm_stage {
public:
    /// `roll` is the advanced triggers' dice; see `advanced_trigger_matcher`.
    llm_stage(stage_services services, ports::clock& clock, std::function<double()> roll = {});

    auto operator()(const events::incoming_message& message) -> stage_result;

    /// Takes the rate limits for an ask that waited on the check, now that
    /// the check agreed. False when they are used up.
    auto claim_reply(const ask_llm& ask) -> bool;

private:
    /// Who of LatiBot's own wrote a message: LatiBot, or one of its personas.
    struct own_writer {
        /// 0 for LatiBot.
        std::int64_t persona_id = 0;

        /// How the model is shown them.
        std::string name;
    };

    /// Who of LatiBot's own wrote a message that only the stages hearing
    /// their own are given: nothing for someone else's webhook, or a bot
    /// nobody allowed, or a persona since renamed or removed.
    [[nodiscard]] auto own_writer_of(const events::incoming_message& message) const -> std::optional<own_writer>;

    /// How a message nobody addressed reaches the model.
    struct route {
        approach how = approach::addressed;
        bool check_first = false;

        /// Who answers when conversation mode takes the message up: a
        /// persona, or 0 for LatiBot.
        std::int64_t persona_id = 0;
    };

    /// How a message nobody addressed reaches the model, if at all: an
    /// advanced trigger, which it sets `fired` to, or conversation mode. A
    /// trigger that belongs to a persona fires only while the persona is
    /// one of `personas`, the enabled ones. A bot's message, `by_bot`, sets
    /// off only a trigger that answers bots, and none that `writer`, the
    /// persona who wrote it or 0 for LatiBot, answers.
    [[nodiscard]] auto unaddressed(const events::incoming_message& message, const llm_settings& settings, std::string_view bot_name,
                                   std::span<const persona> personas, bool by_bot, std::optional<std::int64_t> writer,
                                   std::optional<advanced_trigger>& fired) -> std::optional<route>;

    /// The enabled persona a person's message is for: it starts with the
    /// persona's name, or replies to one of its messages. A bot's message is
    /// for none of them.
    [[nodiscard]] auto spoken_to(const events::incoming_message& message, std::span<const persona> personas) const -> const persona*;

    /// Whether conversation mode takes up a message nobody addressed, and
    /// for whom: it names LatiBot or one of `personas`, or a conversation is
    /// open and nothing says the message is for someone else, when it goes
    /// to whoever of them replied last
    /// (src/modules/llm/docs/Language_Model.md §2.10, §2.11).
    [[nodiscard]] auto join_in(const events::incoming_message& message, const llm_settings& settings, std::string_view bot_name,
                               std::span<const persona> personas) const -> std::optional<route>;

    /// The guards after the switch and the address, in order: the blacklist,
    /// the spend caps, the rate limits unless `take_rate` is false, and for a
    /// bot, `by_bot`, the pacing. The wait before answering when all of them
    /// let it through, nothing when one did not.
    auto admit(const events::incoming_message& message, const llm_settings& settings, bool addressed, bool take_rate, bool by_bot,
               stage_result& result) -> std::optional<std::chrono::seconds>;

    /// One reply from the author's and the channel's per-minute limits.
    auto take_rate_limits(dpp::snowflake guild, dpp::snowflake channel, dpp::snowflake author, const llm_settings& settings) -> bool;

    /// Whether a spend cap is reached, adding the notice to `result` the
    /// first time a guild asks after it was.
    auto over_spend_cap(const events::incoming_message& message, bool addressed, stage_result& result) -> bool;

    stage_services services_;
    ports::clock* clock_;

    advanced_trigger_matcher matcher_;
    rate_limiter users_;
    rate_limiter channels_;
    bot_pacing pacing_;
    spend_notices notices_;
};

} // namespace latibot::llm
