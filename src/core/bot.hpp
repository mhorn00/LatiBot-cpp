#pragma once

#include "core/audio/dectalk_engine.hpp"
#include "core/audio/dectalk_speech.hpp"
#include "core/audio/speech_queue.hpp"
#include "core/audio/voice_mixer.hpp"
#include "core/audio/voice_store.hpp"
#include "core/commands/llm.hpp"
#include "core/commands/registry.hpp"
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
#include "core/events/emoji_copies.hpp"
#include "core/events/emote_reactions.hpp"
#include "core/events/log_channel.hpp"
#include "core/events/media_posts.hpp"
#include "core/events/message_pipeline.hpp"
#include "core/events/reactions.hpp"
#include "core/events/url_replacer.hpp"
#include "core/events/url_rules.hpp"
#include "core/events/voice_sessions.hpp"
#include "core/llm/advanced_triggers.hpp"
#include "core/llm/aliases.hpp"
#include "core/llm/documents.hpp"
#include "core/llm/guards.hpp"
#include "core/llm/memory.hpp"
#include "core/llm/provider.hpp"
#include "core/llm/responder.hpp"
#include "core/llm/spend.hpp"
#include "core/llm/stage.hpp"
#include "core/llm/tools.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/host.hpp"
#include "core/modules/module.hpp"
#include "core/music/music_player.hpp"
#include "core/music/pot_provider.hpp"
#include "core/music/yt_dlp.hpp"
#include "core/ports/clock.hpp"
#include "core/ui/paginator.hpp"
#include "core/ui/panel_routes.hpp"

#include <dpp/dpp.h>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace latibot {

/// Owns the Discord connection and the subsystems hanging off it.
///
/// This is the shell: it wires DPP events to core functions and holds the
/// ports features talk through. The logic itself lives outside, where it can
/// be tested without Discord (docs/testing/README.md).
///
/// It is also the modules' host (docs/modules/Module_Plan_Final.md §4): it
/// builds them with `make_modules`, has each offer its capabilities and then
/// start, and only then connects. The features not yet moved into a module
/// are still its own members.
class bot final : public modules::host {
public:
    bot(config::bootstrap settings, const config::secrets& credentials, const modules::module_factory& make_modules);

    bot(const bot&) = delete;
    auto operator=(const bot&) -> bot& = delete;

    /// Connects and blocks until the bot shuts down.
    auto run() -> void;

    // modules::host
    [[nodiscard]] auto database() -> db::database& override { return database_; }
    [[nodiscard]] auto settings() -> config::guild_settings& override { return guild_settings_; }
    [[nodiscard]] auto bootstrap() const -> const config::bootstrap& override { return settings_; }
    [[nodiscard]] auto section(std::string_view name) -> const nlohmann::json& override;
    [[nodiscard]] auto gateway() -> ports::discord_gateway& override { return gateway_; }
    [[nodiscard]] auto http() -> ports::http_client& override { return http_; }
    [[nodiscard]] auto raw() -> discord::raw_api& override { return raw_; }
    [[nodiscard]] auto clock() -> ports::clock& override { return clock_; }
    [[nodiscard]] auto cluster() -> dpp::cluster& override { return cluster_; }
    [[nodiscard]] auto me() const -> const dpp::user& override { return cluster_.me; }
    [[nodiscard]] auto capabilities() const -> const modules::capability_registry& override { return capabilities_; }
    [[nodiscard]] auto slash_commands() -> commands::registry& override { return commands_; }
    [[nodiscard]] auto panels() -> ui::panel_routes& override { return panels_; }
    auto add_stage(int position, std::string name, events::pipeline::stage_fn stage) -> void override;
    auto every(std::chrono::seconds interval, std::string name, std::function<void()> work) -> void override;
    auto after(std::chrono::seconds delay, std::string name, std::function<void()> work) -> void override;
    auto intents(std::uint32_t wanted) -> void override { module_intents_ |= wanted; }
    auto permission(std::uint64_t bits, std::string purpose) -> void override;
    auto secret(std::string value) -> void override { log_channel_.add_secret(std::move(value)); }
    auto post(events::send_message message) -> void override;
    auto detach(dpp::task<void> work, std::string what) -> void override;

protected:
    auto note_listener(std::string_view name) -> void override { listeners_.emplace_back(name); }

private:
    /// Builds the modules, has each offer, then start, and logs what they
    /// added (docs/modules/Module_Plan_Final.md §4.3).
    auto start_modules(const modules::module_factory& make_modules) -> void;

    auto register_commands() -> void;
    auto register_stages() -> void;
    auto register_events() -> void;

    /// Connected, or reconnected: puts the last `/status` back, and registers
    /// the commands the first time.
    auto on_ready(const dpp::ready_t& event) -> void;

    /// Starts the things that happen on a clock rather than on an event: the
    /// embed tracker's one-second tick, the log channel, auto-leave and the
    /// database backups (docs/features/Operations.md §2). Modules start
    /// their own.
    auto register_timers() -> void;

    /// Why music cannot play, when yt-dlp or ffmpeg was not found; empty
    /// when both were.
    [[nodiscard]] auto music_unavailable() const -> std::string;

    /// Says at startup whether music can play, and asks yt-dlp and ffmpeg
    /// their versions, for the log.
    auto log_music_tools() -> void;

    /// Says whether yt-dlp signs in, and with how many cookies, never what
    /// they are (docs/features/Music.md §4.9).
    auto log_music_account() const -> void;

    /// Starts bgutil's PO token provider when it is set up, and says why not
    /// when it is not (docs/features/Music.md §4.10).
    auto start_pot_provider() -> void;

    /// Warns about anything the bot cannot do in this guild. Never fatal: a
    /// missing permission disables one feature, not the bot
    /// (docs/features/Operations.md §6).
    auto check_permissions(const dpp::guild& guild) const -> void;

    /// Turns a DPP message into the plain struct the stages work on, which is
    /// where the Administrator check happens. `raw_event` is the gateway
    /// frame, the only place a reply says whose message it replies to.
    [[nodiscard]] auto describe(const dpp::message& message, const std::string& raw_event) const -> events::incoming_message;

    /// The provider that serves a kind of model, or null without its key.
    [[nodiscard]] auto provider_for(llm::provider_kind kind) const -> llm::provider*;

    /// Everything `/llm`, `/memory` and their panels work with.
    [[nodiscard]] auto llm_services() -> commands::llm_command_services;

    /// Waits out any pacing, then has the model answer
    /// (docs/features/Language_Model.md).
    auto answer_with_llm(llm::ask_llm ask) -> dpp::task<void>;

    /// Performs what the stages decided.
    auto carry_out(std::vector<events::action> actions) -> void;

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

    /// Someone's voice state changed, the bot's included. Tidies up after the
    /// bot leaves a channel, however that happened, and tells the auto-leave
    /// check whether it is on its own (docs/features/Voice_Channels.md §2.3).
    auto on_voice_state(const dpp::voicestate& state) -> void;

    /// Copies the Java bot's URL rules into a guild, once
    /// (docs/features/Url_Replacement.md §2.7).
    auto import_url_rules(const dpp::guild& guild) -> void;

    /// Somebody pressed Retry on a replacement that found no preview.
    auto retry_replacement(const dpp::interaction_create_t& event, dpp::snowflake message_id, const commands::user_label& who) -> void;

    /// Settles this guild's replacements the last run left mid-watch, once:
    /// its first guild_create hands them over
    /// (docs/features/Url_Replacement.md §2.5).
    auto settle_stranded_replacements(dpp::snowflake guild_id) -> void;

    /// Runs what the embed tracker decided, without holding up the caller.
    auto carry_out(std::vector<events::embed_action> actions) -> void;

    /// One round of keeping the bot's own copies of emojis
    /// (docs/features/Link_Stats.md §10).
    auto copy_emojis() -> dpp::task<void>;

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
    events::url_rule_store url_rules_;
    commands::url_panel url_panel_;
    events::replacement_store replacements_;
    events::media_tracker media_;
    events::reaction_store reactions_;
    events::emote_tracker emotes_;
    events::backfill_progress_store backfill_progress_;
    events::backfill_service backfill_;
    events::emoji_copy_store emoji_copies_;
    events::emoji_copier emoji_copier_;
    events::embed_tracker embed_tracker_;
    events::pipeline pipeline_;

    // Speech (docs/features/Speech.md, docs/features/Voice_Channels.md). The
    // engine starts its worker thread at construction, and is gone before the
    // cluster is.
    audio::dectalk_engine tts_;
    discord::dpp_voice_output voice_output_;

    /// The only writer to a voice connection: speech and music both go
    /// through it (docs/features/Music.md §4.2).
    audio::voice_mixer mixer_;
    audio::speech_queue speech_;
    events::voice_sessions voice_sessions_;
    events::auto_leave auto_leave_;
    audio::voice_store voices_;
    commands::voice_drafts voice_drafts_;
    commands::voice_lab voice_lab_;

    /// The `speech` capability, which the language model speaks through
    /// (docs/modules/Module_Plan_Final.md §5.3).
    audio::dectalk_speech dectalk_speech_;

    // Music (docs/features/Music.md). yt-dlp and ffmpeg are looked for once,
    // at startup; without them the music commands say so and nothing plays.
    std::optional<std::filesystem::path> ytdlp_;
    std::optional<std::filesystem::path> ffmpeg_;
    /// Optional: without it yt-dlp cannot solve YouTube's JavaScript
    /// challenges, and some of YouTube, age-restricted videos above all, fails.
    std::optional<std::filesystem::path> deno_;
    /// bgutil's PO token provider's `server` folder, when it is there
    /// (§4.10).
    std::optional<std::filesystem::path> pot_server_;
    /// The account yt-dlp signs in as when it must, if the owner gave one
    /// (§4.9).
    music::cookie_status ytdlp_cookies_;
    music::ytdlp_resolver music_resolver_;
    music::ytdlp_opener music_opener_;
    music::music_player music_;
    /// Runs the PO token provider for as long as the bot runs; null when it
    /// is not set up.
    std::unique_ptr<music::pot_provider> pot_provider_;

    // The language model (docs/features/Language_Model.md). A provider exists
    // only when its key is set; the stage and the commands ask `provider_for`
    // rather than assume.
    llm::usage_store llm_usage_;
    llm::document_store llm_documents_;
    llm::memory_store llm_memories_;
    llm::alias_store llm_aliases_;
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

    /// Asks yt-dlp and ffmpeg their versions at startup, for the log, without
    /// holding startup up.
    std::jthread music_versions_;

    // What the modules registered (docs/modules/Module_Plan_Final.md §4.2).
    // The routes and the capabilities point into the modules, which are
    // destroyed first.
    modules::capability_registry capabilities_;
    ui::panel_routes panels_;
    std::uint32_t module_intents_ = 0;
    /// Permissions modules asked for, checked with the commands' in each
    /// server.
    std::vector<std::pair<std::uint64_t, std::string>> module_permissions_;
    /// Who listens to which DPP event, for the startup log.
    std::vector<std::string> listeners_;
    /// The config.json sections modules asked for; any other is warned about.
    std::set<std::string, std::less<>> claimed_sections_;

    /// The modules, in dependency order. After everything they were given,
    /// so they are destroyed before any of it.
    modules::module_list modules_;

    /// The pause between the goodbye and shutting down. Last, so it is
    /// joined first when the bot is destroyed, while the cluster it shuts
    /// down still exists.
    std::jthread goodbye_;
};

} // namespace latibot
