#pragma once

#include "core/capabilities/speech.hpp"

#include <dpp/snowflake.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace latibot::testing {

/// The `speech` capability, for the language model's tests: which text
/// channels are a voice session's, and what it was asked to say. DECtalk's
/// own tests cover what the real one does with it.
class mock_speech final : public capabilities::speech {
public:
    /// Each guild's voice session's text channel.
    std::map<dpp::snowflake, dpp::snowflake> session_channels;

    struct said {
        dpp::snowflake guild;
        dpp::snowflake for_user;
        std::string text;
    };
    std::vector<said> spoken;

    /// What `prepare_for_model` puts in front, so a test can tell prepared
    /// text from what the model wrote.
    std::string prepared_mark = "(prepared) ";

    [[nodiscard]] auto speaks_in(dpp::snowflake guild, dpp::snowflake text_channel) const -> bool override {
        const auto found = session_channels.find(guild);
        return found != session_channels.end() && found->second == text_channel;
    }

    [[nodiscard]] auto prepare_for_model(std::string_view text, dpp::snowflake /*guild*/) const -> std::string override {
        return prepared_mark + std::string(text);
    }

    auto say(dpp::snowflake guild, dpp::snowflake for_user, std::string text) -> dpp::task<void> override {
        spoken.push_back({.guild = guild, .for_user = for_user, .text = std::move(text)});
        co_return;
    }

    [[nodiscard]] auto guide_for_model() const -> std::string override { return "## Speaking\nSay it plainly."; }
};

} // namespace latibot::testing
