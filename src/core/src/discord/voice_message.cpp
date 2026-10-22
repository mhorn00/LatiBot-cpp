#include "core/discord/voice_message.hpp"

#include "core/discord/raw_api.hpp"
#include "core/ports/http_client.hpp"

#include <dpp/discordevents.h>
#include <dpp/dispatcher.h>
#include <dpp/json.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <utility>

namespace latibot::discord {

auto waveform(std::span<const std::int16_t> samples, std::size_t buckets) -> std::vector<std::uint8_t> {
    if (samples.empty() || buckets == 0) return {};
    buckets = std::min(buckets, samples.size());

    std::vector<int> peaks(buckets, 0);
    for (std::size_t bucket = 0; bucket < buckets; ++bucket) {
        // Integer bounds, so every sample lands in exactly one bucket.
        const std::size_t begin = bucket * samples.size() / buckets;
        const std::size_t end = (bucket + 1) * samples.size() / buckets;
        for (std::size_t i = begin; i < end; ++i) {
            peaks[bucket] = std::max(peaks[bucket], std::abs(static_cast<int>(samples[i])));
        }
    }

    const int loudest = *std::ranges::max_element(peaks);
    std::vector<std::uint8_t> shape(buckets, 0);
    if (loudest == 0) return shape;
    for (std::size_t bucket = 0; bucket < buckets; ++bucket) {
        shape[bucket] = static_cast<std::uint8_t>(peaks[bucket] * 255 / loudest);
    }
    return shape;
}

auto waveform_base64(std::span<const std::int16_t> samples) -> std::string {
    const std::vector<std::uint8_t> shape = waveform(samples);
    return dpp::base64_encode(shape.data(), static_cast<unsigned int>(shape.size()));
}

auto voice_message_response(std::uint64_t flags, std::chrono::milliseconds duration, std::string_view waveform, std::string_view filename)
    -> std::string {
    constexpr int channel_message_with_source = 4;
    const nlohmann::json payload{
        {"type", channel_message_with_source},
        {"data",
         {{"flags", flags | voice_message_flag},
          {"attachments", nlohmann::json::array({{{"id", 0},
                                                  {"filename", filename},
                                                  {"duration_secs", static_cast<double>(duration.count()) / 1000.0},
                                                  {"waveform", waveform}}})}}}};
    return payload.dump();
}

auto voice_message_form(std::string_view payload_json, const voice_message& message) -> form {
    // A boundary is any line that appears nowhere in the parts; Ogg is
    // binary, so it is checked rather than assumed.
    std::string boundary = "latibot-voice-message";
    for (int tries = 0; payload_json.contains(boundary) || message.file.contains(boundary); ++tries) {
        boundary = std::format("latibot-voice-message-{}", tries);
    }

    std::string body;
    body.reserve(message.file.size() + payload_json.size() + 512);
    body +=
        std::format("--{}\r\nContent-Disposition: form-data; name=\"payload_json\"\r\nContent-Type: application/json\r\n\r\n", boundary);
    body += payload_json;
    body += std::format("\r\n--{}\r\nContent-Disposition: form-data; name=\"files[0]\"; filename=\"{}\"\r\nContent-Type: {}\r\n\r\n",
                        boundary, message.filename, message.mimetype);
    body += message.file;
    body += std::format("\r\n--{}--\r\n", boundary);
    return form{.body = std::move(body), .content_type = std::format("multipart/form-data; boundary={}", boundary)};
}

auto answer_with_voice_message(ports::http_client& http, const dpp::slashcommand_t& event, std::uint64_t flags, voice_message message)
    -> dpp::task<ports::result<void>> {
    const std::string payload = voice_message_response(flags, message.duration, message.waveform, message.filename);
    form sent = voice_message_form(payload, message);
    auto reply =
        co_await http.send({.url = "https://discord.com" +
                                   build_endpoint(std::format("/interactions/{}/{}/callback", event.command.id.str(), event.command.token)),
                            .method = ports::http_method::post,
                            .body = std::move(sent.body),
                            .content_type = std::move(sent.content_type),
                            .headers = {}});
    if (!reply.has_value()) co_return std::unexpected(std::move(reply.error()));
    if (reply->status >= 400) {
        const nlohmann::json body = nlohmann::json::parse(reply->body, nullptr, false);
        co_return std::unexpected(ports::api_error{.http_status = reply->status, .message = describe_refusal(reply->status, body)});
    }
    co_return {};
}

} // namespace latibot::discord
