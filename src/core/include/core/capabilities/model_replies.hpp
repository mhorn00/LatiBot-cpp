#pragma once

#include "core/events/message_pipeline.hpp"

#include <dpp/snowflake.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace latibot::capabilities {

/// One of the language model's personas, as a trigger names who answers it.
struct persona_name {
    std::int64_t id = 0;
    std::string name{};

    /// A persona that is off answers nothing, its triggers included.
    bool enabled = true;
};

/// How the model answers a trigger that fired
/// (src/modules/triggers/docs/Triggers.md §2.6).
enum class model_answer : std::uint8_t {
    /// It chimes in on something nobody said to it: the trigger's prompt
    /// says what about, and the answer arrives silently.
    comment,
    /// It was spoken to, by an @mention, its name first, or a reply: answered
    /// as such, the author notified and a conversation opened.
    spoken_to,
    /// Its name, or some other word, came up: the check model is asked the
    /// trigger's question first, and only a yes is answered, as spoken to.
    analysis,
};

/// A trigger that asks the model to answer, which fired on a message.
struct model_trigger {
    std::int64_t trigger_id = 0;

    /// What to say, in a line; how to say it is the server's trigger style.
    /// May be empty for an answer as spoken to, which needs no steering.
    std::string prompt{};

    /// Who answers: a persona, or 0 for LatiBot.
    std::int64_t persona_id = 0;

    /// Whether LatiBot answers as a Discord reply to the message. A persona
    /// posts through a webhook, which Discord does not let reply.
    bool reply = true;

    model_answer how = model_answer::comment;

    /// For an analysis: the yes-or-no question the check model is asked
    /// about the message, such as "Is this message talking to LatiBot?".
    std::string question{};
};

/// What the triggers module needs of the language model to have it answer
/// a trigger. Offered by the llm module; without it, no trigger asks the
/// model anything.
///
/// The triggers own when the model answers a message: their patterns,
/// conditions, cooldowns and chances, and once `take_over_answering` is
/// called, every reply LatiBot and its personas make but conversation
/// mode's. The model owns everything about whether it may speak: its switch,
/// the blacklist, the spend caps, the rate limits, bot-to-bot pacing, and
/// which messages are LatiBot's or its personas'
/// (src/modules/llm/docs/Language_Model.md §2.2).
class model_replies {
public:
    virtual ~model_replies() = default;

    model_replies() = default;
    model_replies(const model_replies&) = delete;
    auto operator=(const model_replies&) -> model_replies& = delete;

    /// The guild's personas, on or off, to name who answers a trigger.
    [[nodiscard]] virtual auto personas(dpp::snowflake guild) const -> std::vector<persona_name> = 0;

    /// Who of LatiBot's own wrote a message that a stage hears only because
    /// it hears its own: 0 for LatiBot, or a persona's id. Nothing for a
    /// message that sets off no trigger: someone else's webhook, a bot nobody
    /// allowed, a command's response, or one `talk_to` handed to its target.
    [[nodiscard]] virtual auto own_writer(const events::incoming_message& message) const -> std::optional<std::int64_t> = 0;

    /// Whether the message addresses LatiBot: an @mention of it, or its name
    /// first, "LatiBot" or its Discord username.
    [[nodiscard]] virtual auto addresses(const events::incoming_message& message) const -> bool = 0;

    /// Whose message this one replies to, of those who answer: 0 for
    /// LatiBot's (but not a link replacement, which a reply comments on), a
    /// persona's id for one of its posts, or nothing.
    [[nodiscard]] virtual auto replied_to(const events::incoming_message& message) const -> std::optional<std::int64_t> = 0;

    /// The model's answer to a trigger that fired on `message`, for the
    /// pipeline to carry out; none when the model may not answer here and
    /// now, which it logs. Takes the rate limits and the bot pacing when it
    /// agrees. Answering as spoken to past a spend cap, it may be only the
    /// notice saying so. Safe from any thread.
    [[nodiscard]] virtual auto answer(const events::incoming_message& message, const model_trigger& fired)
        -> std::vector<events::action> = 0;

    /// The triggers take over deciding when LatiBot and its personas answer:
    /// from now on the model answers only through `answer`, and on its own
    /// only in conversation mode. Without the triggers module it keeps
    /// deciding for itself.
    virtual auto take_over_answering() -> void = 0;
};

} // namespace latibot::capabilities
