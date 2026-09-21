#pragma once

#include <dpp/dpp.h>

#include <string>

namespace latibot {

/// Owns the Discord connection and, as the port progresses, the subsystems
/// hanging off it (database, commands, audio, LLM).
///
/// Everything testable lives outside this class: `bot` is the shell that wires
/// DPP events to core functions (plan v4 §17.3).
class bot {
public:
    /// `token` is copied by DPP, which takes it as a const reference.
    explicit bot(const std::string& token);

    bot(const bot&) = delete;
    bot& operator=(const bot&) = delete;

    /// Connects and blocks until the bot shuts down.
    void run();

private:
    void register_events();

    dpp::cluster cluster_;
};

} // namespace latibot
