#pragma once

#include <dpp/coro/task.h>
#include <dpp/snowflake.h>

#include <string>
#include <string_view>
#include <vector>

namespace latibot::capabilities {

// A capability is how one feature reaches another that may not be built in
// (docs/modules/Module_Plan_Final.md §5.2). The feature that has it offers
// an implementation; the one that wants it asks, and copes with nothing.

/// Saying text aloud in a server's voice channel, for the language model's
/// replies (src/modules/llm/docs/Language_Model.md §2.5). Offered by DECtalk's
/// speech; without it, the model never speaks.
class speech {
public:
    virtual ~speech() = default;

    speech() = default;
    speech(const speech&) = delete;
    auto operator=(const speech&) -> speech& = delete;

    /// Whether what is posted in `text_channel` is also spoken: it is the
    /// channel of the server's voice session.
    [[nodiscard]] virtual auto speaks_in(dpp::snowflake guild, dpp::snowflake text_channel) const -> bool = 0;

    /// `text` as it will be said, and so as it is posted: the inline commands
    /// the model may not use taken out.
    [[nodiscard]] virtual auto prepare_for_model(std::string_view text, dpp::snowflake guild) const -> std::string = 0;

    /// Says `text` within the server's limits, queued under `for_user` so
    /// they can stop it, in `voice`: one of `voices`, or the default voice
    /// when it is blank or no longer one of them.
    virtual auto say(dpp::snowflake guild, dpp::snowflake for_user, std::string text, std::string_view voice) -> dpp::task<void> = 0;

    /// The names of the voices `say` can use in `guild`, the default first,
    /// for a persona's voice (src/modules/llm/docs/Language_Model.md §2.11).
    [[nodiscard]] virtual auto voices(dpp::snowflake guild) const -> std::vector<std::string> = 0;

    /// What the model is told about speaking, as a section of its
    /// instructions: the inline commands and voices it may use.
    [[nodiscard]] virtual auto guide_for_model() const -> std::string = 0;
};

} // namespace latibot::capabilities
