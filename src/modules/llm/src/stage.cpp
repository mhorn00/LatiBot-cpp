#include "stage.hpp"

#include "core/capabilities/link_replacements.hpp"
#include "core/capabilities/speech.hpp"
#include "core/config/guild_settings.hpp"
#include "core/ports/clock.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "settings.hpp"

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

auto addresses_bot(const events::incoming_message& message, std::string_view bot_name, bool reply_counts) -> bool {
    if (message.mentions_bot || (message.replies_to_bot && reply_counts)) return true;
    if (bot_name.empty()) return false;

    // "latibot, what's up" and "LatiBot what's up", but not "latibots".
    const std::string_view text = util::trim(message.content);
    if (text.size() < bot_name.size() || !util::equals_ignoring_case(text.substr(0, bot_name.size()), bot_name)) return false;
    return text.size() == bot_name.size() || !is_word_character(text[bot_name.size()]);
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
    if (!settings.conversation.enabled || services_.windows == nullptr || util::is_blank(message.content)) return std::nullopt;

    // The check runs on its own model, which needs its key.
    const model_info* checker = find_model(services_.section->check_model);
    if (checker == nullptr || !services_.has_provider(checker->provider)) return std::nullopt;

    const bool open = services_.windows->open(message.channel_id, settings.conversation);

    // Its name anywhere: during a conversation that is talking to it; at
    // other times it may be talking about it, which the check tells apart.
    if (names_bot(message.content, bot_name)) return route{.how = approach::named, .check_first = !open};
    if (!open) return std::nullopt;

    // A reply or a mention is meant for whoever it names, so it is not
    // worth a check. A reply to the bot that counted was addressed already.
    if (!message.reply_to.empty() || message.mentions_others) return std::nullopt;
    return route{.how = approach::joined_in, .check_first = true};
}

auto llm_stage::operator()(const events::incoming_message& message) -> stage_result {
    stage_result result;

    // Every person speaking resets the bot-to-bot count, whether or not the
    // model answers them (src/modules/llm/docs/Language_Model.md §2.7).
    if (!message.from_bot) pacing_.human_spoke(message.channel_id);
    if (services_.activity != nullptr) services_.activity->posted(message.channel_id, message.author_id, message.message_id);
    if (message.guild_id.empty()) return result;

    const llm_settings settings = load_llm_settings(*services_.settings, message.guild_id, *services_.section);
    if (!settings.enabled) return result;

    // A reply to a link replacement comments on the post, not to the bot
    // (src/modules/llm/docs/Language_Model.md §2.1).
    const bool reply_counts =
        !message.replies_to_bot || services_.replacements == nullptr || !services_.replacements->is_replacement(message.reply_to);

    const bot_identity me = services_.me();
    const bool addressed = addresses_bot(message, me.name, reply_counts);

    route chosen{.how = approach::addressed, .check_first = false};
    std::optional<advanced_trigger> fired;
    if (!addressed) {
        // A bot has to address LatiBot to be answered: two bots trading
        // unprompted remarks is the loop pacing is there to stop. And a
        // message a simple trigger answered is not answered twice.
        if (message.from_bot || message.answered) return result;
        fired = matcher_.fire(services_.triggers->for_guild(message.guild_id), message.channel_id, message.content);
        if (fired) {
            chosen.how = approach::trigger;
        } else {
            const std::optional<route> joining = join_in(message, settings, me.name);
            if (!joining) return result;
            chosen = *joining;
        }
    }

    const model_info* model = find_model(settings.model);
    if (model == nullptr || !services_.has_provider(model->provider)) {
        util::log().debug("guild {} wants {}, which has no API key set; not answering", message.guild_id, settings.model);
        return result;
    }

    // From here the message is the model's, answered or not.
    result.consumed = addressed;

    // What waits on a check takes its place in the rate limits only once the
    // check agrees, so a "no" leaves room for the next message.
    const auto wait = admit(message, settings, addressed, !chosen.check_first, result);
    if (!wait) return result;

    result.consumed = true;
    result.answered = true;
    result.actions.emplace_back(
        llm::ask_llm{.guild_id = message.guild_id,
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
                     .speak = services_.speech != nullptr && services_.speech->speaks_in(message.guild_id, message.channel_id),
                     .wait = *wait});
    return result;
}

} // namespace latibot::llm
