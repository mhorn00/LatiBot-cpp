#include "emotes/module.hpp"

#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/modules/host.hpp"
#include "core/ui/panel_routes.hpp"
#include "steal_command.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace latibot::events {
namespace {

constexpr std::string_view module_name = "emotes";

class emotes_module final : public modules::module {
public:
    explicit emotes_module(modules::host& bot)
        : panel_(bot.gateway(), bot.http(), bot.clock(), bot.bootstrap().trusted_guilds,
                 [&bot](dpp::task<void> work, std::string what) { bot.detach(std::move(work), std::move(what)); }) {}

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }

    auto start(modules::host& bot) -> void override {
        bot.slash_commands().add(std::make_unique<commands::steal_command>(panel_));
        bot.panels().add(panel_,
                         {commands::steal_page_view, commands::steal_pick_view, commands::steal_name_view, commands::steal_form_view,
                          commands::steal_add_view},
                         module_name);
    }

private:
    commands::steal_panel panel_;
};

} // namespace
} // namespace latibot::events

namespace latibot::emotes {

auto make_module(modules::host& bot) -> std::unique_ptr<modules::module> {
    return std::make_unique<events::emotes_module>(bot);
}

} // namespace latibot::emotes
