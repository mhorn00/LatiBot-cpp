#include "core/bot.hpp"
#include "core/config/bootstrap.hpp"
#include "core/util/ca_certificates.hpp"
#include "core/util/env.hpp"
#include "core/util/log.hpp"

#include <exception>
#include <filesystem>

// Everything is caught below; clang-tidy still flags main because writing to
// the log inside a handler could itself throw. There is nowhere left to
// report that, so terminating is the correct outcome.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
    try {
        const std::filesystem::path config_path = argc > 1 ? argv[1] : "config.json";

        // .env is a local-run convenience only, gitignored, and never
        // overrides a variable the real environment already set (plan v4
        // §5.1: secrets still come from the environment, just optionally
        // populated from this file first).
        latibot::util::load_dotenv(".env");

        // After .env, so LATIBOT_LOG_COLOR set there counts; before anything
        // else, so every line from here on looks the same.
        latibot::util::apply_log_colors_from_environment();

        // Before the configuration is read, so that reading it is itself
        // logged at the level asked for.
        if (const auto wanted = latibot::config::log_level_from_environment()) {
            latibot::util::log().set_level(*wanted);
        }

        const auto settings = latibot::config::bootstrap::load(config_path);

        // Applied here rather than only in the bot, so that everything below
        // this line is logged at the level the operator asked for.
        latibot::util::log().set_level(settings.log_level);

        // Before anything opens a connection: the OpenSSL we link has no root
        // certificates of its own, so it needs pointing at the system's.
        latibot::util::use_system_certificates(settings.database_path.parent_path() / "ca-bundle.pem");

        const auto credentials = latibot::config::secrets::from_environment();

        latibot::bot bot(settings, credentials);
        bot.run();
        return 0;
    } catch (const std::exception& error) {
        // The logger's default sink is stderr, so this is visible whether or
        // not the config was ever read.
        latibot::util::log().error("fatal: {}", error.what());
        return 1;
    } catch (...) {
        latibot::util::log().error("fatal: unknown error");
        return 1;
    }
}
