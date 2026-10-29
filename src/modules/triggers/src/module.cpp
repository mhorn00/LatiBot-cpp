#include "triggers/module.hpp"

#include "core/capabilities/model_replies.hpp"
#include "core/capabilities/model_tools.hpp"
#include "core/commands/registry.hpp"
#include "core/events/stage_order.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/host.hpp"
#include "core/ui/panel_routes.hpp"
#include "core/util/log.hpp"
#include "fire_command.hpp"
#include "trigger_command.hpp"
#include "trigger_tools.hpp"
#include "triggers.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace latibot::events {
namespace {

// Append only: once a version has shipped, its SQL is never edited, and a
// change becomes the next version.
constexpr std::array<db::migration, 2> triggers_steps{{
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
    {.version = 2, .name = "model and analysis triggers, conditions, built-ins and replies", .sql = R"sql(
        -- Whether its answer is a Discord reply to the message. Off, as
        -- every trigger posted before.
        ALTER TABLE triggers ADD COLUMN reply INTEGER NOT NULL DEFAULT 0;

        -- A model trigger asks the language model to say something rather
        -- than picking a response (src/modules/triggers/docs/Triggers.md
        -- 2.6): what to say, the chance it answers a match, 0 to 1, and who
        -- answers, a persona's id or 0 for LatiBot. Empty prompt: a trigger
        -- with responses, as before.
        ALTER TABLE triggers ADD COLUMN prompt     TEXT    NOT NULL DEFAULT '';
        ALTER TABLE triggers ADD COLUMN chance     REAL    NOT NULL DEFAULT 1.0;
        ALTER TABLE triggers ADD COLUMN persona_id INTEGER NOT NULL DEFAULT 0;

        -- An analysis asks the check model this yes-or-no question about a
        -- message that matched before the model answers (2.6). What sets it
        -- off: 'text', its pattern; 'addressed', an @mention or LatiBot's
        -- name first; 'reply', a reply to whoever answers it.
        ALTER TABLE triggers ADD COLUMN question  TEXT NOT NULL DEFAULT '';
        ALTER TABLE triggers ADD COLUMN condition TEXT NOT NULL DEFAULT 'text';

        -- Which built-in trigger it is, NULL for one someone made: what
        -- LatiBot answered on its own before the triggers took over (2.8).
        -- One of each per guild; a built-in is never removed.
        ALTER TABLE triggers ADD COLUMN builtin TEXT;
        CREATE UNIQUE INDEX triggers_builtin ON triggers (guild_id, builtin) WHERE builtin IS NOT NULL;

        -- Which older tables have been copied in, once each: the language
        -- model kept its advanced triggers in llm_triggers before they moved
        -- here, and the copy happens at startup, since that table may not
        -- exist yet when this runs.
        CREATE TABLE trigger_imports (
            source TEXT PRIMARY KEY
        );
     )sql"},
}};

constexpr std::string_view module_name = "triggers";

class triggers_module final : public modules::module {
public:
    explicit triggers_module(modules::host& bot) : store_(bot.database()) {}

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }
    [[nodiscard]] auto schema() const -> std::span<const db::migration> override { return triggers_steps; }

    auto start(modules::host& bot) -> void override {
        // Model triggers, when the language model is built in; null
        // otherwise, and then no trigger asks it anything
        // (src/modules/triggers/docs/Triggers.md §2.6).
        auto* model = bot.capabilities().find<capabilities::model_replies>();
        // And with it, when LatiBot and its personas answer at all: being
        // addressed, replied to and named are built-in triggers here, and the
        // model keeps only conversation mode (§2.8).
        if (model != nullptr) model->take_over_answering();
        if (const int copied = store_.import_llm_triggers(); copied > 0) {
            util::log().info("copied {} advanced trigger(s) from the language model's old table into triggers", copied);
        }

        responder_.emplace(store_, bot.clock(), std::function<std::uint64_t()>{}, model);

        bot.slash_commands().add(std::make_unique<commands::trigger_command>(store_, model));
        // A message's Apps menu: fire a trigger on it by hand (§2.9). It is
        // its own menu's router too.
        auto fire = std::make_unique<commands::fire_command>(
            store_, *responder_, model, [&bot] { return bot.me().id; },
            [&bot](std::vector<action> actions) {
                for (action& step : actions) {
                    if (auto* message = std::get_if<send_message>(&step)) {
                        bot.post(std::move(*message));
                    } else if (auto* task = std::get_if<background_task>(&step)) {
                        bot.detach(task->run(), task->what);
                    }
                }
            });
        commands::fire_command& fire_router = *fire;
        bot.slash_commands().add(std::move(fire));
        bot.panels().add(fire_router, {commands::trigger_fire_view}, module_name);
        // The language model's own /trigger, when it is built in.
        if (auto* tools = bot.capabilities().find<capabilities::model_tools>()) add_trigger_tools(*tools, store_, model);
        panel_.emplace(store_, model);
        bot.panels().add(
            *panel_,
            {commands::trigger_list_view, commands::trigger_panel_view, commands::trigger_pick_view, commands::trigger_edit_view,
             commands::trigger_delete_view, commands::trigger_confirm_view, commands::trigger_add_view, commands::trigger_add_model_view,
             commands::trigger_form_view, commands::trigger_command_form_view, commands::trigger_toggle_view, commands::trigger_bots_view,
             commands::trigger_silent_view, commands::trigger_previews_view, commands::trigger_reply_view},
            module_name);

        // After link replacement and before the language model, which a
        // reply here keeps quiet (src/core/docs/Message_Pipeline.md §2.2).
        // With the model, it hears the bot's own messages too, since LatiBot
        // and its personas set off each other's model triggers.
        bot.add_stage(
            stage_order::reply, "triggers", [this](const incoming_message& message) { return (*responder_)(message); },
            model == nullptr ? hears::others : hears::own_too);
    }

private:
    trigger_store store_;
    // Made in start, once the model can be looked up.
    std::optional<commands::trigger_panel> panel_;
    std::optional<trigger_responder> responder_;
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
