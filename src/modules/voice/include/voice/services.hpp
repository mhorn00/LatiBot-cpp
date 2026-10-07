#pragma once

#include <dpp/snowflake.h>

#include <functional>
#include <string>

namespace latibot::audio {
class voice_mixer;
}
namespace latibot::events {
class voice_sessions;
}
namespace latibot::modules {
class capability_registry;
}

namespace latibot::voice {

/// What voice gives the modules that require it: dectalk and music
/// (docs/modules/Module_Plan_Final.md §4.2, §5.4).
///
/// Voice offers it as a capability, and a module that requires voice finds
/// it with `required`. The hooks keep "the bot left a channel" and the
/// connection's events in one place, voice's, and run in the order they were
/// added, which is the order the modules are built in. They are added only
/// while the bot starts.
class services {
public:
    virtual ~services() = default;

    services() = default;
    services(const services&) = delete;
    auto operator=(const services&) -> services& = delete;

    /// The only writer to a voice connection: speech and music both go
    /// through it (src/modules/music/docs/Music.md §4.2).
    [[nodiscard]] virtual auto mixer() -> audio::voice_mixer& = 0;

    /// Which servers have a voice session, and in which channels.
    [[nodiscard]] virtual auto sessions() -> events::voice_sessions& = 0;

    /// After the bot leaves a server's voice channel, however that happened:
    /// the session has ended, and the mixer forgets the server once every
    /// hook has run (K9: sessions, speech, music, mixer).
    virtual auto on_left(std::function<void(dpp::snowflake guild)> hook) -> void = 0;

    /// A server's voice connection is ready, once the mixer has heard.
    virtual auto on_ready(std::function<void(dpp::snowflake guild)> hook) -> void = 0;

    /// A marker in the audio sent was reached, once the mixer has heard.
    virtual auto on_marker(std::function<void(dpp::snowflake guild, const std::string& marker)> hook) -> void = 0;
};

/// Voice's services, from what the modules offered. For a module that
/// requires voice, which CMake makes sure is built; throws std::logic_error
/// when it is not there all the same.
[[nodiscard]] auto required(const modules::capability_registry& offered) -> services&;

} // namespace latibot::voice
