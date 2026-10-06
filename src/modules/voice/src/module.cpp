#include "voice/module.hpp"

#include "core/commands/registry.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/host.hpp"
#include "core/util/log.hpp"
#include "dpp_voice_output.hpp"
#include "join_command.hpp"
#include "voice/services.hpp"
#include "voice/voice_mixer.hpp"
#include "voice/voice_sessions.hpp"
#include "voice/voice_state.hpp"
#include "voice_command.hpp"

#include <dpp/dpp.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::voice {
namespace {

constexpr std::string_view module_name = "voice";

class voice_module final : public modules::module, public services {
public:
    explicit voice_module(modules::host& bot) : bot_(&bot), output_(bot.cluster()), mixer_(output_), auto_leave_(bot.clock()) {}

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }

    auto offer(modules::capability_registry& offered) -> void override { offered.offer<services>(*this, module_name); }

    auto start(modules::host& bot) -> void override {
        bot.slash_commands().add(std::make_unique<commands::join_command>());
        bot.slash_commands().add(std::make_unique<commands::leave_command>());
        bot.slash_commands().add(std::make_unique<commands::voice_command>(sessions_, bot.settings()));

        // The hooks run after the mixer on both: a new connection has lost
        // what the old one queued, and music's markers are the mixer's to
        // hand on (docs/features/Voice_Channels.md §3).
        bot.listen(bot.cluster().on_voice_ready, "voice: connections ready", [this](const dpp::voice_ready_t& event) {
            if (event.voice_client == nullptr) return;
            const dpp::snowflake guild = event.voice_client->server_id;
            mixer_.on_ready(guild);
            for (const auto& hook : on_ready_) {
                hook(guild);
            }
        });
        bot.listen(bot.cluster().on_voice_track_marker, "voice: markers reached", [this](const dpp::voice_track_marker_t& event) {
            if (event.voice_client == nullptr) return;
            const dpp::snowflake guild = event.voice_client->server_id;
            mixer_.on_marker(guild, event.track_meta);
            for (const auto& hook : on_marker_) {
                hook(guild, event.track_meta);
            }
        });
        bot.listen(bot.cluster().on_voice_state_update, "voice: voice states",
                   [this](const dpp::voice_state_update_t& event) { on_voice_state(event.state); });

        // Keeps a few seconds queued on each connection playing
        // (docs/features/Music.md §4.2). Most ticks find nothing to do.
        bot.every(std::chrono::seconds{1}, "feeding the mixer", [this] { mixer_.tick(); });

        // Leaving a voice channel nobody else is in, once its guild's grace
        // has passed (docs/features/Voice_Channels.md §2.3). Leaving is the
        // bot's own voice state changing, which on_voice_state tidies up
        // after.
        bot.every(events::auto_leave_tick, "the voice auto-leave check", [this] {
            const auto grace = [this](dpp::snowflake guild) { return commands::voice_grace_for(bot_->settings(), guild); };
            for (const dpp::snowflake guild : auto_leave_.due(grace)) {
                dpp::discord_client* shard = discord::shard_for(bot_->cluster(), guild);
                if (shard == nullptr) continue;
                shard->disconnect_voice(guild);
                util::log().info("left voice in guild {}: nobody else was there for {}", guild, grace(guild));
            }
        });
    }

    // services
    [[nodiscard]] auto mixer() -> audio::voice_mixer& override { return mixer_; }
    [[nodiscard]] auto sessions() -> events::voice_sessions& override { return sessions_; }
    auto on_left(std::function<void(dpp::snowflake)> hook) -> void override { on_left_.push_back(std::move(hook)); }
    auto on_ready(std::function<void(dpp::snowflake)> hook) -> void override { on_ready_.push_back(std::move(hook)); }
    auto on_marker(std::function<void(dpp::snowflake, const std::string&)> hook) -> void override { on_marker_.push_back(std::move(hook)); }

private:
    /// Someone's voice state changed, the bot's included. Tidies up after the
    /// bot leaves a channel, however that happened, and tells the auto-leave
    /// check whether it is on its own (docs/features/Voice_Channels.md §2.3).
    auto on_voice_state(const dpp::voicestate& state) -> void {
        const dpp::snowflake guild = state.guild_id;
        const dpp::snowflake me = bot_->me().id;
        const bool about_the_bot = state.user_id == me;

        if (about_the_bot && state.channel_id.empty()) {
            // Left, whichever way: /leave, /voice stop, the auto-leave, being
            // disconnected by a moderator, or the connection dropping. The
            // session first, then speech and music, then the mixer (K9).
            if (sessions_.end(guild)) util::log().info("voice session in guild {} ended", guild);
            for (const auto& hook : on_left_) {
                hook(guild);
            }
            mixer_.forget(guild);
            auto_leave_.forget(guild);
            return;
        }
        if (about_the_bot) sessions_.moved(guild, state.channel_id);

        // DPP has already updated its cache for this change, so counting from
        // it sees the channel as it is now.
        const dpp::snowflake channel = about_the_bot ? state.channel_id : discord::bot_voice_channel(bot_->cluster(), guild);
        auto_leave_.observe(guild, !channel.empty(), discord::humans_in(guild, channel, me));
    }

    modules::host* bot_;
    discord::dpp_voice_output output_;
    audio::voice_mixer mixer_;
    events::voice_sessions sessions_;
    events::auto_leave auto_leave_;
    std::vector<std::function<void(dpp::snowflake)>> on_left_;
    std::vector<std::function<void(dpp::snowflake)>> on_ready_;
    std::vector<std::function<void(dpp::snowflake, const std::string&)>> on_marker_;
};

} // namespace

auto required(const modules::capability_registry& offered) -> services& {
    auto* found = offered.find<services>();
    if (found == nullptr) throw std::logic_error("a module that requires voice started without it");
    return *found;
}

auto make_module(modules::host& bot) -> std::unique_ptr<modules::module> {
    return std::make_unique<voice_module>(bot);
}

} // namespace latibot::voice
