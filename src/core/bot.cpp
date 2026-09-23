#include "core/bot.hpp"

#include "core/commands/basic.hpp"
#include "core/commands/bots.hpp"
#include "core/commands/preflight.hpp"
#include "core/commands/trigger.hpp"
#include "core/db/migrations.hpp"
#include "core/events/goodbye.hpp"
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
      // i_message_content is privileged and must also be enabled in the
      // Discord developer portal. Without it every guild message arrives with
      // an empty `content`, which silently disables the whole pipeline: the
      // goodbye phrase and the triggers both read it (plan v4 §5.4).
      //
      // i_guild_members is deliberately not requested. The only thing that
      // needs a member is the administrator check, and Discord sends a partial
      // member with each guild message, which DPP caches before the handler
      // runs.
      cluster_(credentials.discord_token, dpp::i_default_intents | dpp::i_message_content),
      gateway_(cluster_),
      http_(cluster_),
      raw_(cluster_),
      bot_allowlist_(database_),
      triggers_(database_),
      trigger_responder_(triggers_, clock_) {
    util::log().set_level(settings_.log_level);

    util::log().info("LatiBot {} starting", version_string());
    util::log().debug("log level {}; {} trusted guild(s), {} trusted user(s)", util::to_string(settings_.log_level),
                      settings_.trusted_guilds.size(), settings_.trusted_users.size());

    // After the line above, so that any migration it applies is logged under a
    // heading rather than before the bot has said it is starting.
    const int version = db::migrate(database_);
    util::log().info("database {} at schema version {}", settings_.database_path.generic_string(), version);

    register_commands();
    register_stages();
    register_events();

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
}

void bot::register_stages() {
    // The order is plan v4 §5.4, and it is a list so that changing it is one
    // line. URL replacement and the LLM stages join it in phases 3 and 5.
    pipeline_.add("goodbye", events::goodbye_stage(guild_settings_));
    pipeline_.add("triggers", [this](const events::incoming_message& message) { return trigger_responder_(message); });
}

void bot::register_events() {
    // DPP's own logging goes through our logger, so there is one format and
    // one level to configure.
    cluster_.on_log([](const dpp::log_t& event) { util::log().write(from_dpp(event.severity), "[dpp] " + event.message); });

    cluster_.on_slashcommand([this](const dpp::slashcommand_t& event) -> dpp::task<void> {
        co_await commands_.dispatch(event.command.get_command_name(), event);
    });

    cluster_.on_ready([this](const dpp::ready_t& event) {
        util::log().info("connected to Discord as {} ({})", cluster_.me.username, cluster_.me.id.str());

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
        util::log().info("in guild {} ({})", guild.name, guild.id.str());

        check_permissions(guild);

        const int seeded = triggers_.seed_defaults(guild.id);
        if (seeded > 0) {
            util::log().info("{}: seeded {} default triggers", guild.name, seeded);
        }

        util::log().debug("{}: {} trigger(s), {} allowed bot(s), goodbye phrase \"{}\"", guild.name, triggers_.for_guild(guild.id).size(),
                          bot_allowlist_.for_guild(guild.id).size(),
                          guild_settings_.get(guild.id, events::goodbye_phrase_key, events::default_goodbye_phrase));
    });

    cluster_.on_message_create([this](const dpp::message_create_t& event) { carry_out(pipeline_.run(describe(event.msg))); });

    cluster_.on_button_click([this](const dpp::button_click_t& event) { on_component(event, event.custom_id, {}); });
    cluster_.on_select_click([this](const dpp::select_click_t& event) {
        on_component(event, event.custom_id, event.values.empty() ? std::string{} : event.values.front());
    });
    cluster_.on_form_submit([this](const dpp::form_submit_t& event) { on_form(event); });
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

void bot::toggle_trigger(std::int64_t id, dpp::snowflake guild, std::string_view who,
                         const std::function<std::string_view(events::trigger&)>& change) {
    auto entry = triggers_.find(id, guild);
    if (!entry) {
        // Deleted from another client while this panel was open. The caller
        // re-renders either way, which is what puts the panel back in step.
        util::log().debug("panel asked to change trigger {}, which is no longer in guild {}", id, guild.str());
        return;
    }

    const std::string_view became = change(*entry);
    triggers_.update(*entry);
    util::log().info("trigger {} in guild {} {} by {} from the panel", id, guild.str(), became, who);
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
    const std::int64_t id = chosen.empty() ? argument_id(*state) : 0;

    const std::string who = commands::describe_user(event.command.get_issuing_user());
    util::log().debug("{} used panel {} page {} argument \"{}\"{} in guild {}", who, state->view, state->page, state->argument,
                      chosen.empty() ? std::string{} : std::format(" chose \"{}\"", chosen), guild.str());

    // Every one of these edits the message the component is on rather than
    // posting a new one, which is why the state rides in the custom_id: there
    // is nothing here to expire, leak, or lose across a restart.
    if (state->view == commands::trigger_list_view) {
        event.reply(dpp::ir_update_message, commands::render_trigger_list(triggers_, guild, state->page));
    } else if (state->view == commands::trigger_panel_view) {
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state->page));
    } else if (state->view == commands::trigger_pick_view) {
        std::int64_t picked = 0;
        const auto [stop, error] = std::from_chars(chosen.data(), chosen.data() + chosen.size(), picked);
        if (error != std::errc{} || stop != chosen.data() + chosen.size()) {
            picked = 0;
        }
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state->page, picked));
    } else if (state->view == commands::trigger_delete_view) {
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state->page, id, /*confirming_delete=*/true));
    } else if (state->view == commands::trigger_confirm_view) {
        util::log().info("trigger {} removed from guild {} by {} from the panel", id, guild.str(), who);
        triggers_.remove(id, guild);
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state->page));
    } else if (state->view == commands::trigger_toggle_view) {
        toggle_trigger(id, guild, who, [](events::trigger& entry) {
            entry.enabled = !entry.enabled;
            return entry.enabled ? "enabled" : "disabled";
        });
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state->page, id));
    } else if (state->view == commands::trigger_bots_view) {
        toggle_trigger(id, guild, who, [](events::trigger& entry) {
            entry.respond_to_bots = !entry.respond_to_bots;
            return entry.respond_to_bots ? "set to answer bots" : "set to ignore bots";
        });
        event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state->page, id));
    } else if (state->view == commands::trigger_add_view) {
        event.dialog(commands::trigger_form(state->page, nullptr));
    } else if (state->view == commands::trigger_edit_view) {
        const auto entry = triggers_.find(id, guild);
        if (!entry) {
            event.reply(dpp::ir_update_message, commands::render_trigger_panel(triggers_, guild, state->page));
            return;
        }
        event.dialog(commands::trigger_form(state->page, &*entry));
    }
}

void bot::on_form(const dpp::form_submit_t& event) {
    const auto state = ui::decode(event.custom_id);
    if (!state || state->view != commands::trigger_form_view) {
        util::log().debug("ignoring a modal submission with an unrecognised id \"{}\"", event.custom_id);
        return;
    }

    const dpp::snowflake guild = event.command.guild_id;
    const std::int64_t id = argument_id(*state);
    const std::string who = commands::describe_user(event.command.get_issuing_user());

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
        util::log().debug("{} submitted an unusable trigger form in guild {}: {}", who, guild.str(), *problem);
        dpp::message complaint(*problem);
        complaint.set_flags(dpp::m_ephemeral);
        event.reply(complaint);
        return;
    }

    const std::int64_t saved = id == 0 ? triggers_.add(entry) : (triggers_.update(entry), entry.id);
    util::log().info("trigger {} {} in guild {} by {} from the panel: {}", saved, id == 0 ? "added" : "updated", guild.str(), who,
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
                    util::log().info("replied in channel {}: \"{}\"", step.channel_id.str(), step.content);
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
        util::log().warn("{} ({}): missing {} for {}", guild.name, guild.id.str(), commands::describe_permissions(missing.permissions),
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
