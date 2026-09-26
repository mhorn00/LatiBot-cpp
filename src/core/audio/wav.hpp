#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace latibot::audio {

/// The size of the header `wav_file` writes.
inline constexpr std::size_t wav_header_size = 44;

/// A complete .wav file holding 16-bit PCM: a 44-byte RIFF header, then the
/// samples, little-endian (plan §12.8).
[[nodiscard]] auto wav_file(std::span<const std::int16_t> samples, std::uint32_t sample_rate, std::uint8_t channels) -> std::string;

/// How many bars Discord draws for a voice message.
inline constexpr std::size_t waveform_buckets = 256;

/// The shape Discord shows for a voice message: the audio cut into `buckets`
/// equal stretches, each the loudest sample in it, scaled so the loudest of
/// all is 255. Silence is all zeros. Fewer samples than buckets gives one
/// bucket per sample.
///
/// The Java bot averaged the signed bytes of the whole file, header and all,
/// which comes out near zero whatever the audio is (plan §12.8).
[[nodiscard]] auto waveform(std::span<const std::int16_t> samples, std::size_t buckets = waveform_buckets) -> std::vector<std::uint8_t>;

/// `waveform`, base64-encoded, as the voice message payload carries it.
[[nodiscard]] auto waveform_base64(std::span<const std::int16_t> samples) -> std::string;

} // namespace latibot::audio
