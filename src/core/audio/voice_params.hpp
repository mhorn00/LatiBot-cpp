#pragma once

#include "core/ports/tts_engine.hpp"

#include <span>
#include <string>
#include <string_view>

namespace latibot::audio {

/// One of DECtalk's built-in speakers.
struct builtin_voice {
    std::string_view name;

    /// The inline command that selects it, such as "[:np]".
    std::string_view command;

    /// DECtalk's own name for it, such as "Perfect Paul".
    std::string_view description;
};

/// The ten voices this build of DECtalk has. Chris is missing: the engine
/// only defines him when built with HLSYN or CHANGES_AFTER_V43, and ours is
/// built with neither (plan §2.2).
[[nodiscard]] auto builtin_voices() -> std::span<const builtin_voice>;

/// Case-insensitive; nullptr for a name that is not a built-in voice.
[[nodiscard]] auto find_builtin_voice(std::string_view name) -> const builtin_voice*;

/// The speaking rate DECtalk accepts, in words per minute.
inline constexpr int min_rate = 75;
inline constexpr int max_rate = 600;
inline constexpr int default_rate = 200;

/// The inline commands that put a fresh engine into `settings`: the voice,
/// then the rate, then a custom voice's `[:dv]` edits. An unknown voice falls
/// back to Paul, and the rate is clamped to what DECtalk accepts.
///
/// Every utterance runs on a fresh engine (plan §21.16), so nothing needs
/// resetting first; this only has to say what differs from the defaults.
[[nodiscard]] auto voice_preamble(const ports::voice_settings& settings) -> std::string;

} // namespace latibot::audio
