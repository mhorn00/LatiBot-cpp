#include "stage.hpp"

#include "core/capabilities/link_replacements.hpp"
#include "core/capabilities/speech.hpp"
#include "core/config/guild_settings.hpp"
#include "core/ports/clock.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "settings.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace latibot::llm {
namespace {

/// The rate limits are per minute (src/modules/llm/docs/Language_Model.md §2.2).
constexpr std::chrono::seconds rate_window{60};

auto is_word_character(char letter) -> bool {
    const auto byte = static_cast<unsigned char>(letter);
    return byte == '_' || byte >= 128 || std::isalnum(byte) != 0;
}

} // namespace

auto starts_with_name(std::string_view content, std::string_view bot_name) -> bool {
    if (bot_name.empty()) return false;

    // "latibot, what's up" and "LatiBot what's up", but not "latibots".
    const std::string_view text = util::trim(content);
    if (text.size() < bot_name.size() || !util::equals_ignoring_case(text.substr(0, bot_name.size()), bot_name)) return false;
    return text.size() == bot_name.size() || !is_word_character(text[bot_name.size()]);
}

auto addresses_bot(const events::incoming_message& message, std::string_view bot_name, bool reply_counts) -> bool {
    return message.mentions_bot || (message.replies_to_bot && reply_counts) || starts_with_name(message.content, model_name) ||
           starts_with_name(message.content, bot_name);
}

auto addressed_persona(std::string_view content, std::span<const persona> personas) -> const persona* {
    const persona* found = nullptr;
    for (const persona& each : personas) {
        if (!starts_with_name(content, each.name)) continue;
        if (found == nullptr || each.name.size() > found->name.size()) found = &each;
    }
    return found;
}

auto spend_cap_reply(const spend_status& status) -> std::string {
    if (status.over_monthly) return "i've hit this month's spending limit, so i'm staying quiet until next month";
    return "i've hit today's spending limit, so i'm staying quiet until tomorrow (UTC)";
}

llm_stage::llm_stage(stage_services services, ports::clock& clock, std::function<double()> roll)
    : services_(std::move(services)),
      clock_(&clock),
      matcher_(clock, std::move(roll)),
      users_(clock, rate_window),
      channels_(clock, rate_window),
      pacing_(clock) {}

auto llm_stage::over_spend_cap(const events::incoming_message& message, bool addressed, stage_result& result) -> bool {
    const spend_status spend = spend_status_at(
        *services_.usage, {.daily = services_.section->spend_cap_daily_usd, .monthly = services_.section->spend_cap_monthly_usd},
        std::chrono::floor<std::chrono::seconds>(clock_->now()));
    if (!spend.over()) return false;

    // Said once per guild per period, where it was asked, and warned about in
    // the log, which is where the admins look
    // (src/modules/llm/docs/Language_Model.md §2.2).
    if (notices_.first(message.guild_id, spend.period)) {
        util::log().warn("the language model is off in guild {} until the spend cap resets: ${:.2f} today, ${:.2f} this month",
                         message.guild_id, spend.today, spend.this_month);
        if (addressed) {
            result.actions.emplace_back(events::send_message{.channel_id = message.channel_id,
                                                             .content = spend_cap_reply(spend),
                                                             .flags = dpp::m_suppress_notifications,
                                                             .what = "the spend cap notice"});
        }
    }
    return true;
}

auto llm_stage::admit(const events::incoming_message& message, const llm_settings& settings, bool addressed, bool take_rate,
                      stage_result& result) -> std::optional<std::chrono::seconds> {
    if (services_.blacklist->blocks(message.guild_id, message.author_id, message.author_roles)) {
        util::log().debug("not answering {} in guild {}: blacklisted", message.author_id, message.guild_id);
        return std::nullopt;
    }

    if (over_spend_cap(message, addressed, result)) return std::nullopt;

    if (take_rate && !take_rate_limits(message.guild_id, message.channel_id, message.author_id, settings)) return std::nullopt;

    if (!message.from_bot) return std::chrono::seconds{0};
    const pacing_decision paced = pacing_.claim(message.guild_id, message.channel_id, settings.pacing);
    if (!paced.allowed) {
        util::log().debug("not answering bot {} in channel {}: {}", message.author_id, message.channel_id, paced.reason);
        return std::nullopt;
    }
    return paced.wait;
}

auto llm_stage::take_rate_limits(dpp::snowflake guild, dpp::snowflake channel, dpp::snowflake author, const llm_settings& settings)
    -> bool {
    if (users_.try_take({guild, author}, settings.user_per_minute) && channels_.try_take({guild, channel}, settings.channel_per_minute)) {
        return true;
    }
    util::log().debug("not answering {} in channel {}: rate limited", author, channel);
    return false;
}

auto llm_stage::claim_reply(const ask_llm& ask) -> bool {
    const llm_settings settings = load_llm_settings(*services_.settings, ask.guild_id, *services_.section);
    return take_rate_limits(ask.guild_id, ask.channel_id, ask.author_id, settings);
}

auto llm_stage::join_in(const events::incoming_message& message, const llm_settings& settings, std::string_view bot_name) const
    -> std::optional<route> {
    if (!settings.conversation.enabled || services_.windows == nullptr) {
        util::log().trace("llm: message {} was not addressed, and conversation mode is off in guild {}", message.message_id,
                          message.guild_id);
        return std::nullopt;
    }
    if (util::is_blank(message.content)) {
        util::log().trace("llm: message {} has no text for conversation mode", message.message_id);
        return std::nullopt;
    }

    // The check runs on its own model, which needs its key.
    const model_info* checker = find_model(services_.section->check_model);
    if (checker == nullptr || !services_.has_provider(checker->provider)) {
        util::log().trace("llm: message {} left alone: no API key for the check model {}", message.message_id,
                          services_.section->check_model);
        return std::nullopt;
    }

    const bool open = services_.windows->open(message.channel_id, settings.conversation);

    // Its name anywhere: during a conversation that is talking to it; at
    // other times it may be talking about it, which the check tells apart.
    if (names_bot(message.content, model_name) || names_bot(message.content, bot_name)) {
        util::log().trace("llm: message {} names the bot, {}", message.message_id,
                          open ? "during a conversation: answering" : "outside a conversation: checking first");
        return route{.how = approach::named, .check_first = !open};
    }
    if (!open) {
        util::log().trace("llm: message {} was not addressed, and no conversation is open in channel {}", message.message_id,
                          message.channel_id);
        return std::nullopt;
    }

    // A reply or a mention is meant for whoever it names, so it is not
    // worth a check. A reply to the bot that counted was addressed already.
    if (!message.reply_to.empty() || message.mentions_others) {
        util::log().trace("llm: message {} in a conversation is for someone else ({}); not checking", message.message_id,
                          message.reply_to.empty() ? "it mentions someone" : "it replies to someone");
        return std::nullopt;
    }
    util::log().trace("llm: message {} in a conversation goes to the check", message.message_id);
    return route{.how = approach::joined_in, .check_first = true};
}

auto llm_stage::spoken_to(const events::incoming_message& message, std::span<const persona> personas) const -> const persona* {
    // A persona never answers a bot: two models trading replies is the loop
    // pacing stops, and a webhook is one step from it
    // (src/modules/llm/docs/Language_Model.md §2.11).
    if (personas.empty() || message.from_bot) return nullptr;
    if (const persona* named = addressed_persona(message.content, personas)) {
        util::log().trace("llm: message {} starts with persona {}'s name", message.message_id, named->id);
        return named;
    }
    if (message.reply_to.empty()) return nullptr;
    const auto poster = services_.personas->poster(message.reply_to);
    if (!poster) return nullptr;
    const auto replied = std::ranges::find(personas, *poster, &persona::id);
    if (replied == personas.end()) return nullptr;
    util::log().trace("llm: message {} replies to persona {}", message.message_id, replied->id);
    return &*replied;
}

auto llm_stage::unaddressed(const events::incoming_message& message, const llm_settings& settings, std::string_view bot_name,
                            std::span<const persona> personas, std::optional<advanced_trigger>& fired) -> std::optional<route> {
    // A bot has to address LatiBot to be answered: two bots trading
    // unprompted remarks is the loop pacing is there to stop. And a message
    // a simple trigger answered is not answered twice.
    if (message.from_bot || message.answered) {
        util::log().trace("llm: message {} left alone: {}", message.message_id,
                          message.from_bot ? "a bot has to address it" : "a simple trigger answered it");
        return std::nullopt;
    }
    std::vector<advanced_trigger> triggers = services_.triggers->for_guild(message.guild_id);
    std::erase_if(triggers, [&](const advanced_trigger& entry) {
        return entry.persona_id != 0 && std::ranges::find(personas, entry.persona_id, &persona::id) == personas.end();
    });
    fired = matcher_.fire(triggers, message.channel_id, message.content);
    if (fired) {
        util::log().trace("llm: message {} fired advanced trigger {}", message.message_id, fired->id);
        return route{.how = approach::trigger, .check_first = false};
    }
    return join_in(message, settings, bot_name);
}

auto llm_stage::operator()(const events::incoming_message& message) -> stage_result {
    stage_result result;

    // Every person speaking resets the bot-to-bot count, whether or not the
    // model answers them (src/modules/llm/docs/Language_Model.md §2.7).
    if (!message.from_bot) pacing_.human_spoke(message.channel_id);
    if (services_.activity != nullptr) services_.activity->posted(message.channel_id, message.author_id, message.message_id);
    if (message.guild_id.empty()) {
        util::log().trace("llm: message {} is not in a server", message.message_id);
        return result;
    }

    const llm_settings settings = load_llm_settings(*services_.settings, message.guild_id, *services_.section);
    if (!settings.enabled) {
        util::log().trace("llm: off in guild {}; message {} left alone", message.guild_id, message.message_id);
        return result;
    }

    // A reply to a link replacement comments on the post, not to the bot
    // (src/modules/llm/docs/Language_Model.md §2.1).
    const bool reply_counts =
        !message.replies_to_bot || services_.replacements == nullptr || !services_.replacements->is_replacement(message.reply_to);

    const bot_identity me = services_.me();
    const bool addressed = addresses_bot(message, me.name, reply_counts);
    util::log().trace(R"(llm: message {} {} (mentions the bot: {}, replies to the bot: {}{}, starts with "{}" or "{}": {}))",
                      message.message_id, addressed ? "addresses the bot" : "does not address the bot", message.mentions_bot,
                      message.replies_to_bot, reply_counts ? "" : ", but to a link replacement", model_name, me.name,
                      starts_with_name(message.content, model_name) || starts_with_name(message.content, me.name));

    // The personas that may answer: the enabled ones. LatiBot addressed
    // wins over a persona named in the same message.
    std::vector<persona> personas =
        services_.personas == nullptr ? std::vector<persona>{} : services_.personas->for_guild(message.guild_id);
    std::erase_if(personas, [](const persona& each) { return !each.enabled; });

    route chosen{.how = approach::addressed, .check_first = false};
    std::optional<advanced_trigger> fired;
    const persona* answering = nullptr;
    if (!addressed) answering = spoken_to(message, personas);
    if (!addressed && answering == nullptr) {
        const std::optional<route> unprompted = unaddressed(message, settings, me.name, personas, fired);
        if (!unprompted) return result;
        chosen = *unprompted;
        if (fired && fired->persona_id != 0) {
            const auto owner = std::ranges::find(personas, fired->persona_id, &persona::id);
            if (owner != personas.end()) answering = &*owner;
        }
    }
    const bool to_persona = answering != nullptr && chosen.how == approach::addressed;

    const std::string_view model_id = answering == nullptr ? std::string_view(settings.model) : answering_model(*answering, settings.model);
    const model_info* model = find_model(model_id);
    if (model == nullptr || !services_.has_provider(model->provider)) {
        util::log().debug("guild {} wants {}, which has no API key set; not answering", message.guild_id, model_id);
        return result;
    }

    // From here the message is the model's, answered or not.
    result.consumed = addressed || to_persona;

    // What waits on a check takes its place in the rate limits only once the
    // check agrees, so a "no" leaves room for the next message.
    const auto wait = admit(message, settings, addressed || to_persona, !chosen.check_first, result);
    if (!wait) return result;

    util::log().debug("llm: handing message {} in channel {} to the model ({}{}{})", message.message_id, message.channel_id,
                      to_string(chosen.how), chosen.check_first ? ", once the check agrees" : "",
                      answering == nullptr ? std::string{} : std::format(", as persona {}", answering->id));
    result.consumed = true;
    result.answered = true;
    result.actions.emplace_back(llm::ask_llm{
        .guild_id = message.guild_id,
        .channel_id = message.channel_id,
        .message_id = message.message_id,
        .author_id = message.author_id,
        .author_name = message.author_name,
        .content = message.content,
        .author_is_bot = message.from_bot,
        .reply_to = message.reply_to,
        .how = chosen.how,
        .check_first = chosen.check_first,
        .trigger_id = fired ? fired->id : 0,
        .context_prompt = fired ? fired->context_prompt : std::string{},
        .speak = answering == nullptr && services_.speech != nullptr && services_.speech->speaks_in(message.guild_id, message.channel_id),
        .wait = *wait,
        .persona_id = answering == nullptr ? 0 : answering->id});
    return result;
}

} // namespace latibot::llm
