#include "links/module.hpp"

#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/config/guild_settings.hpp"
#include "core/events/stage_order.hpp"
#include "core/modules/host.hpp"
#include "core/ports/clock.hpp"
#include "core/ui/panel_routes.hpp"
#include "core/util/log.hpp"
#include "embed_watch.hpp"
#include "links/replacements.hpp"
#include "links/url_rules.hpp"
#include "links_command.hpp"
#include "url_replacer.hpp"

#include <dpp/dpp.h>

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace latibot::events {
namespace {

// Append only: once a version has shipped, its SQL is never edited, and a
// change becomes the next version.
constexpr std::array<db::migration, 1> links_steps{{
    {.version = 1, .name = "url rules and replacements", .sql = R"sql(
        -- Where links to a site go instead, in the order to try them. A rule
        -- is its rows for one domain; position 0 is first.
        CREATE TABLE url_rules (
            guild_id         INTEGER NOT NULL,

            -- Lowercase, without "www.": the host a link is looked up by.
            domain           TEXT    NOT NULL,
            position         INTEGER NOT NULL,
            host             TEXT    NOT NULL,

            -- "/en" for a mirror that translates when asked; NULL for none.
            translate_suffix TEXT,

            PRIMARY KEY (guild_id, domain, position)
        ) WITHOUT ROWID;

        -- Members who asked for their links to be left alone, per guild.
        CREATE TABLE url_opt_outs (
            guild_id INTEGER NOT NULL,
            user_id  INTEGER NOT NULL,
            PRIMARY KEY (guild_id, user_id)
        ) WITHOUT ROWID;

        -- Every mirror a rule has ever used, and never pruned: the recompute
        -- recognises the bot's old messages by these hosts, and they do not
        -- stop existing when a rule changes.
        CREATE TABLE known_mirrors (
            guild_id INTEGER NOT NULL,
            host     TEXT    NOT NULL,
            domain   TEXT    NOT NULL,
            PRIMARY KEY (guild_id, host)
        ) WITHOUT ROWID;

        -- One row per message the bot posted in place of somebody's links,
        -- or, with kind 'image', a person's own image or video whose
        -- reactions linkstats counts. Reaction statistics hang off it, so
        -- rows are kept after the message is gone.
        CREATE TABLE replacement_messages (
            message_id          INTEGER PRIMARY KEY,
            guild_id            INTEGER NOT NULL,
            channel_id          INTEGER NOT NULL,

            -- NULL for an old message whose original could not be found.
            original_message_id INTEGER,
            original_author_id  INTEGER,

            -- pending | ok | failed | retrying
            state               TEXT    NOT NULL,

            -- Unix seconds.
            created_at          INTEGER NOT NULL,
            retried_at          INTEGER,

            -- link | image
            kind                TEXT    NOT NULL DEFAULT 'link'
        );

        CREATE INDEX replacement_messages_by_author ON replacement_messages (guild_id, original_author_id);
        CREATE INDEX replacement_messages_by_kind ON replacement_messages (guild_id, kind);

        -- The links in one replacement, so Retry still knows what to try after
        -- a restart. Mirrors are not stored: Retry uses the rule as it is now.
        CREATE TABLE replacement_links (
            message_id   INTEGER NOT NULL REFERENCES replacement_messages (message_id) ON DELETE CASCADE,
            position     INTEGER NOT NULL,
            original_url TEXT    NOT NULL,
            domain       TEXT    NOT NULL,
            spoilered    INTEGER NOT NULL,
            PRIMARY KEY (message_id, position)
        ) WITHOUT ROWID;
     )sql"},
}};

constexpr std::string_view module_name = "links";

/// The Java bot's rules, if its file was left beside the database.
constexpr std::string_view legacy_url_rules_file = "UrlReplacements.txt";

/// Set once a guild has had the Java bot's rules, so that removing one later
/// is not undone by the next restart.
constexpr std::string_view url_rules_imported_key = "url_rules_imported";

/// The URLs of a message's previews, which is all the embed tracker needs.
auto embed_urls_of(const dpp::message& message) -> std::vector<std::string> {
    std::vector<std::string> urls;
    urls.reserve(message.embeds.size());
    for (const dpp::embed& embed : message.embeds) {
        urls.push_back(embed.url);
    }
    return urls;
}

class links_module;

/// The Retry button on a replacement that found no preview
/// (docs/features/Url_Replacement.md §2.4).
class retry_panel {
public:
    explicit retry_panel(links_module& owner) : owner_(&owner) {}

    auto on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool;

private:
    links_module* owner_;
};

class links_module final : public modules::module {
public:
    explicit links_module(modules::host& bot)
        : bot_(&bot),
          rules_(bot.database()),
          replacements_(bot.database()),
          tracker_(replacements_, bot.clock()),
          panel_(rules_),
          retry_(*this) {}

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }
    [[nodiscard]] auto schema() const -> std::span<const db::migration> override { return links_steps; }

    auto start(modules::host& bot) -> void override {
        // Read before the connection starts, so everything found was cut off
        // by the last run rather than being watched by this one. Each guild's
        // are settled once it connects.
        const std::vector<replacement_record> unsettled = replacements_.unsettled();
        for (const replacement_record& record : unsettled) {
            stranded_[record.guild_id].push_back(record);
        }
        if (!unsettled.empty()) {
            util::log().info("the last run left {} replacement(s) waiting on a preview; settling them as their servers connect",
                             unsettled.size());
        }

        bot.slash_commands().add(std::make_unique<commands::links_command>(rules_));
        bot.slash_commands().add(std::make_unique<commands::urltoggle_command>(rules_));
        bot.panels().add(
            panel_,
            {commands::url_list_view, commands::url_panel_view, commands::url_pick_view, commands::url_edit_view, commands::url_delete_view,
             commands::url_confirm_view, commands::url_add_view, commands::url_form_view, commands::url_switch_view},
            module_name);
        bot.panels().add(retry_, {url_retry_view}, module_name);

        // Before the trigger replies, which a message with a link and a joke
        // gets as well (docs/features/Message_Pipeline.md §2.2).
        bot.add_stage(stage_order::rewrite, "url replacement",
                      carried_out_by<replace_links>(url_replacer(rules_), "posting a replacement", [this](replace_links request) {
                          return post_replacement(bot_->gateway(), replacements_, tracker_, bot_->clock(), std::move(request));
                      }));

        // Without Embed Links the replacement posts but shows nothing, which
        // looks like a broken mirror rather than a missing permission.
        bot.permission(dpp::p_embed_links, "showing link previews in URL replacements");
        bot.permission(dpp::p_manage_messages, "turning off the original preview when a link is replaced");

        bot.listen(bot.cluster().on_guild_create, "links: the Java bot's rules, and replacements left unsettled",
                   [this](const dpp::guild_create_t& event) {
                       const dpp::guild& guild = event.created;
                       import_rules(guild);
                       settle_stranded(guild.id);
                       util::log().debug("{}: URL replacement {} with {} rule(s)", guild.name, rules_.enabled(guild.id) ? "on" : "off",
                                         rules_.for_guild(guild.id).size());
                   });

        // Discord adds link previews by updating the message a moment after
        // it was posted, which is how the embed tracker learns that a mirror
        // worked (docs/features/Url_Replacement.md §3.3). Every update goes to
        // it: the one for our message often arrives without an author, so
        // there is nothing to filter on here.
        bot.listen(bot.cluster().on_message_update, "links: previews arriving",
                   [this](const dpp::message_update_t& event) { carry_out(tracker_.on_embeds(event.msg.id, embed_urls_of(event.msg))); });
        bot.listen(bot.cluster().on_message_delete, "links: replacements deleted",
                   [this](const dpp::message_delete_t& event) { tracker_.forget(event.id); });

        // One timer for every replacement being watched, rather than one
        // each: the tracker knows whose time is up, and a second is as fine as
        // DPP's timers go. Most ticks find nothing and cost a lock.
        bot.every(std::chrono::seconds{1}, "the preview tracker's tick", [this] { carry_out(tracker_.tick()); });
    }

    /// Somebody pressed Retry on a replacement that found no preview.
    auto retry(const dpp::interaction_create_t& event, dpp::snowflake message_id) -> void {
        auto plan = plan_retry(replacements_, rules_, message_id, event.command.guild_id);
        if (const auto* reason = std::get_if<std::string>(&plan)) {
            dpp::message note(*reason);
            note.set_flags(dpp::m_ephemeral);
            event.reply(note);
            return;
        }

        auto& retry = std::get<retry_plan>(plan);
        replacements_.set_state(message_id, replacement_state::retrying);
        util::log().info("{} pressed Retry on replacement {} in guild {}", commands::describe_user(event.command.get_issuing_user()),
                         message_id, event.command.guild_id);

        // Answering the button with the edit is the first attempt, so it
        // cannot be overtaken by another press. Anyone may press it
        // (docs/features/Url_Replacement.md §2.4).
        event.reply(dpp::ir_update_message, build_edit(retry.first));
        carry_out(tracker_.watch(std::move(retry.request)));
    }

private:
    /// Copies the Java bot's URL rules into a guild, once
    /// (docs/features/Url_Replacement.md §2.7).
    auto import_rules(const dpp::guild& guild) -> void {
        config::guild_settings& settings = bot_->settings();
        if (settings.get_bool(guild.id, url_rules_imported_key, false)) return;

        // Marked only once a file was actually read, so dropping the file in
        // after a first run still works.
        const std::filesystem::path legacy = bot_->bootstrap().database_path.parent_path() / legacy_url_rules_file;
        const auto imported = import_url_rules_file(rules_, guild.id, legacy);
        if (!imported) return;

        settings.set_bool(guild.id, url_rules_imported_key, true);
        util::log().info("{}: imported {} URL rule(s) from {}{}", guild.name, *imported, legacy.generic_string(),
                         rules_.enabled(guild.id) ? "" : "; they apply once someone runs /links enable there");
    }

    /// Settles this guild's replacements the last run left mid-watch, once:
    /// its first guild_create hands them over
    /// (docs/features/Url_Replacement.md §2.5).
    auto settle_stranded(dpp::snowflake guild_id) -> void {
        std::vector<replacement_record> mine;
        {
            const std::scoped_lock guard(stranded_mutex_);
            const auto found = stranded_.find(guild_id);
            if (found == stranded_.end()) return;
            mine = std::move(found->second);
            stranded_.erase(found);
        }

        bot_->detach(events::settle_stranded(bot_->gateway(), replacements_, rules_, tracker_, std::move(mine)),
                     "settling replacements the last run left unfinished");
    }

    /// Runs what the embed tracker decided, without holding up the caller.
    auto carry_out(std::vector<embed_action> actions) -> void {
        if (!actions.empty()) bot_->detach(carry_out_embed_actions(bot_->gateway(), std::move(actions)), "updating a replacement");
    }

    modules::host* bot_;
    url_rule_store rules_;
    replacement_store replacements_;
    embed_tracker tracker_;
    commands::url_panel panel_;
    retry_panel retry_;

    /// Replacements the last run left `pending` or `retrying`, by guild.
    std::mutex stranded_mutex_;
    std::map<dpp::snowflake, std::vector<replacement_record>> stranded_;
};

auto retry_panel::on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& /*chosen*/) -> bool {
    if (state.view != url_retry_view) return false;
    owner_->retry(event, dpp::snowflake(state.argument));
    return true;
}

} // namespace
} // namespace latibot::events

namespace latibot::links {

auto schema() noexcept -> db::module_schema {
    return {.module = events::module_name, .steps = events::links_steps};
}

auto make_module(modules::host& bot) -> std::unique_ptr<modules::module> {
    return std::make_unique<events::links_module>(bot);
}

} // namespace latibot::links
