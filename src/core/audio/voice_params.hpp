#pragma once

#include "core/ports/tts_engine.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

/// One `[:dv]` parameter, with DECtalk's own limits for it (ph/ph_vdefi.c;
/// the formant limits are those of an 11025 Hz build).
struct voice_parameter {
    /// As typed in `[:dv]`: "ap".
    std::string_view code;

    /// For the voice lab's forms, which allow 45 characters.
    std::string_view label;
    int min = 0;
    int max = 0;
};

/// The parameters the voice lab edits together. Five at most: a modal holds
/// no more (plan §2.1).
struct voice_parameter_group {
    std::string_view name;
    std::array<std::string_view, 5> codes;
};

/// The 29 parameters that change how this build speaks, in the order they
/// are written out. DECtalk also accepts `ago`, `agvo`, `aguo`, `chink` and
/// `oq`, which only its HLSYN synthesizer reads, and `save`, which the
/// sanitizer removes.
[[nodiscard]] auto voice_parameters() -> std::span<const voice_parameter>;

[[nodiscard]] auto find_voice_parameter(std::string_view code) -> const voice_parameter*;

/// The parameters in groups of five or fewer, the common ones first.
[[nodiscard]] auto voice_parameter_groups() -> std::span<const voice_parameter_group>;

/// A voice built on a built-in one: which, and the `[:dv]` edits made to it
/// (plan §12.6). Every edit is within its parameter's limits.
struct custom_voice {
    std::string base = "paul";

    /// By code, in the order of `voice_parameters`. A parameter not here is
    /// the base voice's own.
    std::vector<std::pair<std::string, int>> edits;

    /// Sets a parameter, clamped to its limits. False for a code that is
    /// not one of `voice_parameters`.
    auto set(std::string_view code, int value) -> bool;

    /// Goes back to the base voice's own value.
    auto clear(std::string_view code) -> void;

    /// The edit to `code`, if there is one.
    [[nodiscard]] auto get(std::string_view code) const -> std::optional<int>;

    /// The edits as `[:dv]` takes them, in table order: "ap 200 pr 150".
    [[nodiscard]] auto dv_parameters() const -> std::string;

    auto operator==(const custom_voice&) const -> bool = default;
};

/// A voice read from text, and what could not be read.
struct parsed_voice {
    custom_voice voice;
    std::vector<std::string> problems;
};

/// Reads edits written as `[:dv]` takes them, with or without the brackets:
/// "ap 200 pr 150", "[:dv ap 200 pr 150]". A voice selection such as
/// "[:nh]" or "[:name harry]" sets the base. Values out of range are
/// clamped and reported; unknown parameters and anything unreadable are
/// reported and skipped.
[[nodiscard]] auto parse_custom_voice(std::string_view text, std::string_view base) -> parsed_voice;

/// The inline commands that put a fresh engine into `settings`: the voice,
/// then the rate, then a custom voice's `[:dv]` edits. An unknown voice falls
/// back to Paul, and the rate is clamped to what DECtalk accepts.
///
/// Every utterance runs on a fresh engine (plan §21.16), so nothing needs
/// resetting first; this only has to say what differs from the defaults.
[[nodiscard]] auto voice_preamble(const ports::voice_settings& settings) -> std::string;

} // namespace latibot::audio
