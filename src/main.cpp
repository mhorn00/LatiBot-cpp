#include "core/bot.hpp"
#include "core/config/bootstrap.hpp"
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

        const auto settings = latibot::config::bootstrap::load(config_path);
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
