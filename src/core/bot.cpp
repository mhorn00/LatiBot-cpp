#include "core/bot.hpp"

#include "core/commands/basic.hpp"
#include "core/commands/bots.hpp"
#include "core/commands/linkstats.hpp"
#include "core/commands/midnight.hpp"
#include "core/commands/nickname.hpp"
#include "core/commands/preflight.hpp"
#include "core/commands/trigger.hpp"
#include "core/commands/urlrepl.hpp"
#include "core/db/backup.hpp"
#include "core/db/migrations.hpp"
#include "core/events/goodbye.hpp"
#include "core/events/nickname_import.hpp"
#include "core/ui/paginator.hpp"
#include "core/util/log.hpp"
#include "core/version.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace latibot {
namespace {

util::log_level from_dpp(dpp::loglevel level) {
    switch (level) {
    case dpp::ll_trace:
        return util::log_level::trace;
    case dpp::ll_debug:
        return util::log_level::debug;
    case dpp::ll_info:
        return util::log_level::info;
    case dpp::ll_warning:
        return util::log_level::warn;
    default:
        // Errors and criticals both matter enough to surface the same way.
        return util::log_level::error;
    }
}

/// The intents to connect with.
///
/// i_message_content is privileged and must also be enabled in the Discord
/// developer portal. Without it every guild message arrives with an empty
/// `content`, which silently disables the whole pipeline: the goodbye phrase
/// and the triggers both read it (plan v4 §5.4).
///
/// i_guild_members is privileged in the same way, and is the only way nickname
/// changes and a complete member list arrive at all (plan v4 §8). It is asked
/// for only when nickname tracking is on, because a bot that asks for an
/// intent it was not granted is refused the gateway outright.
std::uint32_t intents_for(const config::bootstrap& settings) {
    std::uint32_t intents = dpp::i_default_intents | dpp::i_message_content;
    if (settings.track_nicknames) {
        intents |= dpp::i_guild_members;
    }
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
dpp::snowflake guild_of(const dpp::guild_audit_log_entry_create_t& event) {
    const auto frame = nlohmann::json::parse(event.raw_event, nullptr, /*allow_exceptions=*/false);
    if (frame.is_discarded() || !frame.contains("d")) {
        return {};
    }

    const auto& payload = frame.at("d");
    const auto found = payload.find("guild_id");
    if (found == payload.end() || !found->is_string()) {
        return {};
    }
    return dpp::snowflake(found->get<std::string>());
}

/// The nickname an audit entry says a member ended up with, or nothing when
/// the entry is not about a nickname at all.
std::optional<dpp::audit_change> nickname_change_in(const dpp::audit_entry& entry) {
    for (const dpp::audit_change& change : entry.changes) {
        if (change.key == "nick") {
            return change;
        }
    }
    return std::nullopt;
}

/// Runs a coroutine to the end with nobody waiting on it.
///
/// `dpp::job` is DPP's fire-and-forget coroutine. The catch is the point of
/// this function: an exception leaving a job is rethrown on whichever DPP
/// thread resumed it, which would end the process.
dpp::job detach(dpp::task<void> work, std::string what) {
    try {
        co_await std::move(work);
    } catch (const std::exception& error) {
        util::log().error("{} threw: {}", what, error.what());
    } catch (...) {
        util::log().error("{} threw an unknown exception", what);
    }
}

/// The URLs of a message's previews, which is all the embed tracker needs.
std::vector<std::string> embed_urls_of(const dpp::message& message) {
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
std::vector<dpp::snowflake> text_channels(dpp::snowflake guild_id) {
    std::vector<dpp::snowflake> found;
    const dpp::guild* guild = dpp::find_guild(guild_id);
    if (guild == nullptr) {
        return found;
    }

    for (const dpp::snowflake id : guild->channels) {
        const dpp::channel* channel = dpp::find_channel(id);
        if (channel != nullptr && (channel->is_text_channel() || channel->is_news_channel())) {
            found.push_back(id);
        }
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
std::filesystem::path prepare(const std::filesystem::path& database_path) {
    if (database_path.has_parent_path() && !database_path.parent_path().empty()) {
        std::filesystem::create_directories(database_path.parent_path());
    }
    return database_path;
}

} // namespace

bot::bot(config::bootstrap settings, const config::secrets& credentials)
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
      trigger_responder_(triggers_, clock_),
      midnight_(database_),
      midnight_scheduler_(midnight_, clock_),
      url_rules_(database_),
      replacements_(database_),
      reactions_(database_),
      backfill_progress_(database_),
      backfill_(gateway_, url_rules_, replacements_, reactions_, backfill_progress_, clock_),
      embed_tracker_(replacements_, clock_) {
    util::log().set_level(settings_.log_level);

    util::log().info("LatiBot {} starting", version_string());
    util::log().debug("log level {}; {} trusted guild(s), {} trusted user(s)", util::to_string(settings_.log_level),
                      settings_.trusted_guilds.size(), settings_.trusted_users.size());

    // Worth an info line rather than a debug one: it is the difference between
    // a bot that connects and one that Discord turns away, and the reason is
    // a toggle on a web page nobody looks at twice a year.
    if (settings_.track_nicknames) {
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
    const int version = db::migrate(database_);
    util::log().info("database {} at schema version {}", settings_.database_path.generic_string(), version);

    // Years of history from the Java bot, if its file was left beside the
    // database. Importing is idempotent, so this needs no marker file and no
    // "have I done this already" flag (plan v4 §8.3).
    const std::filesystem::path legacy = settings_.database_path.parent_path() / "nicknames.json";
    if (const auto imported = events::import_nicknames_file(nicknames_, legacy); imported.value_or(0) > 0) {
        util::log().info("imported {} nickname entries from {}", *imported, legacy.generic_string());
    }

    register_commands();
    register_stages();
    register_events();
    register_timers();

    std::string stages;
    for (const std::string_view name : pipeline_.stage_names()) {
        if (!stages.empty()) {
            stages += " -> ";
        }
        stages += name;
    }
    util::log().debug("{} commands registered; message stages: {}", commands_.size(), stages);
}

void bot::register_commands() {
    commands::add_basic_commands(commands_, cluster_, clock_, guild_settings_, [this] { cluster_.shutdown(); });
    commands_.add(std::make_unique<commands::trigger_command>(triggers_));
    commands_.add(std::make_unique<commands::bots_command>(bot_allowlist_));
    commands_.add(std::make_unique<commands::nickname_command>(nicknames_, pending_nicknames_, clock_, cluster_));
    commands_.add(std::make_unique<commands::nicknames_command>(nicknames_));
    commands_.add(std::make_unique<commands::midnight_command>(midnight_, clock_));
    commands_.add(std::make_unique<commands::urlrepl_command>(url_rules_));
    commands_.add(std::make_unique<commands::urltoggle_command>(url_rules_));
    commands_.add(std::make_unique<commands::linkstats_command>(
        reactions_, commands::recompute_support{.service = &backfill_,
                                                .discord = &gateway_,
                                                .channels_of = [](dpp::snowflake guild) { return text_channels(guild); },
                                                .bot_id = [this] { return settings_.recompute_bot_id.value_or(cluster_.me.id); }}));
}

void bot::register_stages() {
    // The order is plan v4 §5.4, and it is a list so that changing it is one
    // line. The LLM stages join it in phase 5.
    pipeline_.add("goodbye", events::goodbye_stage(guild_settings_));
    pipeline_.add("url replacement", events::url_replacer(url_rules_));
    pipeline_.add("triggers", [this](const events::incoming_message& message) { return trigger_responder_(message); });
}

void bot::register_events() {
    // DPP's own logging goes through our logger, so there is one format and
    // one level to configure. DPP hands over finished text, so there are no
    // types left to colour; the [dpp] tag is coloured instead, which is what
    // tells its lines apart from ours at a glance.
    cluster_.on_log([this](const dpp::log_t& event) {
        util::log().log(from_dpp(event.severity), "{} {}", util::log_source{"dpp"}, event.message);

        // 4014 is the gateway refusing a privileged intent, and DPP reports it
        // as a websocket number in a reconnect loop. The cause is always the
        // same toggle, so say which one rather than leaving somebody to look
        // the code up (plan v4 §21.2).
        if (settings_.track_nicknames && event.message.find("4014") != std::string::npos) {
            util::log().error(
                "Discord refused the Server Members intent. Enable it under Bot > Privileged Gateway Intents "
                "in the Discord developer portal, or set \"track_nicknames\": false in config.json.");
        }
    });

    cluster_.on_slashcommand([this](const dpp::slashcommand_t& event) -> dpp::task<void> {
        co_await commands_.dispatch(event.command.get_command_name(), event);
    });

    cluster_.on_ready([this](const dpp::ready_t& event) {
        util::log().info("connected to Discord as {} ({})", cluster_.me.username, cluster_.me.id);

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
    });

    // Guilds arrive as guild_create after the gateway connects, including the
    // ones the bot was already in, so this covers both cases plan v4 §7 asks
    // for without a separate sweep on ready.
    cluster_.on_guild_create([this](const dpp::guild_create_t& event) {
        const dpp::guild& guild = event.created;
        util::log().info("in guild {} ({})", guild.name, guild.id);

        check_permissions(guild);
        reconcile_nicknames(guild);
        import_url_rules(guild);

        const int seeded = triggers_.seed_defaults(guild.id);
        if (seeded > 0) {
            util::log().info("{}: seeded {} default triggers", guild.name, seeded);
        }

        util::log().debug("{}: {} trigger(s), {} allowed bot(s), goodbye phrase \"{}\", URL replacement {} with {} rule(s)", guild.name,
                          triggers_.for_guild(guild.id).size(), bot_allowlist_.for_guild(guild.id).size(),
                          guild_settings_.get(guild.id, events::goodbye_phrase_key, events::default_goodbye_phrase),
                          url_rules_.enabled(guild.id) ? "on" : "off", url_rules_.for_guild(guild.id).size());
    });

    cluster_.on_message_create([this](const dpp::message_create_t& event) { carry_out(pipeline_.run(describe(event.msg))); });

    // Discord adds link previews by updating the message a moment after it
    // was posted, which is how the embed tracker learns that a mirror worked
    // (plan v4 §9.3). Every update goes to it: the one for our message often
    // arrives without an author, so there is nothing to filter on here.
    cluster_.on_message_update([this](const dpp::message_update_t& event) {
        const std::vector<std::string> urls = embed_urls_of(event.msg);
        carry_out(embed_tracker_.on_embeds(event.msg.id, urls));
    });
    cluster_.on_message_delete([this](const dpp::message_delete_t& event) { embed_tracker_.forget(event.id); });

    // Reaction statistics (plan v4 §9.6). Every reaction in every channel
    // arrives here; the store counts the ones on our replacements and
    // ignores the rest in the same statement that would have recorded them.
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
    // it if and when it arrives (plan v4 §8.1).
    if (settings_.track_nicknames) {
        cluster_.on_guild_member_update([this](const dpp::guild_member_update_t& event) { on_member_update(event.updated); });
        cluster_.on_guild_audit_log_entry_create(
            [this](const dpp::guild_audit_log_entry_create_t& event) { on_audit_entry(event.entry, guild_of(event)); });
    }

    cluster_.on_autocomplete([this](const dpp::autocomplete_t& event) { commands_.offer_completions(event.name, event); });

    cluster_.on_button_click([this](const dpp::button_click_t& event) { on_component(event, event.custom_id, {}); });
    cluster_.on_select_click([this](const dpp::select_click_t& event) {
        on_component(event, event.custom_id, event.values.empty() ? std::string{} : event.values.front());
    });
    cluster_.on_form_submit([this](const dpp::form_submit_t& event) { on_form(event); });
}

void bot::register_timers() {
    // Polling the wall clock is the fix for the Java bot's random-fire bug: it
    // computed a delay from the wall clock and then waited on a monotonic
    // timer, so a machine that slept woke up and posted at whatever time it
    // happened to be (plan v4 §10).
    cluster_.start_timer([this](dpp::timer) { carry_out(midnight_scheduler_.tick()); },
                         static_cast<std::uint64_t>(events::midnight_tick.count()));

    util::log().debug("midnight messages checked every {}", events::midnight_tick);

    // One timer for every replacement being watched, rather than one each:
    // the tracker knows whose time is up, and a second is as fine as DPP's
    // timers go. Most ticks find nothing and cost a lock.
    cluster_.start_timer([this](dpp::timer) { carry_out(embed_tracker_.tick()); }, 1);

    if (settings_.backup_interval <= std::chrono::minutes::zero() || settings_.backups_to_keep <= 0) {
        util::log().info("database backups are off");
        return;
    }

    const auto every = std::chrono::duration_cast<std::chrono::seconds>(settings_.backup_interval);
    cluster_.start_timer(
        [this](dpp::timer) {
            try {
                const auto written =
                    db::create_backup(database_, settings_.backup_directory, "bot", settings_.backups_to_keep, clock_.now());
                util::log().info("wrote {}", written.generic_string());
            } catch (const std::exception& error) {
                // A backup that fails is worth knowing about and is never
                // worth taking the bot down for.
                util::log().error("could not write a backup to {}: {}", settings_.backup_directory.generic_string(), error.what());
            }
        },
        static_cast<std::uint64_t>(every.count()));

    util::log().info("backing up to {} every {}, keeping {}", settings_.backup_directory.generic_string(), settings_.backup_interval,
                     settings_.backups_to_keep);
}

namespace {

/// The id a panel button carries, or 0 when it carries none.
std::int64_t argument_id(const ui::page_state& state) {
    std::int64_t id = 0;
    const char* begin = state.argument.data();
    const char* end = begin + state.argument.size();
    const auto [stop, error] = std::from_chars(begin, end, id);
    return error == std::errc{} && stop == end ? id : 0;
}

/// A modal's field, by the id it was built with.
std::string field_of(const dpp::form_submit_t& event, std::string_view name) {
    for (const dpp::component& row : event.components) {
        for (const dpp::component& input : row.components) {
            if (input.custom_id == name) {
                if (const auto* text = std::get_if<std::string>(&input.value)) {
                    return *text;
                }
            }
        }
    }
    return {};
}

} // namespace

std::optional<std::int64_t> bot::record_nickname(dpp::snowflake guild_id, dpp::snowflake user_id,
                                                 const std::optional<std::string>& nickname, events::nickname_source source) {
    const auto latest = nicknames_.latest(guild_id, user_id);
    if (!events::is_new_nickname(latest, nickname)) {
        return std::nullopt;
    }

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

void bot::on_member_update(const dpp::guild_member& member) {
    // DPP's cached member is already the new one by the time this runs, so
    // "what were they called before" can only come from our own history.
    const std::string current = member.get_nickname();
    const std::optional<std::string> nickname = current.empty() ? std::nullopt : std::optional(current);

    // A change the bot just made is already in the history with the invoker
    // against it, and recording it again would lose that (plan v4 §8.1).
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
    // notices the audit entry never turning up (plan v4 §8.1).
    attribute_later(member.guild_id, member.user_id, *row);
}

void bot::attribute_later(dpp::snowflake guild_id, dpp::snowflake user_id, std::int64_t row) {
    // A self-cancelling repeat, which is the one-shot DPP does not have. The
    // handle arrives in the callback, so nothing has to be kept alive here.
    cluster_.start_timer(
        [this, guild_id, user_id, row](dpp::timer handle) {
            cluster_.stop_timer(handle);

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
                                            if (entries == nullptr) {
                                                return;
                                            }

                                            for (const dpp::audit_entry& entry : entries->entries) {
                                                if (entry.target_id == user_id) {
                                                    on_audit_entry(entry, guild_id);
                                                }
                                            }
                                        });
        },
        static_cast<std::uint64_t>(events::audit_fallback_delay.count()));
}

void bot::on_audit_entry(const dpp::audit_entry& entry, dpp::snowflake guild_id) {
    if (entry.type != dpp::aut_member_update || guild_id.empty()) {
        return;
    }

    const auto change = nickname_change_in(entry);
    if (!change) {
        return;
    }

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

void bot::reconcile_nicknames(const dpp::guild& guild) {
    if (!settings_.track_nicknames) {
        return;
    }

    // Changes made while the bot was not running have nobody to attribute
    // them to, which is why they are marked as their own source rather than
    // guessed at (plan v4 §8.4).
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

void bot::import_url_rules(const dpp::guild& guild) {
    if (guild_settings_.get_bool(guild.id, url_rules_imported_key, false)) {
        return;
    }

    // Marked only once a file was actually read, so dropping the file in
    // after a first run still works.
    const std::filesystem::path legacy = settings_.database_path.parent_path() / legacy_url_rules_file;
    const auto imported = events::import_url_rules_file(url_rules_, guild.id, legacy);
    if (!imported) {
        return;
    }

    guild_settings_.set_bool(guild.id, url_rules_imported_key, true);
    util::log().info("{}: imported {} URL rule(s) from {}{}", guild.name, *imported, legacy.generic_string(),
                     url_rules_.enabled(guild.id) ? "" : "; they apply once someone runs /urlrepl enable there");
}

void bot::retry_replacement(const dpp::interaction_create_t& event, dpp::snowflake message_id, const commands::user_label& who) {
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
    // be overtaken by another press. Anyone may press it (plan v4 §9.4).
    event.reply(dpp::ir_update_message, events::build_edit(retry.first));
    carry_out(embed_tracker_.watch(std::move(retry.request)));
}

std::chrono::sys_seconds bot::now_seconds() const {
    return std::chrono::floor<std::chrono::seconds>(clock_.now());
}

void bot::carry_out(std::vector<events::embed_action> actions) {
    if (!actions.empty()) {
        detach(events::carry_out_embed_actions(gateway_, std::move(actions)), "updating a replacement");
    }
}

void bot::toggle_trigger(std::int64_t id, dpp::snowflake guild, const commands::user_label& who,
                         const std::function<std::string_view(events::trigger&)>& change) {
    auto entry = triggers_.find(id, guild);
    if (!entry) {
        // Deleted from another client while this panel was open. The caller
        // re-renders either way, which is what puts the panel back in step.
        util::log().debug("panel asked to change trigger {}, which is no longer in guild {}", id, guild);
        return;
    }

    const std::string_view became = change(*entry);
    triggers_.update(*entry);
    util::log().info("trigger {} in guild {} {} by {} from the panel", id, guild, became, who);
}

void bot::on_component(const dpp::interaction_create_t& event, const std::string& custom_id, const std::string& chosen) {
    const auto state = ui::decode(custom_id);
    if (!state) {
        // Someone else's component, or one of ours from a build that encoded
        // them differently. Ignoring it is right; saying so is how you find out.
        util::log().debug("ignoring a component with an unrecognised id \"{}\"", custom_id);
        return;
    }

    const dpp::snowflake guild = event.command.guild_id;
    const commands::user_label who = commands::describe_user(event.command.get_issuing_user());
    util::log().debug("{} used panel {} page {} argument \"{}\"{} in guild {}", who, state->view, state->page, state->argument,
                      chosen.empty() ? std::string{} : std::format(" chose \"{}\"", chosen), guild);

    // Every one of these edits the message the component is on rather than
    // posting a new one, which is why the state rides in the custom_id: there
    // is nothing here to expire, leak, or lose across a restart.
    if (state->view == commands::nickname_history_view) {
        const dpp::snowflake subject(state->argument);
        event.reply(dpp::ir_update_message, commands::render_nickname_history(nicknames_.history(guild, subject), subject, state->page));
    } else if (state->view == events::url_retry_view) {
        retry_replacement(event, dpp::snowflake(state->argument), who);
    } else if (state->view == commands::board_view) {
        // The board's filters ride in the argument, so every page is the
        // same board as the first.
        if (const auto board = commands::decode_board(state->argument)) {
            event.reply(dpp::ir_update_message, commands::render_board(reactions_, guild, board->first, board->second, state->page));
        }
    } else if (!on_trigger_component(event, *state, chosen, who) && !on_url_component(event, *state, chosen, who)) {
        util::log().debug("no panel handles the view \"{}\"", state->view);
    }
}

bool bot::on_trigger_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen,
                               const commands::user_label& who) {
    const dpp::snowflake guild = event.command.guild_id;
    const std::int64_t id = chosen.empty() ? argument_id(state) : 0;

    if (state.view == commands::trigger_list_view) {
        event.reply(dpp::ir_update_message, commands::render_trigger_list(triggers_, guild, state.page));
    } else if (state.view == commands::trigger_panel_view) {
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state.page));
    } else if (state.view == commands::trigger_pick_view) {
        std::int64_t picked = 0;
        const auto [stop, error] = std::from_chars(chosen.data(), chosen.data() + chosen.size(), picked);
        if (error != std::errc{} || stop != chosen.data() + chosen.size()) {
            picked = 0;
        }
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state.page, picked));
    } else if (state.view == commands::trigger_delete_view) {
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state.page, id, /*confirming_delete=*/true));
    } else if (state.view == commands::trigger_confirm_view) {
        util::log().info("trigger {} removed from guild {} by {} from the panel", id, guild, who);
        triggers_.remove(id, guild);
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state.page));
    } else if (state.view == commands::trigger_toggle_view) {
        toggle_trigger(id, guild, who, [](events::trigger& entry) {
            entry.enabled = !entry.enabled;
            return entry.enabled ? "enabled" : "disabled";
        });
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state.page, id));
    } else if (state.view == commands::trigger_bots_view) {
        toggle_trigger(id, guild, who, [](events::trigger& entry) {
            entry.respond_to_bots = !entry.respond_to_bots;
            return entry.respond_to_bots ? "set to answer bots" : "set to ignore bots";
        });
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state.page, id));
    } else if (state.view == commands::trigger_add_view) {
        event.dialog(commands::trigger_form(state.page, nullptr));
    } else if (state.view == commands::trigger_edit_view) {
        const auto entry = triggers_.find(id, guild);
        if (entry) {
            event.dialog(commands::trigger_form(state.page, &*entry));
        } else {
            event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state.page));
        }
    } else {
        return false;
    }
    return true;
}

bool bot::on_url_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen,
                           const commands::user_label& who) {
    const dpp::snowflake guild = event.command.guild_id;

    if (state.view == commands::url_list_view) {
        event.reply(dpp::ir_update_message, commands::render_url_rule_list(url_rules_, guild, state.page));
    } else if (state.view == commands::url_panel_view) {
        event.reply(dpp::ir_update_message, commands::render_url_panel(url_rules_, guild, state.page));
    } else if (state.view == commands::url_pick_view) {
        event.reply(dpp::ir_update_message, commands::render_url_panel(url_rules_, guild, state.page, chosen));
    } else if (state.view == commands::url_delete_view) {
        event.reply(dpp::ir_update_message,
                    commands::render_url_panel(url_rules_, guild, state.page, state.argument, /*confirming_delete=*/true));
    } else if (state.view == commands::url_confirm_view) {
        if (url_rules_.remove(guild, state.argument)) {
            util::log().info("URL rule for {} removed from guild {} by {} from the panel", state.argument, guild, who);
        }
        event.reply(dpp::ir_update_message, commands::render_url_panel(url_rules_, guild, state.page));
    } else if (state.view == commands::url_switch_view) {
        commands::switch_url_replacement(url_rules_, guild, state.argument == "on", who, " from the panel");
        event.reply(dpp::ir_update_message, commands::render_url_panel(url_rules_, guild, state.page));
    } else if (state.view == commands::url_add_view) {
        event.dialog(commands::url_rule_form(state.page, nullptr));
    } else if (state.view == commands::url_edit_view) {
        // Removed from another client while this panel was open.
        const auto rule = url_rules_.find(guild, state.argument);
        if (!rule) {
            event.reply(dpp::ir_update_message, commands::render_url_panel(url_rules_, guild, state.page));
        } else {
            event.dialog(commands::url_rule_form(state.page, &*rule));
        }
    } else {
        return false;
    }
    return true;
}

void bot::on_url_form(const dpp::form_submit_t& event, const ui::page_state& state) {
    const dpp::snowflake guild = event.command.guild_id;
    const commands::user_label who = commands::describe_user(event.command.get_issuing_user());

    const auto built = commands::build_rule(field_of(event, "domain"), field_of(event, "mirrors"));
    if (const auto* problem = std::get_if<std::string>(&built)) {
        util::log().debug("{} submitted an unusable URL rule in guild {}: {}", who, guild, *problem);
        dpp::message complaint(*problem);
        complaint.set_flags(dpp::m_ephemeral);
        event.reply(complaint);
        return;
    }

    const auto& rule = std::get<events::url_rule>(built);

    // The argument is the domain the modal was opened for, so a changed site
    // is a rename rather than a second rule.
    const std::string& previous = state.argument;
    if (!previous.empty() && previous != rule.domain) {
        url_rules_.remove(guild, previous);
    }
    url_rules_.set(guild, rule);

    std::string what = "changed";
    if (previous.empty()) {
        what = "added";
    } else if (previous != rule.domain) {
        what = std::format("renamed from {}", previous);
    }
    util::log().info("URL rule for {} {} in guild {} by {} from the panel: {}", rule.domain, what, guild, who,
                     commands::describe_mirrors(rule.mirrors));
    event.reply(dpp::ir_update_message, commands::render_url_panel(url_rules_, guild, state.page, rule.domain));
}

void bot::on_form(const dpp::form_submit_t& event) {
    const auto state = ui::decode(event.custom_id);
    if (state && state->view == commands::url_form_view) {
        on_url_form(event, *state);
        return;
    }
    if (!state || state->view != commands::trigger_form_view) {
        util::log().debug("ignoring a modal submission with an unrecognised id \"{}\"", event.custom_id);
        return;
    }

    const dpp::snowflake guild = event.command.guild_id;
    const std::int64_t id = argument_id(*state);
    const commands::user_label who = commands::describe_user(event.command.get_issuing_user());

    // Zero means add. Anything else has to still exist: somebody could have
    // deleted it from another client while the modal was open.
    events::trigger entry;
    if (id != 0) {
        auto existing = triggers_.find(id, guild);
        if (!existing) {
            event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state->page));
            return;
        }
        entry = std::move(*existing);
    } else {
        entry.guild_id = guild;
        entry.cooldown = events::default_trigger_cooldown;
    }

    const commands::form_fields fields{.pattern = field_of(event, "pattern"),
                                       .responses = field_of(event, "responses"),
                                       .mode = field_of(event, "mode"),
                                       .cooldown = field_of(event, "cooldown")};

    if (const auto problem = commands::apply_form(entry, fields)) {
        util::log().debug("{} submitted an unusable trigger form in guild {}: {}", who, guild, *problem);
        dpp::message complaint(*problem);
        complaint.set_flags(dpp::m_ephemeral);
        event.reply(complaint);
        return;
    }

    const std::int64_t saved = id == 0 ? triggers_.add(entry) : (triggers_.update(entry), entry.id);
    util::log().info("trigger {} {} in guild {} by {} from the panel: {}", saved, id == 0 ? "added" : "updated", guild, who,
                     commands::describe(entry));
    event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state->page, saved));
}

events::incoming_message bot::describe(const dpp::message& message) const {
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

void bot::carry_out(const std::vector<events::action>& actions) {
    for (const events::action& wanted : actions) {
        std::visit(
            [this](const auto& step) {
                using step_type = std::decay_t<decltype(step)>;

                if constexpr (std::is_same_v<step_type, events::send_message>) {
                    dpp::message reply(step.channel_id, step.content);
                    // Suppressed notifications, as the Java bot did: these are
                    // jokes and acknowledgements, not things to be pinged for.
                    reply.set_flags(dpp::m_suppress_notifications);
                    cluster_.message_create(reply);
                    util::log().info("replied in channel {}: \"{}\"", step.channel_id, step.content);
                } else if constexpr (std::is_same_v<step_type, events::replace_links>) {
                    detach(events::post_replacement(gateway_, replacements_, embed_tracker_, clock_, step), "posting a replacement");
                } else if constexpr (std::is_same_v<step_type, events::stop_bot>) {
                    util::log().info("shutting down on request from a message");
                    // Detached, so the pause does not block DPP's event
                    // thread. The process is on its way out either way.
                    const auto delay = step.after;
                    std::thread([this, delay] {
                        std::this_thread::sleep_for(delay);
                        cluster_.shutdown();
                    }).detach();
                }
            },
            wanted);
    }
}

void bot::check_permissions(const dpp::guild& guild) const {
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
    required.push_back({.permissions = commands_.required_bot_permissions(), .purpose = "the registered commands"});

    const std::uint64_t granted = guild.base_permissions(self->second);
    const std::vector<commands::gap> gaps = commands::unmet(required, granted);
    for (const commands::gap& missing : gaps) {
        util::log().warn("{} ({}): missing {} for {}", guild.name, guild.id, commands::describe_permissions(missing.permissions),
                         missing.purpose);
    }

    if (gaps.empty()) {
        util::log().debug("{}: every permission the bot needs is granted", guild.name);
    }
}

void bot::run() {
    util::log().info("connecting to Discord");
    cluster_.start(dpp::st_wait);

    // start() returns once the cluster has stopped, so this is the last thing
    // the bot says: a log that ends here stopped on purpose, and one that ends
    // anywhere else did not.
    util::log().info("disconnected; LatiBot has stopped");
}

} // namespace latibot
