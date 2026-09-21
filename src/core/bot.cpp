#include "core/bot.hpp"

#include "core/version.hpp"

#include <string>

namespace latibot {

bot::bot(const std::string& token) : cluster_(token) {
    register_events();
}

void bot::register_events() {
    cluster_.on_log(dpp::utility::cout_logger());

    cluster_.on_slashcommand([](const dpp::slashcommand_t& event) {
        if (event.command.get_command_name() == "ping") {
            event.reply("Pong!");
        }
    });

    cluster_.on_ready([this](const dpp::ready_t&) {
        cluster_.log(dpp::ll_info, "LatiBot " + std::string(version_string()) + " ready");
        if (dpp::run_once<struct register_bot_commands>()) {
            cluster_.global_command_create(
                dpp::slashcommand("ping", "Ping pong test command", cluster_.me.id));
        }
    });
}

void bot::run() {
    cluster_.start(dpp::st_wait);
}

} // namespace latibot
