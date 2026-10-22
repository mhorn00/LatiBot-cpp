#pragma once

#include "ask.hpp"
#include "llm_config.hpp"
#include "models.hpp"
#include "personas.hpp"
#include "prompt.hpp"
#include "provider.hpp"
#include "settings.hpp"

#include <dpp/coro/task.h>
#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace latibot::capabilities {
class speech;
}

namespace latibot::config {
class guild_settings;
} // namespace latibot::config

namespace latibot::ports {
class clock;
class discord_gateway;
} // namespace latibot::ports

namespace latibot::llm {

class alias_store;
class document_store;
class memory_store;
class people;
class persona_webhooks;
class tool_registry;
class usage_store;

/// Who the bot is, which the prompt needs and only the connection knows.
struct bot_identity {
    dpp::snowflake id;
    std::string name;
};

/// Everything the responder works through. Every pointer but `speech` must
/// be set; without it, replies are posted and never spoken.
struct responder_services {
    ports::discord_gateway* discord = nullptr;
    ports::clock* clock = nullptr;
    const config::guild_settings* settings = nullptr;
    /// config.json's llm section.
    const llm_config* section = nullptr;
    const document_store* documents = nullptr;
    const memory_store* memories = nullptr;
    usage_store* usage = nullptr;
    const tool_registry* tools = nullptr;

    /// Who everyone is to the model (src/modules/llm/docs/Language_Model.md §3.8).
    alias_store* aliases = nullptr;

    /// The provider that serves models of a kind, or null when there is no
    /// key for it.
    std::function<provider*(provider_kind)> provider_for;

    /// Saying replies aloud, when DECtalk is built in
    /// (docs/modules/Module_Plan_Final.md §5.3).
    capabilities::speech* speech = nullptr;

    /// The personas, and posting as them
    /// (src/modules/llm/docs/Language_Model.md §2.11). Without them an ask
    /// for a persona is not answered.
    persona_store* personas = nullptr;
    persona_webhooks* webhooks = nullptr;
};

/// How many memories are put in front of the model before it answers.
inline constexpr std::size_t memories_shown = 8;

/// A message as the model is shown it: the author's display name, or their
/// username when they have none.
[[nodiscard]] auto to_context(const dpp::message& message, dpp::snowflake bot_id) -> context_message;

/// What happened to one `ask_llm`, for the log and for tests.
struct answer_report {
    /// The messages posted, in order.
    std::vector<std::string> posted;

    usage used;
    double cost = 0;

    /// Why nothing was posted, or empty.
    std::string failure;
};

/// What the conversation check decided (src/modules/llm/docs/Language_Model.md
/// §2.10), for the log and for tests.
struct check_report {
    /// The bot should reply. False when the check could not be asked, too.
    bool yes = false;

    usage used;
    double cost = 0;

    /// Why the check could not be asked, or empty.
    std::string failure;
};

/// How many recent messages the check reads, and roughly how many tokens of
/// them: enough to see who is talking to whom, and cheap.
inline constexpr int check_context_messages = 8;
inline constexpr std::size_t check_context_tokens = 1500;

/// Its answer is one word.
inline constexpr int check_output_tokens = 8;

/// What the bot says, when addressed, if the model could not answer.
[[nodiscard]] auto failure_reply(const ports::api_error& error) -> std::string;

/// Answers a message with the model (src/modules/llm/docs/Language_Model.md): reads
/// the recent conversation, builds the prompt, runs the tool loop, records
/// the spend, then posts the reply, and speaks it when the channel is a voice
/// session's (src/modules/llm/docs/Language_Model.md §2.5).
///
/// Thread-safe: every answer is independent, and the stores it uses guard
/// themselves.
class responder {
public:
    responder(responder_services services, std::function<bot_identity()> me);

    auto answer(llm::ask_llm ask) -> dpp::task<answer_report>;

    /// Asks `llm.check_model` whether the bot should reply to the message,
    /// from the last few messages and what it replies to, and records what
    /// that cost (src/modules/llm/docs/Language_Model.md §2.10).
    auto check(const llm::ask_llm& ask) -> dpp::task<check_report>;

private:
    /// The messages before the one being answered, oldest first. Their
    /// authors, and whom they mention, are met in `cast`.
    [[nodiscard]] auto recent_messages(const llm::ask_llm& ask, int wanted, dpp::snowflake bot_id, people& cast) const
        -> dpp::task<std::vector<context_message>>;

    /// A fetched message as the transcript shows it to whoever answers
    /// `ask`: a persona's by its name, and the bot's own as LatiBot's when a
    /// persona is answering (src/modules/llm/docs/Language_Model.md §3.9).
    [[nodiscard]] auto shown(const dpp::message& message, const llm::ask_llm& ask, dpp::snowflake bot_id) const -> context_message;

    /// The message the one being answered replies to: from `history` when it
    /// is there, fetched when not, and nothing when it is not a reply or the
    /// message is gone. Its author, and whom it mentions, are met in `cast`.
    [[nodiscard]] auto replied_message(const llm::ask_llm& ask, const std::vector<context_message>& history, dpp::snowflake bot_id,
                                       people& cast) const -> dpp::task<std::optional<context_message>>;

    /// The first request: the instructions, the memories and the question,
    /// with everyone in them as aliases. For a persona, its own documents,
    /// and no memories or tools.
    [[nodiscard]] auto build_request(const llm::ask_llm& ask, const llm_settings& settings, const model_info& model,
                                     const std::vector<context_message>& history, const context_message* replied_to,
                                     std::chrono::sys_seconds now, const persona* as, people& cast) const -> request;

    /// Tells whoever addressed the bot, or the persona, that the model could
    /// not answer.
    auto apologise(const llm::ask_llm& ask, const ports::api_error& error, const persona* as) const -> dpp::task<void>;

    /// Posts the reply's parts, the first as a reply, recording each in
    /// `report` and stopping at the first that fails. A persona's go through
    /// its webhook, and are not replies, which a webhook cannot send.
    auto post(const llm::ask_llm& ask, const std::vector<std::string>& parts, const persona* as, answer_report& report) const
        -> dpp::task<void>;

    /// Posts one message as the persona, and notes that it did.
    auto post_as(const persona& as, dpp::message message) const -> dpp::task<ports::result<dpp::message>>;

    responder_services services_;
    std::function<bot_identity()> me_;
};

} // namespace latibot::llm
