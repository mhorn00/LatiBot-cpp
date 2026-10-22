#include "wav.hpp"

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

} // namespace latibot::audio
