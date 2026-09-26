#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace latibot::audio {

/// Discord's voice format: 48 kHz, stereo, 16-bit.
inline constexpr std::uint32_t discord_sample_rate = 48000;
inline constexpr std::uint8_t discord_channels = 2;

/// Scales every sample by `percent` / 100, clipping rather than wrapping
/// where the result no longer fits. 100 changes nothing; 0 is silence.
auto apply_volume(std::span<std::int16_t> samples, int percent) -> void;

/// Samples quieter than this, either way, count as silence: about -54 dB.
inline constexpr int silence_threshold = 64;

/// Cuts the silence after the last sound down to `keep` samples, or leaves
/// it when there is less than that. All silence becomes empty.
///
/// DECtalk ends every utterance with a few hundred milliseconds of silence,
/// which a queue of utterances would otherwise wait through.
auto trim_trailing_silence(std::vector<std::int16_t>& samples, std::size_t keep) -> void;

/// Converts mono audio at `source_rate` to what Discord takes: 48 kHz, stereo,
/// interleaved (plan §12.3).
///
/// 11025 to 48000 is not a whole ratio (about 4.35), so each output sample
/// is interpolated at its fractional position in the input. Linear
/// interpolation is ample for speech this lo-fi. The output is as long as
/// the input, to the nearest output frame.
[[nodiscard]] auto to_discord(std::span<const std::int16_t> mono, std::uint32_t source_rate) -> std::vector<std::int16_t>;

} // namespace latibot::audio
