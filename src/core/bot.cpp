#include "core/bot.hpp"

#include "core/commands/basic.hpp"
#include "core/commands/bots.hpp"
#include "core/commands/chat.hpp"
#include "core/commands/logs.hpp"
#include "core/commands/music.hpp"
#include "core/commands/preflight.hpp"
#include "core/commands/speak.hpp"
#include "core/commands/voice.hpp"
#include "core/db/backup.hpp"
#include "core/db/schema_versions.hpp"
#include "core/db/schemas.hpp"
#include "core/discord/dpp_log.hpp"
#include "core/discord/message_flags.hpp"
#include "core/discord/voice_state.hpp"
#include "core/events/goodbye.hpp"
#include "core/events/stage_order.hpp"
#include "core/modules/host.hpp"
#include "core/modules/module.hpp"
#include "core/ui/interaction.hpp"
#include "core/ui/paginator.hpp"
#include "core/util/log.hpp"
#include "core/util/process.hpp"
#include "core/util/text.hpp"
#include "core/version.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace latibot {
namespace {

/// The intents to connect with, before the modules add theirs.
///
/// i_message_content is privileged and must also be enabled in the Discord
/// developer portal. Without it every guild message arrives with an empty
/// `content`, which silently disables the whole pipeline: the goodbye phrase
/// and the triggers both read it (docs/features/Operations.md §3).
constexpr std::uint32_t core_intents = dpp::i_default_intents | dpp::i_message_content;

/// Who wrote the message a reply replies to, or 0.
///
/// DPP reads the reference but not the message it points at, which Discord
/// sends whole in the same frame. Only parsed for replies, so an ordinary
/// message costs nothing.
auto replied_to_author(const std::string& raw_event) -> dpp::snowflake {
    const auto frame = nlohmann::json::parse(raw_event, nullptr, /*allow_exceptions=*/false);
    if (frame.is_discarded() || !frame.contains("d")) return {};

    const auto& payload = frame.at("d");
    const auto referenced = payload.find("referenced_message");
    if (referenced == payload.end() || !referenced->is_object()) return {};
    const auto author = referenced->find("author");
    if (author == referenced->end() || !author->is_object()) return {};
    const auto id = author->find("id");
    if (id == author->end() || !id->is_string()) return {};
    return dpp::snowflake(id->get<std::string>());
}

using ui::answer_privately;

/// What a button, menu or form no panel claims hears back. It is one of ours,
/// since Discord only sends the bot its own, so it is from a build whose
/// panels were different.
constexpr std::string_view stale_component_reply = "that's from an older version of me; run the command again for a fresh one";

/// Makes sure the folder holding the database exists, so a first run on a
/// clean machine works without setup. Returns by value: handing back a
/// reference to the parameter would dangle if a caller ever passed a
/// temporary.
auto prepare(const std::filesystem::path& database_path) -> std::filesystem::path {
    if (database_path.has_parent_path() && !database_path.parent_path().empty()) {
        std::filesystem::create_directories(database_path.parent_path());
    }
    return database_path;
}

/// bgutil's PO token provider's `server` folder: where config.json says, or
/// beside the bot. Nothing when it is not there.
auto locate_pot_server(const config::bootstrap& settings) -> std::optional<std::filesystem::path> {
    std::filesystem::path server = settings.music.pot_provider_path;
    if (server.empty()) {
        const auto directory = util::executable_directory();
        if (!directory) return std::nullopt;
        server = *directory / "bgutil-ytdlp-pot-provider" / "server";
    }
    std::error_code error;
    if (!std::filesystem::is_directory(server, error)) return std::nullopt;
    std::filesystem::path found = std::filesystem::absolute(server, error);
    return error ? server : found;
}

/// What every run of yt-dlp is told: where Deno is, and the PO token
/// provider's address when its plugin is beside yt-dlp to ask it.
auto music_extras(const std::optional<std::filesystem::path>& deno, const std::optional<std::filesystem::path>& ytdlp, int pot_port)
    -> music::ytdlp_extras {
    music::ytdlp_extras extras{.deno = deno, .pot_provider = {}, .sign_in = {}};
    if (ytdlp && music::pot_plugin_installed(*ytdlp)) extras.pot_provider = music::pot_provider_address(pot_port);
    return extras;
}

/// What the log channel masks, in case anything ever logs one of them.
auto secrets_of(const config::secrets& credentials) -> std::vector<std::string> {
    return {credentials.discord_token};
}

} // namespace

bot::bot(config::bootstrap settings, const config::secrets& credentials, const modules::module_factory& make_modules)
    : settings_(std::move(settings)),
      database_(prepare(settings_.database_path)),
      guild_settings_(database_),
      cluster_(credentials.discord_token, core_intents),
      gateway_(cluster_),
      http_(cluster_),
      raw_(cluster_),
      bot_allowlist_(database_),
      voice_output_(cluster_),
      mixer_(voice_output_),
      speech_(mixer_),
      auto_leave_(clock_),
      voices_(database_),
      voice_drafts_(clock_),
      voice_lab_(voice_drafts_, voices_, clock_,
                 {.engine = &tts_, .queue = &speech_, .settings = &guild_settings_, .bootstrap = &settings_, .voices = &voices_}),
      dectalk_speech_(tts_, speech_, voice_sessions_, guild_settings_),
      ytdlp_(util::locate_program("yt-dlp", settings_.music.ytdlp_path)),
      ffmpeg_(util::locate_program("ffmpeg", settings_.music.ffmpeg_path)),
      deno_(util::locate_program("deno", settings_.music.deno_path)),
      pot_server_(locate_pot_server(settings_)),
      ytdlp_cookies_(music::load_cookies(credentials.ytdlp_cookies, credentials.ytdlp_firefox_profile,
                                         settings_.database_path.parent_path() / "yt-dlp-runs")),
      music_resolver_(ytdlp_.value_or("yt-dlp.exe"), std::chrono::seconds{30}, 2, ytdlp_cookies_.source,
                      music_extras(deno_, ytdlp_, settings_.music.pot_provider_port)),
      music_opener_(ytdlp_.value_or("yt-dlp.exe"), ffmpeg_.value_or("ffmpeg.exe"), true, ytdlp_cookies_.source,
                    music_extras(deno_, ytdlp_, settings_.music.pot_provider_port)),
      music_(music_opener_, mixer_,
             {.volume_percent = [this](dpp::snowflake guild) { return commands::music_volume_for(guild_settings_, guild); },
              .track_limit = [this](dpp::snowflake guild) { return commands::track_limit_for(guild_settings_, guild); },
              .notify =
                  [this](dpp::snowflake channel, std::string text) {
                      post(events::send_message{.channel_id = channel,
                                                .content = std::move(text),
                                                .flags = dpp::m_suppress_notifications,
                                                .what = "a note about a track"});
                  }}),
      log_destinations_(guild_settings_),
      log_channel_(gateway_, clock_, secrets_of(credentials)) {
    util::log().set_level(settings_.log_level);

    util::log().info("LatiBot {} starting", version_string());
    util::log().debug("log level {}; {} trusted guild(s), {} trusted user(s)", util::to_string(settings_.log_level),
                      settings_.trusted_guilds.size(), settings_.trusted_users.size());

    // After the line above, so that any migration it applies is logged under a
    // heading rather than before the bot has said it is starting.
    // The core's tables and those of the features not yet in modules; each
    // module's follow when the modules are built (start_modules).
    db::prepare_schema_versions(database_);
    for (const db::module_schema& schema : db::builtin_schemas()) {
        db::apply_schema(database_, schema);
    }

    // Music plays through the mixer, which reads it from the player.
    mixer_.set_music(&music_);
    log_music_tools();
    start_pot_provider();

    // As soon as the setting can be read, so the rest of starting up is in
    // the channel too. It is posted once the connection is up.
    if (const auto destination = log_destinations_.find()) {
        log_channel_.start(*destination);
        util::log().info("posting the log to channel {} in guild {} at {}", destination->channel_id, destination->guild_id,
                         util::to_string(destination->level));
    }

    register_commands();
    register_stages();
    register_events();
    register_timers();
    start_modules(make_modules);

    std::string stages;
    for (const std::string_view name : pipeline_.stage_names()) {
        if (!stages.empty()) stages += " -> ";
        stages += name;
    }
    util::log().debug("{} commands registered; message stages: {}", commands_.size(), stages);
}

auto bot::register_commands() -> void {
    commands::add_basic_commands(commands_, cluster_, clock_, guild_settings_, [this] { cluster_.shutdown(); });
    commands_.add(std::make_unique<commands::bots_command>(bot_allowlist_));
    commands_.add(std::make_unique<commands::logs_command>(settings_, log_destinations_, log_channel_, gateway_));

    const commands::speech_services speech{
        .engine = &tts_, .queue = &speech_, .settings = &guild_settings_, .bootstrap = &settings_, .voices = &voices_};
    commands_.add(std::make_unique<commands::speak_command>(speech));
    commands_.add(std::make_unique<commands::tts_command>(speech, voice_lab_));
    commands_.add(std::make_unique<commands::chat_command>(speech, raw_));
    commands_.add(std::make_unique<commands::voice_command>(voice_sessions_, guild_settings_));
    commands_.add(std::make_unique<commands::music_command>(commands::music_services{
        .player = &music_, .resolver = &music_resolver_, .settings = &guild_settings_, .unavailable = music_unavailable()}));
}

auto bot::register_stages() -> void {
    // Each at its position, which decides where it runs, whatever order the
    // lines are in (events/stage_order.hpp, docs/features/Message_Pipeline.md
    // §2.2). Modules add theirs: links' replacement at `rewrite`, triggers'
    // replies at `reply`, the language model at `model`.
    pipeline_.add(events::stage_order::stop, "goodbye", events::goodbye_stage(guild_settings_));
}

auto bot::register_events() -> void {
    // DPP's own logging goes through our logger, so there is one format and
    // one level to configure. DPP hands over finished text, so there are no
    // types left to colour; the [dpp] tag is coloured instead, which is what
    // tells its lines apart from ours at a glance.
    cluster_.on_log([](const dpp::log_t& event) {
        util::log().log(discord::log_level_of(event.severity), "{} {}", util::log_source{"dpp"}, event.message);
    });

    // A coroutine handler: DPP keeps `event` alive until it finishes, so a
    // command may keep using it after an await.
    cluster_.on_slashcommand([this](const dpp::slashcommand_t& event) -> dpp::task<void> {
        co_await commands_.dispatch(event.command.get_command_name(), event);
    });

    cluster_.on_ready([this](const dpp::ready_t& event) { on_ready(event); });

    // Guilds arrive as guild_create after the gateway connects, including the
    // ones the bot was already in, so this covers startup and later joins
    // alike without a separate sweep on ready
    // (docs/features/Operations.md §6).
    cluster_.on_guild_create([this](const dpp::guild_create_t& event) {
        const dpp::guild& guild = event.created;
        util::log().info("in guild {} ({})", guild.name, guild.id);

        check_permissions(guild);

        util::log().debug("{}: {} allowed bot(s), goodbye phrase \"{}\"", guild.name, bot_allowlist_.for_guild(guild.id).size(),
                          guild_settings_.get(guild.id, events::goodbye_phrase_key, events::default_goodbye_phrase));
    });

    // Every message: reduce it to plain data, let the stages decide, then do
    // what they asked. Handlers run on DPP's thread pool, so two messages
    // can be in here at once.
    cluster_.on_message_create(
        [this](const dpp::message_create_t& event) { carry_out(pipeline_.run(describe(event.msg, event.raw_event))); });

    cluster_.on_autocomplete([this](const dpp::autocomplete_t& event) { commands_.offer_completions(event.name, event); });

    // Panels: buttons and menus go to `on_component` and modals to `on_form`,
    // both routed by the view name in the custom_id.
    cluster_.on_button_click([this](const dpp::button_click_t& event) { on_component(event, event.custom_id, {}); });
    cluster_.on_select_click([this](const dpp::select_click_t& event) {
        on_component(event, event.custom_id, event.values.empty() ? std::string{} : event.values.front());
    });
    cluster_.on_form_submit([this](const dpp::form_submit_t& event) { on_form(event); });

    // Speech waits for the connection to be ready, and is told when each
    // utterance finishes playing (docs/features/Voice_Channels.md §3).
    // The mixer first on both: a new connection has lost what the old one
    // queued, and music's markers are the mixer's to hand on.
    cluster_.on_voice_ready([this](const dpp::voice_ready_t& event) {
        if (event.voice_client == nullptr) return;
        mixer_.on_ready(event.voice_client->server_id);
        speech_.on_ready(event.voice_client->server_id);
    });
    cluster_.on_voice_track_marker([this](const dpp::voice_track_marker_t& event) {
        if (event.voice_client == nullptr) return;
        mixer_.on_marker(event.voice_client->server_id, event.track_meta);
        speech_.on_marker(event.voice_client->server_id, event.track_meta);
    });
    cluster_.on_voice_state_update([this](const dpp::voice_state_update_t& event) { on_voice_state(event.state); });
}

auto bot::on_ready(const dpp::ready_t& event) -> void {
    util::log().info("connected to Discord as {} ({})", cluster_.me.username, cluster_.me.id);

    // A presence lasts one session, so every connect, a reconnect included,
    // puts the last /status back (docs/features/Basic_Commands.md §2).
    if (const auto status = commands::load_status(guild_settings_)) {
        cluster_.set_presence(commands::presence_for(*status));
        util::log().info("status restored: {} \"{}\"", status->type.empty() ? "playing" : status->type, status->text);
    }

    if (!dpp::run_once<struct register_bot_commands>()) {
        // on_ready fires again after a reconnect; the commands are global
        // and already registered, so this is the normal path, not a fault.
        util::log().debug("reconnected (session {}); commands already registered", event.session_id);
        return;
    }

    if (commands_.size() == 0) {
        // A bulk create with an empty list deletes every registered
        // command, which is not what "no commands built yet" should mean.
        util::log().warn("no commands registered; skipping command registration");
        return;
    }

    const std::vector<dpp::slashcommand> payloads = commands_.build_all(cluster_.me.id);
    cluster_.global_bulk_command_create(payloads);

    util::log().info("registering {} commands with Discord", payloads.size());
    for (const dpp::slashcommand& payload : payloads) {
        util::log().debug("  /{}: {}", payload.name, payload.description);
    }
}

namespace {

/// A timer's work, logging what it throws rather than letting it reach DPP.
///
/// DPP runs timers on its socket thread and puts a repeating one back in its
/// queue only after the callback returns, so one exception would stop that
/// timer for the life of the process, leaving a single log line that does
/// not say which timer it was.
auto run_guarded(std::string_view what, const std::function<void()>& work) -> void {
    try {
        work();
    } catch (const std::exception& error) {
        modules::report_failure(what, &error);
    } catch (...) {
        modules::report_failure(what, nullptr);
    }
}

} // namespace

auto bot::every(std::chrono::seconds interval, std::string name, std::function<void()> work) -> void {
    cluster_.start_timer([name = std::move(name), work = std::move(work)](dpp::timer) { run_guarded(name, work); },
                         static_cast<std::uint64_t>(interval.count()));
}

auto bot::after(std::chrono::seconds delay, std::string name, std::function<void()> work) -> void {
    // A self-cancelling repeat, which is the one-shot DPP does not have. The
    // handle arrives in the callback, so nothing has to be kept alive here.
    cluster_.start_timer(
        [this, name = std::move(name), work = std::move(work)](dpp::timer handle) {
            cluster_.stop_timer(handle);
            run_guarded(name, work);
        },
        static_cast<std::uint64_t>(delay.count()));
}

auto bot::register_timers() -> void {
    // Keeps a few seconds of music queued on each connection playing it
    // (docs/features/Music.md §4.2). Most ticks find nothing to do.
    every(std::chrono::seconds{1}, "feeding music", [this] { mixer_.tick(); });

    // Posts what has been logged since the last tick, if a channel is set.
    // A tick with nothing waiting, or no channel, costs a coroutine that
    // takes a lock and returns.
    every(events::log_channel_tick, "the log channel's tick", [this] { detach(log_channel_.flush(), "posting the log"); });

    // Leaving a voice channel nobody else is in, once its guild's grace has
    // passed (docs/features/Voice_Channels.md §2.3). Leaving is the bot's own
    // voice state changing, which on_voice_state tidies up after.
    every(events::auto_leave_tick, "the voice auto-leave check", [this] {
        const auto grace = [this](dpp::snowflake guild) { return commands::voice_grace_for(guild_settings_, guild); };
        for (const dpp::snowflake guild : auto_leave_.due(grace)) {
            dpp::discord_client* shard = discord::shard_for(cluster_, guild);
            if (shard == nullptr) continue;
            shard->disconnect_voice(guild);
            util::log().info("left voice in guild {}: nobody else was there for {}", guild, grace(guild));
        }
    });

    if (settings_.backup_interval <= std::chrono::minutes::zero() || settings_.backups_to_keep <= 0) {
        util::log().info("database backups are off");
        return;
    }

    every(std::chrono::duration_cast<std::chrono::seconds>(settings_.backup_interval), "the database backup", [this] {
        try {
            const auto written = db::create_backup(database_, settings_.backup_directory, "bot", settings_.backups_to_keep, clock_.now());
            util::log().info("wrote {}", written.generic_string());
        } catch (const std::exception& error) {
            // A backup that fails is worth knowing about and is never worth
            // taking the bot down for.
            util::log().error("could not write a backup to {}: {}", settings_.backup_directory.generic_string(), error.what());
        }
    });

    util::log().info("backing up to {} every {}, keeping {}", settings_.backup_directory.generic_string(), settings_.backup_interval,
                     settings_.backups_to_keep);
}

auto bot::on_voice_state(const dpp::voicestate& state) -> void {
    const dpp::snowflake guild = state.guild_id;
    const bool about_the_bot = state.user_id == cluster_.me.id;

    if (about_the_bot && state.channel_id.empty()) {
        // Left, whichever way: /leave, /voice stop, the auto-leave, being
        // disconnected by a moderator, or the connection dropping.
        if (voice_sessions_.end(guild)) util::log().info("voice session in guild {} ended", guild);
        speech_.forget(guild);
        // Leaving takes the music queue with it (docs/features/Music.md §3.4).
        music_.forget(guild);
        mixer_.forget(guild);
        auto_leave_.forget(guild);
        return;
    }
    if (about_the_bot) voice_sessions_.moved(guild, state.channel_id);

    // DPP has already updated its cache for this change, so counting from it
    // sees the channel as it is now.
    const dpp::snowflake channel = about_the_bot ? state.channel_id : discord::bot_voice_channel(cluster_, guild);
    auto_leave_.observe(guild, !channel.empty(), discord::humans_in(guild, channel, cluster_.me.id));
}

auto bot::log_music_tools() -> void {
    if (!ytdlp_ || !ffmpeg_) {
        util::log().warn("music is off: {}", music_unavailable());
        return;
    }
    const std::filesystem::path ytdlp = *ytdlp_;
    const std::filesystem::path ffmpeg = *ffmpeg_;
    util::log().info("music uses yt-dlp at {} and ffmpeg at {}", ytdlp.string(), ffmpeg.string());
    // A warning, not a reason to turn music off: most sites need no
    // JavaScript, and yt-dlp gets some of YouTube without it.
    if (deno_) {
        util::log().info("yt-dlp solves YouTube's JavaScript challenges with Deno at {}", deno_->string());
    } else {
        util::log().warn(
            "Deno was not found, so yt-dlp cannot solve YouTube's JavaScript challenges, and some YouTube videos will fail, "
            "age-restricted ones above all. Install Deno 2.3 or newer (winget install DenoLand.Deno), beside the bot or on PATH, "
            "or name it in config.json (music.deno_path)");
    }
    log_music_account();
    // Which versions, off the startup path: an old yt-dlp is the usual
    // reason a site stops working, and asking takes a second or two.
    std::vector<std::pair<std::filesystem::path, const char*>> programs{{ytdlp, "--version"}, {ffmpeg, "-version"}};
    if (deno_) programs.emplace_back(*deno_, "--version");
    // Only the log call in the catch could still throw, as in main(), and
    // there is nowhere left to report that.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    music_versions_ = std::jthread([programs = std::move(programs)] {
        // Everything inside the try: a thread must let nothing out.
        try {
            for (const auto& [program, flag] : programs) {
                const auto ran = util::run({.path = program, .arguments = {flag}, .working_directory = {}}, std::chrono::seconds{20});
                const auto first_line = util::lines(ran.output);
                util::log().info("{}: {}", program.stem().string(), first_line.empty() ? "no version given" : first_line.front());
            }
        } catch (const std::exception& error) {
            util::log().warn("could not ask yt-dlp, ffmpeg or Deno its version: {}", error.what());
        } catch (...) { // NOLINT(bugprone-empty-catch)
        }
    });
}

auto bot::log_music_account() const -> void {
    const music::cookie_status& cookies = ytdlp_cookies_;
    const std::string named = cookies.named().generic_string();
    if (named.empty()) {
        util::log().debug("music fetches signed out: neither LATIBOT_YTDLP_FIREFOX_PROFILE nor LATIBOT_YTDLP_COOKIES is set");
        return;
    }
    const bool profile = !cookies.profile.empty();
    if (cookies.both_named) {
        util::log().warn("LATIBOT_YTDLP_FIREFOX_PROFILE and LATIBOT_YTDLP_COOKIES are both set; music uses the Firefox profile, not {}",
                         cookies.file.generic_string());
    }
    // A warning: the owner set it to sign in, and it will not.
    if (!cookies.source) {
        util::log().warn("music fetches signed out: {} names {}, which {}",
                         profile ? "LATIBOT_YTDLP_FIREFOX_PROFILE" : "LATIBOT_YTDLP_COOKIES", named, cookies.problem);
        return;
    }
    // Counts only. The cookies are a sign-in, and never logged.
    util::log().info("music signs in when it must with the {} {}: {} cookie(s), {} of them for youtube.com",
                     profile ? "Firefox profile" : "cookies in", named, cookies.found.cookies, cookies.found.youtube);
    const std::string_view again = profile ? "open Firefox with it, sign in to YouTube, and close Firefox" : "export it again signed in";
    if (cookies.found.youtube == 0) {
        util::log().warn("{} has no youtube.com cookies, so YouTube will see music as signed out; {}", named, again);
    } else if (!cookies.found.youtube_sign_in) {
        util::log().warn(
            "{} has no youtube.com SAPISID or __Secure-3PAPISID cookie, which yt-dlp needs to sign in; {} "
            "(docs/features/Music.md §4.9)",
            named, again);
    }
    if (cookies.unsaved) {
        util::log().warn(
            "Firefox has cookies for {} that it has not yet saved where yt-dlp reads them; close Firefox, which saves "
            "them, and keep it closed while the bot runs",
            named);
    }
    if (cookies.found.malformed > 0) {
        util::log().warn("{} line(s) of {} are not cookies in the Netscape format, and yt-dlp will skip them", cookies.found.malformed,
                         named);
    }
}

auto bot::start_pot_provider() -> void {
    if (!ytdlp_ || !ffmpeg_) return;
    const std::string address = music::pot_provider_address(settings_.music.pot_provider_port);
    const bool plugin = music::pot_plugin_installed(*ytdlp_);
    const std::string plugins = (ytdlp_->parent_path() / "yt-dlp-plugins").string();

    if (!pot_server_) {
        if (!settings_.music.pot_provider_path.empty()) {
            util::log().warn("music.pot_provider_path in config.json names {}, which is not a folder, so no PO token provider runs",
                             settings_.music.pot_provider_path.generic_string());
        } else if (plugin) {
            util::log().info(
                "bgutil's PO token plugin is in {}, but its provider is not beside the bot; yt-dlp asks {} for tokens, "
                "so run one there (docs/features/Music.md §4.10)",
                plugins, address);
        } else {
            util::log().info(
                "yt-dlp gets no PO tokens, so YouTube may refuse some of its requests; Install-Dependencies.ps1 sets "
                "a provider up (docs/features/Music.md §4.10)");
        }
        return;
    }
    const std::string server = pot_server_->string();
    if (!plugin) {
        util::log().warn(
            "bgutil's PO token provider is at {}, but its yt-dlp plugin is not in {}, so yt-dlp would never ask it; "
            "run Install-Dependencies.ps1 again",
            server, plugins);
        return;
    }
    if (!deno_) {
        util::log().warn("bgutil's PO token provider at {} runs with Deno, which was not found", server);
        return;
    }
    if (!music::pot_provider_ready(*pot_server_)) {
        util::log().warn(
            "bgutil's PO token provider at {} is not set up: its packages are not installed; run "
            "Install-Dependencies.ps1 again",
            server);
        return;
    }
    pot_provider_ =
        std::make_unique<music::pot_provider>(music::pot_provider_program(*deno_, *pot_server_, settings_.music.pot_provider_port));
    util::log().info("yt-dlp gets PO tokens from bgutil's provider, run with Deno from {}, at {}", server, address);
}

auto bot::music_unavailable() const -> std::string {
    if (ytdlp_ && ffmpeg_) return {};
    std::string missing = "yt-dlp and ffmpeg";
    if (ytdlp_) missing = "ffmpeg";
    if (ffmpeg_) missing = "yt-dlp";
    return std::format(
        "i can't play music: {} isn't installed where i can find it. Put it beside the bot or on PATH, or name it in "
        "config.json (music.ytdlp_path, music.ffmpeg_path)",
        missing);
}

auto bot::on_component(const dpp::interaction_create_t& event, const std::string& custom_id, const std::string& chosen) -> void {
    const auto state = ui::decode(custom_id);
    if (!state) {
        // Discord only sends the bot its own components, so this is one of
        // ours from a build that encoded them differently.
        util::log().debug("a component with an unrecognised id \"{}\"", custom_id);
        answer_privately(event, stale_component_reply);
        return;
    }

    const dpp::snowflake guild = event.command.guild_id;
    const commands::user_label who = commands::describe_user(event.command.get_issuing_user());
    util::log().debug("{} used panel {} page {} argument \"{}\"{} in guild {}", who, state->view, state->page, state->argument,
                      chosen.empty() ? std::string{} : std::format(" chose \"{}\"", chosen), guild);

    // A button that fails says so, as a command does, rather than leaving
    // Discord to say the interaction failed. Only the catch and the unclaimed
    // branch answer here, so nothing a panel already answered is answered
    // twice, unless it threw after answering.
    try {
        if (!route_component(event, *state, chosen)) {
            util::log().debug("no panel handles the view {} with argument '{}'", state->view, state->argument);
            answer_privately(event, stale_component_reply);
        }
    } catch (const std::exception& error) {
        util::log().error("panel {} failed for {} in guild {}: {}", state->view, who, guild, error.what());
        answer_privately(event, commands::command_failed_reply);
    }
}

auto bot::route_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool {
    // Every one of these edits the message the component is on rather than
    // posting a new one, which is why the state rides in the custom_id: there
    // is nothing here to expire, leak, or lose across a restart.
    // A module's panel, by the view it claimed (docs/modules/Module_Plan_Final.md
    // §4.6), and then those still in the core, each saying whether the view
    // was one of its own.
    if (panels_.claimed(state.view)) return panels_.on_component(event, state, chosen);
    return voice_lab_.on_component(event, state, chosen) || commands::on_music_component(music_, event, state);
}

auto bot::on_form(const dpp::form_submit_t& event) -> void {
    const auto state = ui::decode(event.custom_id);

    // Every form the bot sends has fields, and Discord sends every one back,
    // empty or not. None at all means the submission was misread, and
    // acting on it would save blanks over what was there.
    if (ui::form_fields(event).empty()) {
        util::log().warn("a modal submission \"{}\" arrived with no fields that could be read; nothing was changed", event.custom_id);
        answer_privately(event, "that form came back empty, so nothing was changed; try again");
        return;
    }

    // Answered when it fails or is not recognised, as a button is.
    try {
        if (state && (panels_.on_form(event, *state) || voice_lab_.on_form(event, *state))) {
            // Each panel answers its own.
        } else {
            util::log().debug("a modal submission with an unrecognised id \"{}\"", event.custom_id);
            answer_privately(event, stale_component_reply);
        }
    } catch (const std::exception& error) {
        util::log().error("form {} failed for {} in guild {}: {}", event.custom_id,
                          commands::describe_user(event.command.get_issuing_user()), event.command.guild_id, error.what());
        answer_privately(event, commands::command_failed_reply);
    }
}

auto bot::describe(const dpp::message& message, const std::string& raw_event) const -> events::incoming_message {
    events::incoming_message described;
    described.guild_id = message.guild_id;
    described.channel_id = message.channel_id;
    described.author_id = message.author.id;
    described.from_self = message.author.id == cluster_.me.id;
    described.from_bot = message.author.is_bot();
    described.author_is_allowed_bot = described.from_bot && bot_allowlist_.contains(message.guild_id, message.author.id);
    described.message_id = message.id;
    described.embeds_suppressed = (message.flags & dpp::m_suppress_embeds) != 0;
    described.content = message.content;

    // The name people see: a server nickname, a display name, or the
    // username, whichever is there first.
    described.author_name = message.member.get_nickname();
    if (described.author_name.empty()) described.author_name = message.author.global_name;
    if (described.author_name.empty()) described.author_name = message.author.username;
    described.author_roles = message.member.get_roles();

    described.mentions_bot =
        std::ranges::any_of(message.mentions, [this](const auto& mention) { return mention.first.id == cluster_.me.id; });
    if (!message.message_reference.message_id.empty()) described.replies_to_bot = replied_to_author(raw_event) == cluster_.me.id;

    // Administrator is a guild-level question, so it needs the guild and the
    // member: a message carries neither on its own.
    if (const dpp::guild* guild = dpp::find_guild(message.guild_id); guild != nullptr) {
        const auto member = guild->members.find(message.author.id);
        if (member != guild->members.end()) {
            const std::uint64_t permissions = guild->base_permissions(member->second);
            described.author_is_administrator = (permissions & dpp::p_administrator) != 0;
        }
    }

    return described;
}

auto bot::post(events::send_message message) -> void {
    std::vector<events::action> one;
    one.emplace_back(std::move(message));
    carry_out(std::move(one));
}

auto bot::detach(dpp::task<void> work, std::string what) -> void {
    ui::detach(std::move(work), std::move(what));
}

auto bot::add_stage(int position, std::string name, events::pipeline::stage_fn stage) -> void {
    pipeline_.add(position, std::move(name), std::move(stage));
}

auto bot::section(std::string_view name) -> const nlohmann::json& {
    claimed_sections_.emplace(name);
    static const nlohmann::json none = nlohmann::json::object();
    const auto found = settings_.sections.find(std::string(name));
    return found != settings_.sections.end() ? *found : none;
}

auto bot::permission(std::uint64_t bits, std::string purpose) -> void {
    module_permissions_.emplace_back(bits, std::move(purpose));
}

auto bot::start_modules(const modules::module_factory& make_modules) -> void {
    // DECtalk's speech, until DECtalk is a module of its own and offers it
    // itself (docs/modules/Module_Plan_Final.md §5.3).
    capabilities_.offer<capabilities::speech>(dectalk_speech_, "dectalk (still in the core)");
    modules_ = modules::start_modules(make_modules, *this, capabilities_);
    // Before connecting, which is when DPP reads them.
    cluster_.intents |= module_intents_;

    std::string names;
    for (const auto& each : modules_) {
        if (!names.empty()) names += ", ";
        names += each->name();
    }
    util::log().info("{} module(s){}{}", modules_.size(), names.empty() ? "" : ": ", names);
    for (const auto& [name, unused] : settings_.sections.items()) {
        if (claimed_sections_.contains(name)) continue;
        util::log().warn(R"(config.json has a "{}" section, but this build has no module that reads it; it is ignored)", name);
    }

    std::string versions;
    for (const auto& [name, version] : db::recorded_versions(database_)) {
        if (!versions.empty()) versions += ", ";
        versions += std::format("{} {}", name, version);
    }
    util::log().info("database {}: {}", settings_.database_path.generic_string(), versions);
    for (const std::string& listener : listeners_) {
        util::log().debug("  listening: {}", listener);
    }
}

auto bot::carry_out(std::vector<events::action> actions) -> void {
    for (events::action& wanted : actions) {
        std::visit(
            [this](auto& step) {
                using step_type = std::decay_t<decltype(step)>;

                if constexpr (std::is_same_v<step_type, events::send_message>) {
                    dpp::message reply(step.channel_id, step.content);
                    discord::apply_flags(reply, step.flags, discord::channel_message_flags);

                    // Logged once Discord answers, not before: a midnight
                    // message has claimed its day already, so a refusal here
                    // (no Send Messages, a deleted channel, text over the
                    // limit) is that day's message gone, and the log is the
                    // only place that can say so.
                    cluster_.message_create(reply,
                                            [what = step.what, channel = step.channel_id, content = step.content,
                                             flags = discord::describe_flags(reply.flags)](const dpp::confirmation_callback_t& done) {
                                                if (done.is_error()) {
                                                    const dpp::error_info error = done.get_error();
                                                    util::log().warn("could not post {} in channel {}: {}", what, channel,
                                                                     error.human_readable.empty() ? error.message : error.human_readable);
                                                    return;
                                                }
                                                util::log().info("posted {} in channel {} ({}): \"{}\"", what, channel, flags, content);
                                            });
                } else if constexpr (std::is_same_v<step_type, events::background_task>) {
                    detach(step.run(), step.what);
                } else if constexpr (std::is_same_v<step_type, events::stop_bot>) {
                    util::log().info("shutting down on request from a message");
                    // A thread of its own, so the pause holds up none of
                    // DPP's, and a member, so ~bot waits for it. Detached, it
                    // could still be inside shutdown() while the cluster it
                    // was shutting down was being destroyed. If the bot is
                    // going down anyway, it stops waiting at once.
                    const auto delay = step.after;
                    goodbye_ = std::jthread([this, delay](const std::stop_token& stopping) {
                        std::mutex pause;
                        std::condition_variable_any wake;
                        std::unique_lock lock(pause);
                        wake.wait_for(lock, stopping, delay, [] { return false; });
                        if (!stopping.stop_requested()) cluster_.shutdown();
                    });
                }
            },
            wanted);
    }
}

auto bot::check_permissions(const dpp::guild& guild) const -> void {
    const auto self = guild.members.find(cluster_.me.id);
    if (self == guild.members.end()) {
        // Without GUILD_MEMBERS the bot's own member object may be absent.
        // Say so once rather than reporting every permission as missing.
        util::log().debug("no member record for the bot in {}; skipping the permission check", guild.name);
        return;
    }

    std::vector<commands::requirement> required;
    const auto passive = commands::passive_requirements();
    required.assign(passive.begin(), passive.end());
    for (const auto& [bits, purpose] : module_permissions_) {
        required.push_back({.permissions = bits, .purpose = purpose});
    }
    required.push_back({.permissions = commands_.required_bot_permissions(), .purpose = "the registered commands"});

    const std::uint64_t granted = guild.base_permissions(self->second);
    const std::vector<commands::gap> gaps = commands::unmet(required, granted);
    for (const commands::gap& missing : gaps) {
        util::log().warn("{} ({}): missing {} for {}", guild.name, guild.id, commands::describe_permissions(missing.permissions),
                         missing.purpose);
    }

    if (gaps.empty()) util::log().debug("{}: every permission the bot needs is granted", guild.name);
}

auto bot::run() -> void {
    util::log().info("connecting to Discord");
    cluster_.start(dpp::st_wait);

    // start() returns once the cluster has stopped, so this is the last thing
    // the bot says: a log that ends here stopped on purpose, and one that ends
    // anywhere else did not.
    util::log().info("disconnected; LatiBot has stopped");
}

} // namespace latibot
