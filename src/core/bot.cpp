#include "core/bot.hpp"

#include "core/commands/basic.hpp"
#include "core/commands/bots.hpp"
#include "core/commands/chat.hpp"
#include "core/commands/linkstats.hpp"
#include "core/commands/logs.hpp"
#include "core/commands/midnight.hpp"
#include "core/commands/music.hpp"
#include "core/commands/nickname.hpp"
#include "core/commands/preflight.hpp"
#include "core/commands/speak.hpp"
#include "core/commands/trigger.hpp"
#include "core/commands/urlrepl.hpp"
#include "core/commands/voice.hpp"
#include "core/db/backup.hpp"
#include "core/db/schema_versions.hpp"
#include "core/db/schemas.hpp"
#include "core/discord/dpp_log.hpp"
#include "core/discord/message_flags.hpp"
#include "core/discord/voice_state.hpp"
#include "core/events/goodbye.hpp"
#include "core/events/nickname_import.hpp"
#include "core/events/stage_order.hpp"
#include "core/llm/anthropic.hpp"
#include "core/llm/config_check.hpp"
#include "core/llm/memory_tools.hpp"
#include "core/llm/models.hpp"
#include "core/llm/openai.hpp"
#include "core/module/host.hpp"
#include "core/module/module.hpp"
#include "core/ui/interaction.hpp"
#include "core/ui/paginator.hpp"
#include "core/util/log.hpp"
#include "core/util/process.hpp"
#include "core/util/text.hpp"
#include "core/util/url_scan.hpp"
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

/// The intents to connect with.
///
/// i_message_content is privileged and must also be enabled in the Discord
/// developer portal. Without it every guild message arrives with an empty
/// `content`, which silently disables the whole pipeline: the goodbye phrase
/// and the triggers both read it (docs/features/Operations.md §3).
///
/// i_guild_members is privileged in the same way, and is the only way
/// nickname changes and a complete member list arrive at all
/// (docs/features/Operations.md §3). It is asked for only when nickname
/// tracking is on, because a bot that asks for an intent it was not granted
/// is refused the gateway outright.
auto intents_for(const config::bootstrap& settings) -> std::uint32_t {
    std::uint32_t intents = dpp::i_default_intents | dpp::i_message_content;
    if (settings.nicknames.track_changes) intents |= dpp::i_guild_members;
    return intents;
}

/// How many audit entries the delayed fallback asks for.
///
/// Enough to find one change among the moderation that happened around it,
/// small enough to stay one page.
constexpr std::uint32_t audit_fallback_entries = 25;

/// The guild an audit entry belongs to.
///
/// `dpp::audit_entry` does not carry it, and the event's own payload is the
/// only place it appears, so this reaches past DPP into the raw frame.
auto guild_of(const dpp::guild_audit_log_entry_create_t& event) -> dpp::snowflake {
    const auto frame = nlohmann::json::parse(event.raw_event, nullptr, /*allow_exceptions=*/false);
    if (frame.is_discarded() || !frame.contains("d")) return {};

    const auto& payload = frame.at("d");
    const auto found = payload.find("guild_id");
    if (found == payload.end() || !found->is_string()) return {};
    return dpp::snowflake(found->get<std::string>());
}

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

/// The nickname an audit entry says a member ended up with, or nothing when
/// the entry is not about a nickname at all.
auto nickname_change_in(const dpp::audit_entry& entry) -> std::optional<dpp::audit_change> {
    for (const dpp::audit_change& change : entry.changes) {
        if (change.key == "nick") return change;
    }
    return std::nullopt;
}

using ui::answer_privately;
using ui::update_panel;

/// What a button, menu or form no panel claims hears back. It is one of ours,
/// since Discord only sends the bot its own, so it is from a build whose
/// panels were different.
constexpr std::string_view stale_component_reply = "that's from an older version of me; run the command again for a fresh one";

/// The URLs of a message's previews, which is all the embed tracker needs.
auto embed_urls_of(const dpp::message& message) -> std::vector<std::string> {
    std::vector<std::string> urls;
    urls.reserve(message.embeds.size());
    for (const dpp::embed& embed : message.embeds) {
        urls.push_back(embed.url);
    }
    return urls;
}

/// Every channel in a guild that holds ordinary messages, from DPP's cache,
/// for a recompute that was not given one. Threads are left out: listing the
/// archived ones is its own set of calls, and links in them are rare.
auto text_channels(dpp::snowflake guild_id) -> std::vector<dpp::snowflake> {
    std::vector<dpp::snowflake> found;
    const dpp::guild* guild = dpp::find_guild(guild_id);
    if (guild == nullptr) return found;

    for (const dpp::snowflake id : guild->channels) {
        const dpp::channel* channel = dpp::find_channel(id);
        if (channel != nullptr && (channel->is_text_channel() || channel->is_news_channel())) found.push_back(id);
    }
    return found;
}

/// The Java bot's rules, if its file was left beside the database.
constexpr std::string_view legacy_url_rules_file = "UrlReplacements.txt";

/// Set once a guild has had the Java bot's rules, so that removing one later
/// is not undone by the next restart.
constexpr std::string_view url_rules_imported_key = "url_rules_imported";

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
    std::vector<std::string> secrets{credentials.discord_token};
    if (credentials.anthropic_key) secrets.push_back(*credentials.anthropic_key);
    if (credentials.openai_key) secrets.push_back(*credentials.openai_key);
    return secrets;
}

} // namespace

bot::bot(config::bootstrap settings, const config::secrets& credentials, const module::module_factory& make_modules)
    : settings_(std::move(settings)),
      database_(prepare(settings_.database_path)),
      guild_settings_(database_),
      cluster_(credentials.discord_token, intents_for(settings_)),
      gateway_(cluster_),
      http_(cluster_),
      raw_(cluster_),
      bot_allowlist_(database_),
      nicknames_(database_),
      triggers_(database_),
      trigger_panel_(triggers_),
      trigger_responder_(triggers_, clock_),
      midnight_(database_),
      midnight_scheduler_(midnight_, clock_),
      url_rules_(database_),
      url_panel_(url_rules_),
      replacements_(database_),
      media_(replacements_, guild_settings_, clock_),
      reactions_(database_),
      emotes_(replacements_, reactions_),
      backfill_progress_(database_),
      backfill_(gateway_, url_rules_, replacements_, reactions_, backfill_progress_, clock_),
      emoji_copies_(database_),
      emoji_copier_(emoji_copies_, http_, gateway_, clock_, settings_.linkstats.emoji_copy_min_uses),
      embed_tracker_(replacements_, clock_),
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
      llm_usage_(database_),
      llm_documents_(database_),
      llm_memories_(database_),
      llm_aliases_(database_),
      llm_blacklist_(database_),
      llm_triggers_(database_),
      anthropic_(credentials.anthropic_key ? std::make_unique<llm::anthropic_provider>(http_, *credentials.anthropic_key) : nullptr),
      openai_(credentials.openai_key ? std::make_unique<llm::openai_provider>(http_, *credentials.openai_key) : nullptr),
      responder_({.discord = &gateway_,
                  .clock = &clock_,
                  .settings = &guild_settings_,
                  .bootstrap = &settings_,
                  .documents = &llm_documents_,
                  .memories = &llm_memories_,
                  .usage = &llm_usage_,
                  .tools = &llm_tools_,
                  .aliases = &llm_aliases_,
                  .provider_for = [this](llm::provider_kind kind) { return provider_for(kind); },
                  .speech = &dectalk_speech_},
                 [this] { return llm::bot_identity{.id = cluster_.me.id, .name = cluster_.me.username}; }),
      llm_stage_({.settings = &guild_settings_,
                  .bootstrap = &settings_,
                  .blacklist = &llm_blacklist_,
                  .triggers = &llm_triggers_,
                  .usage = &llm_usage_,
                  .speech = &dectalk_speech_,
                  .has_provider = [this](llm::provider_kind kind) { return provider_for(kind) != nullptr; },
                  .me = [this] { return llm::bot_identity{.id = cluster_.me.id, .name = cluster_.me.username}; }},
                 clock_),
      llm_panels_(llm_services()),
      log_destinations_(guild_settings_),
      log_channel_(gateway_, clock_, secrets_of(credentials)) {
    util::log().set_level(settings_.log_level);

    // A model the bot cannot price stops startup, as a bad config.json
    // key does, before anything connects.
    llm::check_config(settings_);

    util::log().info("LatiBot {} starting", version_string());
    util::log().debug("log level {}; {} trusted guild(s), {} trusted user(s)", util::to_string(settings_.log_level),
                      settings_.trusted_guilds.size(), settings_.trusted_users.size());

    // Worth an info line rather than a debug one: it is the difference between
    // a bot that connects and one that Discord turns away, and the reason is
    // a toggle on a web page nobody looks at twice a year.
    if (settings_.nicknames.track_changes) {
        util::log().info("nickname tracking is on; this needs the Server Members intent enabled in the Discord developer portal");
    } else {
        util::log().info("nickname tracking is off; /nickname still works, but changes made elsewhere are not recorded");
    }

    // A warning rather than info: it changes what a recompute records, and
    // it should not be the kind of thing that stays set by accident.
    if (settings_.recompute_bot_id) {
        util::log().warn(
            "LATIBOT_DEBUG_RECOMPUTE_BOT_ID is set: /linkstats recompute reads replacements posted by {}, not this bot's own. "
            "Pass fresh:true to go over channels already recomputed without it.",
            *settings_.recompute_bot_id);
    }

    // After the line above, so that any migration it applies is logged under a
    // heading rather than before the bot has said it is starting.
    // The core's tables and those of the features not yet in modules; each
    // module's follow when the modules are built (start_modules).
    db::prepare_schema_versions(database_);
    for (const db::module_schema& schema : db::builtin_schemas()) {
        db::apply_schema(database_, schema);
    }

    // The model's memory, as tools it can call
    // (docs/features/Language_Model.md §3.4).
    llm::add_memory_tools(llm_tools_, llm_memories_);

    // Info: a missing key is the whole reason the model would never answer,
    // and the log is where that question gets asked.
    if (anthropic_ == nullptr && openai_ == nullptr) {
        util::log().info("the language model is off everywhere: neither ANTHROPIC_API_KEY nor OPENAI_API_KEY is set");
    } else {
        util::log().info("the language model can use {}{}{}; {} by default, capped at ${:.2f} a day and ${:.2f} a month",
                         anthropic_ != nullptr ? "Anthropic" : "", anthropic_ != nullptr && openai_ != nullptr ? " and " : "",
                         openai_ != nullptr ? "OpenAI" : "", settings_.llm.model, settings_.llm.spend_cap_daily_usd,
                         settings_.llm.spend_cap_monthly_usd);
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

    // Years of history from the Java bot, if its file was left beside the
    // database. Importing is idempotent, so this needs no marker file and no
    // "have I done this already" flag (docs/features/Nicknames.md §4).
    const std::filesystem::path legacy = settings_.database_path.parent_path() / "nicknames.json";
    if (const auto imported = events::import_nicknames_file(nicknames_, legacy); imported.value_or(0) > 0) {
        util::log().info("imported {} nickname entries from {}", *imported, legacy.generic_string());
    }

    // Read before the connection starts, so everything found was cut off by
    // the last run rather than being watched by this one. Each guild's are
    // settled once it connects.
    const std::vector<events::replacement_record> unsettled = replacements_.unsettled();
    for (const events::replacement_record& record : unsettled) {
        stranded_[record.guild_id].push_back(record);
    }
    if (!unsettled.empty()) {
        util::log().info("the last run left {} replacement(s) waiting on a preview; settling them as their servers connect",
                         unsettled.size());
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
    commands_.add(std::make_unique<commands::trigger_command>(triggers_));
    commands_.add(std::make_unique<commands::bots_command>(bot_allowlist_));
    commands_.add(std::make_unique<commands::nickname_command>(nicknames_, pending_nicknames_, clock_, cluster_));
    commands_.add(std::make_unique<commands::nicknames_command>(nicknames_));
    commands_.add(std::make_unique<commands::midnight_command>(midnight_, clock_));
    commands_.add(std::make_unique<commands::links_command>(url_rules_));
    commands_.add(std::make_unique<commands::urltoggle_command>(url_rules_));
    commands_.add(std::make_unique<commands::logs_command>(settings_, log_destinations_, log_channel_, gateway_));
    commands_.add(std::make_unique<commands::llm_command>(llm_services()));
    commands_.add(std::make_unique<commands::memory_command>(llm_services()));

    const commands::speech_services speech{
        .engine = &tts_, .queue = &speech_, .settings = &guild_settings_, .bootstrap = &settings_, .voices = &voices_};
    commands_.add(std::make_unique<commands::speak_command>(speech));
    commands_.add(std::make_unique<commands::tts_command>(speech, voice_lab_));
    commands_.add(std::make_unique<commands::chat_command>(speech, raw_));
    commands_.add(std::make_unique<commands::voice_command>(voice_sessions_, guild_settings_));
    commands_.add(std::make_unique<commands::music_command>(commands::music_services{
        .player = &music_, .resolver = &music_resolver_, .settings = &guild_settings_, .unavailable = music_unavailable()}));
    commands_.add(std::make_unique<commands::linkstats_command>(
        reactions_,
        commands::recompute_support{.service = &backfill_,
                                    .discord = &gateway_,
                                    .channels_of = [](dpp::snowflake guild) { return text_channels(guild); },
                                    .bot_id = [this] { return settings_.recompute_bot_id.value_or(cluster_.me.id); }},
        &guild_settings_));
}

auto bot::register_stages() -> void {
    // The order of docs/features/Message_Pipeline.md §2.2, as a list so that
    // changing it is one line. The model is last: it consumes what it
    // answers, and a simple trigger's reply before it keeps an advanced
    // trigger quiet.
    pipeline_.add(events::stage_order::stop, "goodbye", events::goodbye_stage(guild_settings_));
    pipeline_.add(events::stage_order::rewrite, "url replacement",
                  events::carried_out_by<events::replace_links>(
                      events::url_replacer(url_rules_), "posting a replacement", [this](events::replace_links request) {
                          return events::post_replacement(gateway_, replacements_, embed_tracker_, clock_, std::move(request));
                      }));
    pipeline_.add(events::stage_order::reply, "triggers",
                  [this](const events::incoming_message& message) { return trigger_responder_(message); });
    pipeline_.add(events::stage_order::model, "language model",
                  events::carried_out_by<llm::ask_llm>([this](const events::incoming_message& message) { return llm_stage_(message); },
                                                       "answering with the language model",
                                                       [this](llm::ask_llm ask) { return answer_with_llm(std::move(ask)); }));
}

auto bot::provider_for(llm::provider_kind kind) const -> llm::provider* {
    return kind == llm::provider_kind::openai ? openai_.get() : anthropic_.get();
}

auto bot::llm_services() -> commands::llm_command_services {
    return {.settings = &guild_settings_,
            .bootstrap = &settings_,
            .documents = &llm_documents_,
            .triggers = &llm_triggers_,
            .blacklist = &llm_blacklist_,
            .memories = &llm_memories_,
            .aliases = &llm_aliases_,
            .usage = &llm_usage_,
            .http = &http_,
            .clock = &clock_,
            .has_provider = [this](llm::provider_kind kind) { return provider_for(kind) != nullptr; }};
}

auto bot::answer_with_llm(llm::ask_llm ask) -> dpp::task<void> {
    // Bot-to-bot pacing (docs/features/Language_Model.md §2.7). The turn was
    // claimed when the stage decided, so the wait only spaces it out.
    if (ask.wait > std::chrono::seconds::zero()) co_await cluster_.co_sleep(static_cast<std::uint64_t>(ask.wait.count()));
    co_await responder_.answer(std::move(ask));
}

auto bot::register_events() -> void {
    // DPP's own logging goes through our logger, so there is one format and
    // one level to configure. DPP hands over finished text, so there are no
    // types left to colour; the [dpp] tag is coloured instead, which is what
    // tells its lines apart from ours at a glance.
    cluster_.on_log([this](const dpp::log_t& event) {
        util::log().log(discord::log_level_of(event.severity), "{} {}", util::log_source{"dpp"}, event.message);

        // 4014 is the gateway refusing a privileged intent, and DPP reports
        // it as a websocket number in a reconnect loop. The cause is always
        // the same toggle, so say which one rather than leaving somebody to
        // look the code up (docs/features/Operations.md §3).
        if (settings_.nicknames.track_changes && event.message.contains("4014")) {
            util::log().error(
                "Discord refused the Server Members intent. Enable it under Bot > Privileged Gateway Intents "
                "in the Discord developer portal, or set \"track_changes\": false in the \"nicknames\" section of config.json.");
        }
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
        reconcile_nicknames(guild);
        import_url_rules(guild);
        settle_stranded_replacements(guild.id);

        util::log().debug("{}: {} trigger(s), {} allowed bot(s), goodbye phrase \"{}\", URL replacement {} with {} rule(s)", guild.name,
                          triggers_.for_guild(guild.id).size(), bot_allowlist_.for_guild(guild.id).size(),
                          guild_settings_.get(guild.id, events::goodbye_phrase_key, events::default_goodbye_phrase),
                          url_rules_.enabled(guild.id) ? "on" : "off", url_rules_.for_guild(guild.id).size());
    });

    // Every message: reduce it to plain data, let the stages decide, then do
    // what they asked. Handlers run on DPP's thread pool, so two messages
    // can be in here at once.
    cluster_.on_message_create([this](const dpp::message_create_t& event) {
        carry_out(pipeline_.run(describe(event.msg, event.raw_event)));
        // Somebody's image or video, in a server that counts reactions on
        // them (docs/features/Link_Stats.md §9).
        const dpp::message& message = event.msg;
        media_.on_message({.message_id = message.id,
                           .guild_id = message.guild_id,
                           .channel_id = message.channel_id,
                           .author_id = message.author.id,
                           .from_person = !message.author.is_bot() && message.webhook_id.empty(),
                           .has_media = events::has_media(message),
                           .has_links = !util::find_links(message.content).empty()});
        // Emotes sent as a message of their own after a post, which count
        // as reactions to it (docs/features/Link_Stats.md §12).
        if (!message.guild_id.empty()) emotes_.on_message(message.channel_id, events::as_emote_message(events::describe_history(message)));
    });

    // Discord adds link previews by updating the message a moment after it
    // was posted, which is how the embed tracker learns that a mirror worked
    // (docs/features/Url_Replacement.md §3.3). Every update goes to it: the
    // one for our message often arrives without an author, so there is
    // nothing to filter on here.
    cluster_.on_message_update([this](const dpp::message_update_t& event) {
        const std::vector<std::string> urls = embed_urls_of(event.msg);
        carry_out(embed_tracker_.on_embeds(event.msg.id, urls));
        // The preview that shows a link was an image arrives here too, and
        // emotes already sent after it are counted then.
        if (media_.on_update(event.msg.id, events::has_media(event.msg))) emotes_.on_post(event.msg.channel_id, event.msg.id);
    });
    cluster_.on_message_delete([this](const dpp::message_delete_t& event) {
        embed_tracker_.forget(event.id);
        emotes_.on_delete(event.channel_id, event.id);
    });

    // Reaction statistics (docs/features/Link_Stats.md §3). Every reaction in
    // every channel arrives here; the store counts the ones on our
    // replacements and ignores the rest in the same statement that would have
    // recorded them.
    cluster_.on_message_reaction_add([this](const dpp::message_reaction_add_t& event) {
        const dpp::emoji& emoji = event.reacting_emoji;
        const auto reacted = events::reaction_emoji(emoji.id, emoji.name, emoji.is_animated());
        if (reactions_.add(event.message_id, event.reacting_user.id, reacted, now_seconds())) {
            util::log().debug("{} reacted {} to replacement {}", event.reacting_user.id, reacted.key, event.message_id);
        }
    });
    cluster_.on_message_reaction_remove([this](const dpp::message_reaction_remove_t& event) {
        const auto reacted = events::reaction_emoji(event.reacting_emoji.id, event.reacting_emoji.name);
        if (reactions_.remove(event.message_id, event.reacting_user_id, reacted.key, now_seconds())) {
            util::log().debug("{} took back {} on replacement {}", event.reacting_user_id, reacted.key, event.message_id);
        }
    });
    cluster_.on_message_reaction_remove_emoji([this](const dpp::message_reaction_remove_emoji_t& event) {
        const auto reacted = events::reaction_emoji(event.reacting_emoji.id, event.reacting_emoji.name);
        if (const int gone = reactions_.remove_emoji(event.message_id, reacted.key, now_seconds()); gone > 0) {
            util::log().debug("{} cleared from replacement {}: {} reaction(s)", reacted.key, event.message_id, gone);
        }
    });
    cluster_.on_message_reaction_remove_all([this](const dpp::message_reaction_remove_all_t& event) {
        if (const int gone = reactions_.remove_all(event.message_id, now_seconds()); gone > 0) {
            util::log().debug("every reaction cleared from replacement {}: {}", event.message_id, gone);
        }
    });

    // Only when tracking is on, because DPP warns about a handler attached
    // without the intent that feeds it — which would be true and useless
    // noise for somebody who turned the feature off deliberately.
    //
    // Recording and attributing are separate events on purpose: the change is
    // written down the moment it is seen, and the audit log fills in who did
    // it if and when it arrives (docs/features/Nicknames.md §3).
    if (settings_.nicknames.track_changes) {
        cluster_.on_guild_member_update([this](const dpp::guild_member_update_t& event) { on_member_update(event.updated); });
        cluster_.on_guild_audit_log_entry_create(
            [this](const dpp::guild_audit_log_entry_create_t& event) { on_audit_entry(event.entry, guild_of(event)); });
    }

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
        module::report_failure(what, &error);
    } catch (...) {
        module::report_failure(what, nullptr);
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
    // Polling the wall clock is the fix for the Java bot's random-fire bug:
    // it computed a delay from the wall clock and then waited on a monotonic
    // timer, so a machine that slept woke up and posted at whatever time it
    // happened to be (docs/features/Midnight.md §1).
    every(events::midnight_tick, "the midnight tick", [this] { carry_out(midnight_scheduler_.tick()); });

    util::log().debug("midnight messages checked every {}", events::midnight_tick);

    // One timer for every replacement being watched, rather than one each:
    // the tracker knows whose time is up, and a second is as fine as DPP's
    // timers go. Most ticks find nothing and cost a lock.
    every(std::chrono::seconds{1}, "the preview tracker's tick", [this] { carry_out(embed_tracker_.tick()); });

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

    // The bot's own copies of the emojis it has seen, a few at a time. The
    // copies belong to the bot's application, which it only knows once
    // connected.
    if (emoji_copier_.enabled()) {
        every(events::copy_round_interval, "copying emojis", [this] {
            if (!cluster_.me.id.empty()) detach(copy_emojis(), "copying emojis");
        });
        util::log().info("keeping copies of emojis used at least {} time{}", settings_.linkstats.emoji_copy_min_uses,
                         settings_.linkstats.emoji_copy_min_uses == 1 ? "" : "s");
    } else {
        util::log().info("copying emojis is off");
    }

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

auto bot::record_nickname(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname,
                          events::nickname_source source) -> std::optional<std::int64_t> {
    const auto latest = nicknames_.latest(guild_id, user_id);
    if (!events::is_new_nickname(latest, nickname)) return std::nullopt;

    const std::int64_t row = nicknames_.record({.guild_id = guild_id,
                                                .user_id = user_id,
                                                .nickname = nickname,
                                                .changed_at = clock_.now(),
                                                // Nobody yet: the audit log fills this in if it can.
                                                .changed_by = std::nullopt,
                                                .source = source,
                                                .imported_raw = {}});

    util::log().info("{} in guild {} is now called {} (recorded as {})", user_id, guild_id,
                     nickname ? std::format("\"{}\"", *nickname) : "nothing", events::to_string(source));
    return row;
}

auto bot::on_member_update(const dpp::guild_member& member) -> void {
    // DPP's cached member is already the new one by the time this runs, so
    // "what were they called before" can only come from our own history.
    const std::string current = member.get_nickname();
    const std::optional<std::string> nickname = current.empty() ? std::nullopt : std::optional(current);

    // A change the bot just made is already in the history with the invoker
    // against it, and recording it again would lose that
    // (docs/features/Nicknames.md §3).
    if (pending_nicknames_.claim(member.guild_id, member.user_id, nickname, clock_.now())) {
        util::log().debug("member update for {} in guild {} is the change /nickname just made", member.user_id, member.guild_id);
        return;
    }

    const auto row = record_nickname(member.guild_id, member.user_id, nickname, events::nickname_source::seen);
    if (!row) {
        // Member updates fire for roles, timeouts and avatars too, so most of
        // them are not about a nickname at all.
        util::log().trace("member update for {} in guild {} changed no nickname", member.user_id, member.guild_id);
        return;
    }

    // Recording never waits on attribution, so this is the only thing that
    // notices the audit entry never turning up
    // (docs/features/Nicknames.md §3).
    attribute_later(member.guild_id, member.user_id, *row);
}

auto bot::attribute_later(dpp::snowflake guild_id, dpp::snowflake user_id, std::int64_t row) -> void {
    after(events::audit_fallback_delay, "the audit log fallback", [this, guild_id, user_id, row] {
        const auto waiting = nicknames_.find(row);
        if (!waiting || waiting->changed_by) {
            // The gateway entry arrived, which is the ordinary path.
            return;
        }

        util::log().debug("no audit entry arrived for nickname row {}; asking Discord", row);
        cluster_.guild_auditlog_get(guild_id, 0, dpp::aut_member_update, 0, 0, audit_fallback_entries,
                                    [this, guild_id, user_id](const dpp::confirmation_callback_t& reply) {
                                        if (reply.is_error()) {
                                            // Almost always a missing View Audit Log, which the
                                            // permission preflight already warns about per guild.
                                            util::log().debug("could not read the audit log for guild {}: {}", guild_id,
                                                              reply.get_error().message);
                                            return;
                                        }

                                        const auto* entries = std::get_if<dpp::auditlog>(&reply.value);
                                        if (entries == nullptr) return;

                                        // Every recent entry about this member goes through the
                                        // same path as a live one, which decides which row, if
                                        // any, it attributes.
                                        for (const dpp::audit_entry& entry : entries->entries) {
                                            if (entry.target_id == user_id) on_audit_entry(entry, guild_id);
                                        }
                                    });
    });
}

auto bot::on_audit_entry(const dpp::audit_entry& entry, dpp::snowflake guild_id) -> void {
    if (entry.type != dpp::aut_member_update || guild_id.empty()) return;

    const auto change = nickname_change_in(entry);
    if (!change) return;

    const std::optional<std::string> nickname = events::audit_nickname(change->new_value);
    const auto row = nicknames_.unattributed(guild_id, entry.target_id, nickname, clock_.now(), events::pending_nickname_ttl);
    if (!row) {
        util::log().debug("audit entry {} names no change we are still waiting to attribute", entry.id);
        return;
    }

    if (!events::may_attribute(*row, entry.user_id, cluster_.me.id)) {
        // Discord names the bot whenever the bot called the API, which would
        // overwrite the one attribution that was never in doubt.
        util::log().debug("audit entry {} attributes a change to the bot itself; leaving row {} alone", entry.id, row->id);
        return;
    }

    if (nicknames_.attribute(row->id, entry.user_id, events::nickname_source::audit_log)) {
        util::log().info("{}'s nickname change in guild {} was made by {}", row->user_id, guild_id, entry.user_id);
    }
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

auto bot::reconcile_nicknames(const dpp::guild& guild) -> void {
    if (!settings_.nicknames.track_changes) return;

    // Changes made while the bot was not running have nobody to attribute
    // them to, which is why they are marked as their own source rather than
    // guessed at (docs/features/Nicknames.md §3).
    int recorded = 0;
    for (const auto& [user_id, member] : guild.members) {
        const std::string current = member.get_nickname();
        if (record_nickname(guild.id, user_id, current.empty() ? std::nullopt : std::optional(current), events::nickname_source::startup)) {
            ++recorded;
        }
    }

    util::log().debug("{}: checked {} member(s) for nickname changes made while offline, recorded {}", guild.name, guild.members.size(),
                      recorded);
}

auto bot::import_url_rules(const dpp::guild& guild) -> void {
    if (guild_settings_.get_bool(guild.id, url_rules_imported_key, false)) return;

    // Marked only once a file was actually read, so dropping the file in
    // after a first run still works.
    const std::filesystem::path legacy = settings_.database_path.parent_path() / legacy_url_rules_file;
    const auto imported = events::import_url_rules_file(url_rules_, guild.id, legacy);
    if (!imported) return;

    guild_settings_.set_bool(guild.id, url_rules_imported_key, true);
    util::log().info("{}: imported {} URL rule(s) from {}{}", guild.name, *imported, legacy.generic_string(),
                     url_rules_.enabled(guild.id) ? "" : "; they apply once someone runs /links enable there");
}

auto bot::retry_replacement(const dpp::interaction_create_t& event, dpp::snowflake message_id, const commands::user_label& who) -> void {
    auto plan = events::plan_retry(replacements_, url_rules_, message_id, event.command.guild_id);
    if (const auto* reason = std::get_if<std::string>(&plan)) {
        dpp::message note(*reason);
        note.set_flags(dpp::m_ephemeral);
        event.reply(note);
        return;
    }

    auto& retry = std::get<events::retry_plan>(plan);
    replacements_.set_state(message_id, events::replacement_state::retrying);
    util::log().info("{} pressed Retry on replacement {} in guild {}", who, message_id, event.command.guild_id);

    // Answering the button with the edit is the first attempt, so it cannot
    // be overtaken by another press. Anyone may press it
    // (docs/features/Url_Replacement.md §2.4).
    event.reply(dpp::ir_update_message, events::build_edit(retry.first));
    carry_out(embed_tracker_.watch(std::move(retry.request)));
}

auto bot::settle_stranded_replacements(dpp::snowflake guild_id) -> void {
    std::vector<events::replacement_record> mine;
    {
        const std::scoped_lock guard(stranded_mutex_);
        const auto found = stranded_.find(guild_id);
        if (found == stranded_.end()) return;
        mine = std::move(found->second);
        stranded_.erase(found);
    }

    detach(events::settle_stranded(gateway_, replacements_, url_rules_, embed_tracker_, std::move(mine)),
           "settling replacements the last run left unfinished");
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

auto bot::now_seconds() const -> std::chrono::sys_seconds {
    return std::chrono::floor<std::chrono::seconds>(clock_.now());
}

auto bot::copy_emojis() -> dpp::task<void> {
    co_await emoji_copier_.run_round();
}

auto bot::carry_out(std::vector<events::embed_action> actions) -> void {
    if (!actions.empty()) detach(events::carry_out_embed_actions(gateway_, std::move(actions)), "updating a replacement");
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
        if (!route_component(event, *state, chosen, who)) {
            util::log().debug("no panel handles the view {} with argument '{}'", state->view, state->argument);
            answer_privately(event, stale_component_reply);
        }
    } catch (const std::exception& error) {
        util::log().error("panel {} failed for {} in guild {}: {}", state->view, who, guild, error.what());
        answer_privately(event, commands::command_failed_reply);
    }
}

auto bot::route_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen,
                          const commands::user_label& who) -> bool {
    const dpp::snowflake guild = event.command.guild_id;

    // Every one of these edits the message the component is on rather than
    // posting a new one, which is why the state rides in the custom_id: there
    // is nothing here to expire, leak, or lose across a restart.
    if (state.view == commands::nickname_history_view) {
        const dpp::snowflake subject(state.argument);
        update_panel(event, commands::render_nickname_history(nicknames_.history(guild, subject), subject, state.page));
    } else if (state.view == events::url_retry_view) {
        retry_replacement(event, dpp::snowflake(state.argument), who);
    } else if (panels_.claimed(state.view)) {
        // A module's panel (docs/modules/Module_Plan_Final.md §4.6).
        return panels_.on_component(event, state, chosen);
    } else {
        // Each panel's router says whether the view was one of its own.
        return trigger_panel_.on_component(event, state, chosen) || url_panel_.on_component(event, state, chosen) ||
               voice_lab_.on_component(event, state, chosen) || llm_panels_.on_component(event, state, chosen) ||
               commands::on_linkstats_component(reactions_, event, state, chosen) || commands::on_music_component(music_, event, state);
    }
    return true;
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
        if (state && (panels_.on_form(event, *state) || trigger_panel_.on_form(event, *state) || url_panel_.on_form(event, *state) ||
                      voice_lab_.on_form(event, *state) || llm_panels_.on_form(event, *state))) {
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

auto bot::start_modules(const module::module_factory& make_modules) -> void {
    modules_ = module::start_modules(make_modules, *this, capabilities_);
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
