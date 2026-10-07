#include "responder.hpp"

#include "aliases.hpp"
#include "core/capabilities/speech.hpp"
#include "core/config/guild_settings.hpp"
#include "core/ports/clock.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "documents.hpp"
#include "memory.hpp"
#include "settings.hpp"
#include "spend.hpp"
#include "tools.hpp"

#include <format>
#include <ranges>
#include <tuple>
#include <utility>

namespace latibot::llm {
namespace {

auto seconds_now(const ports::clock& clock) -> std::chrono::sys_seconds {
    return std::chrono::floor<std::chrono::seconds>(clock.now());
}

auto joined(const std::vector<std::string>& names) -> std::string {
    std::string text;
    for (const std::string& name : names) {
        if (!text.empty()) text += ", ";
        text += name;
    }
    return text;
}

} // namespace

auto to_context(const dpp::message& message, dpp::snowflake bot_id) -> context_message {
    return {.id = message.id,
            .author_id = message.author.id,
            .author_name = message.author.global_name.empty() ? message.author.username : message.author.global_name,
            .from_me = message.author.id == bot_id,
            .from_bot = message.author.is_bot(),
            .content = message.content};
}

auto failure_reply(const ports::api_error& error) -> std::string {
    // 429 is a rate limit, 529 an overloaded model: both pass on their own.
    if (error.http_status == 429 || error.http_status == 529) return "i'm a bit overloaded right now; try me again in a minute";
    return "sorry, i couldn't come up with anything just now";
}

responder::responder(responder_services services, std::function<bot_identity()> me) : services_(std::move(services)), me_(std::move(me)) {}

auto responder::answer(llm::ask_llm ask) -> dpp::task<answer_report> {
    answer_report report;
    const bot_identity me = me_();
    const bool addressed = ask.trigger_id == 0;

    const llm_settings settings = load_llm_settings(*services_.settings, ask.guild_id, *services_.section);
    const model_info* model = find_model(settings.model);
    provider* answering = model == nullptr ? nullptr : services_.provider_for(model->provider);
    if (answering == nullptr) {
        // The stage checks this too; a key can only have gone since.
        report.failure = std::format("no provider for {}", settings.model);
        util::log().warn("cannot answer in guild {}: {}", ask.guild_id, report.failure);
        co_return report;
    }

    // Typing first, so the wait for the model reads as the bot thinking
    // rather than ignoring whoever asked
    // (src/modules/llm/docs/Language_Model.md §2.3). It lasts ten seconds, which
    // covers most replies; a failure to show it is not worth a line.
    std::ignore = co_await services_.discord->start_typing(ask.channel_id);

    // Everyone the model hears of is an alias, never an id or a name
    // (src/modules/llm/docs/Language_Model.md §3.8). Met as they are read, so that
    // every name is known before any text is sanitized.
    people cast(*services_.aliases, *services_.discord, ask.guild_id, me.id, me.name);
    cast.meet(ask.author_id, ask.author_name);
    cast.meet_mentioned(ask.content);

    const int wanted = addressed ? settings.context_messages : settings.trigger_context;
    const std::vector<context_message> history = co_await recent_messages(ask, wanted, me.id, cast);

    const auto now = seconds_now(*services_.clock);
    request call = build_request(ask, settings, *model, history, now, cast);

    const tool_context context{
        .guild_id = ask.guild_id, .channel_id = ask.channel_id, .author_id = ask.author_id, .now = now, .cast = &cast};
    auto outcome = co_await run_tool_loop(
        *answering, std::move(call), *services_.tools, context, services_.section->tool_rounds, [&](const usage& used) {
            report.used += used;
            report.cost += services_.usage->record(ask.guild_id, *model, used, seconds_now(*services_.clock));
        });

    if (!outcome.has_value()) {
        report.failure = outcome.error().message;
        util::log().warn("{} could not answer {} in channel {}: {}", model->id, ask.author_id, ask.channel_id, report.failure);
        if (addressed) co_await apologise(ask, outcome.error());
        co_return report;
    }

    // Names back in, where the model wrote markers. Spoken, a mention is a
    // name too.
    std::string text = cast.restore(outcome.value().text, ask.speak);
    if (outcome.value().stop == stop_reason::max_tokens) {
        util::log().debug("the reply in channel {} ran out of its {} tokens", ask.channel_id, settings.max_output_tokens);
    }

    // What is spoken is posted too, keeping the inline commands the
    // sanitizer allowed, so the channel sees what was said as it was said
    // (src/modules/llm/docs/Language_Model.md §2.5).
    const bool speaking = ask.speak && services_.speech != nullptr;
    if (speaking) text = services_.speech->prepare_for_model(text, ask.guild_id);

    if (util::is_blank(text)) {
        report.failure = "the model said nothing";
        util::log().info("{} had nothing to say to {} in channel {} ({})", model->id, ask.author_id, ask.channel_id,
                         to_string(outcome.value().stop));
        co_return report;
    }

    co_await post(ask, split_for_discord(text), report);

    const std::string why = addressed ? std::string("addressed") : std::format("advanced trigger {}", ask.trigger_id);
    const std::string tools =
        outcome.value().tools_run.empty() ? std::string{} : std::format("; tools: {}", joined(outcome.value().tools_run));
    util::log().info("{} answered {} in channel {} ({}): {} in, {} cached, {} out, ${:.4f}{}", model->id, ask.author_id, ask.channel_id,
                     why, report.used.input_tokens + report.used.cache_write_tokens, report.used.cache_read_tokens,
                     report.used.output_tokens, report.cost, tools);

    if (speaking && !report.posted.empty()) co_await services_.speech->say(ask.guild_id, ask.author_id, std::move(text));
    co_return report;
}

auto responder::recent_messages(const llm::ask_llm& ask, int wanted, dpp::snowflake bot_id, people& cast) const
    -> dpp::task<std::vector<context_message>> {
    std::vector<context_message> history;
    if (wanted <= 0) co_return history;

    const auto page = co_await services_.discord->get_messages(ask.channel_id, ask.message_id, static_cast<std::uint64_t>(wanted));
    if (!page.has_value()) {
        util::log().debug("could not read the recent messages in channel {}: {}", ask.channel_id, page.error().message);
        co_return history;
    }

    // Newest first from Discord; the transcript reads oldest first.
    for (const dpp::message& message : std::views::reverse(page.value())) {
        history.push_back(to_context(message, bot_id));
        // A fetched message carries no server nicknames; the cache has those.
        cast.meet(message.author.id, history.back().author_name, message.author.username);
        for (const auto& [user, member] : message.mentions) {
            std::string shown = member.get_nickname();
            if (shown.empty()) shown = user.global_name;
            if (shown.empty()) shown = user.username;
            cast.meet(user.id, shown, user.username);
        }
        cast.meet_mentioned(message.content);
    }
    co_return history;
}

auto responder::build_request(const llm::ask_llm& ask, const llm_settings& settings, const model_info& model,
                              const std::vector<context_message>& history, std::chrono::sys_seconds now, people& cast) const -> request {
    const bool addressed = ask.trigger_id == 0;
    const context_message latest{.id = ask.message_id,
                                 .author_id = ask.author_id,
                                 .author_name = ask.author_name,
                                 .from_me = false,
                                 .from_bot = ask.author_is_bot,
                                 .content = ask.content};

    // Searched with what the model will see, since memories name people by
    // alias; whom they are about are met before any text is sanitized.
    const std::vector<memory> memories =
        relevant_memories(*services_.memories, ask.guild_id, ask.author_id, cast.sanitize(ask.content), memories_shown);
    for (const memory& entry : memories) {
        if (entry.subject) cast.meet(*entry.subject);
    }

    // The documents keep the names they were written with, so the same text
    // goes each time and stays cached; only mentions in them become aliases.
    request call;
    call.model = std::string(model.id);
    call.stable_system = stable_instructions(
        {.system_document = cast.sanitize(services_.documents->text(ask.guild_id, document_kind::system), false),
         .personality = cast.sanitize(services_.documents->text(ask.guild_id, document_kind::personality), false),
         .trigger_style =
             addressed ? std::string{} : cast.sanitize(services_.documents->text(ask.guild_id, document_kind::trigger_style), false),
         .speaking_guide = ask.speak && services_.speech != nullptr ? services_.speech->guide_for_model() : std::string{}});
    call.varying_system = varying_instructions(memories, now, cast);
    call.conversation.push_back(
        {.from = speaker::user,
         .text = question_for(history, latest, ask.context_prompt, static_cast<std::size_t>(settings.context_tokens), cast),
         .calls = {},
         .results = {},
         .raw = {}});
    call.tools = services_.tools->definitions();
    call.max_output_tokens = settings.max_output_tokens;
    return call;
}

auto responder::apologise(const llm::ask_llm& ask, const ports::api_error& error) const -> dpp::task<void> {
    dpp::message apology(ask.channel_id, failure_reply(error));
    apology.set_reference(ask.message_id, ask.guild_id, ask.channel_id, false);
    apology.set_allowed_mentions(false, false, false, false);
    // Best effort: an apology that cannot be posted has nowhere left to go.
    std::ignore = co_await services_.discord->send_message(std::move(apology));
}

auto responder::post(const llm::ask_llm& ask, const std::vector<std::string>& parts, answer_report& report) const -> dpp::task<void> {
    const bool addressed = ask.trigger_id == 0;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        dpp::message reply(ask.channel_id, parts[index]);
        if (index == 0) reply.set_reference(ask.message_id, ask.guild_id, ask.channel_id, false);

        // Nothing the model writes pings anyone. Someone who addressed the
        // bot is notified of the reply, as with any reply; an advanced
        // trigger's comment arrives silently, as a simple trigger's does.
        reply.set_allowed_mentions(false, false, false, addressed && index == 0);
        if (!addressed) reply.set_flags(dpp::m_suppress_notifications);

        const auto sent = co_await services_.discord->send_message(std::move(reply));
        if (!sent.has_value()) {
            report.failure = sent.error().message;
            util::log().warn("could not post the model's reply in channel {}: {}", ask.channel_id, report.failure);
            co_return;
        }
        report.posted.push_back(parts[index]);
    }
}

} // namespace latibot::llm
