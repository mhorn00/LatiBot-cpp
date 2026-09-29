#pragma once

#include "core/audio/dectalk_engine.hpp"
#include "core/audio/speech_queue.hpp"
#include "core/audio/voice_store.hpp"
#include "core/commands/llm.hpp"
#include "core/commands/registry.hpp"
#include "core/commands/trigger.hpp"
#include "core/commands/urlrepl.hpp"
#include "core/commands/voice_lab.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/discord/dpp_gateway.hpp"
#include "core/discord/dpp_http_client.hpp"
#include "core/discord/dpp_voice_output.hpp"
#include "core/discord/raw_api.hpp"
#include "core/events/backfill.hpp"
#include "core/events/bot_allowlist.hpp"
#include "core/events/log_channel.hpp"
#include "core/events/message_pipeline.hpp"
#include "core/events/midnight.hpp"
#include "core/events/nicknames.hpp"
#include "core/events/reactions.hpp"
#include "core/events/triggers.hpp"
#include "core/events/url_replacer.hpp"
#include "core/events/url_rules.hpp"
#include "core/events/voice_sessions.hpp"
#include "core/llm/advanced_triggers.hpp"
#include "core/llm/documents.hpp"
#include "core/llm/guards.hpp"
#include "core/llm/memory.hpp"
#include "core/llm/provider.hpp"
#include "core/llm/responder.hpp"
#include "core/llm/spend.hpp"
#include "core/llm/stage.hpp"
#include "core/llm/tools.hpp"
#include "core/ports/clock.hpp"
#include "core/ui/paginator.hpp"

#include <dpp/dpp.h>

#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace latibot {

/// Owns the Discord connection and the subsystems hanging off it.
///
/// This is the shell: it wires DPP events to core functions and holds the
/// ports features talk through. The logic itself lives outside, where it can
/// be tested without Discord (plan §17.3).
class bot {
public:
    bot(config::bootstrap settings, const config::secrets& credentials);

    bot(const bot&) = delete;
    auto operator=(const bot&) -> bot& = delete;

    /// Connects and blocks until the bot shuts down.
    auto run() -> void;

private:
    auto register_commands() -> void;
    auto register_stages() -> void;
    auto register_events() -> void;

    /// Connected, or reconnected: puts the last `/status` back, and registers
    /// the commands the first time.
    auto on_ready(const dpp::ready_t& event) -> void;

    /// Starts the things that happen on a clock rather than on an event: the
    /// midnight messages, the embed tracker's one-second tick and the
    /// database backups (plan §10, §9.3, §5.2).
    auto register_timers() -> void;

    /// Warns about anything the bot cannot do in this guild. Never fatal: a
    /// missing permission disables one feature, not the bot (plan §7).
    auto check_permissions(const dpp::guild& guild) const -> void;

    /// Turns a DPP message into the plain struct the stages work on, which is
    /// where the Administrator check happens. `raw_event` is the gateway
    /// frame, the only place a reply says whose message it replies to.
    [[nodiscard]] auto describe(const dpp::message& message, const std::string& raw_event) const -> events::incoming_message;

    /// The provider that serves a kind of model, or null without its key.
    [[nodiscard]] auto provider_for(llm::provider_kind kind) const -> llm::provider*;

    /// Everything `/llm`, `/memory` and their panels work with.
    [[nodiscard]] auto llm_services() -> commands::llm_command_services;

    /// Waits out any pacing, then has the model answer (plan §14).
    auto answer_with_llm(events::ask_llm ask) -> dpp::task<void>;

    /// Performs what the stages decided.
    auto carry_out(const std::vector<events::action>& actions) -> void;

    /// Buttons and select menus. `chosen` is the select menu's value, empty
    /// for a button. Both arrive here because a panel mixes the two and the
    /// custom_id says what to do either way; the id is passed separately
    /// because DPP puts it on each event type rather than on their base.
    auto on_component(const dpp::interaction_create_t& event, const std::string& custom_id, const std::string& chosen) -> void;

    /// Does what a decoded component asks. False when no panel claims it,
    /// which `on_component` answers.
    auto route_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen,
                         const commands::user_label& who) -> bool;

    /// Modal submissions.
    auto on_form(const dpp::form_submit_t& event) -> void;

    /// Records a nickname change, if it is one, and says which row it wrote.
    ///
    /// Shared by the gateway event and the startup sweep, because "is this
    /// different from what we last saw" is the same question either way
    /// (plan §8.4).
    auto record_nickname(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname,
                         events::nickname_source source) -> std::optional<std::int64_t>;

    /// A nickname change seen on the gateway.
    auto on_member_update(const dpp::guild_member& member) -> void;

    /// An audit entry that may name who made a change already recorded.
    auto on_audit_entry(const dpp::audit_entry& entry, dpp::snowflake guild_id) -> void;

    /// Asks Discord for the audit log a little later, for the one row it was
    /// hoping to attribute.
    ///
    /// The safety net for a gateway entry that never arrived — a reconnect, a
    /// dropped event (plan §8.1). Costs one API call per change that is
    /// still unattributed when it runs, which is normally none of them.
    auto attribute_later(dpp::snowflake guild_id, dpp::snowflake user_id, std::int64_t row) -> void;

    /// Someone's voice state changed, the bot's included. Tidies up after the
    /// bot leaves a channel, however that happened, and tells the auto-leave
    /// check whether it is on its own (plan §13).
    auto on_voice_state(const dpp::voicestate& state) -> void;

    /// Writes down nicknames that changed while the bot was not running.
    auto reconcile_nicknames(const dpp::guild& guild) -> void;

    /// Copies the Java bot's URL rules into a guild, once (plan §9.5).
    auto import_url_rules(const dpp::guild& guild) -> void;

    /// Somebody pressed Retry on a replacement that found no preview.
    auto retry_replacement(const dpp::interaction_create_t& event, dpp::snowflake message_id, const commands::user_label& who) -> void;

    /// Settles this guild's replacements the last run left mid-watch, once:
    /// its first guild_create hands them over (plan §9.4).
    auto settle_stranded_replacements(dpp::snowflake guild_id) -> void;

    /// Runs what the embed tracker decided, without holding up the caller.
    auto carry_out(std::vector<events::embed_action> actions) -> void;

    /// The clock's time to the second, which is what the database stores.
    [[nodiscard]] auto now_seconds() const -> std::chrono::sys_seconds;

    config::bootstrap settings_;
    db::database database_;
    config::guild_settings guild_settings_;

    dpp::cluster cluster_;
    commands::registry commands_;

    discord::dpp_gateway gateway_;
    discord::dpp_http_client http_;
    discord::raw_api raw_;
    ports::system_clock clock_;

    events::bot_allowlist bot_allowlist_;
    events::nickname_store nicknames_;
    events::pending_nicknames pending_nicknames_;
    events::trigger_store triggers_;
    commands::trigger_panel trigger_panel_;
    events::trigger_responder trigger_responder_;
    events::midnight_store midnight_;
    events::midnight_scheduler midnight_scheduler_;
    events::url_rule_store url_rules_;
    commands::url_panel url_panel_;
    events::replacement_store replacements_;
    events::reaction_store reactions_;
    events::backfill_progress_store backfill_progress_;
    events::backfill_service backfill_;
    events::embed_tracker embed_tracker_;
    events::pipeline pipeline_;

    // Speech (plan §12, §13). The engine starts its worker thread at
    // construction, and is gone before the cluster is.
    audio::dectalk_engine tts_;
    discord::dpp_voice_output voice_output_;
    audio::speech_queue speech_;
    events::voice_sessions voice_sessions_;
    events::auto_leave auto_leave_;
    audio::voice_store voices_;
    commands::voice_drafts voice_drafts_;
    commands::voice_lab voice_lab_;

    // The language model (plan §14). A provider exists only when its key is
    // set; the stage and the commands ask `provider_for` rather than assume.
    llm::usage_store llm_usage_;
    llm::document_store llm_documents_;
    llm::memory_store llm_memories_;
    llm::blacklist_store llm_blacklist_;
    llm::advanced_trigger_store llm_triggers_;
    llm::tool_registry llm_tools_;
    std::unique_ptr<llm::provider> anthropic_;
    std::unique_ptr<llm::provider> openai_;
    llm::responder responder_;
    llm::llm_stage llm_stage_;
    commands::llm_panels llm_panels_;

    /// Where the log is posted (`/logs`). Destroyed before everything above
    /// it, and unhooked from the logger as it goes, so a line logged while
    /// the rest shuts down reaches the console and nothing else.
    events::log_destination_store log_destinations_;
    events::log_channel log_channel_;

    /// Replacements the last run left `pending` or `retrying`, by guild. Read
    /// in the constructor, before anything can be posted, so none of them is
    /// one this run is still watching.
    std::mutex stranded_mutex_;
    std::map<dpp::snowflake, std::vector<events::replacement_record>> stranded_;

    /// The pause between the goodbye and shutting down. Last, so it is
    /// joined first when the bot is destroyed, while the cluster it shuts
    /// down still exists.
    std::jthread goodbye_;
};

} // namespace latibot
