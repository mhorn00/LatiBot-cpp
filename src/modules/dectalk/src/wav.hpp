#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace latibot::audio {

/// The size of the header `wav_file` writes.
inline constexpr std::size_t wav_header_size = 44;

/// A complete .wav file holding 16-bit PCM: a 44-byte RIFF header, then the
/// samples, little-endian (src/modules/dectalk/docs/Speech.md §4.4).
[[nodiscard]] auto wav_file(std::span<const std::int16_t> samples, std::uint32_t sample_rate, std::uint8_t channels) -> std::string;

} // namespace latibot::audio
