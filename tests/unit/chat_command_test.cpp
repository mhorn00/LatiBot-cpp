// The voice message /chat answers with (plan §12.8).

#include "core/commands/chat.hpp"

#include <dpp/json.h>
#include <dpp/message.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("a voice message answers the interaction with its duration and waveform", "[commands]") {
    const std::string payload = latibot::commands::voice_message_response(dpp::m_suppress_notifications, 2500ms, "AAAA");
    const nlohmann::json parsed = nlohmann::json::parse(payload);

    CHECK(parsed["type"] == 4); // a message, in answer to the command
    CHECK(parsed["data"]["flags"] == (dpp::m_suppress_notifications | 8192U));
    CHECK_FALSE(parsed["data"].contains("content")); // a voice message carries nothing else

    REQUIRE(parsed["data"]["attachments"].size() == 1);
    const nlohmann::json& attachment = parsed["data"]["attachments"][0];
    CHECK(attachment["id"] == 0); // the first file part, files[0]
    CHECK(attachment["filename"] == "voice-message.wav");
    CHECK(attachment["duration_secs"] == 2.5);
    CHECK(attachment["waveform"] == "AAAA");
}
