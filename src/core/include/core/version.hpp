#pragma once

#include <string_view>

namespace latibot {

inline constexpr int version_major = 0;
inline constexpr int version_minor = 1;
inline constexpr int version_patch = 0;

/// Whether this is a Debug build: NDEBUG is not defined.
#ifdef NDEBUG
inline constexpr bool debug_build = false;
#else
inline constexpr bool debug_build = true;
#endif

/// Human-readable version, e.g. "0.1.0".
[[nodiscard]] auto version_string() noexcept -> std::string_view;

} // namespace latibot
