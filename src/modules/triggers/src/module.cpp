#include "triggers/module.hpp"

#include "core/commands/registry.hpp"
#include "core/events/stage_order.hpp"
#include "core/modules/host.hpp"
#include "core/ui/panel_routes.hpp"
#include "trigger_command.hpp"
#include "triggers.hpp"

#include <array>
#include <memory>
#include <string_view>

namespace latibot::events {
namespace {

// Append only: once a version has shipped, its SQL is never edited, and a
// change becomes the next version.
constexpr std::array<db::migration, 1> triggers_steps{{
    {.version = 1, .name = "triggers", .sql = R"sql(
        CREATE TABLE triggers (
            id              INTEGER PRIMARY KEY,
            guild_id        INTEGER NOT NULL,
            pattern         TEXT    NOT NULL,
            match_mode      TEXT    NOT NULL,
            cooldown_s      INTEGER NOT NULL,
            enabled         INTEGER NOT NULL,

            -- Hearing a bot is not the same as answering it, so each trigger
            -- opts in.
            respond_to_bots INTEGER NOT NULL DEFAULT 0,

            -- How its replies are posted, as Discord's message flags: 4096 is
            -- SUPPRESS_NOTIFICATIONS, 4 is SUPPRESS_EMBEDS.
            message_flags   INTEGER NOT NULL DEFAULT 4096
        );

        CREATE INDEX triggers_by_guild ON triggers (guild_id);

        -- Rows rather than a list column, so each response can carry its own
        -- weight and be edited on its own. rowid order is the order they were
        -- added, which is the order the command and the panel show.
        CREATE TABLE trigger_responses (
            trigger_id INTEGER NOT NULL REFERENCES triggers (id) ON DELETE CASCADE,
            response   TEXT    NOT NULL,
            weight     INTEGER NOT NULL DEFAULT 1
        );

        CREATE INDEX trigger_responses_by_trigger ON trigger_responses (trigger_id);
     )sql"},
}};

constexpr std::string_view module_name = "triggers";

class triggers_module final : public modules::module {
public:
    explicit triggers_module(modules::host& bot) : store_(bot.database()), panel_(store_), responder_(store_, bot.clock()) {}

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }
    [[nodiscard]] auto schema() const -> std::span<const db::migration> override { return triggers_steps; }

    auto start(modules::host& bot) -> void override {
        bot.slash_commands().add(std::make_unique<commands::trigger_command>(store_));
        bot.panels().add(
            panel_,
            {commands::trigger_list_view, commands::trigger_panel_view, commands::trigger_pick_view, commands::trigger_edit_view,
             commands::trigger_delete_view, commands::trigger_confirm_view, commands::trigger_add_view, commands::trigger_form_view,
             commands::trigger_toggle_view, commands::trigger_bots_view, commands::trigger_silent_view, commands::trigger_previews_view},
            module_name);
        // After link replacement and before the language model, which a
        // reply here keeps quiet (docs/features/Message_Pipeline.md §2.2).
        bot.add_stage(stage_order::reply, "triggers", [this](const incoming_message& message) { return responder_(message); });
    }

private:
    trigger_store store_;
    commands::trigger_panel panel_;
    trigger_responder responder_;
};

} // namespace

auto triggers_schema() noexcept -> db::module_schema {
    return {.module = module_name, .steps = triggers_steps};
}

} // namespace latibot::events

namespace latibot::triggers {

auto make_module(modules::host& bot) -> std::unique_ptr<modules::module> {
    return std::make_unique<events::triggers_module>(bot);
}

} // namespace latibot::triggers
