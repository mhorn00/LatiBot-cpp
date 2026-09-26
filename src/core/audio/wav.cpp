#include "core/audio/wav.hpp"

#include <dpp/discordevents.h>

#include <algorithm>
#include <cstdlib>

namespace latibot::audio {
namespace {

auto put_u16(std::string& out, std::uint16_t value) -> void {
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>((value >> 8U) & 0xFFU));
}

auto put_u32(std::string& out, std::uint32_t value) -> void {
    put_u16(out, static_cast<std::uint16_t>(value & 0xFFFFU));
    put_u16(out, static_cast<std::uint16_t>(value >> 16U));
}

} // namespace

auto wav_file(std::span<const std::int16_t> samples, std::uint32_t sample_rate, std::uint8_t channels) -> std::string {
    constexpr std::uint16_t bits = 16;
    constexpr std::uint16_t pcm_format = 1;
    const auto data_bytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const auto block_align = static_cast<std::uint16_t>(channels * (bits / 8));

    std::string out;
    out.reserve(wav_header_size + data_bytes);
    out += "RIFF";
    put_u32(out, static_cast<std::uint32_t>(wav_header_size - 8) + data_bytes);
    out += "WAVE";
    out += "fmt ";
    put_u32(out, 16); // the size of the fmt chunk that follows
    put_u16(out, pcm_format);
    put_u16(out, channels);
    put_u32(out, sample_rate);
    put_u32(out, sample_rate * block_align);
    put_u16(out, block_align);
    put_u16(out, bits);
    out += "data";
    put_u32(out, data_bytes);

    for (const std::int16_t sample : samples) {
        put_u16(out, static_cast<std::uint16_t>(sample));
    }
    return out;
}

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

} // namespace latibot::audio
