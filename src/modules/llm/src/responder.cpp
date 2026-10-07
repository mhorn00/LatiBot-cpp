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

#include <algorithm>
#include <format>
#include <ranges>
#include <tuple>
#include <utility>

namespace latibot::llm {
namespace {

auto seconds_now(const ports::clock& clock) -> std::chrono::sys_seconds {
    return std::chrono::floor<std::chrono::seconds>(clock.now());
}

/// Meets a fetched message's author, and everyone it mentions, so that
/// every name is known before any text is sanitized. A fetched message
/// carries no server nicknames; the cache has those.
auto meet_everyone_in(const dpp::message& message, const context_message& shown, people& cast) -> void {
    cast.meet(message.author.id, shown.author_name, message.author.username);
    for (const auto& [user, member] : message.mentions) {
        std::string name = member.get_nickname();
        if (name.empty()) name = user.global_name;
        if (name.empty()) name = user.username;
        cast.meet(user.id, name, user.username);
    }
    cast.meet_mentioned(message.content);
}

/// Why the model answered, for the log.
auto why(const llm::ask_llm& ask) -> std::string {
    switch (ask.how) {
    case approach::addressed:
        return "addressed";
    case approach::named:
        return "named";
    case approach::joined_in:
        return "joined in";
    case approach::trigger:
        return std::format("advanced trigger {}", ask.trigger_id);
    }
    return "asked";
}

/// The message being answered, as the transcript shows it.
auto latest_of(const llm::ask_llm& ask) -> context_message {
    return {.id = ask.message_id,
            .author_id = ask.author_id,
            .author_name = ask.author_name,
            .from_me = false,
            .from_bot = ask.author_is_bot,
            .content = ask.content};
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
    const bool addressed = spoke_to_bot(ask);

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
    // covers most replies; a failure to show it is not worth a line. Not
    // when joining in, where the model may yet say nothing.
    if (ask.how != approach::joined_in) std::ignore = co_await services_.discord->start_typing(ask.channel_id);

    // Everyone the model hears of is an alias, never an id or a name
    // (src/modules/llm/docs/Language_Model.md §3.8). Met as they are read, so that
    // every name is known before any text is sanitized.
    people cast(*services_.aliases, *services_.discord, ask.guild_id, me.id, me.name);
    cast.meet(ask.author_id, ask.author_name);
    cast.meet_mentioned(ask.content);

    const int wanted = ask.how == approach::trigger ? settings.trigger_context : settings.context_messages;
    const std::vector<context_message> history = co_await recent_messages(ask, wanted, me.id, cast);
    const std::optional<context_message> replied_to = co_await replied_message(ask, history, me.id, cast);

    const auto now = seconds_now(*services_.clock);
    request call = build_request(ask, settings, *model, history, replied_to ? &*replied_to : nullptr, now, cast);
    util::log().trace(
        "llm: asking {} about message {}: {} character(s) of instructions, {} of memories and time, {} of conversation, "
        "{} tool(s), at most {} token(s) back",
        call.model, ask.message_id, call.stable_system.size(), call.varying_system.size(),
        call.conversation.empty() ? 0 : call.conversation.front().text.size(), call.tools.size(), call.max_output_tokens);

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

    util::log().trace("llm: {} finished message {} after {} request(s) ({}): {} character(s) of reply", model->id, ask.message_id,
                      outcome.value().requests, to_string(outcome.value().stop), outcome.value().text.size());

    // Joining in, the model may decide the message was not for it after all
    // (src/modules/llm/docs/Language_Model.md §2.10).
    if (is_silence(outcome.value().text)) {
        report.failure = "the model chose to say nothing";
        util::log().info("{} chose to say nothing to {} in channel {} ({}): ${:.4f}", model->id, ask.author_id, ask.channel_id, why(ask),
                         report.cost);
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

    const std::string tools =
        outcome.value().tools_run.empty() ? std::string{} : std::format("; tools: {}", joined(outcome.value().tools_run));
    // How much of the channel it read, so a reply that seems to miss the
    // conversation can be told apart from one that never saw it.
    const std::string read = std::format("{} earlier message(s){}", history.size(), replied_to ? " and the one replied to" : "");
    util::log().info("{} answered {} in channel {} ({}, read {}): {} in, {} cached, {} out, ${:.4f}{}", model->id, ask.author_id,
                     ask.channel_id, why(ask), read, report.used.input_tokens + report.used.cache_write_tokens,
                     report.used.cache_read_tokens, report.used.output_tokens, report.cost, tools);

    if (speaking && !report.posted.empty()) co_await services_.speech->say(ask.guild_id, ask.author_id, std::move(text));
    co_return report;
}

auto responder::check(const llm::ask_llm& ask) -> dpp::task<check_report> {
    check_report report;
    const model_info* model = find_model(services_.section->check_model);
    provider* checking = model == nullptr ? nullptr : services_.provider_for(model->provider);
    if (checking == nullptr) {
        // The stage checks this too; a key can only have gone since.
        report.failure = std::format("no provider for {}", services_.section->check_model);
        util::log().debug("cannot check message {} in guild {}: {}", ask.message_id, ask.guild_id, report.failure);
        co_return report;
    }

    // The same aliases as the answer, never an id or a name
    // (src/modules/llm/docs/Language_Model.md §3.8).
    const bot_identity me = me_();
    people cast(*services_.aliases, *services_.discord, ask.guild_id, me.id, me.name);
    cast.meet(ask.author_id, ask.author_name);
    cast.meet_mentioned(ask.content);
    const std::vector<context_message> history = co_await recent_messages(ask, check_context_messages, me.id, cast);
    const std::optional<context_message> replied_to = co_await replied_message(ask, history, me.id, cast);

    request call;
    call.model = std::string(model->id);
    call.stable_system = std::string(check_instructions());
    call.conversation.push_back(
        {.from = speaker::user,
         .text = check_question(history, latest_of(ask), replied_to ? &*replied_to : nullptr, check_context_tokens, cast),
         .calls = {},
         .results = {},
         .raw = {}});
    call.allow_tools = false;
    call.max_output_tokens = check_output_tokens;

    const auto outcome = co_await checking->complete(std::move(call));
    if (!outcome.has_value()) {
        report.failure = outcome.error().message;
        util::log().warn("{} could not check message {} in channel {}: {}", model->id, ask.message_id, ask.channel_id, report.failure);
        co_return report;
    }
    report.used = outcome.value().used;
    report.cost = services_.usage->record(ask.guild_id, *model, report.used, seconds_now(*services_.clock));
    report.yes = check_says_yes(outcome.value().reply.text);
    util::log().trace("llm: the check on message {} answered \"{}\" ({})", ask.message_id, util::trim(outcome.value().reply.text),
                      to_string(outcome.value().stop));
    util::log().debug("{} says {} to replying to {} in channel {} ({}): ${:.5f}", model->id, report.yes ? "yes" : "no", ask.author_id,
                      ask.channel_id, why(ask), report.cost);
    co_return report;
}

auto responder::recent_messages(const llm::ask_llm& ask, int wanted, dpp::snowflake bot_id, people& cast) const
    -> dpp::task<std::vector<context_message>> {
    std::vector<context_message> history;
    if (wanted <= 0) {
        util::log().trace("llm: reading no earlier messages for message {}", ask.message_id);
        co_return history;
    }

    const auto page = co_await services_.discord->get_messages(ask.channel_id, ask.message_id, static_cast<std::uint64_t>(wanted));
    if (!page.has_value()) {
        util::log().debug("could not read the recent messages in channel {}: {}", ask.channel_id, page.error().message);
        co_return history;
    }

    // Newest first from Discord; the transcript reads oldest first.
    for (const dpp::message& message : std::views::reverse(page.value())) {
        history.push_back(to_context(message, bot_id));
        meet_everyone_in(message, history.back(), cast);
    }
    const auto own = std::ranges::count_if(history, &context_message::from_me);
    util::log().trace("llm: read {} of the {} message(s) asked for before message {} in channel {}, {} of them the bot's", history.size(),
                      wanted, ask.message_id, ask.channel_id, own);
    co_return history;
}

auto responder::replied_message(const llm::ask_llm& ask, const std::vector<context_message>& history, dpp::snowflake bot_id,
                                people& cast) const -> dpp::task<std::optional<context_message>> {
    if (ask.reply_to.empty()) co_return std::nullopt;
    const auto in_history = std::ranges::find(history, ask.reply_to, &context_message::id);
    if (in_history != history.end()) {
        util::log().trace("llm: message {} replies to {}, among the recent messages", ask.message_id, ask.reply_to);
        co_return *in_history;
    }

    const auto fetched = co_await services_.discord->get_message(ask.channel_id, ask.reply_to);
    if (!fetched.has_value()) {
        // Deleted since, most likely; the answer goes ahead without it.
        util::log().debug("could not read message {}, which {} replies to: {}", ask.reply_to, ask.message_id, fetched.error().message);
        co_return std::nullopt;
    }
    context_message shown = to_context(fetched.value(), bot_id);
    meet_everyone_in(fetched.value(), shown, cast);
    util::log().trace("llm: message {} replies to {}, fetched{}", ask.message_id, ask.reply_to, shown.from_me ? "; it is the bot's" : "");
    co_return shown;
}

auto responder::build_request(const llm::ask_llm& ask, const llm_settings& settings, const model_info& model,
                              const std::vector<context_message>& history, const context_message* replied_to, std::chrono::sys_seconds now,
                              people& cast) const -> request {
    const context_message latest = latest_of(ask);

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
         .trigger_style = ask.how != approach::trigger
                              ? std::string{}
                              : cast.sanitize(services_.documents->text(ask.guild_id, document_kind::trigger_style), false),
         .speaking_guide = ask.speak && services_.speech != nullptr ? services_.speech->guide_for_model() : std::string{}});
    call.varying_system = varying_instructions(memories, now, cast);
    call.conversation.push_back({.from = speaker::user,
                                 .text = question_for(history, latest, replied_to, ask.how, ask.context_prompt,
                                                      static_cast<std::size_t>(settings.context_tokens), cast),
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
    const bool addressed = spoke_to_bot(ask);
    for (std::size_t index = 0; index < parts.size(); ++index) {
        dpp::message reply(ask.channel_id, parts[index]);
        if (index == 0) reply.set_reference(ask.message_id, ask.guild_id, ask.channel_id, false);

        // Nothing the model writes pings anyone. Someone who addressed the
        // bot is notified of the reply, as with any reply; an advanced
        // trigger's comment arrives silently, as a simple trigger's does, and
        // so does joining in a conversation.
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
