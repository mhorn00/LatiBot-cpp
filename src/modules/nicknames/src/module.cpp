#include "nicknames/module.hpp"

#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/modules/host.hpp"
#include "core/ports/clock.hpp"
#include "core/ui/interaction.hpp"
#include "core/ui/panel_routes.hpp"
#include "core/util/log.hpp"
#include "nickname_command.hpp"
#include "nickname_import.hpp"
#include "nicknames.hpp"
#include "nicknames_config.hpp"

#include <dpp/dpp.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace latibot::events {
namespace {

// Append only: once a version has shipped, its SQL is never edited, and a
// change becomes the next version.
constexpr std::array<db::migration, 1> nicknames_steps{{
    {.version = 1, .name = "nickname_history", .sql = R"sql(
        -- Every nickname a member has had here, however the change was made.
        -- Ids are stored raw and names resolved at display time, so a member
        -- who has left still has a readable history.
        CREATE TABLE nickname_history (
            id           INTEGER PRIMARY KEY,
            guild_id     INTEGER NOT NULL,
            user_id      INTEGER NOT NULL,

            -- NULL means the nickname was cleared, which is not the same as "".
            nickname     TEXT,

            -- Unix seconds. Compared against dates, so it is wall clock.
            changed_at   INTEGER NOT NULL,

            -- NULL means nobody could be named.
            changed_by   INTEGER,

            -- command | audit_log | seen | startup | imported: how far the
            -- attribution above can be trusted.
            source       TEXT    NOT NULL,

            -- The original timestamp text from nicknames.json, so the timezone
            -- conversion can be redone.
            imported_raw TEXT
        );

        -- Every read is "this member, newest first"; the partial index is for
        -- the audit log looking for a row it can still attribute.
        CREATE INDEX nickname_history_by_member ON nickname_history (guild_id, user_id, changed_at DESC);
        CREATE INDEX nickname_history_unattributed ON nickname_history (guild_id, user_id, changed_at)
            WHERE changed_by IS NULL;
     )sql"},
}};

constexpr std::string_view module_name = "nicknames";

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

/// The nickname an audit entry says a member ended up with, or nothing when
/// the entry is not about a nickname at all.
auto nickname_change_in(const dpp::audit_entry& entry) -> std::optional<dpp::audit_change> {
    for (const dpp::audit_change& change : entry.changes) {
        if (change.key == "nick") return change;
    }
    return std::nullopt;
}

/// The pages of a member's history, `/nicknames`' buttons. Every one edits
/// the message it is on, so the state rides in the custom_id: there is
/// nothing here to expire, leak, or lose across a restart.
class history_panel {
public:
    explicit history_panel(nickname_store& store) : store_(&store) {}

    auto on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& /*chosen*/) -> bool {
        if (state.view != commands::nickname_history_view) return false;
        const dpp::snowflake subject(state.argument);
        ui::update_panel(event, commands::render_nickname_history(store_->history(event.command.guild_id, subject), subject, state.page));
        return true;
    }

private:
    nickname_store* store_;
};

class nicknames_module final : public modules::module {
public:
    explicit nicknames_module(modules::host& bot)
        : bot_(&bot), config_(nicknames::nicknames_section().read(bot.section(module_name))), store_(bot.database()), panel_(store_) {}

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }
    [[nodiscard]] auto schema() const -> std::span<const db::migration> override { return nicknames_steps; }

    auto start(modules::host& bot) -> void override {
        bot.slash_commands().add(std::make_unique<commands::nickname_command>(store_, pending_, bot.clock(), bot.cluster()));
        bot.slash_commands().add(std::make_unique<commands::nicknames_command>(store_));
        bot.panels().add(panel_, {commands::nickname_history_view}, module_name);

        // Years of history from the Java bot, if its file was left beside the
        // database. Importing is idempotent, so this needs no marker file and
        // no "have I done this already" flag (docs/features/Nicknames.md §4).
        const std::filesystem::path legacy = bot.bootstrap().database_path.parent_path() / "nicknames.json";
        if (const auto imported = import_nicknames_file(store_, legacy); imported.value_or(0) > 0) {
            util::log().info("imported {} nickname entries from {}", *imported, legacy.generic_string());
        }

        // Worth an info line rather than a debug one: it is the difference
        // between a bot that connects and one that Discord turns away, and
        // the reason is a toggle on a web page nobody looks at twice a year.
        if (!config_.track_changes) {
            util::log().info("nickname tracking is off; /nickname still works, but changes made elsewhere are not recorded");
            return;
        }
        util::log().info("nickname tracking is on; this needs the Server Members intent enabled in the Discord developer portal");

        // The only way nickname changes and a complete member list arrive at
        // all (docs/features/Operations.md §3). Privileged: asked for only
        // while tracking is on, because a bot that asks for an intent it was
        // not granted is refused the gateway outright.
        bot.intents(dpp::i_guild_members);
        bot.permission(dpp::p_view_audit_log, "naming who changed a nickname");

        // Recording and attributing are separate events on purpose: the
        // change is written down the moment it is seen, and the audit log
        // fills in who did it if and when it arrives
        // (docs/features/Nicknames.md §3). Attached only while tracking is
        // on, because DPP warns about a handler attached without the intent
        // that feeds it.
        bot.listen(bot.cluster().on_guild_member_update, "nicknames: member updates",
                   [this](const dpp::guild_member_update_t& event) { on_member_update(event.updated); });
        bot.listen(bot.cluster().on_guild_audit_log_entry_create, "nicknames: audit log entries",
                   [this](const dpp::guild_audit_log_entry_create_t& event) { on_audit_entry(event.entry, guild_of(event)); });
        bot.listen(bot.cluster().on_guild_create, "nicknames: changes made while offline",
                   [this](const dpp::guild_create_t& event) { reconcile(event.created); });

        // 4014 is the gateway refusing a privileged intent, and DPP reports
        // it as a websocket number in a reconnect loop. The cause is always
        // the same toggle, so say which one rather than leaving somebody to
        // look the code up (docs/features/Operations.md §3).
        bot.listen(bot.cluster().on_log, "nicknames: the Server Members intent refused", [](const dpp::log_t& event) {
            if (!event.message.contains("4014")) return;
            util::log().error(
                "Discord refused the Server Members intent. Enable it under Bot > Privileged Gateway Intents "
                "in the Discord developer portal, or set \"track_changes\": false in the \"nicknames\" section of config.json.");
        });
    }

private:
    /// Records a nickname change, if it is one, and says which row it wrote.
    ///
    /// Shared by the gateway event and the startup sweep, because "is this
    /// different from what we last saw" is the same question either way
    /// (docs/features/Nicknames.md §3).
    auto record(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname, nickname_source source)
        -> std::optional<std::int64_t> {
        const auto latest = store_.latest(guild_id, user_id);
        if (!is_new_nickname(latest, nickname)) return std::nullopt;

        const std::int64_t row = store_.record({.guild_id = guild_id,
                                                .user_id = user_id,
                                                .nickname = nickname,
                                                .changed_at = bot_->clock().now(),
                                                // Nobody yet: the audit log fills this in if it can.
                                                .changed_by = std::nullopt,
                                                .source = source,
                                                .imported_raw = {}});

        util::log().info("{} in guild {} is now called {} (recorded as {})", user_id, guild_id,
                         nickname ? std::format("\"{}\"", *nickname) : "nothing", to_string(source));
        return row;
    }

    /// A nickname change seen on the gateway.
    auto on_member_update(const dpp::guild_member& member) -> void {
        // DPP's cached member is already the new one by the time this runs,
        // so "what were they called before" can only come from our own
        // history.
        const std::string current = member.get_nickname();
        const std::optional<std::string> nickname = current.empty() ? std::nullopt : std::optional(current);

        // A change the bot just made is already in the history with the
        // invoker against it, and recording it again would lose that
        // (docs/features/Nicknames.md §3).
        if (pending_.claim(member.guild_id, member.user_id, nickname, bot_->clock().now())) {
            util::log().debug("member update for {} in guild {} is the change /nickname just made", member.user_id, member.guild_id);
            return;
        }

        const auto row = record(member.guild_id, member.user_id, nickname, nickname_source::seen);
        if (!row) {
            // Member updates fire for roles, timeouts and avatars too, so most
            // of them are not about a nickname at all.
            util::log().trace("member update for {} in guild {} changed no nickname", member.user_id, member.guild_id);
            return;
        }

        // Recording never waits on attribution, so this is the only thing
        // that notices the audit entry never turning up
        // (docs/features/Nicknames.md §3).
        attribute_later(member.guild_id, member.user_id, *row);
    }

    /// Asks Discord for the audit log a little later, for the one row it was
    /// hoping to attribute.
    ///
    /// The safety net for a gateway entry that never arrived — a reconnect, a
    /// dropped event (docs/features/Nicknames.md §3). Costs one API call per
    /// change that is still unattributed when it runs, which is normally none
    /// of them.
    auto attribute_later(dpp::snowflake guild_id, dpp::snowflake user_id, std::int64_t row) -> void {
        bot_->after(audit_fallback_delay, "the audit log fallback", [this, guild_id, user_id, row] {
            const auto waiting = store_.find(row);
            if (!waiting || waiting->changed_by) {
                // The gateway entry arrived, which is the ordinary path.
                return;
            }

            util::log().debug("no audit entry arrived for nickname row {}; asking Discord", row);
            bot_->cluster().guild_auditlog_get(guild_id, 0, dpp::aut_member_update, 0, 0, audit_fallback_entries,
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

    /// An audit entry that may name who made a change already recorded.
    auto on_audit_entry(const dpp::audit_entry& entry, dpp::snowflake guild_id) -> void {
        if (entry.type != dpp::aut_member_update || guild_id.empty()) return;

        const auto change = nickname_change_in(entry);
        if (!change) return;

        const std::optional<std::string> nickname = audit_nickname(change->new_value);
        const auto row = store_.unattributed(guild_id, entry.target_id, nickname, bot_->clock().now(), pending_nickname_ttl);
        if (!row) {
            util::log().debug("audit entry {} names no change we are still waiting to attribute", entry.id);
            return;
        }

        if (!may_attribute(*row, entry.user_id, bot_->me().id)) {
            // Discord names the bot whenever the bot called the API, which
            // would overwrite the one attribution that was never in doubt.
            util::log().debug("audit entry {} attributes a change to the bot itself; leaving row {} alone", entry.id, row->id);
            return;
        }

        if (store_.attribute(row->id, entry.user_id, nickname_source::audit_log)) {
            util::log().info("{}'s nickname change in guild {} was made by {}", row->user_id, guild_id, entry.user_id);
        }
    }

    /// Writes down nicknames that changed while the bot was not running.
    ///
    /// Changes made while the bot was not running have nobody to attribute
    /// them to, which is why they are marked as their own source rather than
    /// guessed at (docs/features/Nicknames.md §3).
    auto reconcile(const dpp::guild& guild) -> void {
        int recorded = 0;
        for (const auto& [user_id, member] : guild.members) {
            const std::string current = member.get_nickname();
            if (record(guild.id, user_id, current.empty() ? std::nullopt : std::optional(current), nickname_source::startup)) {
                ++recorded;
            }
        }

        util::log().debug("{}: checked {} member(s) for nickname changes made while offline, recorded {}", guild.name, guild.members.size(),
                          recorded);
    }

    modules::host* bot_;
    nicknames::nicknames_config config_;
    nickname_store store_;
    pending_nicknames pending_;
    history_panel panel_;
};

} // namespace

auto nicknames_schema() noexcept -> db::module_schema {
    return {.module = module_name, .steps = nicknames_steps};
}

} // namespace latibot::events

namespace latibot::nicknames {

auto make_module(modules::host& bot) -> std::unique_ptr<modules::module> {
    return std::make_unique<events::nicknames_module>(bot);
}

auto config_defaults() -> nlohmann::ordered_json {
    return nicknames_section().defaults();
}

} // namespace latibot::nicknames
