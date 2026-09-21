#include "core/bot.hpp"
#include "core/util/env.hpp"

#include <exception>
#include <iostream>

// Everything is caught below; clang-tidy still flags main because writing to
// std::cerr inside a handler could itself throw. There is nowhere left to
// report that, so terminating is the correct outcome.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    try {
        const auto token = latibot::util::env_var("DISCORD_BOT_TOKEN");
        if (!token || token->empty()) {
            std::cerr << "DISCORD_BOT_TOKEN environment variable not set\n";
            return 1;
        }

        latibot::bot bot(*token);
        bot.run();
        return 0;
    } catch (const std::exception& e) {
        // Nothing above main can report this, and an escaped exception would
        // terminate without a usable message.
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "fatal: unknown error\n";
        return 1;
    }
}
