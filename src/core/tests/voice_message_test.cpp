// Voice messages: the payload that answers a command with one, and the
// waveform Discord draws for it (src/modules/dectalk/docs/Speech.md §4.4,
// src/modules/soundboard/docs/Soundboard.md §3).

#include "core/discord/voice_message.hpp"

#include "mocks/mock_http.hpp"

#include <dpp/dispatcher.h>
#include <dpp/json.h>
#include <dpp/message.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using latibot::discord::voice_message;
using latibot::discord::voice_message_form;
using latibot::discord::voice_message_response;
using latibot::discord::waveform;
using latibot::discord::waveform_base64;
using namespace std::chrono_literals;

TEST_CASE("a voice message answers the interaction with its duration and waveform", "[discord]") {
    const std::string payload = voice_message_response(dpp::m_suppress_notifications, 2500ms, "AAAA", "voice-message.ogg");
    const nlohmann::json parsed = nlohmann::json::parse(payload);

    CHECK(parsed["type"] == 4); // a message, in answer to the command
    CHECK(parsed["data"]["flags"] == (dpp::m_suppress_notifications | 8192U));
    CHECK_FALSE(parsed["data"].contains("content")); // a voice message carries nothing else

    REQUIRE(parsed["data"]["attachments"].size() == 1);
    const nlohmann::json& attachment = parsed["data"]["attachments"][0];
    CHECK(attachment["id"] == 0); // the first file part, files[0]
    CHECK(attachment["filename"] == "voice-message.ogg");
    CHECK(attachment["duration_secs"] == 2.5);
    CHECK(attachment["waveform"] == "AAAA");
}

TEST_CASE("the waveform of silence is flat", "[discord]") {
    const auto shape = waveform(std::vector<std::int16_t>(10000, 0));

    REQUIRE(shape.size() == 256);
    CHECK(shape == std::vector<std::uint8_t>(256, 0));
}

TEST_CASE("the waveform follows where the sound is", "[discord]") {
    // Silence, then a loud stretch, then a quiet one: the Java version would
    // have drawn this as flat noise.
    std::vector<std::int16_t> samples(2560, 0);
    for (std::size_t i = 1024; i < 2048; ++i) {
        samples[i] = (i % 2 == 0) ? 20000 : -20000;
    }
    for (std::size_t i = 2048; i < 2560; ++i) {
        samples[i] = (i % 2 == 0) ? 5000 : -5000;
    }

    const auto shape = waveform(samples);

    REQUIRE(shape.size() == 256);
    CHECK(shape[0] == 0);
    CHECK(shape[101] == 0);
    CHECK(shape[103] == 255);
    CHECK(shape[203] == 255);
    CHECK(shape[210] == 63); // 5000 of 20000
    CHECK(shape[255] == 63);
}

TEST_CASE("a waveform of fewer samples than bars has one bar per sample", "[discord]") {
    CHECK(waveform(std::vector<std::int16_t>{0, 100, -200}) == std::vector<std::uint8_t>{0, 127, 255});
    CHECK(waveform({}).empty());
}

TEST_CASE("the waveform is sent as base64 of its 256 bytes", "[discord]") {
    const std::string encoded = waveform_base64(std::vector<std::int16_t>(10000, 0));

    // 256 bytes encode to 344 characters, padding included, and all-zero
    // bytes encode to 'A's.
    REQUIRE(encoded.size() == 344);
    CHECK(encoded.find_first_not_of("A=") == std::string::npos);
}

namespace {

auto clip() -> voice_message {
    return {.file = "OggS and the rest", .filename = "voice-message.ogg", .mimetype = "audio/ogg", .duration = 1500ms, .waveform = "AAAA"};
}

auto command(std::uint64_t id, std::string token) -> dpp::slashcommand_t {
    dpp::slashcommand_t event;
    event.command.id = dpp::snowflake{id};
    event.command.token = std::move(token);
    return event;
}

} // namespace

TEST_CASE("the voice message's file is files[0], the part attachment 0 names", "[discord]") {
    // DPP names a lone file's part "file", which Discord does not match to
    // attachment 0 and refuses as an invalid form body.
    const auto made = voice_message_form(R"({"type":4})", clip());

    REQUIRE(made.content_type.starts_with("multipart/form-data; boundary="));
    const std::string boundary = made.content_type.substr(std::string_view("multipart/form-data; boundary=").size());
    CHECK(made.body.starts_with("--" + boundary + "\r\n"));
    CHECK(
        made.body.contains("Content-Disposition: form-data; name=\"payload_json\"\r\nContent-Type: application/json\r\n\r\n"
                           R"({"type":4})"
                           "\r\n--" +
                           boundary + "\r\n"));
    CHECK(
        made.body.contains("Content-Disposition: form-data; name=\"files[0]\"; filename=\"voice-message.ogg\"\r\n"
                           "Content-Type: audio/ogg\r\n\r\nOggS and the rest\r\n"));
    CHECK(made.body.ends_with("\r\n--" + boundary + "--\r\n"));
}

TEST_CASE("a boundary that turns up in the file is not used", "[discord]") {
    voice_message tricky = clip();
    tricky.file = "--latibot-voice-message";

    const auto made = voice_message_form("{}", tricky);

    CHECK(made.content_type == "multipart/form-data; boundary=latibot-voice-message-0");
}

TEST_CASE("a voice message answers the interaction at its callback, with the form", "[discord]") {
    latibot::testing::mock_http http;
    http.queue(204, "");

    const auto sent =
        latibot::discord::answer_with_voice_message(http, command(123, "abc"), dpp::m_suppress_notifications, clip()).sync_wait();

    CHECK(sent.has_value());
    REQUIRE(http.requests.size() == 1);
    const auto& request = http.requests.front();
    CHECK(request.url == "https://discord.com/api/v10/interactions/123/abc/callback");
    CHECK(request.method == latibot::ports::http_method::post);
    CHECK(request.content_type.starts_with("multipart/form-data; boundary="));
    CHECK(request.body.contains("name=\"files[0]\""));
    CHECK(request.body.contains(R"("duration_secs":1.5)"));
    // The interaction's token is what lets it answer; the bot's is never sent.
    CHECK(request.headers.empty());
}

TEST_CASE("a refused voice message says which field Discord refused", "[discord]") {
    latibot::testing::mock_http http;
    http.queue(400, R"({"code": 50035, "message": "Invalid Form Body",
                        "errors": {"data": {"attachments": {"0": {"_errors": [{"code": "X", "message": "Unknown file"}]}}}}})");

    const auto sent = latibot::discord::answer_with_voice_message(http, command(1, "t"), 0, clip()).sync_wait();

    REQUIRE_FALSE(sent.has_value());
    CHECK(sent.error().http_status == 400);
    CHECK(sent.error().message == "Discord returned 400: Invalid Form Body; data.attachments.0: Unknown file");
}

TEST_CASE("a voice message posted in a channel goes as the bot, with the form", "[discord]") {
    latibot::testing::mock_http http;
    http.queue(200, R"({"id": "9876"})");

    const auto sent = latibot::discord::post_voice_message(http, "the-token", dpp::snowflake{555}, 0, clip()).sync_wait();

    REQUIRE(sent.has_value());
    CHECK(*sent == dpp::snowflake{9876});
    REQUIRE(http.requests.size() == 1);
    const auto& request = http.requests.front();
    CHECK(request.url == "https://discord.com/api/v10/channels/555/messages");
    CHECK(request.body.contains("name=\"files[0]\""));
    CHECK(request.body.contains(R"("flags":8192)"));
    CHECK_FALSE(request.body.contains(R"("type")"));
    REQUIRE(request.headers.size() == 1);
    CHECK(request.headers[0] == std::pair<std::string, std::string>{"Authorization", "Bot the-token"});
}

TEST_CASE("a voice message the channel refuses says why", "[discord]") {
    latibot::testing::mock_http http;
    http.queue(403, R"({"code": 50013, "message": "Missing Permissions"})");
    const auto sent = latibot::discord::post_voice_message(http, "t", dpp::snowflake{1}, 0, clip()).sync_wait();
    REQUIRE_FALSE(sent.has_value());
    CHECK(sent.error().http_status == 403);
}
