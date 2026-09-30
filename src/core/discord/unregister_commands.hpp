#pragma once

#include <string>

namespace latibot::discord {

/// `--unregister-commands`: signs in to Discord as the bot, deletes every
/// slash command it has registered (`commands::unregister_all`), and signs
/// out again.
///
/// Uses the REST API alone and never opens the gateway, so the bot does not
/// come online, no event reaches it, and nothing else of it runs: no
/// database, no timers. Its next ordinary start registers the commands
/// again. True when everything was deleted; what failed is logged.
[[nodiscard]] auto unregister_commands(const std::string& token) -> bool;

} // namespace latibot::discord
