#pragma once

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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
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
struct model_trigger;
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

/// What the bot says, once per guild per day or month, when a spend cap
/// stops it answering (src/modules/llm/docs/Language_Model.md §2.2).
[[nodiscard]] auto spend_cap_reply(const spend_status& status) -> std::string;

struct stage_services {
    const config::guild_settings* settings = nullptr;
    /// config.json's llm section.
    const llm_config* section = nullptr;
    const blacklist_store* blacklist = nullptr;
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

    /// Whether a message of LatiBot's or a persona's in a channel is one
    /// `talk_to` posted, whose target is answering it already: such a
    /// message sets off nothing more. Forgets it once asked. Null: none is.
    std::function<bool(dpp::snowflake channel, std::string_view content)> handed_off;
};

/// Who of LatiBot's own wrote a message: LatiBot, or one of its personas.
struct own_writer {
    /// 0 for LatiBot.
    std::int64_t persona_id = 0;

    /// How the model is shown them.
    std::string name;
};

/// The pipeline's last stage: decides whether the model answers a message,
/// and if so hands the shell an `ask_llm`
/// (src/core/docs/Message_Pipeline.md §2.2,
/// src/modules/llm/docs/Language_Model.md §2.1).
///
/// Everything that can refuse runs here, before any money is spent, in this
/// order: the guild's switch; being addressed, or else conversation mode
/// taking up a message nothing else answered; the blacklist; the spend caps;
/// the rate limits; and for a bot, the pacing
/// (src/modules/llm/docs/Language_Model.md §2.2, §2.7). Consumes what it answers,
/// and what it refuses when addressed, so nothing after it sees a message
/// meant for the model.
///
/// It hears LatiBot's own messages and webhooks' too
/// (`events::hears::own_too`), and of those answers only a persona's that
/// speaks to LatiBot (src/modules/llm/docs/Language_Model.md §2.11).
///
/// The triggers that ask the model to speak are the triggers module's, which
/// reaches this through `trigger_ask`, `addresses`, `replied_to` and
/// `own_writer_of`, behind the `model_replies` capability. Once it calls
/// `hand_over`, this stage answers nothing itself but conversation mode:
/// being addressed, replied to and named are the triggers' to answer
/// (src/modules/triggers/docs/Triggers.md §2.8).
///
/// Safe to call from several threads at once.
class llm_stage {
public:
    llm_stage(stage_services services, ports::clock& clock);

    auto operator()(const events::incoming_message& message) -> stage_result;

    /// Who of LatiBot's own wrote a message that only the stages hearing
    /// their own are given: nothing for someone else's webhook, or a bot
    /// nobody allowed, or a persona since renamed or removed.
    [[nodiscard]] auto own_writer_of(const events::incoming_message& message) const -> std::optional<own_writer>;

    /// Whether the message addresses LatiBot: an @mention, or its name
    /// first. A reply is `replied_to`'s.
    [[nodiscard]] auto addresses(const events::incoming_message& message) const -> bool;

    /// Whose message this one replies to: 0 for LatiBot's, but not a link
    /// replacement, or a persona's id; nothing for anyone else's.
    [[nodiscard]] auto replied_to(const events::incoming_message& message) const -> std::optional<std::int64_t>;

    /// What a trigger that fired on `message` comes to: the ask, once
    /// everything that can refuse lets it through (the switch, the channel,
    /// a persona that is on and, writing to LatiBot, one that talks, a key
    /// for its model, then the guards `admit` runs); or, answered as spoken
    /// to past a spend cap, its notice; or nothing, logged.
    auto trigger_ask(const events::incoming_message& message, const capabilities::model_trigger& fired) -> stage_result;

    /// From now on the triggers decide when LatiBot and its personas
    /// answer; this stage keeps only conversation mode.
    auto hand_over() -> void { handed_over_ = true; }

    /// Takes the rate limits for an ask that waited on the check, now that
    /// the check agreed. False when they are used up.
    auto claim_reply(const ask_llm& ask) -> bool;

    /// Claims a turn for one model answering another in a channel, as
    /// `talk_to` asks for: the model is on there, no spend cap is reached,
    /// and the bot pacing allows it (src/modules/llm/docs/Language_Model.md
    /// §2.7). The wait before answering, or why not.
    auto claim_bot_turn(dpp::snowflake guild, dpp::snowflake channel) -> std::expected<std::chrono::seconds, std::string>;

private:
    /// How a message nobody addressed reaches the model.
    struct route {
        approach how = approach::addressed;
        bool check_first = false;

        /// Who answers when conversation mode takes the message up: a
        /// persona, or 0 for LatiBot.
        std::int64_t persona_id = 0;
    };

    /// How a message nobody addressed reaches the model, if at all:
    /// conversation mode, for a person's message nothing else answered. A
    /// bot, `by_bot`, has to address it.
    [[nodiscard]] auto unaddressed(const events::incoming_message& message, const llm_settings& settings, std::string_view bot_name,
                                   std::span<const persona> personas, bool by_bot) const -> std::optional<route>;

    /// Whether the message speaks to the model, and to whom: LatiBot
    /// addressed (`addressed`), a persona replied to, or LatiBot spoken to by
    /// a persona. `ours` is who wrote it, of LatiBot's own; `personas` the
    /// enabled ones.
    struct spoken {
        bool addressed = false;
        const persona* answering = nullptr;
        std::optional<route> to_latibot;
    };
    [[nodiscard]] auto spoken_to_whom(const events::incoming_message& message, const std::optional<own_writer>& ours,
                                      const llm_settings& settings, std::span<const persona> personas) const -> spoken;

    /// Whether a reply to the bot counts as addressing it: not when it
    /// replies to a link replacement, which comments on the post.
    [[nodiscard]] auto reply_counts(const events::incoming_message& message) const -> bool;

    /// The guild's enabled personas.
    [[nodiscard]] auto enabled_personas(dpp::snowflake guild) const -> std::vector<persona>;

    /// Whether a persona's message, `writer`'s, speaks to LatiBot by name,
    /// as a person's may: one it starts with, or an @mention, is answered as
    /// addressed; one named in passing goes, with conversation mode on, to
    /// the check, unless LatiBot is talking here already. Only a persona that
    /// `talks` (`personas` are the enabled ones), never by a reply, since
    /// LatiBot answers with one; and no persona answers its name, from
    /// anyone (src/modules/llm/docs/Language_Model.md §2.11).
    [[nodiscard]] auto spoken_to_latibot(const events::incoming_message& message, const own_writer& writer, const llm_settings& settings,
                                         std::string_view bot_name, std::span<const persona> personas) const -> std::optional<route>;

    /// The enabled persona a person's message is for: it replies to one of
    /// its messages. A persona answers no name. A bot's message is for none
    /// of them.
    [[nodiscard]] auto spoken_to(const events::incoming_message& message, std::span<const persona> personas) const -> const persona*;

    /// Whether conversation mode takes up a message nobody addressed, and
    /// for whom: it names LatiBot, or a conversation is open and nothing says
    /// the message is for someone else, when it goes to whoever of LatiBot
    /// and `personas` replied last
    /// (src/modules/llm/docs/Language_Model.md §2.10, §2.11).
    [[nodiscard]] auto join_in(const events::incoming_message& message, const llm_settings& settings, std::string_view bot_name,
                               std::span<const persona> personas) const -> std::optional<route>;

    /// What this stage still answers once the triggers took over: a
    /// person's message in an open conversation that nobody addressed,
    /// replied to or named, and nothing answered.
    [[nodiscard]] auto conversation_only(const events::incoming_message& message, const llm_settings& settings, bool spoken_to,
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

    /// Set once, at startup, before any message.
    std::atomic<bool> handed_over_{false};

    rate_limiter users_;
    rate_limiter channels_;
    bot_pacing pacing_;
    spend_notices notices_;
};

} // namespace latibot::llm
