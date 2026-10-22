#pragma once

#include "core/commands/registry.hpp"

#include <dpp/snowflake.h>

#include <functional>
#include <string>
#include <string_view>

namespace latibot::ports {
class http_client;
}
namespace latibot::soundboard {
class clip_maker;
class sound_catalog;
} // namespace latibot::soundboard

namespace latibot::commands {

/// What `/soundboard` uses.
struct soundboard_services {
    const soundboard::sound_catalog* sounds = nullptr;
    soundboard::clip_maker* clips = nullptr;
    /// What the voice message is sent through.
    ports::http_client* http = nullptr;

    /// Told whenever someone looks through a server's sounds, so that ones
    /// added since they were last read turn up; may be empty.
    std::function<void(dpp::snowflake)> looked_in;
};

/// Answers with a soundboard sound as a voice message
/// (src/modules/soundboard/docs/Soundboard.md §3). Needs no voice channel.
class soundboard_command final : public command {
public:
    explicit soundboard_command(soundboard_services services);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;
    auto autocomplete(const dpp::autocomplete_t& event) const -> void override;

private:
    command_info info_;
    soundboard_services services_;
};

/// What `/soundboard` says to a sound it cannot find.
[[nodiscard]] auto unknown_sound_reply(std::string_view wanted) -> std::string;

} // namespace latibot::commands
