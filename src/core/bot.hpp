#pragma once

#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/discord/dpp_gateway.hpp"
#include "core/discord/dpp_http_client.hpp"
#include "core/discord/raw_api.hpp"
#include "core/events/backfill.hpp"
#include "core/events/bot_allowlist.hpp"
#include "core/events/message_pipeline.hpp"
#include "core/events/midnight.hpp"
#include "core/events/nicknames.hpp"
#include "core/events/reactions.hpp"
#include "core/events/triggers.hpp"
#include "core/events/url_replacer.hpp"
#include "core/events/url_rules.hpp"
#include "core/ports/clock.hpp"
#include "core/ui/paginator.hpp"

#include <dpp/dpp.h>

namespace latibot {

/// Owns the Discord connection and the subsystems hanging off it.
///
/// This is the shell: it wires DPP events to core functions and holds the
/// ports features talk through. The logic itself lives outside, where it can
/// be tested without Discord (plan v4 §17.3).
class bot {
public:
    bot(config::bootstrap settings, const config::secrets& credentials);

    bot(const bot&) = delete;
    bot& operator=(const bot&) = delete;

    /// Connects and blocks until the bot shuts down.
    void run();

    [[nodiscard]] commands::registry& commands() noexcept { return commands_; }
    [[nodiscard]] db::database& database() noexcept { return database_; }
    [[nodiscard]] config::guild_settings& guild_settings() noexcept { return guild_settings_; }
    [[nodiscard]] ports::discord_gateway& gateway() noexcept { return gateway_; }
    [[nodiscard]] ports::http_client& http() noexcept { return http_; }
    [[nodiscard]] discord::raw_api& raw() noexcept { return raw_; }
    [[nodiscard]] ports::clock& clock() noexcept { return clock_; }
    [[nodiscard]] const config::bootstrap& settings() const noexcept { return settings_; }

private:
    void register_commands();
    void register_stages();
    void register_events();

    /// Starts the two things that happen on a clock rather than on an event:
    /// the midnight messages and the database backups (plan v4 §10, §5.2).
    void register_timers();

    /// Warns about anything the bot cannot do in this guild. Never fatal: a
    /// missing permission disables one feature, not the bot (plan v4 §7).
    void check_permissions(const dpp::guild& guild) const;

    /// Turns a DPP message into the plain struct the stages work on, which is
    /// where the Administrator check happens.
    [[nodiscard]] events::incoming_message describe(const dpp::message& message) const;

    /// Performs what the stages decided.
    void carry_out(const std::vector<events::action>& actions);

    /// Buttons and select menus. `chosen` is the select menu's value, empty
    /// for a button. Both arrive here because a panel mixes the two and the
    /// custom_id says what to do either way; the id is passed separately
    /// because DPP puts it on each event type rather than on their base.
    void on_component(const dpp::interaction_create_t& event, const std::string& custom_id, const std::string& chosen);

    /// Modal submissions.
    void on_form(const dpp::form_submit_t& event);

    /// The URL rule modal: add, edit, or rename a rule.
    void on_url_form(const dpp::form_submit_t& event, const ui::page_state& state);

    /// The trigger panel's and list's buttons and menu. False when the view
    /// is not one of theirs.
    bool on_trigger_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen,
                              const commands::user_label& who);

    /// The URL rule panel's buttons and menu. False when `view` is not one of
    /// its views.
    bool on_url_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen,
                          const commands::user_label& who);

    /// Records a nickname change, if it is one, and says which row it wrote.
    ///
    /// Shared by the gateway event and the startup sweep, because "is this
    /// different from what we last saw" is the same question either way
    /// (plan v4 §8.4).
    std::optional<std::int64_t> record_nickname(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname,
                                                events::nickname_source source);

    /// A nickname change seen on the gateway.
    void on_member_update(const dpp::guild_member& member);

    /// An audit entry that may name who made a change already recorded.
    void on_audit_entry(const dpp::audit_entry& entry, dpp::snowflake guild_id);

    /// Asks Discord for the audit log a little later, for the one row it was
    /// hoping to attribute.
    ///
    /// The safety net for a gateway entry that never arrived — a reconnect, a
    /// dropped event (plan v4 §8.1). Costs one API call per change that is
    /// still unattributed when it runs, which is normally none of them.
    void attribute_later(dpp::snowflake guild_id, dpp::snowflake user_id, std::int64_t row);

    /// Writes down nicknames that changed while the bot was not running.
    void reconcile_nicknames(const dpp::guild& guild);

    /// Copies the Java bot's URL rules into a guild, once (plan v4 §9.5).
    void import_url_rules(const dpp::guild& guild);

    /// Somebody pressed Retry on a replacement that found no preview.
    void retry_replacement(const dpp::interaction_create_t& event, dpp::snowflake message_id, const commands::user_label& who);

    /// Runs what the embed tracker decided, without holding up the caller.
    void carry_out(std::vector<events::embed_action> actions);

    /// The clock's time to the second, which is what the database stores.
    [[nodiscard]] std::chrono::sys_seconds now_seconds() const;

    /// Applies one change to a trigger from the panel and logs what happened.
    /// `change` returns the past-tense verb for the log, so the two toggles
    /// differ only in the field they flip.
    void toggle_trigger(std::int64_t id, dpp::snowflake guild, const commands::user_label& who,
                        const std::function<std::string_view(events::trigger&)>& change);

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
    events::trigger_responder trigger_responder_;
    events::midnight_store midnight_;
    events::midnight_scheduler midnight_scheduler_;
    events::url_rule_store url_rules_;
    events::replacement_store replacements_;
    events::reaction_store reactions_;
    events::backfill_progress_store backfill_progress_;
    events::backfill_service backfill_;
    events::embed_tracker embed_tracker_;
    events::pipeline pipeline_;
};

} // namespace latibot
