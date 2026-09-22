#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::commands {

/// One thing the bot needs a permission for.
///
/// The purpose is carried alongside the bits so a warning can say what stops
/// working, rather than naming a permission and leaving the reader to guess
/// (plan v4 §7).
struct requirement {
    std::uint64_t permissions = 0;
    std::string_view purpose;
};

/// A requirement a guild does not satisfy, narrowed to the missing bits.
struct gap {
    std::uint64_t permissions = 0;
    std::string_view purpose;
};

/// What the bot needs beyond the commands themselves.
///
/// Commands declare their own needs through `command_info`; this covers the
/// passive features, which have nobody to declare for them. Each phase adds
/// its entries as the feature lands, so the check never warns about something
/// that is not implemented yet.
[[nodiscard]] std::span<const requirement> passive_requirements() noexcept;

/// Requirements that `granted` does not satisfy.
///
/// Administrator satisfies everything, exactly as Discord treats it, so a
/// guild that granted it produces no warnings.
[[nodiscard]] std::vector<gap> unmet(std::span<const requirement> required, std::uint64_t granted);

/// Permission bits as Discord's own names, comma separated, e.g.
/// "Manage Nicknames, View Audit Log". Unknown bits are reported in hex
/// rather than dropped, so a newly added permission is still visible.
[[nodiscard]] std::string describe_permissions(std::uint64_t permissions);

} // namespace latibot::commands
