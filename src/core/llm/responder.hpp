#pragma once

#include "core/events/message_pipeline.hpp"
#include "core/llm/models.hpp"
#include "core/llm/prompt.hpp"
#include "core/llm/provider.hpp"
#include "core/llm/settings.hpp"

#include <dpp/coro/task.h>
#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace latibot::audio {
class speech_queue;
}

namespace latibot::config {
class guild_settings;
struct bootstrap;
} // namespace latibot::config

namespace latibot::ports {
class clock;
class discord_gateway;
class tts_engine;
} // namespace latibot::ports

namespace latibot::llm {

class document_store;
class memory_store;
class tool_registry;
class usage_store;

/// Who the bot is, which the prompt needs and only the connection knows.
struct bot_identity {
    dpp::snowflake id;
    std::string name;
};

/// Everything the responder works through. Every pointer but the speech
/// pair must be set; without those, replies are posted and never spoken.
struct responder_services {
    ports::discord_gateway* discord = nullptr;
    ports::clock* clock = nullptr;
    const config::guild_settings* settings = nullptr;
    const config::bootstrap* bootstrap = nullptr;
    const document_store* documents = nullptr;
    const memory_store* memories = nullptr;
    usage_store* usage = nullptr;
    const tool_registry* tools = nullptr;

    /// The provider that serves models of a kind, or null when there is no
    /// key for it.
    std::function<provider*(provider_kind)> provider_for;

    ports::tts_engine* engine = nullptr;
    audio::speech_queue* speech = nullptr;
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

/// What the bot says, when addressed, if the model could not answer.
[[nodiscard]] auto failure_reply(const ports::api_error& error) -> std::string;

/// Answers a message with the model (plan §14): reads the recent
/// conversation, builds the prompt, runs the tool loop, records the spend,
/// then posts the reply, and speaks it when the channel is a voice
/// session's (plan §14.2).
///
/// Thread-safe: every answer is independent, and the stores it uses guard
/// themselves.
class responder {
public:
    responder(responder_services services, std::function<bot_identity()> me);

    auto answer(events::ask_llm ask) -> dpp::task<answer_report>;

private:
    /// The messages before the one being answered, oldest first.
    [[nodiscard]] auto recent_messages(const events::ask_llm& ask, int wanted, dpp::snowflake bot_id) const
        -> dpp::task<std::vector<context_message>>;

    /// The first request: the instructions, the memories and the question.
    [[nodiscard]] auto build_request(const events::ask_llm& ask, const llm_settings& settings, const model_info& model,
                                     const std::vector<context_message>& history, const bot_identity& me,
                                     std::chrono::sys_seconds now) const -> request;

    /// Tells whoever addressed the bot that the model could not answer.
    auto apologise(const events::ask_llm& ask, const ports::api_error& error) const -> dpp::task<void>;

    /// Posts the reply's parts, the first as a reply, recording each in
    /// `report` and stopping at the first that fails.
    auto post(const events::ask_llm& ask, const std::vector<std::string>& parts, answer_report& report) const -> dpp::task<void>;

    auto speak(const events::ask_llm& ask, std::string text) const -> dpp::task<void>;

    responder_services services_;
    std::function<bot_identity()> me_;
};

} // namespace latibot::llm
